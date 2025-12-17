#include "KeyboardWrapper.h"

uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

KeyboardWrapper::KeyboardWrapper() {
}

void KeyboardWrapper::begin() {
  // Set USB device descriptors before begin()
  TinyUSBDevice.setProductDescriptor("STT Microphone");
  TinyUSBDevice.setManufacturerDescriptor("Dayne");
  
  // Manual begin() is required on core without built-in support
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }
  delay(100);

  // Setup HID
  usb_hid.setBootProtocol(HID_ITF_PROTOCOL_KEYBOARD);
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setStringDescriptor("ESP32 Keyboard");

  usb_hid.begin();

  // Re-enumerate if already mounted
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
}

bool KeyboardWrapper::isReady() {
  return TinyUSBDevice.mounted() && usb_hid.ready();
}

void KeyboardWrapper::sendKey(uint8_t keycode, uint8_t modifier) {
  while (!usb_hid.ready()) {
    if (!TinyUSBDevice.mounted()) return;
    delay(1);
  }

  if (TinyUSBDevice.suspended()) {
    TinyUSBDevice.remoteWakeup();
  }

  uint8_t keycodes[6] = {keycode, 0, 0, 0, 0, 0};
  usb_hid.keyboardReport(0, modifier, keycodes);
  delay(5);
  while (!usb_hid.ready()) {
    if (!TinyUSBDevice.mounted()) return;
    delay(1);
  }
  usb_hid.keyboardRelease(0);
  delay(5);
}

void KeyboardWrapper::print(const char* str) {
  for (size_t i = 0; str[i] != '\0'; i++) {
    uint8_t c = (uint8_t)str[i];
    
    // Skip characters outside the lookup table range
    if (c > 127) continue;
    
    uint8_t const conv_table[128][2] =  { HID_ASCII_TO_KEYCODE };
    uint8_t modifier   = 0;
    uint8_t keycode = conv_table[c][1];
    if ( conv_table[c][0] ) modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
    
    if (keycode != 0) {
      sendKey(keycode, modifier);
    }
  }
}

void KeyboardWrapper::print(String str) {
  print(str.c_str());
}