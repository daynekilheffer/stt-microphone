#include "./KeyboardWrapper.h"

KeyboardWrapper kboard;

bool activeState = false;

void setup() {
  kboard.begin();
  
  // led pin setup
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  if (!kboard.isReady()) return;

  static uint32_t lastSendTime = 0;

  if (millis() - lastSendTime <= 5000) {
    return;
  }
  lastSendTime = millis();
  
  kboard.print("Key sent\n");
}