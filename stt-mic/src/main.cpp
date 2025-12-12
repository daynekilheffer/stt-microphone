#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "driver/i2s.h"
#include "esp_sleep.h"
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include "AudioTools.h"

#include "../include/secrets.h"

// BLE configuration
#define BLE_SERVER_NAME "ESP32 Keyboard Server"
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define SLEEP_TIMEOUT_MS 30000  // 30 seconds of inactivity

// I2S mic pins
#define I2S_WS  3    // LRCLK
#define I2S_SD  20   // DOUT
#define I2S_SCK 8    // BCLK
#define LED_PIN D10
#define BUTTON_PIN 5

// Audio settings
#define SAMPLE_RATE    8000   // 8kHz sample rate
#define CHANNELS       1      // Mono
#define BITS_PER_SAMPLE 16    // 16-bit samples

// Buffer settings
#define CHUNK_SIZE 512
#define MAX_RECORDING_SIZE (8000 * 2 * 3)  // 3 seconds max at 8kHz 16-bit (48KB)

// Audio-tools objects
I2SStream i2sStream;
NumberFormatConverterStream converter(i2sStream);
FilteredStream<int32_t, int16_t> filtered(converter, CHUNK_SIZE / 4);

// Recording buffer (raw LINEAR16 audio) - allocate dynamically to avoid stack overflow
uint8_t* recordingBuffer = nullptr;

uint32_t lastActivityTime = 0;

// BLE client variables
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
bool bleConnected = false;
BLEAdvertisedDevice* bleServerDevice = nullptr;

// BLE callbacks
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveName() && advertisedDevice.getName() == BLE_SERVER_NAME) {
      Serial.print("Found BLE keyboard server: ");
      Serial.println(advertisedDevice.toString().c_str());
      bleServerDevice = new BLEAdvertisedDevice(advertisedDevice);
      BLEDevice::getScan()->stop();
    }
  }
};

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    bleConnected = true;
    Serial.println("BLE connected");
  }

  void onDisconnect(BLEClient* pclient) {
    bleConnected = false;
    Serial.println("BLE disconnected");
  }
};

bool connectToBLEServer() {
  if (bleServerDevice == nullptr) {
    Serial.println("No BLE server device found");
    return false;
  }

  Serial.print("Connecting to BLE server: ");
  Serial.println(bleServerDevice->getAddress().toString().c_str());

  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  if (!pClient->connect(bleServerDevice)) {
    Serial.println("Failed to connect to BLE server");
    return false;
  }

  Serial.println("Connected to BLE server");

  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println("Failed to find service UUID");
    pClient->disconnect();
    return false;
  }

  pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.println("Failed to find characteristic UUID");
    pClient->disconnect();
    return false;
  }

  Serial.println("BLE characteristic ready");
  bleConnected = true;
  return true;
}

void sendTextViaBLE(const char* text) {
  if (!bleConnected || pRemoteCharacteristic == nullptr) {
    Serial.println("BLE not connected, attempting to connect...");
    if (!connectToBLEServer()) {
      Serial.println("Failed to connect to BLE server");
      return;
    }
  }

  try {
    pRemoteCharacteristic->writeValue(text, strlen(text));
    Serial.println("Text sent via BLE");
  } catch (...) {
    Serial.println("Failed to send text via BLE");
    bleConnected = false;
  }
}

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

  // Configure I2S stream with audio-tools
  auto i2s_config = i2sStream.defaultConfig(RX_MODE);
  i2s_config.sample_rate = SAMPLE_RATE;
  i2s_config.bits_per_sample = 32;  // Most MEMS mics output 32-bit
  i2s_config.channels = CHANNELS;
  i2s_config.i2s_format = I2S_STD_FORMAT;
  i2s_config.pin_bck = I2S_SCK;
  i2s_config.pin_ws = I2S_WS;
  i2s_config.pin_data = I2S_SD;
  i2s_config.use_apll = false;
  i2s_config.auto_clear = true;
  
  i2sStream.begin(i2s_config);
  
  // Setup converter: 32-bit input to 16-bit output
  converter.begin(32, 16);  // from_bits, to_bits
  filtered.begin();

  // Allocate recording buffer BEFORE BLE to ensure we have memory
  recordingBuffer = (uint8_t*)malloc(MAX_RECORDING_SIZE);
  if (recordingBuffer == nullptr) {
    Serial.println("Failed to allocate recording buffer!");
    ESP.restart();
  }
  Serial.print("Allocated recording buffer: ");
  Serial.print(MAX_RECORDING_SIZE);
  Serial.println(" bytes");

  // Initialize BLE
  Serial.println("Initializing BLE client...");
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  
  Serial.println("Scanning for BLE keyboard server...");
  pBLEScan->start(5, false);
  
  if (bleServerDevice != nullptr) {
    connectToBLEServer();
  } else {
    Serial.println("BLE keyboard server not found");
  }

  Serial.println("Setup complete.");
  
  // Initialize last activity time
  lastActivityTime = millis();
}

