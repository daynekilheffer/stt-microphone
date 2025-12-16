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

private:
  Adafruit_USBD_HID usb_hid;
  void sendKey(uint8_t keycode, uint8_t modifier = 0);
};

#endif