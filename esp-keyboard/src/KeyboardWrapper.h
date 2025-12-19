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

private:
  Adafruit_USBD_HID usb_hid;
  void sendKey(uint8_t keycode, uint8_t modifier = 0);
  
  // State for non-blocking character sending
  enum KeyState { IDLE, PRESS_SENT, WAITING_FOR_RELEASE };
  KeyState keyState = IDLE;
  const char* pendingStr = nullptr;
  size_t pendingIndex = 0;
};

#endif