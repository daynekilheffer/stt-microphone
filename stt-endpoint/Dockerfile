# Dockerfile for Cloud Run deployment
FROM --platform=linux/amd64 golang:1.24-alpine AS builder

WORKDIR /app

# Copy go mod files
COPY go.mod go.sum ./
RUN go mod download

# Copy source code
COPY *.go ./

# Build the application for AMD64
RUN CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -o stt-endpoint .

# Final stage
FROM --platform=linux/amd64 alpine:latest

RUN apk --no-cache add ca-certificates

WORKDIR /app

# Copy binary from builder
COPY --from=builder /app/stt-endpoint .

# Create output directory
RUN mkdir -p /tmp/audio

# Cloud Run expects port 8080
ENV PORT=8080

# Run the application
CMD ["./stt-endpoint", "/tmp/audio"]
