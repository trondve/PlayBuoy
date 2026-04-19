#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

// Version checking API (HTTP-only)
bool checkForFirmwareUpdate(const char* baseUrl);
bool downloadAndCheckVersion(const char* versionUrl);
String getServerFirmwareVersion(const char* versionUrl);
bool downloadAndInstallFirmware(const char* firmwareUrl, const uint8_t* expectedSha256 = nullptr);

#endif // OTA_H
