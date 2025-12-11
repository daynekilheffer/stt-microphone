# STT Endpoint - Cloud Run Deployment

This directory contains Terraform configuration to deploy the STT endpoint to Google Cloud Run.

## Prerequisites

1. **Install Terraform** (>= 1.0)
   ```bash
   brew install terraform
   ```

2. **Authenticate with Google Cloud**
   ```bash
   gcloud auth application-default login
   ```

3. **Set your project**
   ```bash
   gcloud config set project daynek-stt-microphone-480916
   ```

## Deployment Steps

This is a **two-phase deployment** to handle the chicken-and-egg problem with container images.

### Phase 1: Create Infrastructure (Without Cloud Run Service)

1. **Initialize Terraform**
   ```bash
   cd terraform
   terraform init
   ```

2. **Deploy infrastructure** (Artifact Registry, Storage, IAM)
   ```bash
   terraform apply
   ```
   
   This creates the Artifact Registry repository but NOT the Cloud Run service yet.

3. **Verify the repository was created**
   ```bash
   gcloud artifacts repositories list --location=us-east4
   ```

### Phase 2: Build, Push, and Deploy Service

4. **Configure Docker for Artifact Registry**
   ```bash
   gcloud auth configure-docker us-east4-docker.pkg.dev
   ```

5. **Build and push the container**
   ```bash
   cd stt-endpoint
   
   # Build the container
   docker build -t us-east4-docker.pkg.dev/daynek-stt-microphone-480916/stt-endpoint/stt-endpoint:latest .
   
   # Push to Artifact Registry
   docker push us-east4-docker.pkg.dev/daynek-stt-microphone-480916/stt-endpoint/stt-endpoint:latest
   ```
   
   Or use Cloud Build:
   ```bash
   gcloud builds submit --tag us-east4-docker.pkg.dev/daynek-stt-microphone-480916/stt-endpoint/stt-endpoint:latest
   ```

6. **Update terraform.tfvars**
   
   Edit `terraform/terraform.tfvars` and uncomment/update the container_image line:
   ```terraform
   container_image = "us-east4-docker.pkg.dev/daynek-stt-microphone-480916/stt-endpoint/stt-endpoint:latest"
   ```

7. **Deploy the Cloud Run service**
   ```bash
   cd terraform
   terraform apply
   ```
   
   This will now create the Cloud Run service with your image.

8. **Get the service URL**
   ```bash
   terraform output service_url
   ```

### 9. Update Your ESP32 Configuration

Update the `STT_ENDPOINT_HOST` in your ESP32 code with the Cloud Run URL (without the https:// prefix).

For example, if the URL is `https://stt-endpoint-abc123-ue.a.run.app`, use:
```cpp
const char* STT_ENDPOINT_HOST = "stt-endpoint-abc123-ue.a.run.app";
const char* STT_ENDPOINT_PROTOCOL = "https";
const int   STT_ENDPOINT_PORT = 443;
```

## Configuration

Edit `terraform.tfvars` to customize:

- `project_id`: Your GCP project ID
- `region`: Deployment region
- `container_image`: Your container image URL
- `allow_unauthenticated`: Whether to allow public access
- `min_instances`/`max_instances`: Scaling configuration
- `cpu_limit`/`memory_limit`: Resource limits

## Security Considerations

**Current setup allows unauthenticated access** (`allow_unauthenticated = true`). For production:

1. Set `allow_unauthenticated = false`
2. Use one of these authentication methods:
   - Cloud Endpoints with API keys
   - Identity-Aware Proxy (IAP)
   - Service account authentication
   - VPN or Cloud Interconnect

## Audio Storage

Audio files are saved to a Cloud Storage bucket with:
- 7-day retention policy (configurable)
- Uniform bucket-level access
- Automatic cleanup of old files

## Costs

Approximate monthly costs (with default settings):
- Cloud Run: Free tier covers first 2 million requests
- Speech-to-Text API: ~$0.006 per 15 seconds of audio
- Cloud Storage: ~$0.020 per GB per month
- Artifact Registry: ~$0.10 per GB per month

## Cleanup

To destroy all resources:

```bash
terraform destroy
```

## Troubleshooting

### View logs
```bash
gcloud run services logs read stt-endpoint --limit=50
```

### Check service status
```bash
gcloud run services describe stt-endpoint --region=us-east4
```

### Test the endpoint
```bash
curl -X POST https://YOUR-SERVICE-URL.run.app/stream \
  -H "Content-Type: audio/wav" \
  --data-binary @test-audio.wav
```
