output "service_url" {
  description = "URL of the deployed Cloud Run service"
  value       = var.container_image != null ? google_cloud_run_v2_service.stt_endpoint[0].uri : "Not deployed yet - set container_image variable"
}

output "service_name" {
  description = "Name of the Cloud Run service"
  value       = var.container_image != null ? google_cloud_run_v2_service.stt_endpoint[0].name : "Not deployed yet"
}

output "service_account_email" {
  description = "Email of the service account used by Cloud Run"
  value       = google_service_account.stt_endpoint.email
}

output "audio_bucket_name" {
  description = "Name of the Cloud Storage bucket for audio files"
  value       = google_storage_bucket.audio_files.name
}

output "artifact_registry_repository" {
  description = "Artifact Registry repository for container images"
  value       = google_artifact_registry_repository.stt_endpoint.id
}
