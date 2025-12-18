#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "driver/i2s.h"
#include "esp_sleep.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include "AudioTools.h"

#include "../include/secrets.h"

// ESP-NOW configuration
uint8_t serverMacAddress[] = STT_KEYBOARD_SERVER_MAC;

#define STT_MIC_SLEEP_TIMEOUT_MS 30000  // 30 seconds of inactivity
#define STT_MIC_MAX_ESPNOW_PAYLOAD 250  // Maximum ESP-NOW payload size

// I2S mic pins
#define STT_MIC_I2S_WS  3    // LRCLK
#define STT_MIC_I2S_SD  20   // DOUT
#define STT_MIC_I2S_SCK 8    // BCLK
#define STT_MIC_LED_PIN D10
#define STT_MIC_BUTTON_PIN 5

// Audio settings
#define STT_MIC_SAMPLE_RATE    16000  // 16kHz sample rate for better quality
#define STT_MIC_CHANNELS       1      // Mono
#define STT_MIC_BITS_PER_SAMPLE 16    // 16-bit samples

// Buffer settings
#define STT_MIC_CHUNK_SIZE 256  // Reduced from 512 to save memory

// Audio-tools objects
I2SStream i2sStream;
NumberFormatConverterStream converter(i2sStream);
FilteredStream<int32_t, int16_t> filtered(converter, STT_MIC_CHUNK_SIZE / 4);

uint32_t lastActivityTime = 0;

// ESP-NOW variables
bool espnowReady = false;
volatile bool espnowSendSuccess = false;

// WiFi client objects (global to preserve TLS session cache)
WiFiClient httpClient;
WiFiClientSecure httpsClient;

// ESP-NOW callbacks
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  espnowSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  Serial.print("ESP-NOW callback - MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac_addr[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.print(" Status: ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("SUCCESS");
  } else {
    Serial.print("FAILED (");
    Serial.print(status);
    Serial.println(")");
  }
}

bool initESPNow() {
  Serial.print("Server MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", serverMacAddress[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return false;
  }
  
  Serial.println("ESP-NOW initialized");
  
  // Register send callback
  esp_now_register_send_cb(onDataSent);
  
  // Add peer (server)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, serverMacAddress, 6);
  peerInfo.channel = 0;  // 0 = use current channel (both devices on same WiFi)
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
    return false;
  }
  
  Serial.println("ESP-NOW peer added");
  espnowReady = true;
  return true;
}

void sendTextToKeyboard(const char* text) {
  if (!espnowReady) {
    Serial.println("ESP-NOW not ready");
    return;
  }
  
  size_t textLen = strlen(text);
  Serial.print("Sending text (");
  Serial.print(textLen);
  Serial.println(" bytes) via ESP-NOW...");
  
  size_t offset = 0;
  
  // Send in chunks if text is too long
  while (offset < textLen) {
    size_t chunkLen = min((size_t)STT_MIC_MAX_ESPNOW_PAYLOAD, textLen - offset);
    
    espnowSendSuccess = false;
    esp_err_t result = esp_now_send(serverMacAddress, (uint8_t*)(text + offset), chunkLen);
    
    if (result == ESP_OK) {
      // Wait for send callback (with timeout)
      unsigned long timeout = millis();
      while (!espnowSendSuccess && (millis() - timeout < 1000)) {
        delay(10);
      }
      
      if (espnowSendSuccess) {
        Serial.print("Sent ");
        Serial.print(chunkLen);
        Serial.println(" bytes via ESP-NOW");
      } else {
        Serial.print("ESP-NOW send timeout at offset ");
        Serial.println(offset);
        break;
      }
    } else {
      Serial.print("ESP-NOW send error: ");
      Serial.println(result);
      break;
    }
    
    offset += chunkLen;
    delay(50);  // Small delay between chunks
  }
}

