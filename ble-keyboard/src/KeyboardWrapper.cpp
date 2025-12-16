#include "KeyboardWrapper.h"

uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

KeyboardWrapper::KeyboardWrapper() {
}

void KeyboardWrapper::begin() {
  // Manual begin() is required on core without built-in support
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

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
  if (!isReady()) return;
  
  if (TinyUSBDevice.suspended()) {
    TinyUSBDevice.remoteWakeup();
  }

  uint8_t keycodes[6] = {keycode, 0, 0, 0, 0, 0};
  usb_hid.keyboardReport(0, modifier, keycodes);
  delay(2);
  usb_hid.keyboardRelease(0);
}

void KeyboardWrapper::print(const char* str) {
  for (size_t i = 0; str[i] != '\0'; i++) {
    char c = str[i];
    uint8_t keycode = 0;
    uint8_t modifier = 0;

    // Convert character to HID keycode
    if (c >= 'a' && c <= 'z') {
      keycode = HID_KEY_A + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
      keycode = HID_KEY_A + (c - 'A');
      modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
    } else if (c >= '1' && c <= '9') {
      keycode = HID_KEY_1 + (c - '1');
    } else if (c == '0') {
      keycode = HID_KEY_0;
    } else if (c == ' ') {
      keycode = HID_KEY_SPACE;
    } else if (c == '\n') {
      keycode = HID_KEY_ENTER;
    }

    if (keycode != 0) {
      sendKey(keycode, modifier);
    }
  }
}

void KeyboardWrapper::print(String str) {
  print(str.c_str());
}