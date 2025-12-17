#include "./KeyboardWrapper.h"

KeyboardWrapper kboard;
bool printingEnabled = false;
bool lastButtonState = HIGH;

void setup() {
  pinMode(D8, OUTPUT);
  pinMode(D10, INPUT_PULLUP);
  kboard.begin();
  digitalWrite(D8, LOW);
}

void loop() {
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
  
  kboard.print("a");
}