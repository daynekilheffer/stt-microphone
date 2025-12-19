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
  if (!usb_hid.ready() || !TinyUSBDevice.mounted()) return;
  
  if (TinyUSBDevice.suspended()) {
    TinyUSBDevice.remoteWakeup();
  }

  uint8_t keycodes[6] = {keycode, 0, 0, 0, 0, 0};
  usb_hid.keyboardReport(0, modifier, keycodes);
}

void KeyboardWrapper::task() {
  if (!TinyUSBDevice.mounted() || !pendingStr) return;
  
  // State machine for non-blocking character sending
  switch (keyState) {
    case IDLE:
      if (pendingStr[pendingIndex] == '\0') {
        // Done with string
        pendingStr = nullptr;
        pendingIndex = 0;
        return;
      }
      
      // Send next character
      if (usb_hid.ready()) {
        uint8_t c = (uint8_t)pendingStr[pendingIndex];
        
        if (c <= 127) {
          uint8_t const conv_table[128][2] = { HID_ASCII_TO_KEYCODE };
          uint8_t keycode = conv_table[c][1];
          
          if (keycode != 0) {
            uint8_t modifier = conv_table[c][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
            sendKey(keycode, modifier);
            keyState = PRESS_SENT;
            return;
          }
        }
        
        // move to next character
        pendingIndex++;
      }
      break;
      
    case PRESS_SENT:
      if (usb_hid.ready()) {
        usb_hid.keyboardRelease(0);
        keyState = WAITING_FOR_RELEASE;
      }
      break;
      
    case WAITING_FOR_RELEASE:
      if (usb_hid.ready()) {
        keyState = IDLE;
        pendingIndex++;
      }
      break;
  }
}

void KeyboardWrapper::print(const char* str) {
  // Queue the string for non-blocking sending
  pendingStr = str;
  pendingIndex = 0;
  keyState = IDLE;
}

void KeyboardWrapper::print(String str) {
  print(str.c_str());
}