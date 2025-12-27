#include "KeyboardWrapper.h"

uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

volatile bool KeyboardWrapper::reportConsumed = true;

static volatile uint32_t callbackCount = 0;

// Quick LED pulse for debugging (non-blocking)
static void ledPulse(uint8_t pin, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(pin, HIGH);
    delay(30);
    digitalWrite(pin, LOW);
    if (i < count - 1) delay(30);
  }
}

KeyboardWrapper::KeyboardWrapper() {
}

void KeyboardWrapper::onReportComplete(uint8_t instance, uint8_t const* report, uint16_t len) {
  (void)instance;
  (void)report;
  (void)len;
  callbackCount++;
  reportConsumed = true;
}

// TinyUSB callback for HID report complete
extern "C" void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
  KeyboardWrapper::onReportComplete(instance, report, len);
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
        // Done with string - show status with LED
        // Quick blink = callbacks working, Long blink = timeouts
        if (callbackCount > 0) {
          ledPulse(D8, 2);  // 2 quick blinks = callbacks work
        } else {
          digitalWrite(D8, HIGH);
          delay(1500);  // Long blink = all timeouts
          digitalWrite(D8, LOW);
        }
        pendingStr = nullptr;
        pendingIndex = 0;
        return;
      }
      
      // Send next character
      if (usb_hid.ready() && (millis() - lastCharComplete >= CHAR_SPACING_MS)) {
        uint8_t c = (uint8_t)pendingStr[pendingIndex];
        
        if (c <= 127) {
          uint8_t const conv_table[128][2] = { HID_ASCII_TO_KEYCODE };
          uint8_t keycode = conv_table[c][1];
          
          if (keycode != 0) {
            uint8_t modifier = conv_table[c][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
            reportConsumed = false;
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
      if (reportConsumed && usb_hid.ready()) {
        stateTimer = millis();
        keyState = PRESS_WAIT;
      }
      break;
      
    case PRESS_WAIT:
      if (millis() - stateTimer >= PRESS_HOLD_MS) {
        reportConsumed = false;
        usb_hid.keyboardRelease(0);
        keyState = RELEASE_SENT;
      }
      break;
      
    case RELEASE_SENT:
      if (reportConsumed && usb_hid.ready()) {
        stateTimer = millis();
        keyState = RELEASE_WAIT;
      }
      break;
      
    case RELEASE_WAIT:
      if (millis() - stateTimer >= RELEASE_HOLD_MS) {
        lastCharComplete = millis();
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
  callbackCount = 0;
}

void KeyboardWrapper::print(String str) {
  print(str.c_str());
}