package main

import (
	"context"
	_ "embed"
	"encoding/json"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"os"
	"path/filepath"
	"time"

	speech "cloud.google.com/go/speech/apiv2"
	speechpb "cloud.google.com/go/speech/apiv2/speechpb"
)

// //go:embed nether_sample.wav
// var inputFile []byte

func main() {
	if len(os.Args) < 2 {
		slog.Error("usage: program <output-directory>")
		os.Exit(1)
	}

	outputDir := os.Args[1]

	// Ensure output directory exists
	if err := os.MkdirAll(outputDir, 0755); err != nil {
		slog.Error("failed to create output directory", "error", err)
		os.Exit(1)
	}

	ctx := context.Background()
	mux := http.NewServeMux()

	client, err := speech.NewClient(ctx)
	if err != nil {
		panic(err)
	}
	defer client.Close()
	// slog.Info("len", "key", len(inputFile))
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		body, err := io.ReadAll(r.Body)
		if err != nil {
			http.Error(w, "failed to read request body", http.StatusBadRequest)
			return
		}

		// Save audio file with unique timestamp-based filename
		timestamp := time.Now().Format("20060102-150405.000")
		filename := fmt.Sprintf("audio-%s.wav", timestamp)
		filepath := filepath.Join(outputDir, filename)

		if err := os.WriteFile(filepath, body, 0644); err != nil {
			slog.Error("failed to write audio file", "error", err, "path", filepath)
			http.Error(w, "failed to save audio file", http.StatusInternalServerError)
			return
		}
		slog.Info("saved audio file", "path", filepath, "size", len(body))

		start := time.Now()
		req := &speechpb.RecognizeRequest{
			Recognizer: "projects/dayne-ad32/locations/global/recognizers/_",
			Config: &speechpb.RecognitionConfig{
				DecodingConfig: &speechpb.RecognitionConfig_AutoDecodingConfig{},
				LanguageCodes:  []string{"en-US"},
				Model:          "short",
			},
			AudioSource: &speechpb.RecognizeRequest_Content{
				Content: body,
			},
		}
		resp, err := client.Recognize(r.Context(), req)
		if err != nil {
			slog.Error("failed to recognize speech", "error", err)
			http.Error(w, "internal server error", http.StatusInternalServerError)
			return
		}
		slog.Info("recognized speech", "duration", time.Since(start), "results", len(resp.Results))
		b, err := json.Marshal(map[string]any{
			"results": resp.Results,
		})
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		w.Write(b)
	})

	// Streaming endpoint
	mux.HandleFunc("/stream", func(w http.ResponseWriter, r *http.Request) {
		// Set headers for SSE
		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("Connection", "keep-alive")
		w.Header().Set("X-Accel-Buffering", "no")

		flusher, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "streaming not supported", http.StatusInternalServerError)
			return
		}

		body, err := io.ReadAll(r.Body)
		if err != nil {
			http.Error(w, "failed to read request body", http.StatusBadRequest)
			return
		}

		// Save audio file with unique timestamp-based filename
		timestamp := time.Now().Format("20060102-150405.000")
		filename := fmt.Sprintf("audio-%s.wav", timestamp)
		filepath := filepath.Join(outputDir, filename)

		if err := os.WriteFile(filepath, body, 0644); err != nil {
			slog.Error("failed to write audio file", "error", err, "path", filepath)
			http.Error(w, "failed to save audio file", http.StatusInternalServerError)
			return
		}
		slog.Info("saved audio file for streaming", "path", filepath, "size", len(body))

		start := time.Now()
		stream, err := client.StreamingRecognize(r.Context())
		if err != nil {
			slog.Error("failed to create stream", "error", err)
			http.Error(w, "failed to create stream", http.StatusInternalServerError)
			return
		}

		// Send initial config
		if err := stream.Send(&speechpb.StreamingRecognizeRequest{
			Recognizer: "projects/dayne-ad32/locations/global/recognizers/_",
			StreamingRequest: &speechpb.StreamingRecognizeRequest_StreamingConfig{
				StreamingConfig: &speechpb.StreamingRecognitionConfig{
					Config: &speechpb.RecognitionConfig{
						DecodingConfig: &speechpb.RecognitionConfig_AutoDecodingConfig{},
						LanguageCodes:  []string{"en-US"},
						Model:          "short",
					},
				},
			},
		}); err != nil {
			slog.Error("failed to send config", "error", err)
			http.Error(w, "failed to send config", http.StatusInternalServerError)
			return
		}

		// Send audio in chunks
		chunkSize := 8192
		for i := 0; i < len(body); i += chunkSize {
			end := i + chunkSize
			if end > len(body) {
				end = len(body)
			}

			if err := stream.Send(&speechpb.StreamingRecognizeRequest{
				StreamingRequest: &speechpb.StreamingRecognizeRequest_Audio{
					Audio: body[i:end],
				},
			}); err != nil {
				slog.Error("failed to send audio chunk", "error", err)
				return
			}
		}

		if err := stream.CloseSend(); err != nil {
			slog.Error("failed to close send", "error", err)
			return
		}

		// Receive and stream results
		for {
			resp, err := stream.Recv()
			if err == io.EOF {
				break
			}
			if err != nil {
				slog.Error("failed to receive stream response", "error", err)
				fmt.Fprintf(w, "event: error\ndata: %s\n\n", err.Error())
				flusher.Flush()
				return
			}

			// Send each result as an SSE event
			for _, result := range resp.Results {
				data, err := json.Marshal(map[string]any{
					"result":  result,
					"isFinal": result.IsFinal,
				})
				if err != nil {
					slog.Error("failed to marshal result", "error", err)
					continue
				}
				fmt.Fprintf(w, "data: %s\n\n", data)
				flusher.Flush()
			}
		}

		slog.Info("streaming recognition completed", "duration", time.Since(start))
		fmt.Fprintf(w, "event: done\ndata: {}\n\n")
		flusher.Flush()
	})

	if err := http.ListenAndServe(":7878", mux); err != nil {
		panic(err)
	}
}
