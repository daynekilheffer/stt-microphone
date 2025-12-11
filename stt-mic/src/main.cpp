#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "driver/i2s.h"
#include "esp_sleep.h"

#include "../include/secrets.h"

// ---------------------- CONFIG ----------------------

const char* STT_ENDPOINT_PROTOCOL = "http";
const char* STT_ENDPOINT_HOST = "10.0.0.17";
const int   STT_ENDPOINT_PORT = 7878;
const char* STT_ENDPOINT_PATH = "/stream";

#define SLEEP_TIMEOUT_MS 10000  // 10 seconds of inactivity

// I2S mic pins
#if defined(BOARD_SEEED_XIAO_ESP32C3)
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

uint32_t lastActivityTime = 0;

void setup() {
  Serial.begin(SERIAL_BAUD);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  // Configure wake up source for ESP32-C3 deep sleep
  // Use RTC GPIO wakeup - wakes when GPIO goes LOW (button press)
  uint64_t wakeup_pin_mask = (1ULL << BUTTON_PIN);
  esp_deep_sleep_enable_gpio_wakeup(wakeup_pin_mask, ESP_GPIO_WAKEUP_GPIO_LOW);

  // Check if we woke from deep sleep
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println("Woke from deep sleep via button");
  }

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
  
  // Initialize last activity time
  lastActivityTime = millis();
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

// -------------------- STREAMING RECORD & UPLOAD -----------------------
void recordAndStreamUpload() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost.");
    return;
  }

  uint32_t startTime = millis();
  Serial.println("Recording & streaming...");
  digitalWrite(LED_PIN, HIGH);

  // Connect to server
  WiFiClient client;
  if (!client.connect(STT_ENDPOINT_HOST, STT_ENDPOINT_PORT)) {
    Serial.println("Connection failed");
    digitalWrite(LED_PIN, LOW);
    return;
  }
  Serial.println("Connected to server.");

  size_t totalBytes = 0;
  size_t bytesRead = 0;
  int32_t i2sBuffer[128];
  int16_t audioChunk[128];
  uint32_t recordStart = millis();
  size_t sampleCount = 0;

  // Send HTTP headers with chunked encoding
  client.print("POST ");
  client.print(STT_ENDPOINT_PATH);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(STT_ENDPOINT_HOST);
  client.println("Content-Type: audio/wav");
  client.println("Transfer-Encoding: chunked");
  client.println("Connection: close");
  client.println();

  // Prepare WAV header (we'll update size later, but send placeholder)
  uint8_t wavHeader[44];
  addWavHeader(wavHeader, 0);  // Placeholder size
  
  // Send WAV header as first chunk
  client.printf("%X\r\n", 44);
  client.write(wavHeader, 44);
  client.print("\r\n");

  // Record and stream audio data as chunks
  bool recording = true;
  while (recording) {
    // Stop if max time reached
    if (millis() - recordStart > (MAX_SECONDS * 1000UL)) {
      Serial.println("Max recording time reached.");
      break;
    }

    // Stop if buffer would overflow
    if (sampleCount >= MAX_SAMPLES) {
      Serial.println("Max samples reached.");
      break;
    }

    // Read 32-bit I2S samples
    i2s_read(I2S_NUM_0, i2sBuffer, sizeof(i2sBuffer), &bytesRead, portMAX_DELAY);
    
    // Convert 32-bit samples to 16-bit by shifting down
    size_t samplesRead = bytesRead / sizeof(int32_t);
    size_t samplesToWrite = 0;
    
    for (size_t i = 0; i < samplesRead && sampleCount < MAX_SAMPLES; i++) {
      audioChunk[samplesToWrite++] = (int16_t)(i2sBuffer[i] >> 14);
      sampleCount++;
    }
    
    // Send the chunk if we have data
    if (samplesToWrite > 0) {
      size_t chunkBytes = samplesToWrite * sizeof(int16_t);
      
      // Write chunk size in hex
      client.printf("%X\r\n", chunkBytes);
      // Write chunk data
      client.write((uint8_t*)audioChunk, chunkBytes);
      client.print("\r\n");
      
      totalBytes += chunkBytes;
    }
    
    // Check button state AFTER processing the data
    if (digitalRead(BUTTON_PIN) != LOW) {
      recording = false;
    }
  }

  uint32_t recordStop = millis();
  uint32_t recordDuration = recordStop - recordStart;

  // Send final zero-length chunk to signal end
  client.println("0");
  client.println();

  digitalWrite(LED_PIN, LOW);

  Serial.print("Recording stopped. Bytes streamed: ");
  Serial.println(totalBytes);
  Serial.print("Record duration: ");
  Serial.print(recordDuration);
  Serial.println(" ms");

  // Wait for response
  uint32_t responseStart = millis();
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println("Response timeout");
      client.stop();
      return;
    }
  }

  // Read HTTP headers until we find the JSON body
  String jsonResponse = "";
  bool headersComplete = false;
  bool foundJson = false;
  
  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    
    if (!headersComplete) {
      // Empty line signals end of headers
      if (line.length() == 0) {
        headersComplete = true;
      }
    } else {
      // We're in the body now
      if (line.startsWith("{") && line.indexOf("\"text\"") > 0) {
        jsonResponse = line;
        foundJson = true;
        break;
      }
    }
  }

  uint32_t responseTime = millis() - responseStart;
  uint32_t totalTime = millis() - startTime;
  
  // Parse and display the transcription using ArduinoJson
  if (foundJson) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonResponse);
    
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
    } else if (!doc["text"].isNull()) {
      const char* transcription = doc["text"];
      Serial.println("\n=== Transcription ===");
      Serial.println(transcription);
      Serial.println("=====================\n");
    }
  } else {
    Serial.println("No transcription received");
  }
  
  Serial.print("Response time: ");
  Serial.print(responseTime);
  Serial.println(" ms");
  Serial.print("Total time: ");
  Serial.print(totalTime);
  Serial.println(" ms");

  client.stop();
}

// ------------------------- LOOP --------------------------
void loop() {

  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(30);  // debounce

    if (digitalRead(BUTTON_PIN) == LOW) {
      lastActivityTime = millis();  // Update activity time
      recordAndStreamUpload();
      delay(500);
      lastActivityTime = millis();  // Update after completion
    }
  }

  // Check for inactivity timeout
  if (millis() - lastActivityTime > SLEEP_TIMEOUT_MS) {
    Serial.println("Entering deep sleep due to inactivity...");
    Serial.flush();  // Make sure message is sent
    delay(100);
    
    // Turn off LED
    digitalWrite(LED_PIN, LOW);
    
    // Enter deep sleep
    esp_deep_sleep_start();
  }
  
  delay(100);  // Small delay to avoid busy loop
}
