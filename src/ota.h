#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

// Firmware signature verification
#define FIRMWARE_SIGNATURE_SIZE 64
#define FIRMWARE_PUBLIC_KEY "YOUR_PUBLIC_KEY_HERE"

bool checkAndPerformOTA(const char* updateUrl);
bool verifyFirmwareSignature(const uint8_t* firmware, size_t length, const char* signature);

#endif // OTA_H
