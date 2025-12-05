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

	if err := http.ListenAndServe(":7878", mux); err != nil {
		panic(err)
	}
}
