#include "./KeyboardWrapper.h"
#include <WiFi.h>
#include <esp_now.h>

KeyboardWrapper kboard;
bool printingEnabled = false;
bool lastButtonState = HIGH;

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

void setup() {
  pinMode(D8, OUTPUT);
  pinMode(D10, INPUT_PULLUP);
  kboard.begin();
  digitalWrite(D8, LOW);

  // Initialize WiFi in station mode for ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    return;
  }

  // Register receive callback
  esp_now_register_recv_cb(onDataRecv);
}

void loop() {
  // Handle received ESP-NOW data
  if (dataReceived) {
    dataReceived = false;
    
    if (kboard.isReady()) {
      kboard.print(receivedMessage);
    }
  }

  // Read button and toggle on press (LOW when pressed)
  bool buttonState = digitalRead(D10);
  if (buttonState == LOW && lastButtonState == HIGH) {
    printingEnabled = !printingEnabled;
    delay(50); // Debounce
  }
  lastButtonState = buttonState;

  if (!kboard.isReady()) return;
  digitalWrite(D8, printingEnabled ? HIGH : LOW);

  if (!printingEnabled) return;

  static uint32_t lastSendTime = 0;

  if (millis() - lastSendTime <= 5000) {
    return;
  }
  lastSendTime = millis();
  
  uint64_t mac = ESP.getEfuseMac();

  char buf[18];
  snprintf(buf, sizeof(buf),
         "%02X:%02X:%02X:%02X:%02X:%02X",
         (uint8_t)(mac >> 40),
         (uint8_t)(mac >> 32),
         (uint8_t)(mac >> 24),
         (uint8_t)(mac >> 16),
         (uint8_t)(mac >> 8),
         (uint8_t)(mac));

  kboard.print(buf);
}