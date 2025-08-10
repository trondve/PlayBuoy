#include "ota.h"
#include "rtc_state.h"
#include "config.h"
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>

#define SerialMon Serial

// Version comparison function
int compareVersions(const String& version1, const String& version2) {
  int v1[3] = {0, 0, 0};
  int v2[3] = {0, 0, 0};
  
  // Parse version strings (format: "major.minor.patch")
  sscanf(version1.c_str(), "%d.%d.%d", &v1[0], &v1[1], &v1[2]);
  sscanf(version2.c_str(), "%d.%d.%d", &v2[0], &v2[1], &v2[2]);
  
  // Compare major version
  if (v1[0] > v2[0]) return 1;
  if (v1[0] < v2[0]) return -1;
  
  // Compare minor version
  if (v1[1] > v2[1]) return 1;
  if (v1[1] < v2[1]) return -1;
  
  // Compare patch version
  if (v1[2] > v2[2]) return 1;
  if (v1[2] < v2[2]) return -1;
  
  return 0; // Versions are equal
}

String getServerFirmwareVersion(const char* versionUrl) {
  HTTPClient http;
  http.begin(versionUrl);
  
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    SerialMon.printf("Failed to get version info. HTTP code: %d\n", httpCode);
    http.end();
    return "";
  }
  
  String payload = http.getString();
  http.end();
  
  // Try to parse as JSON first
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, payload);
  
  if (!error) {
    // JSON format: {"version": "1.0.1", "url": "firmware.bin"}
    if (doc.containsKey("version")) {
      return doc["version"].as<String>();
    }
  }
  
  // Fallback: treat as plain text version
  payload.trim();
  if (payload.length() > 0 && payload.length() < 20) {
    return payload;
  }
  
  SerialMon.println("Invalid version format received");
  return "";
}

bool downloadAndCheckVersion(const char* versionUrl) {
  SerialMon.println("Checking for firmware updates...");
  
  String serverVersion = getServerFirmwareVersion(versionUrl);
  if (serverVersion.length() == 0) {
    SerialMon.println("Could not retrieve server version");
    return false;
  }
  
  String currentVersion = String(FIRMWARE_VERSION);
  SerialMon.printf("Current version: %s\n", currentVersion.c_str());
  SerialMon.printf("Server version: %s\n", serverVersion.c_str());
  
  int comparison = compareVersions(serverVersion, currentVersion);
  
  if (comparison > 0) {
    SerialMon.println("✅ New firmware available!");
    return true;
  } else if (comparison == 0) {
    SerialMon.println("✅ Firmware is up to date");
  } else {
    SerialMon.println("⚠️ Server version is older than current version");
  }
  
  return false;
}

bool downloadAndInstallFirmware(const char* firmwareUrl) {
  SerialMon.printf("Downloading firmware from: %s\n", firmwareUrl);
  
  HTTPClient http;
  http.begin(firmwareUrl);
  
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    SerialMon.printf("HTTP GET failed. Code: %d\n", httpCode);
    http.end();
    return false;
  }
  
  int contentLength = http.getSize();
  if (contentLength <= 0) {
    SerialMon.println("Content length is invalid.");
    http.end();
    return false;
  }
  
  SerialMon.printf("Firmware size: %d bytes\n", contentLength);
  
  if (!Update.begin(contentLength)) {
    SerialMon.println("Update.begin() failed.");
    http.end();
    return false;
  }
  
  WiFiClient* stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  
  if (written != contentLength) {
    SerialMon.printf("Written only %d of %d bytes.\n", written, contentLength);
    http.end();
    Update.abort();
    return false;
  }
  
  // Verify firmware integrity
  if (!Update.end()) {
    SerialMon.printf("Update.end() failed. Error: %s\n", Update.errorString());
    http.end();
    Update.abort();
    return false;
  }
  
  if (!Update.isFinished()) {
    SerialMon.println("Update did not finish.");
    http.end();
    Update.abort();
    return false;
  }
  
  SerialMon.println("✅ Firmware download and verification successful");
  http.end();
  return true;
}

bool checkForFirmwareUpdate(const char* baseUrl) {
  if (rtcState.firmwareUpdateAttempted) {
    SerialMon.println("OTA already attempted this cycle. Skipping.");
    return false;
  }
  
  // Construct version URL (e.g., baseUrl + "/version.json" or baseUrl + ".version")
  String versionUrl = String(baseUrl);
  if (versionUrl.endsWith(".bin")) {
    versionUrl = versionUrl.substring(0, versionUrl.length() - 4) + ".version";
  } else {
    versionUrl += "/version.json";
  }
  
  SerialMon.printf("Checking version at: %s\n", versionUrl.c_str());
  
  // Check if new version is available
  if (!downloadAndCheckVersion(versionUrl.c_str())) {
    return false;
  }
  
  // Mark that we've attempted an update this cycle
  markFirmwareUpdateAttempted();
  
  // Download and install the firmware
  if (downloadAndInstallFirmware(baseUrl)) {
    SerialMon.println("OTA update successful. Rebooting...");
    delay(1000);
    ESP.restart();
    return true;
  } else {
    SerialMon.println("OTA update failed. Continuing with current firmware.");
    return false;
  }
}

// Legacy function for backward compatibility
bool checkAndPerformOTA(const char* url) {
  return checkForFirmwareUpdate(url);
}

bool verifyFirmwareSignature(const uint8_t* firmware, size_t length, const char* signature) {
  // TODO: Implement firmware signature verification
  SerialMon.println("Firmware signature verification not implemented");
  return true;
}
