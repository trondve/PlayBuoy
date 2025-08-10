#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

// Firmware signature verification
#define FIRMWARE_SIGNATURE_SIZE 64
#define FIRMWARE_PUBLIC_KEY "YOUR_PUBLIC_KEY_HERE"

// OTA functions
bool checkAndPerformOTA(const char* updateUrl);
bool verifyFirmwareSignature(const uint8_t* firmware, size_t length, const char* signature);

// Version checking functions
bool checkForFirmwareUpdate(const char* baseUrl);
bool downloadAndCheckVersion(const char* versionUrl);
bool downloadAndInstallFirmware(const char* firmwareUrl);
String getServerFirmwareVersion(const char* versionUrl);

#endif // OTA_H
