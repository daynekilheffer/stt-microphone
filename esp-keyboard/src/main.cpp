#include "./KeyboardWrapper.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <../include/secrets.h>

#ifndef STT_DEBUG
#define STT_DEBUG 0
#endif

#ifndef STT_BUTTON_DEBUG
#define STT_BUTTON_DEBUG 0
#endif

KeyboardWrapper kboard;

// ESP-NOW received data
volatile bool dataReceived = false;
char receivedMessage[250];

// ESP-NOW receive callback
void onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
  if (len > 0 && len < sizeof(receivedMessage)) {
    memcpy(receivedMessage, data, len);
    receivedMessage[len] = '\0';
    dataReceived = true;
  }
}
uint8_t getAPChannel(const char* ssid) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  
  for (int i = 0; i < n; i++) {
    if (strcmp(WiFi.SSID(i).c_str(), ssid) == 0) {
      uint8_t channel = WiFi.channel(i);
      WiFi.scanDelete();
      return channel;
    }
  }
  WiFi.scanDelete();
  return 1;  // Default fallback
}

void setup() {
  pinMode(D8, OUTPUT);
  #ifdef STT_BUTTON_DEBUG
  pinMode(D10, INPUT_PULLUP);
  #endif
  kboard.begin();
  digitalWrite(D8, LOW);

  // Flash LED to indicate startup
  for (int i = 0; i < 3; i++) {
    digitalWrite(D8, HIGH);
    delay(100);
    digitalWrite(D8, LOW);
    delay(100);
  }

  // Initialize WiFi in station mode for ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  uint8_t channel = getAPChannel(STT_WIFI_SSID);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  
  #if STT_DEBUG
  kboard.print("ssid: " + String(STT_WIFI_SSID) + ", channel: " + String(channel) + "\n");
  kboard.print("MAC: " + WiFi.macAddress() + "\n");
  #endif

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    return;
  }

  // Register receive callback
  esp_now_register_recv_cb(onDataRecv);
}

void loop() {
  // Call keyboard task for non-blocking character sending
  kboard.task();
  
  #ifdef STT_BUTTON_DEBUG
  if (digitalRead(D10) == LOW) {
    // Flash LED to indicate button press
    digitalWrite(D8, HIGH);
    delay(50);
    digitalWrite(D8, LOW);
    delay(50);
    
    if (kboard.isReady()) {
      kboard.print("pressed lorem ipsum");
    }
    // Debounce delay
    delay(300);
  }
  #endif

  // Handle received ESP-NOW data
  if (dataReceived) {
    dataReceived = false;
    
    // Flash LED to indicate message received
    digitalWrite(D8, HIGH);
    delay(50);
    digitalWrite(D8, LOW);
    delay(50);
    
    if (kboard.isReady()) {
      kboard.print(receivedMessage);
    }
  }
}