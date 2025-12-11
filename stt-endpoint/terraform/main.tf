terraform {
  required_version = ">= 1.0"
  required_providers {
    google = {
      source  = "hashicorp/google"
      version = "~> 6.0"
    }
  }
}

provider "google" {
  project = var.project_id
  region  = var.region
}

# Enable required APIs
resource "google_project_service" "run_api" {
  service            = "run.googleapis.com"
  disable_on_destroy = false
}

resource "google_project_service" "speech_api" {
  service            = "speech.googleapis.com"
  disable_on_destroy = false
}

resource "google_project_service" "artifact_registry_api" {
  service            = "artifactregistry.googleapis.com"
  disable_on_destroy = false
}

# Artifact Registry repository for container images
resource "google_artifact_registry_repository" "stt_endpoint" {
  location      = var.region
  repository_id = "stt-endpoint"
  description   = "STT endpoint container images"
  format        = "DOCKER"

  depends_on = [google_project_service.artifact_registry_api]
}

# Cloud Storage bucket for audio files
resource "google_storage_bucket" "audio_files" {
  name          = "${var.project_id}-stt-audio"
  location      = var.region
  force_destroy = var.force_destroy_bucket

  uniform_bucket_level_access = true

  lifecycle_rule {
    condition {
      age = var.audio_retention_days
    }
    action {
      type = "Delete"
    }
  }
}

# Service account for Cloud Run
resource "google_service_account" "stt_endpoint" {
  account_id   = "stt-endpoint"
  display_name = "STT Endpoint Service Account"
}

# Grant Speech API access to service account
resource "google_project_iam_member" "speech_user" {
  project = var.project_id
  role    = "roles/speech.client"
  member  = "serviceAccount:${google_service_account.stt_endpoint.email}"
}

# Grant storage access to service account
resource "google_storage_bucket_iam_member" "audio_writer" {
  bucket = google_storage_bucket.audio_files.name
  role   = "roles/storage.objectCreator"
  member = "serviceAccount:${google_service_account.stt_endpoint.email}"
}

# Cloud Run service (only deploy if container_image is provided)
resource "google_cloud_run_v2_service" "stt_endpoint" {
  count = var.container_image != null ? 1 : 0
  
  # disable protection during development
  deletion_protection = false

  name     = "stt-endpoint"
  location = var.region

  template {
    service_account = google_service_account.stt_endpoint.email

    containers {
      image = var.container_image

      ports {
        container_port = 8080
      }

      env {
        name  = "OUTPUT_DIR"
        value = "/tmp/audio"
      }

      env {
        name  = "GOOGLE_CLOUD_PROJECT"
        value = var.project_id
      }

      resources {
        limits = {
          cpu    = var.cpu_limit
          memory = var.memory_limit
        }
      }

      startup_probe {
        http_get {
          path = "/healthz"
        }
        initial_delay_seconds = 0
        timeout_seconds       = 1
        period_seconds        = 3
        failure_threshold     = 10
      }
    }

    scaling {
      min_instance_count = var.min_instances
      max_instance_count = var.max_instances
    }

    timeout = "30s"
  }

  traffic {
    type    = "TRAFFIC_TARGET_ALLOCATION_TYPE_LATEST"
    percent = 100
  }

  depends_on = [
    google_project_service.run_api,
    google_project_service.speech_api
  ]
}

# Allow unauthenticated access (adjust based on your security requirements)
resource "google_cloud_run_v2_service_iam_member" "public_access" {
  count = var.container_image != null && var.allow_unauthenticated ? 1 : 0

  location = google_cloud_run_v2_service.stt_endpoint[0].location
  name     = google_cloud_run_v2_service.stt_endpoint[0].name
  role     = "roles/run.invoker"
  member   = "allUsers"
}