void setup() {
  Serial.begin(STT_MIC_SERIAL_BAUD);

  pinMode(STT_MIC_BUTTON_PIN, INPUT_PULLUP);
  pinMode(STT_MIC_LED_PIN, OUTPUT);

  // Configure wake up source for ESP32-C3 deep sleep
  // Use RTC GPIO wakeup - wakes when GPIO goes LOW (button press)
  uint64_t wakeup_pin_mask = (1ULL << STT_MIC_BUTTON_PIN);
  esp_deep_sleep_enable_gpio_wakeup(wakeup_pin_mask, ESP_GPIO_WAKEUP_GPIO_LOW);

  // Check if we woke from deep sleep
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println("Woke from deep sleep via button");
  }

  // WiFi
  WiFi.begin(STT_MIC_WIFI_SSID, STT_MIC_WIFI_PASS);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  
  // determine the channel we're on
  uint8_t channel;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&channel, &second);
  Serial.print("WiFi connected on channel: ");
  Serial.println(channel);

  // Configure I2S stream with audio-tools
  auto i2s_config = i2sStream.defaultConfig(RX_MODE);
  i2s_config.sample_rate = STT_MIC_SAMPLE_RATE;
  i2s_config.bits_per_sample = 32;  // Most MEMS mics output 32-bit
  i2s_config.channels = STT_MIC_CHANNELS;
  i2s_config.i2s_format = I2S_STD_FORMAT;
  i2s_config.pin_bck = STT_MIC_I2S_SCK;
  i2s_config.pin_ws = STT_MIC_I2S_WS;
  i2s_config.pin_data = STT_MIC_I2S_SD;
  i2s_config.use_apll = false;
  i2s_config.auto_clear = true;
  
  i2sStream.begin(i2s_config);
  
  // Setup converter: 32-bit input to 16-bit output
  converter.begin(32, 16);  // from_bits, to_bits
  filtered.begin();

  // Initialize ESP-NOW
  Serial.println("Initializing ESP-NOW...");
  if (initESPNow()) {
    Serial.println("ESP-NOW ready");
  } else {
    Serial.println("ESP-NOW initialization failed");
  }
  
  Serial.println("Setup complete.");
  
  // Initialize last activity time
  lastActivityTime = millis();
}

