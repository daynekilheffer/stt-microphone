#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "driver/i2s.h"

#include "../include/secrets.h"

// ---------------------- CONFIG ----------------------

const char* POST_URL = "http://10.0.0.17:7878";

// I2S mic pins
#if defined(BOARD_QTPY_ESP32C3)
  #define I2S_WS  1    // LRCLK
  #define I2S_SD  4    // DOUT
  #define I2S_SCK 3    // BCLK
  #define LED_PIN LED_BUILTIN
  #define BUTTON_PIN 7
#elif defined(BOARD_SEEED_XIAO_ESP32C3)
  #define I2S_WS  3    // LRCLK
  #define I2S_SD  20   // DOUT
  #define I2S_SCK 8    // BCLK
  #define LED_PIN D10
  #define BUTTON_PIN 5
#else
  #error "Please define board type"
#endif

// Audio settings
#define SAMPLE_RATE    8000   // Reduced from 16000 for longer recording
#define MAX_SECONDS    10
#define MAX_SAMPLES    (SAMPLE_RATE * MAX_SECONDS)
#define MAX_BYTES      (MAX_SAMPLES * sizeof(int16_t))

// Add 44 bytes for WAV header
uint8_t audioBuffer[MAX_BYTES + 44];

// ----------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");

  // I2S config
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);

  Serial.println("Setup complete.");
}

// ------------------- WAV HEADER --------------------
void addWavHeader(uint8_t* buffer, uint32_t dataSize) {
  memcpy(buffer, "RIFF", 4);
  uint32_t chunkSize = dataSize + 36;
  memcpy(buffer + 4, &chunkSize, 4);
  memcpy(buffer + 8, "WAVEfmt ", 8);

  uint32_t subchunk1Size = 16;
  uint16_t audioFormat = 1;
  uint16_t numChannels = 1;
  uint32_t sampleRate = SAMPLE_RATE;
  uint32_t byteRate = SAMPLE_RATE * 2;
  uint16_t blockAlign = 2;
  uint16_t bitsPerSample = 16;

  memcpy(buffer + 16, &subchunk1Size, 4);
  memcpy(buffer + 20, &audioFormat, 2);
  memcpy(buffer + 22, &numChannels, 2);
  memcpy(buffer + 24, &sampleRate, 4);
  memcpy(buffer + 28, &byteRate, 4);
  memcpy(buffer + 32, &blockAlign, 2);
  memcpy(buffer + 34, &bitsPerSample, 2);

  memcpy(buffer + 36, "data", 4);
  memcpy(buffer + 40, &dataSize, 4);
}

// -------------------- RMS CALCULATION -------------------
float calculateRMS(const int16_t* samples, size_t numSamples) {
  if (numSamples == 0) return 0.0f;
  
  double sumSquares = 0;
  for (size_t i = 0; i < numSamples; ++i) {
    sumSquares += samples[i] * samples[i];
  }
  return sqrt(sumSquares / numSamples);
}

// -------------------- RECORD LOOP -----------------------
size_t recordWhileButtonHeld() {
  Serial.println("Recording...");
  digitalWrite(LED_PIN, HIGH);

  size_t totalBytes = 0;
  size_t bytesRead = 0;
  int32_t i2sBuffer[128];  // Temporary buffer for 32-bit samples

  uint32_t start = millis();

  // Start writing audio after WAV header space
  int16_t* samples = (int16_t*)(audioBuffer + 44);
  size_t sampleCount = 0;

  while (digitalRead(BUTTON_PIN) == LOW) {

    // Stop if max time reached
    if (millis() - start > (MAX_SECONDS * 1000UL)) {
      Serial.println("Max recording time reached.");
      break;
    }

    // Stop if buffer would overflow
    if (sampleCount >= MAX_SAMPLES) {
      Serial.println("Max buffer used.");
      break;
    }

    // Read 32-bit I2S samples
    i2s_read(I2S_NUM_0, i2sBuffer, sizeof(i2sBuffer), &bytesRead, portMAX_DELAY);
    
    // Convert 32-bit samples to 16-bit by shifting down
    size_t samplesRead = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < samplesRead && sampleCount < MAX_SAMPLES; i++) {
      // Shift right 14 bits to get 18-bit data into 16-bit range
      samples[sampleCount++] = (int16_t)(i2sBuffer[i] >> 14);
    }
    
    totalBytes = sampleCount * sizeof(int16_t);
  }

  digitalWrite(LED_PIN, LOW);

  Serial.print("Recording stopped. Bytes captured: ");
  Serial.println(totalBytes);

  // Calculate and display RMS to verify audio was captured
  if (totalBytes > 0) {
    size_t numSamples = totalBytes / sizeof(int16_t);
    float rms = calculateRMS(samples, numSamples);
    Serial.print("RMS level: ");
    Serial.println(rms);
    
    if (rms < 10.0f) {
      Serial.println("WARNING: RMS is very low - check microphone!");
    }
  }

  return totalBytes;
}

// ------------------------ POST ---------------------
void postAudio(uint8_t* data, size_t len) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost.");
    return;
  }

  HTTPClient http;
  http.begin(POST_URL);
  http.addHeader("Content-Type", "audio/wav");

  Serial.println("Uploading...");

  int code = http.POST(data, len);

  Serial.print("HTTP response: ");
  Serial.println(code);

  if (code > 0) {
    Serial.println(http.getString());
  }

  http.end();
}

// ------------------------- LOOP --------------------------
void loop() {

  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(30);  // debounce

    if (digitalRead(BUTTON_PIN) == LOW) {
      size_t captured = recordWhileButtonHeld();

      // Add WAV header
      addWavHeader(audioBuffer, captured);

      // Send WAV file
      postAudio(audioBuffer, captured + 44);

      delay(500);
    }
  }
}
