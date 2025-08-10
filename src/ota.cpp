#define TINY_GSM_MODEM_SIM7000
#include "ota.h"
#include "rtc_state.h"
#include "config.h"
#include <TinyGsmClient.h>
#include <Update.h>
#include <ArduinoJson.h>

#define SerialMon Serial

// Declare external modem from main.cpp
extern TinyGsm modem;

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

// Simple HTTP GET request using modem's built-in HTTP client
String getServerFirmwareVersion(const char* versionUrl) {
  SerialMon.println("CHECKING FOR FIRMWARE UPDATES");
  SerialMon.printf("Version URL: %s\n", versionUrl);
  SerialMon.printf("Current firmware version: %s\n", FIRMWARE_VERSION);
  SerialMon.println("----------------------------------------");
  
  // Initialize HTTP service
  SerialMon.println("Initializing HTTP service...");
  modem.sendAT("+HTTPINIT");
  if (modem.waitResponse() != 1) {
    SerialMon.println("Failed to initialize HTTP service");
    return "";
  }
  
  // Set URL
  SerialMon.printf("Setting URL: %s\n", versionUrl);
  modem.sendAT("+HTTPPARA=\"URL\",\"" + String(versionUrl) + "\"");
  if (modem.waitResponse() != 1) {
    SerialMon.println("Failed to set URL parameter");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Set minimal headers to avoid 603 errors
  SerialMon.println("Setting minimal headers...");
  modem.sendAT("+HTTPPARA=\"USERDATA\",\"User-Agent: PlayBuoy/1.0\"");
  if (modem.waitResponse() != 1) {
    SerialMon.println("Warning: Custom headers not supported");
  }
  
  // Set data size to 0 for GET request
  SerialMon.println("Setting HTTP data size...");
  modem.sendAT("+HTTPDATA=0,10000");
  if (modem.waitResponse() != 1) {
    SerialMon.println("Failed to set HTTP data size");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Execute HTTP GET request
  SerialMon.println("Executing HTTP GET request...");
  modem.sendAT("+HTTPACTION=0");
  if (modem.waitResponse(30000L) != 1) {
    SerialMon.println("HTTP GET request failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Read HTTP response
  SerialMon.println("Reading HTTP response...");
  modem.sendAT("+HTTPREAD");
  if (modem.waitResponse(10000L) != 1) {
    SerialMon.println("Failed to get HTTPREAD response");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Read data with proper timeout handling
  String response = "";
  unsigned long timeout = millis();
  
  // Wait for data to start arriving
  while (!modem.stream.available() && millis() - timeout < 5000) {
    delay(10);
  }
  
  // Read all available data
  while (modem.stream.available() || millis() - timeout < 10000) {
    if (modem.stream.available()) {
      char c = modem.stream.read();
      response += c;
      timeout = millis();
    }
  }
  
  SerialMon.printf("Raw response: '%s'\n", response.c_str());
  SerialMon.printf("Response length: %d\n", response.length());
  
  // Terminate HTTP service
  modem.sendAT("+HTTPTERM");
  modem.waitResponse();
  
  // Parse response - look for version string
  response.trim();
  
  // Check if response looks like a version string (x.x.x format)
  if (response.length() > 0 && response.length() < 20) {
    if (response.indexOf('.') > 0 && response.indexOf('.') < response.length() - 1) {
      SerialMon.printf("Extracted version: %s\n", response.c_str());
      return response;
    }
  }
  
  SerialMon.println("Invalid version format in response");
  return "";
}

bool downloadAndCheckVersion(const char* versionUrl) {
  SerialMon.println("CHECKING FOR FIRMWARE UPDATES");
  SerialMon.printf("Version URL: %s\n", versionUrl);
  SerialMon.printf("Current firmware version: %s\n", FIRMWARE_VERSION);
  SerialMon.println("----------------------------------------");
  
  String serverVersion = getServerFirmwareVersion(versionUrl);
  
  if (serverVersion.length() == 0) {
    SerialMon.println("Could not retrieve server version");
    return false;
  }
  
  SerialMon.printf("Server version retrieved: %s\n", serverVersion.c_str());
  
  int comparison = compareVersions(serverVersion, FIRMWARE_VERSION);
  
  SerialMon.printf("Version comparison: %s vs %s = %d\n", 
                   serverVersion.c_str(), FIRMWARE_VERSION, comparison);
  
  if (comparison > 0) {
    SerialMon.println("NEW FIRMWARE AVAILABLE!");
    SerialMon.printf("Server: %s > Current: %s\n", serverVersion.c_str(), FIRMWARE_VERSION);
    return true;
  } else if (comparison == 0) {
    SerialMon.println("Firmware is up to date");
    SerialMon.printf("Server: %s = Current: %s\n", serverVersion.c_str(), FIRMWARE_VERSION);
  } else {
    SerialMon.println("Server version is older than current version");
    SerialMon.printf("Server: %s < Current: %s\n", serverVersion.c_str(), FIRMWARE_VERSION);
  }
  
  return false;
}

bool downloadAndInstallFirmware(const char* firmwareUrl) {
  SerialMon.printf("Downloading firmware from: %s\n", firmwareUrl);
  
  // For now, we'll need to implement a different approach for firmware download
  // as the modem's HTTP client doesn't easily support large binary downloads
  SerialMon.println("Firmware download via modem HTTP client not yet implemented");
  SerialMon.println("This requires a different approach for large binary downloads");
  
  return false;
}

bool checkForFirmwareUpdate(const char* baseUrl) {
  if (rtcState.firmwareUpdateAttempted) {
    SerialMon.println("OTA already attempted this cycle. Skipping.");
    return false;
  }
  
  SerialMon.printf("OTA: Base URL received: %s\n", baseUrl);
  
  // Construct version URL (e.g., baseUrl + ".version")
  String versionUrl = String(baseUrl);
  SerialMon.printf("OTA: Initial versionUrl: %s\n", versionUrl.c_str());
  
  if (versionUrl.endsWith(".bin")) {
    versionUrl = versionUrl.substring(0, versionUrl.length() - 4) + ".version";
    SerialMon.printf("OTA: URL ends with .bin, modified to: %s\n", versionUrl.c_str());
  } else {
    // For base URLs, append .version (not /version.json)
    versionUrl += ".version";
    SerialMon.printf("OTA: URL does not end with .bin, appended .version: %s\n", versionUrl.c_str());
  }
  
  SerialMon.printf("OTA: Final version URL: %s\n", versionUrl.c_str());
  
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