// -------------------- STREAMING RECORD & UPLOAD -----------------------
void recordAndStreamUpload() {
  uint32_t funcStart = millis();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[%lu] WiFi lost.\n", millis() - funcStart);
    return;
  }

  Serial.printf("[%lu] Streaming audio...\n", millis() - funcStart);
  digitalWrite(STT_MIC_LED_PIN, HIGH);

  // Determine if we need HTTPS or HTTP
  bool useHttps = (strcmp(STT_ENDPOINT_PROTOCOL, "https") == 0);

  // Use global client objects to preserve TLS session cache
  WiFiClient* client;
  
  if (useHttps) {
    Serial.printf("[%lu] Using HTTPS...\n", millis() - funcStart);
    httpsClient.setInsecure();  // Skip certificate verification (use for development)
    // For production, use: httpsClient.setCACert(root_ca);
    client = &httpsClient;
  } else {
    Serial.printf("[%lu] Using HTTP...\n", millis() - funcStart);
    client = &httpClient;
  }
  
  Serial.printf("[%lu] starting connection\n", millis() - funcStart);
  if (!client->connect(STT_ENDPOINT_HOST, STT_ENDPOINT_PORT)) {
    Serial.printf("[%lu] Connection failed\n", millis() - funcStart);
    digitalWrite(STT_MIC_LED_PIN, LOW);
    return;
  }
  
  Serial.printf("[%lu] Connection established\n", millis() - funcStart);

  // Send HTTP headers manually
  client->print("POST ");
  client->print(STT_ENDPOINT_PATH);
  client->println(" HTTP/1.1");
  client->print("Host: ");
  client->print(STT_ENDPOINT_HOST);
  client->print(":");
  client->println(STT_ENDPOINT_PORT);
  client->println("Content-Type: audio/l16");
  client->print("X-Dayne-Sample-Rate: ");
  client->println(STT_MIC_SAMPLE_RATE);
  client->print("X-Dayne-Channels: ");
  client->println(STT_MIC_CHANNELS);
  client->print("X-Dayne-Bits-Per-Sample: ");
  client->println(STT_MIC_BITS_PER_SAMPLE);
  client->println("Transfer-Encoding: chunked");
  client->println("Connection: close");
  client->println();  // End of headers
  client->flush();  // Ensure headers are sent before starting audio

  // Give server time to process headers
  delay(20);
  
  Serial.printf("[%lu] Starting audio streaming...\n", millis() - funcStart);

  // Stream audio while button is pressed
  uint32_t startTime = millis();
  size_t totalBytes = 0;
  size_t totalChunks = 0;
  uint8_t chunk[STT_MIC_CHUNK_SIZE];
  
  while (digitalRead(STT_MIC_BUTTON_PIN) == LOW) {
    // Read from filtered stream (already converted to 16-bit)
    size_t bytesRead = filtered.readBytes(chunk, STT_MIC_CHUNK_SIZE);
    
    if (bytesRead > 0) {
      // Send as HTTP chunk: size in hex + CRLF + data + CRLF
      char chunkSize[16];
      sprintf(chunkSize, "%X\r\n", bytesRead);
      
      size_t headerWritten = client->print(chunkSize);
      size_t dataWritten = client->write(chunk, bytesRead);
      size_t trailerWritten = client->print("\r\n");
      
      // Check if write succeeded
      if (headerWritten == 0 || dataWritten != bytesRead || trailerWritten == 0) {
        Serial.printf("[%lu] Write failed!\n", millis() - funcStart);
        Serial.print("Header: "); Serial.print(headerWritten);
        Serial.print(", Data: "); Serial.print(dataWritten);
        Serial.print(", Trailer: "); Serial.println(trailerWritten);
        break;
      }
      
      totalBytes += bytesRead;
      totalChunks++;
      
      // Flush after every few chunks to ensure data is sent
      if (totalChunks % 10 == 0) {
        client->flush();
      }
    }
    
    // Safety timeout (max 10 seconds)
    if (millis() - startTime > 10000) {
      Serial.printf("[%lu] Max streaming time reached\n", millis() - funcStart);
      break;
    }
    
    // Check connection
    if (!client->connected()) {
      Serial.printf("[%lu] Connection lost at ", millis() - funcStart);
      Serial.print(totalBytes);
      Serial.println(" bytes");
      break;
    }
    
    yield();
  }
  
  Serial.printf("[%lu] Sending final chunk...\n", millis() - funcStart);
  // Send final chunk (size 0) to signal end
  client->print("0\r\n\r\n");
  client->flush();  // Ensure final chunk is sent
  Serial.printf("[%lu] Final chunk flushed\n", millis() - funcStart);
  
  uint32_t duration = millis() - startTime;
  digitalWrite(STT_MIC_LED_PIN, LOW);
  Serial.printf("[%lu] Streaming stopped. Duration: ", millis() - funcStart);
  Serial.print(duration);
  Serial.print(" ms, Bytes: ");
  Serial.print(totalBytes);
  Serial.print(", Chunks: ");
  Serial.println(totalChunks);

  // Read response
  Serial.printf("[%lu] Reading response...\n", millis() - funcStart);
  
  // Read status line immediately (don't wait for available())
  String statusLine = "";
  unsigned long timeout = millis();
  while (statusLine.length() == 0 && (millis() - timeout < 5000)) {
    if (client->connected()) {
      statusLine = client->readStringUntil('\n');
    }
    if (statusLine.length() == 0) delay(10);
  }
  
  if (statusLine.length() == 0) {
    Serial.printf("[%lu] Response timeout\n", millis() - funcStart);
    client->stop();
    return;
  }
  
  Serial.printf("[%lu] HTTP Status: ", millis() - funcStart);
  Serial.println(statusLine);

  // Skip headers
  timeout = millis();
  while (client->connected() && (millis() - timeout < 2000)) {
    String line = client->readStringUntil('\n');
    if (line == "\r") break;  // End of headers
    if (line.length() == 0) delay(10);
  }

  // Read response body
  String response = "";
  timeout = millis();
  while (client->connected() && (millis() - timeout < 2000)) {
    if (client->available()) {
      char c = client->read();
      response += c;
    } else {
      delay(10);
    }
    // Check if we have complete JSON
    if (response.endsWith("}")) break;
  }
  
  client->stop();
  Serial.printf("[%lu] Response: ", millis() - funcStart);
  Serial.println(response);
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  
  if (error) {
    Serial.printf("[%lu] JSON parse error: ", millis() - funcStart);
    Serial.println(error.c_str());
  } else if (!doc["text"].isNull()) {
    const char* transcription = doc["text"];
    Serial.printf("[%lu] \n=== Transcription ===\n", millis() - funcStart);
    Serial.println(transcription);
    Serial.println("=====================\n");
    
    sendTextToKeyboard(transcription);
  }
}

// ------------------------- LOOP --------------------------
void loop() {

  if (digitalRead(STT_MIC_BUTTON_PIN) == LOW) {
    delay(30);  // debounce

    if (digitalRead(STT_MIC_BUTTON_PIN) == LOW) {
      lastActivityTime = millis();  // Update activity time
      recordAndStreamUpload();
      delay(500);
      lastActivityTime = millis();  // Update after completion
    }
  }

  // Check for inactivity timeout
  if (millis() - lastActivityTime > STT_MIC_SLEEP_TIMEOUT_MS) {
    Serial.println("Entering deep sleep due to inactivity...");
    Serial.flush();  // Make sure message is sent
    delay(100);
    
    // Turn off LED
    digitalWrite(STT_MIC_LED_PIN, LOW);
    
    // Enter deep sleep
    esp_deep_sleep_start();
  }
  
  delay(100);  // Small delay to avoid busy loop
}
