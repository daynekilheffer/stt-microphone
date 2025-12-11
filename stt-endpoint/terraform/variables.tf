variable "project_id" {
  description = "GCP Project ID"
  type        = string
}

variable "region" {
  description = "GCP region for resources"
  type        = string
  default     = "us-east4"
}

variable "container_image" {
  description = "Container image to deploy (e.g., gcr.io/PROJECT/stt-endpoint:latest). Leave null for initial setup."
  type        = string
  default     = null
}

variable "cpu_limit" {
  description = "CPU limit for Cloud Run service"
  type        = string
  default     = "1"
}

variable "memory_limit" {
  description = "Memory limit for Cloud Run service"
  type        = string
  default     = "512Mi"
}

variable "min_instances" {
  description = "Minimum number of instances"
  type        = number
  default     = 0
}

variable "max_instances" {
  description = "Maximum number of instances"
  type        = number
  default     = 10
}

variable "allow_unauthenticated" {
  description = "Allow unauthenticated access to the service"
  type        = bool
  default     = false
}

variable "audio_retention_days" {
  description = "Number of days to retain audio files in storage"
  type        = number
  default     = 7
}

variable "force_destroy_bucket" {
  description = "Allow destroying bucket with objects (use with caution)"
  type        = bool
  default     = false
}
