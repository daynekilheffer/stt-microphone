project_id = "daynek-stt-microphone-480916"
region     = "us-east4"

# Leave null for initial setup, then uncomment after pushing container image
container_image = "us-east4-docker.pkg.dev/daynek-stt-microphone-480916/stt-endpoint/stt-endpoint:latest"
# container_image = null

# Resource limits
cpu_limit    = "1"
memory_limit = "512Mi"

# Scaling
min_instances = 0
max_instances = 1

# Security - set to true if you want to allow unauthenticated access
# For production, consider using Cloud Endpoints, API Gateway, or IAM authentication
allow_unauthenticated = true

# Audio file retention
audio_retention_days = 7
force_destroy_bucket = false