// -------------------- STREAMING RECORD & UPLOAD -----------------------
void recordAndStreamUpload() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost.");
    return;
  }

  Serial.println("Streaming audio...");
  digitalWrite(LED_PIN, HIGH);

  // Connect to server
  WiFiClient client;
  if (!client.connect(STT_ENDPOINT_HOST, STT_ENDPOINT_PORT)) {
    Serial.println("Connection failed");
    digitalWrite(LED_PIN, LOW);
    return;
  }

  // Send HTTP headers manually
  client.print("POST ");
  client.print(STT_ENDPOINT_PATH);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.print(STT_ENDPOINT_HOST);
  client.print(":");
  client.println(STT_ENDPOINT_PORT);
  client.println("Content-Type: audio/l16");
  client.print("X-Dayne-Sample-Rate: ");
  client.println(SAMPLE_RATE);
  client.print("X-Dayne-Channels: ");
  client.println(CHANNELS);
  client.print("X-Dayne-Bits-Per-Sample: ");
  client.println(BITS_PER_SAMPLE);
  client.println("Transfer-Encoding: chunked");
  client.println("Connection: close");
  client.println();  // End of headers

  // Stream audio while button is pressed
  uint32_t startTime = millis();
  size_t totalBytes = 0;
  uint8_t chunk[CHUNK_SIZE];
  
  while (digitalRead(BUTTON_PIN) == LOW) {
    // Read from filtered stream (already converted to 16-bit)
    size_t bytesRead = filtered.readBytes(chunk, CHUNK_SIZE);
    
    if (bytesRead > 0) {
      // Send as HTTP chunk: size in hex + CRLF + data + CRLF
      char chunkSize[16];
      sprintf(chunkSize, "%X\r\n", bytesRead);
      client.print(chunkSize);
      client.write(chunk, bytesRead);
      client.print("\r\n");
      
      totalBytes += bytesRead;
    }
    
    // Safety timeout (max 10 seconds)
    if (millis() - startTime > 10000) {
      Serial.println("Max streaming time reached");
      break;
    }
    
    // Check connection
    if (!client.connected()) {
      Serial.println("Connection lost");
      break;
    }
    
    yield();
  }
  
  // Send final chunk (size 0) to signal end
  client.print("0\r\n\r\n");
  
  uint32_t duration = millis() - startTime;
  digitalWrite(LED_PIN, LOW);
  Serial.print("Streaming stopped. Duration: ");
  Serial.print(duration);
  Serial.print(" ms, Bytes: ");
  Serial.println(totalBytes);

  // Read response
  Serial.println("Reading response...");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println("Response timeout");
      client.stop();
      return;
    }
    delay(10);
  }

  // Read status line
  String statusLine = client.readStringUntil('\n');
  Serial.print("HTTP Status: ");
  Serial.println(statusLine);

  // Skip headers
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;  // End of headers
  }

  // Read response body
  String response = "";
  while (client.available()) {
    response += client.readString();
  }
  
  client.stop();
  Serial.println("Response: " + response);
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
  } else if (!doc["text"].isNull()) {
    const char* transcription = doc["text"];
    Serial.println("\n=== Transcription ===");
    Serial.println(transcription);
    Serial.println("=====================\n");
    
    sendTextViaBLE(transcription);
  }
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
