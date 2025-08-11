#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

// Version checking API (HTTP-only)
bool checkForFirmwareUpdate(const char* baseUrl);
bool downloadAndCheckVersion(const char* versionUrl);
String getServerFirmwareVersion(const char* versionUrl);

#endif // OTA_H
