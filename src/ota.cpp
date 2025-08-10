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

String getServerFirmwareVersion(const char* versionUrl) {
  // Parse URL to extract host and path
  String url = String(versionUrl);
  String host = "";
  String path = "";
  
  // Remove protocol
  if (url.startsWith("https://")) {
    url = url.substring(8);
  } else if (url.startsWith("http://")) {
    url = url.substring(7);
  }
  
  // Split host and path
  int slashIndex = url.indexOf('/');
  if (slashIndex > 0) {
    host = url.substring(0, slashIndex);
    path = url.substring(slashIndex);
  } else {
    host = url;
    path = "/";
  }
  
  SerialMon.printf("Checking version from: %s%s\n", host.c_str(), path.c_str());
  
  TinyGsmClient client(modem);
  
  // GitHub requires HTTPS - no HTTP fallback
  bool connected = false;
  if (host.indexOf("githubusercontent.com") >= 0 || url.startsWith("https://")) {
    SerialMon.printf("Attempting HTTPS connection to %s:443\n", host.c_str());
    SerialMon.printf("URL: %s\n", url.c_str());
    SerialMon.printf("Host: %s\n", host.c_str());
    SerialMon.printf("Path: %s\n", path.c_str());
    
    connected = client.connect(host.c_str(), 443);
    if (connected) {
      SerialMon.println("✅ HTTPS connection successful");
    } else {
      SerialMon.println("❌ HTTPS connection failed");
      SerialMon.printf("Connection failed - no error code available\n");
    }
  } else {
    SerialMon.printf("Using HTTP connection to %s:80\n", host.c_str());
    connected = client.connect(host.c_str(), 80);
  }
  
  if (!connected) {
    SerialMon.printf("Failed to connect to %s\n", host.c_str());
    return "";
  }
  
  // Build HTTP request with comprehensive headers
  String request = 
    String("GET ") + path + " HTTP/1.1\r\n" +
    "Host: " + host + "\r\n" +
    "User-Agent: PlayBuoy/1.0\r\n" +
    "Accept: text/plain, application/json, */*\r\n" +
    "Accept-Encoding: identity\r\n" +
    "Cache-Control: no-cache\r\n" +
    "Connection: close\r\n\r\n";
  
  SerialMon.println("=== HTTP REQUEST ===");
  SerialMon.println(request);
  SerialMon.println("=== END REQUEST ===");
  
  int bytesWritten = client.print(request);
  SerialMon.printf("Request bytes written: %d\n", bytesWritten);
  
  // Read response with comprehensive debugging
  String response = "";
  unsigned long timeout = millis();
  int bytesRead = 0;
  int availableCount = 0;
  
  SerialMon.println("=== READING HTTP RESPONSE ===");
  SerialMon.printf("Client connected: %s\n", client.connected() ? "YES" : "NO");
  SerialMon.printf("Client available: %d\n", client.available());
  
  while (client.connected() && millis() - timeout < 15000) { // Increased timeout
    availableCount = client.available();
    if (availableCount > 0) {
      SerialMon.printf("Available bytes: %d\n", availableCount);
      
      for (int i = 0; i < availableCount && i < 10; i++) { // Read up to 10 bytes at once
        char c = client.read();
        response += c;
        bytesRead++;
        
        // Log first 50 bytes in detail
        if (bytesRead <= 50) {
          SerialMon.printf("Byte %d: 0x%02X ('%c') [available: %d]\n", 
                          bytesRead, (unsigned char)c, 
                          (c >= 32 && c <= 126) ? c : '?', 
                          client.available());
        }
      }
      
      // Reset timeout on successful read
      timeout = millis();
    } else {
      delay(100); // Small delay when no data available
    }
  }
  
  SerialMon.printf("=== RESPONSE SUMMARY ===\n");
  SerialMon.printf("Total bytes read: %d\n", bytesRead);
  SerialMon.printf("Response length: %d\n", response.length());
  SerialMon.printf("Client still connected: %s\n", client.connected() ? "YES" : "NO");
  SerialMon.printf("Client available: %d\n", client.available());
  
  client.stop();
  
  // Parse HTTP response with detailed debugging
  SerialMon.println("=== PARSING HTTP RESPONSE ===");
  SerialMon.printf("Full response length: %d\n", response.length());
  
  // Show first 200 characters of response
  String responsePreview = response.substring(0, min(200, (int)response.length()));
  SerialMon.println("Response preview:");
  SerialMon.println(responsePreview);
  
  int bodyStart = response.indexOf("\r\n\r\n");
  SerialMon.printf("Body start position: %d\n", bodyStart);
  
  if (bodyStart > 0) {
    String headers = response.substring(0, bodyStart);
    String body = response.substring(bodyStart + 4);
    body.trim();
    
    SerialMon.println("=== HTTP HEADERS ===");
    SerialMon.println(headers);
    SerialMon.println("=== END HEADERS ===");
    
    SerialMon.printf("Version response body: '%s' (length: %d)\n", body.c_str(), body.length());
    
    // Try to parse as JSON first
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, body);
    
    if (!error) {
      // JSON format: {"version": "1.0.1", "url": "firmware.bin"}
      if (doc.containsKey("version")) {
        String version = doc["version"].as<String>();
        SerialMon.printf("Parsed JSON version: %s\n", version.c_str());
        return version;
      }
    }
    
    // Fallback: treat as plain text version
    if (body.length() > 0 && body.length() < 20) {
      SerialMon.printf("Using plain text version: %s\n", body.c_str());
      return body;
    }
    
    // Handle HTML-wrapped content from raw.githubusercontent.com
    if (body.indexOf("<pre>") >= 0) {
      int preStart = body.indexOf("<pre>");
      int preEnd = body.indexOf("</pre>");
      if (preStart >= 0 && preEnd > preStart) {
        String version = body.substring(preStart + 5, preEnd);
        version.trim();
        SerialMon.printf("Extracted version from HTML: %s\n", version.c_str());
        return version;
      }
    }
  }
  
  SerialMon.printf("Invalid version format received. Full response: %s\n", response.c_str());
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
  
  // Parse URL to extract host and path
  String url = String(firmwareUrl);
  String host = "";
  String path = "";
  
  // Remove protocol
  if (url.startsWith("https://")) {
    url = url.substring(8);
  } else if (url.startsWith("http://")) {
    url = url.substring(7);
  }
  
  // Split host and path
  int slashIndex = url.indexOf('/');
  if (slashIndex > 0) {
    host = url.substring(0, slashIndex);
    path = url.substring(slashIndex);
  } else {
    host = url;
    path = "/";
  }
  
  TinyGsmClient client(modem);
  
  // GitHub requires HTTPS - no HTTP fallback
  bool connected = false;
  if (host.indexOf("githubusercontent.com") >= 0 || url.startsWith("https://")) {
    SerialMon.printf("Attempting HTTPS connection to %s:443\n", host.c_str());
    SerialMon.printf("URL: %s\n", url.c_str());
    SerialMon.printf("Host: %s\n", host.c_str());
    SerialMon.printf("Path: %s\n", path.c_str());
    
    connected = client.connect(host.c_str(), 443);
    if (connected) {
      SerialMon.println("✅ HTTPS connection successful");
    } else {
      SerialMon.println("❌ HTTPS connection failed");
      SerialMon.printf("Connection failed - no error code available\n");
    }
  } else {
    SerialMon.printf("Using HTTP connection to %s:80\n", host.c_str());
    connected = client.connect(host.c_str(), 80);
  }
  
  if (!connected) {
    SerialMon.printf("Failed to connect to %s\n", host.c_str());
    return false;
  }
  
  // Build HTTP request with comprehensive headers
  String request = 
    String("GET ") + path + " HTTP/1.1\r\n" +
    "Host: " + host + "\r\n" +
    "User-Agent: PlayBuoy/1.0\r\n" +
    "Accept: text/plain, application/json, */*\r\n" +
    "Accept-Encoding: identity\r\n" +
    "Cache-Control: no-cache\r\n" +
    "Connection: close\r\n\r\n";
  
  SerialMon.println("=== HTTP REQUEST ===");
  SerialMon.println(request);
  SerialMon.println("=== END REQUEST ===");
  
  int bytesWritten = client.print(request);
  SerialMon.printf("Request bytes written: %d\n", bytesWritten);
  
  // Read HTTP headers to find content length
  String headers = "";
  int contentLength = 0;
  bool foundBody = false;
  
  while (client.connected() && !foundBody) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      headers += line + "\n";
      
      if (line.startsWith("Content-Length: ")) {
        contentLength = line.substring(16).toInt();
      }
      
      if (line.length() <= 1) { // Empty line marks end of headers
        foundBody = true;
      }
    }
  }
  
  if (contentLength <= 0) {
    SerialMon.println("Content length is invalid.");
    client.stop();
    return false;
  }
  
  SerialMon.printf("Firmware size: %d bytes\n", contentLength);
  
  if (!Update.begin(contentLength)) {
    SerialMon.println("Update.begin() failed.");
    client.stop();
    return false;
  }
  
  // Read firmware data
  size_t written = 0;
  unsigned long timeout = millis();
  
  while (written < contentLength && client.connected() && millis() - timeout < 300000) { // 5 minute timeout
    if (client.available()) {
      uint8_t buffer[1024];
      int bytesRead = client.read(buffer, min(1024, (int)(contentLength - written)));
      if (bytesRead > 0) {
        size_t bytesWritten = Update.write(buffer, bytesRead);
        written += bytesWritten;
        timeout = millis(); // Reset timeout on successful read
        
        // Progress indicator
        if (written % 10000 == 0) {
          SerialMon.printf("Downloaded: %d/%d bytes (%.1f%%)\n", written, contentLength, (float)written/contentLength*100);
        }
      }
    }
  }
  
  if (written != contentLength) {
    SerialMon.printf("Written only %d of %d bytes.\n", written, contentLength);
    client.stop();
    Update.abort();
    return false;
  }
  
  // Verify firmware integrity
  if (!Update.end()) {
    SerialMon.printf("Update.end() failed. Error: %s\n", Update.errorString());
    client.stop();
    Update.abort();
    return false;
  }
  
  if (!Update.isFinished()) {
    SerialMon.println("Update did not finish.");
    client.stop();
    Update.abort();
    return false;
  }
  
  SerialMon.println("✅ Firmware download and verification successful");
  client.stop();
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
