#ifndef KEYBOARD_WRAPPER_H
#define KEYBOARD_WRAPPER_H

#include "Adafruit_TinyUSB.h"

class KeyboardWrapper {
public:
  KeyboardWrapper();
  void begin();
  void print(const char* str);
  void print(String str);
  bool isReady();
  void task(); // Must be called in loop() for non-blocking operation
  
  // Track when host has consumed the report
  static volatile bool reportConsumed;
  static void onReportComplete(uint8_t instance, uint8_t const* report, uint16_t len);

private:
  Adafruit_USBD_HID usb_hid;
  void sendKey(uint8_t keycode, uint8_t modifier = 0);
  
  // State for non-blocking character sending
  enum KeyState { IDLE, PRESS_SENT, PRESS_WAIT, RELEASE_SENT, RELEASE_WAIT };
  KeyState keyState = IDLE;
  const char* pendingStr = nullptr;
  size_t pendingIndex = 0;
  unsigned long lastCharComplete = 0;
  unsigned long stateTimer = 0;
  static const unsigned long CHAR_SPACING_MS = 10; // Min time between characters
  static const unsigned long PRESS_HOLD_MS = 16;   // Hold press before release
  static const unsigned long RELEASE_HOLD_MS = 16; // Hold release before next char
};

#endif