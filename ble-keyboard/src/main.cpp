#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "USB.h"
#include "USBHIDKeyboard.h"

USBHIDKeyboard Keyboard;

#ifndef SERIAL_BAUD_RATE
#define SERIAL_BAUD_RATE 9600
#endif

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("Device connected");
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("Device disconnected");
    BLEDevice::startAdvertising();
  }
};

class CharacteristicCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();

    if (value.length() > 0) {
    Serial.print("Received: ");
    for (int i = 0; i < value.length(); i++) {
      Serial.print(value[i]);
    }
    Serial.println();

    Keyboard.print(value.c_str());
    }
  }
};

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.println("Starting BLE Keyboard Server...");

  USB.productName("ESP32 BLE Keyboard");

  Keyboard.begin();
  USB.begin();

  BLEDevice::init("ESP32 Keyboard Server");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
            CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE
          );

  pCharacteristic->setCallbacks(new CharacteristicCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  
  // Add a user description to the characteristic
  BLEDescriptor *pDescriptor = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
  pDescriptor->setValue("Text Input");
  pCharacteristic->addDescriptor(pDescriptor);

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Waiting for client connection to receive text...");
}

void loop() {
  delay(2000);
}