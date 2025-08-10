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

// Forward declarations
String testMethod6_EnhancedAtCommandsHttps(const char* versionUrl);

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

// Test method 1: Modem's built-in HTTP client with HTTPS
String testMethod1_ModemHttpHttps(const char* versionUrl) {
  SerialMon.println("🔬 TEST METHOD 1: Modem HTTP Client (HTTPS)");
  
  // Initialize HTTP service
  SerialMon.println("  → Initializing HTTP service...");
  modem.sendAT("+HTTPINIT");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ Failed to initialize HTTP service");
    return "";
  }
  
  // Set URL (keep HTTPS)
  SerialMon.printf("  → Setting URL: %s\n", versionUrl);
  modem.sendAT("+HTTPPARA=\"URL\",\"" + String(versionUrl) + "\"");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ Failed to set URL parameter");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Set custom headers
  SerialMon.println("  → Setting custom headers...");
  modem.sendAT("+HTTPPARA=\"USERDATA\",\"User-Agent: PlayBuoy/1.0\\r\\nAccept: text/plain, application/json\"");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ⚠️ Custom headers not supported");
  }
  
  // Set data size
  modem.sendAT("+HTTPDATA=0,10000");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ Failed to set HTTP data size");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Execute request
  SerialMon.println("  → Executing HTTP GET request...");
  modem.sendAT("+HTTPACTION=0");
  if (modem.waitResponse(30000L) != 1) {
    SerialMon.println("  ❌ HTTP GET request failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Read response
  SerialMon.println("  → Reading HTTP response...");
  modem.sendAT("+HTTPREAD");
  if (modem.waitResponse(10000L) != 1) {
    SerialMon.println("  ❌ Failed to get HTTPREAD response");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Read data
  String response = "";
  unsigned long timeout = millis();
  while (modem.stream.available() || millis() - timeout < 10000) {
    if (modem.stream.available()) {
      char c = modem.stream.read();
      response += c;
      timeout = millis();
    }
  }
  
  SerialMon.printf("  → Raw response: '%s'\n", response.c_str());
  SerialMon.printf("  → Response length: %d\n", response.length());
  
  // Terminate
  modem.sendAT("+HTTPTERM");
  modem.waitResponse();
  
  // Parse response
  response.trim();
  if (response.length() > 0 && response.length() < 20) {
    SerialMon.printf("  ✅ Extracted version: %s\n", response.c_str());
    return response;
  }
  
  SerialMon.println("  ❌ Invalid version format");
  return "";
}

// Test method 2: Modem's built-in HTTP client with HTTP (fallback)
String testMethod2_ModemHttpHttp(const char* versionUrl) {
  SerialMon.println("🔬 TEST METHOD 2: Modem HTTP Client (HTTP)");
  
  // Convert HTTPS to HTTP
  String httpUrl = String(versionUrl);
  if (httpUrl.startsWith("https://")) {
    httpUrl = "http://" + httpUrl.substring(8);
    SerialMon.printf("  → Converted to HTTP: %s\n", httpUrl.c_str());
  }
  
  // Initialize HTTP service
  SerialMon.println("  → Initializing HTTP service...");
  modem.sendAT("+HTTPINIT");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ Failed to initialize HTTP service");
    return "";
  }
  
  // Set URL
  modem.sendAT("+HTTPPARA=\"URL\",\"" + httpUrl + "\"");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ Failed to set URL parameter");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Set data size
  modem.sendAT("+HTTPDATA=0,10000");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ Failed to set HTTP data size");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Execute request
  SerialMon.println("  → Executing HTTP GET request...");
  modem.sendAT("+HTTPACTION=0");
  if (modem.waitResponse(30000L) != 1) {
    SerialMon.println("  ❌ HTTP GET request failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Read response
  SerialMon.println("  → Reading HTTP response...");
  modem.sendAT("+HTTPREAD");
  if (modem.waitResponse(10000L) != 1) {
    SerialMon.println("  ❌ Failed to get HTTPREAD response");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Read data
  String response = "";
  unsigned long timeout = millis();
  while (modem.stream.available() || millis() - timeout < 10000) {
    if (modem.stream.available()) {
      char c = modem.stream.read();
      response += c;
      timeout = millis();
    }
  }
  
  SerialMon.printf("  → Raw response: '%s'\n", response.c_str());
  SerialMon.printf("  → Response length: %d\n", response.length());
  
  // Terminate
  modem.sendAT("+HTTPTERM");
  modem.waitResponse();
  
  // Parse response
  response.trim();
  if (response.length() > 0 && response.length() < 20) {
    SerialMon.printf("  ✅ Extracted version: %s\n", response.c_str());
    return response;
  }
  
  SerialMon.println("  ❌ Invalid version format");
  return "";
}

// Test method 3: TinyGsmClient with HTTPS
String testMethod3_TinyGsmHttps(const char* versionUrl) {
  SerialMon.println("🔬 TEST METHOD 3: TinyGsmClient (HTTPS)");
  
  // Parse URL
  String url = String(versionUrl);
  String host = "";
  String path = "";
  
  if (url.startsWith("https://")) {
    url = url.substring(8);
  }
  
  int slashIndex = url.indexOf('/');
  if (slashIndex > 0) {
    host = url.substring(0, slashIndex);
    path = url.substring(slashIndex);
  } else {
    host = url;
    path = "/";
  }
  
  SerialMon.printf("  → Host: %s\n", host.c_str());
  SerialMon.printf("  → Path: %s\n", path.c_str());
  
  // Create client
  TinyGsmClient client(modem);
  
  // Connect
  SerialMon.printf("  → Connecting to %s:443...\n", host.c_str());
  if (!client.connect(host.c_str(), 443)) {
    SerialMon.printf("  ❌ Connection failed to %s:443\n", host.c_str());
    return "";
  }
  
  SerialMon.println("  ✅ Connected successfully");
  
  // Send HTTP request
  String request = "GET " + path + " HTTP/1.1\r\n";
  request += "Host: " + host + "\r\n";
  request += "User-Agent: PlayBuoy/1.0\r\n";
  request += "Accept: text/plain, application/json\r\n";
  request += "Accept-Encoding: identity\r\n";
  request += "Connection: close\r\n";
  request += "\r\n";
  
  SerialMon.printf("  → Sending request:\n%s", request.c_str());
  client.print(request);
  
  // Read response
  SerialMon.println("  → Reading response...");
  String response = "";
  unsigned long timeout = millis();
  
  while (client.connected() || millis() - timeout < 10000) {
    if (client.available()) {
      char c = client.read();
      response += c;
      timeout = millis();
    }
  }
  
  SerialMon.printf("  → Raw response length: %d\n", response.length());
  SerialMon.printf("  → Response preview: %s\n", response.substring(0, min(200, (int)response.length())).c_str());
  
  client.stop();
  
  // Parse response
  int bodyStart = response.indexOf("\r\n\r\n");
  if (bodyStart > 0) {
    String body = response.substring(bodyStart + 4);
    body.trim();
    SerialMon.printf("  → Body: '%s'\n", body.c_str());
    
    if (body.length() > 0 && body.length() < 20) {
      SerialMon.printf("  ✅ Extracted version: %s\n", body.c_str());
      return body;
    }
  }
  
  SerialMon.println("  ❌ Could not parse response body");
  return "";
}

// Test method 4: TinyGsmClient with HTTP
String testMethod4_TinyGsmHttp(const char* versionUrl) {
  SerialMon.println("🔬 TEST METHOD 4: TinyGsmClient (HTTP)");
  
  // Convert to HTTP
  String url = String(versionUrl);
  if (url.startsWith("https://")) {
    url = "http://" + url.substring(8);
  }
  
  String host = "";
  String path = "";
  
  if (url.startsWith("http://")) {
    url = url.substring(7);
  }
  
  int slashIndex = url.indexOf('/');
  if (slashIndex > 0) {
    host = url.substring(0, slashIndex);
    path = url.substring(slashIndex);
  } else {
    host = url;
    path = "/";
  }
  
  SerialMon.printf("  → Host: %s\n", host.c_str());
  SerialMon.printf("  → Path: %s\n", path.c_str());
  
  // Create client
  TinyGsmClient client(modem);
  
  // Connect
  SerialMon.printf("  → Connecting to %s:80...\n", host.c_str());
  if (!client.connect(host.c_str(), 80)) {
    SerialMon.printf("  ❌ Connection failed to %s:80\n", host.c_str());
    return "";
  }
  
  SerialMon.println("  ✅ Connected successfully");
  
  // Send HTTP request
  String request = "GET " + path + " HTTP/1.1\r\n";
  request += "Host: " + host + "\r\n";
  request += "User-Agent: PlayBuoy/1.0\r\n";
  request += "Accept: text/plain, application/json\r\n";
  request += "Accept-Encoding: identity\r\n";
  request += "Connection: close\r\n";
  request += "\r\n";
  
  SerialMon.printf("  → Sending request:\n%s", request.c_str());
  client.print(request);
  
  // Read response
  SerialMon.println("  → Reading response...");
  String response = "";
  unsigned long timeout = millis();
  
  while (client.connected() || millis() - timeout < 10000) {
    if (client.available()) {
      char c = client.read();
      response += c;
      timeout = millis();
    }
  }
  
  SerialMon.printf("  → Raw response length: %d\n", response.length());
  SerialMon.printf("  → Response preview: %s\n", response.substring(0, min(200, (int)response.length())).c_str());
  
  client.stop();
  
  // Parse response
  int bodyStart = response.indexOf("\r\n\r\n");
  if (bodyStart > 0) {
    String body = response.substring(bodyStart + 4);
    body.trim();
    SerialMon.printf("  → Body: '%s'\n", body.c_str());
    
    if (body.length() > 0 && body.length() < 20) {
      SerialMon.printf("  ✅ Extracted version: %s\n", body.c_str());
      return body;
    }
  }
  
  SerialMon.println("  ❌ Could not parse response body");
  return "";
}

// Test method 5: Direct AT command with detailed debugging
String testMethod5_DirectAtCommands(const char* versionUrl) {
  SerialMon.println("🔬 TEST METHOD 5: Direct AT Commands");
  
  // Convert to HTTP for testing
  String httpUrl = String(versionUrl);
  if (httpUrl.startsWith("https://")) {
    httpUrl = "http://" + httpUrl.substring(8);
  }
  
  SerialMon.printf("  → Testing URL: %s\n", httpUrl.c_str());
  
  // Use modem object instead of SerialAT
  SerialMon.println("  → Sending AT+HTTPINIT");
  modem.sendAT("+HTTPINIT");
  delay(1000);
  
  SerialMon.println("  → Sending AT+HTTPPARA=\"URL\"");
  modem.sendAT("+HTTPPARA=\"URL\",\"" + httpUrl + "\"");
  delay(1000);
  
  SerialMon.println("  → Sending AT+HTTPDATA");
  modem.sendAT("+HTTPDATA=0,10000");
  delay(1000);
  
  SerialMon.println("  → Sending AT+HTTPACTION=0");
  modem.sendAT("+HTTPACTION=0");
  delay(5000);
  
  SerialMon.println("  → Sending AT+HTTPREAD");
  modem.sendAT("+HTTPREAD");
  delay(3000);
  
  // Read all available data
  String response = "";
  while (modem.stream.available()) {
    char c = modem.stream.read();
    response += c;
  }
  
  SerialMon.printf("  → AT Response: '%s'\n", response.c_str());
  
  // Terminate
  modem.sendAT("+HTTPTERM");
  delay(1000);
  
  // Try to extract version
  response.trim();
  if (response.length() > 0 && response.length() < 100) {
    SerialMon.printf("  → Extracted: '%s'\n", response.c_str());
    return response;
  }
  
  SerialMon.println("  ❌ No valid response");
  return "";
}

// Test method 6: Enhanced AT command with proper content reading
String testMethod6_EnhancedAtCommands(const char* versionUrl) {
  SerialMon.println("🔬 TEST METHOD 6: Enhanced AT Commands (FIXED)");
  
  // Convert to HTTP for testing
  String httpUrl = String(versionUrl);
  if (httpUrl.startsWith("https://")) {
    httpUrl = "http://" + httpUrl.substring(8);
  }
  
  SerialMon.printf("  → Testing URL: %s\n", httpUrl.c_str());
  
  // Initialize HTTP service
  SerialMon.println("  → Sending AT+HTTPINIT");
  modem.sendAT("+HTTPINIT");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ HTTPINIT failed");
    return "";
  }
  
  // Set URL
  SerialMon.println("  → Sending AT+HTTPPARA=\"URL\"");
  modem.sendAT("+HTTPPARA=\"URL\",\"" + httpUrl + "\"");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ HTTPPARA failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Set data size
  SerialMon.println("  → Sending AT+HTTPDATA");
  modem.sendAT("+HTTPDATA=0,10000");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ HTTPDATA failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Execute request
  SerialMon.println("  → Sending AT+HTTPACTION=0");
  modem.sendAT("+HTTPACTION=0");
  if (modem.waitResponse(30000L) != 1) {
    SerialMon.println("  ❌ HTTPACTION failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Read response with proper parsing
  SerialMon.println("  → Sending AT+HTTPREAD");
  modem.sendAT("+HTTPREAD");
  
  // Wait for the modem to respond to the command
  if (modem.waitResponse(10000L) != 1) {
    SerialMon.println("  ❌ HTTPREAD command failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Now read the actual content - FIXED VERSION
  String content = "";
  unsigned long timeout = millis();
  
  // Wait for the modem to start sending data
  while (!modem.stream.available() && millis() - timeout < 5000) {
    delay(10);
  }
  
  // Read all available data
  while (modem.stream.available() || millis() - timeout < 10000) {
    if (modem.stream.available()) {
      char c = modem.stream.read();
      content += c;
      timeout = millis();
    }
  }
  
  SerialMon.printf("  → Raw content: '%s'\n", content.c_str());
  SerialMon.printf("  → Content length: %d\n", content.length());
  
  // Terminate
  modem.sendAT("+HTTPTERM");
  modem.waitResponse();
  
  // Parse the content - look for the actual version string
  content.trim();
  
  // If we got a 301 redirect, try to follow it
  if (content.indexOf("+HTTPACTION: 0,301") >= 0) {
    SerialMon.println("  ⚠️ Got 301 redirect, trying HTTPS...");
    return testMethod6_EnhancedAtCommandsHttps(versionUrl);
  }
  
  // Look for the actual version content
  if (content.length() > 0 && content.length() < 20) {
    // Check if it looks like a version string (x.x.x format)
    if (content.indexOf('.') > 0 && content.indexOf('.') < content.length() - 1) {
      SerialMon.printf("  ✅ Extracted version: %s\n", content.c_str());
      return content;
    }
  }
  
  SerialMon.println("  ❌ No valid version found in content");
  return "";
}

// Test method 7: Direct GitHub test with minimal headers
String testMethod7_DirectGitHubTest(const char* versionUrl) {
  SerialMon.println("🔬 TEST METHOD 7: Direct GitHub Test (Minimal Headers)");
  
  // Initialize HTTP service
  SerialMon.println("  → Sending AT+HTTPINIT");
  modem.sendAT("+HTTPINIT");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ HTTPINIT failed");
    return "";
  }
  
  // Set URL (keep HTTPS)
  SerialMon.printf("  → Setting URL: %s\n", versionUrl);
  modem.sendAT("+HTTPPARA=\"URL\",\"" + String(versionUrl) + "\"");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ HTTPPARA failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Set minimal headers that GitHub accepts
  SerialMon.println("  → Setting minimal headers...");
  modem.sendAT("+HTTPPARA=\"USERDATA\",\"User-Agent: PlayBuoy/1.0\"");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ⚠️ Custom headers not supported");
  }
  
  // Set data size
  modem.sendAT("+HTTPDATA=0,10000");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ HTTPDATA failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Execute request
  SerialMon.println("  → Executing HTTP GET request...");
  modem.sendAT("+HTTPACTION=0");
  if (modem.waitResponse(30000L) != 1) {
    SerialMon.println("  ❌ HTTPACTION failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Read response
  SerialMon.println("  → Reading HTTP response...");
  modem.sendAT("+HTTPREAD");
  if (modem.waitResponse(10000L) != 1) {
    SerialMon.println("  ❌ HTTPREAD command failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Read content with proper waiting
  String content = "";
  unsigned long timeout = millis();
  
  // Wait for data to start arriving
  while (!modem.stream.available() && millis() - timeout < 5000) {
    delay(10);
  }
  
  // Read all available data
  while (modem.stream.available() || millis() - timeout < 10000) {
    if (modem.stream.available()) {
      char c = modem.stream.read();
      content += c;
      timeout = millis();
    }
  }
  
  SerialMon.printf("  → Raw content: '%s'\n", content.c_str());
  SerialMon.printf("  → Content length: %d\n", content.length());
  
  // Terminate
  modem.sendAT("+HTTPTERM");
  modem.waitResponse();
  
  // Parse content
  content.trim();
  if (content.length() > 0 && content.length() < 20) {
    if (content.indexOf('.') > 0 && content.indexOf('.') < content.length() - 1) {
      SerialMon.printf("  ✅ Extracted version: %s\n", content.c_str());
      return content;
    }
  }
  
  SerialMon.println("  ❌ No valid version in response");
  return "";
}

// Helper method for HTTPS after 301 redirect
String testMethod6_EnhancedAtCommandsHttps(const char* versionUrl) {
  SerialMon.println("  → Following 301 redirect to HTTPS...");
  
  // Initialize HTTP service
  modem.sendAT("+HTTPINIT");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ HTTPINIT failed");
    return "";
  }
  
  // Set URL (keep HTTPS)
  modem.sendAT("+HTTPPARA=\"URL\",\"" + String(versionUrl) + "\"");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ HTTPPARA failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Set data size
  modem.sendAT("+HTTPDATA=0,10000");
  if (modem.waitResponse() != 1) {
    SerialMon.println("  ❌ HTTPDATA failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Execute request
  modem.sendAT("+HTTPACTION=0");
  if (modem.waitResponse(30000L) != 1) {
    SerialMon.println("  ❌ HTTPACTION failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Read response
  modem.sendAT("+HTTPREAD");
  if (modem.waitResponse(10000L) != 1) {
    SerialMon.println("  ❌ HTTPREAD command failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return "";
  }
  
  // Read content - FIXED VERSION
  String content = "";
  unsigned long timeout = millis();
  
  // Wait for the modem to start sending data
  while (!modem.stream.available() && millis() - timeout < 5000) {
    delay(10);
  }
  
  // Read all available data
  while (modem.stream.available() || millis() - timeout < 10000) {
    if (modem.stream.available()) {
      char c = modem.stream.read();
      content += c;
      timeout = millis();
    }
  }
  
  SerialMon.printf("  → HTTPS content: '%s'\n", content.c_str());
  
  // Terminate
  modem.sendAT("+HTTPTERM");
  modem.waitResponse();
  
  // Parse content
  content.trim();
  if (content.length() > 0 && content.length() < 20) {
    if (content.indexOf('.') > 0 && content.indexOf('.') < content.length() - 1) {
      SerialMon.printf("  ✅ Extracted version from HTTPS: %s\n", content.c_str());
      return content;
    }
  }
  
  SerialMon.println("  ❌ No valid version in HTTPS response");
  return "";
}

String getServerFirmwareVersion(const char* versionUrl) {
  SerialMon.printf("🚀 STARTING MULTI-METHOD OTA TEST\n");
  SerialMon.printf("Target URL: %s\n", versionUrl);
  SerialMon.println("==========================================");
  
  // Test the direct GitHub method first (most likely to work)
  SerialMon.println("🎯 PRIORITY: Testing Direct GitHub Test (Method 7)");
  String directResult = testMethod7_DirectGitHubTest(versionUrl);
  if (directResult.length() > 0) {
    SerialMon.printf("✅ Direct GitHub method succeeded: %s\n", directResult.c_str());
    return directResult;
  }
  
  // Test the enhanced method second
  SerialMon.println("🎯 SECONDARY: Testing Enhanced AT Commands (Method 6)");
  String enhancedResult = testMethod6_EnhancedAtCommands(versionUrl);
  if (enhancedResult.length() > 0) {
    SerialMon.printf("✅ Enhanced method succeeded: %s\n", enhancedResult.c_str());
    return enhancedResult;
  }
  
  // Fallback to other methods if enhanced method fails
  String results[5];
  
  // Method 1: Modem HTTP HTTPS
  results[0] = testMethod1_ModemHttpHttps(versionUrl);
  
  // Method 2: Modem HTTP HTTP
  results[1] = testMethod2_ModemHttpHttp(versionUrl);
  
  // Method 3: TinyGsm HTTPS
  results[2] = testMethod3_TinyGsmHttps(versionUrl);
  
  // Method 4: TinyGsm HTTP
  results[3] = testMethod4_TinyGsmHttp(versionUrl);
  
  // Method 5: Direct AT
  results[4] = testMethod5_DirectAtCommands(versionUrl);
  
  // Analyze results
  SerialMon.println("==========================================");
  SerialMon.println("📊 TEST RESULTS SUMMARY:");
  
  for (int i = 0; i < 5; i++) {
    SerialMon.printf("Method %d: %s\n", i + 1, results[i].length() > 0 ? results[i].c_str() : "FAILED");
  }
  
  // Return first successful result
  for (int i = 0; i < 5; i++) {
    if (results[i].length() > 0) {
      SerialMon.printf("✅ Using result from Method %d: %s\n", i + 1, results[i].c_str());
      return results[i];
    }
  }
  
  SerialMon.println("❌ All test methods failed");
  return "";
}

bool downloadAndCheckVersion(const char* versionUrl) {
  SerialMon.println("🔍 CHECKING FOR FIRMWARE UPDATES");
  SerialMon.printf("Version URL: %s\n", versionUrl);
  SerialMon.printf("Current firmware version: %s\n", FIRMWARE_VERSION);
  SerialMon.println("----------------------------------------");
  
  String serverVersion = getServerFirmwareVersion(versionUrl);
  
  if (serverVersion.length() == 0) {
    SerialMon.println("❌ Could not retrieve server version");
    SerialMon.println("All test methods failed to get version");
    return false;
  }
  
  SerialMon.printf("✅ Server version retrieved: %s\n", serverVersion.c_str());
  
  int comparison = compareVersions(serverVersion, FIRMWARE_VERSION);
  
  SerialMon.printf("Version comparison: %s vs %s = %d\n", 
                   serverVersion.c_str(), FIRMWARE_VERSION, comparison);
  
  if (comparison > 0) {
    SerialMon.println("🎉 NEW FIRMWARE AVAILABLE!");
    SerialMon.printf("Server: %s > Current: %s\n", serverVersion.c_str(), FIRMWARE_VERSION);
    return true;
  } else if (comparison == 0) {
    SerialMon.println("✅ Firmware is up to date");
    SerialMon.printf("Server: %s = Current: %s\n", serverVersion.c_str(), FIRMWARE_VERSION);
  } else {
    SerialMon.println("⚠️ Server version is older than current version");
    SerialMon.printf("Server: %s < Current: %s\n", serverVersion.c_str(), FIRMWARE_VERSION);
  }
  
  return false;
}

bool downloadAndInstallFirmware(const char* firmwareUrl) {
  SerialMon.printf("Downloading firmware from: %s\n", firmwareUrl);
  
  // Use modem's built-in HTTP client for firmware download
  // This should handle HTTPS properly
  
  // Initialize HTTP service
  SerialMon.println("Initializing HTTP service for firmware download...");
  modem.sendAT("+HTTPINIT");
  if (modem.waitResponse() != 1) {
    SerialMon.println("Failed to initialize HTTP service");
    return false;
  }
  
  // Set HTTP parameters
  SerialMon.println("Setting HTTP parameters...");
  modem.sendAT("+HTTPPARA=\"URL\",\"" + String(firmwareUrl) + "\"");
  if (modem.waitResponse() != 1) {
    SerialMon.println("Failed to set URL parameter");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return false;
  }
  
  // Set HTTP data size to 0 (GET request)
  modem.sendAT("+HTTPDATA=0,10000");
  if (modem.waitResponse() != 1) {
    SerialMon.println("Failed to set HTTP data size");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return false;
  }
  
  // Execute HTTP GET request
  SerialMon.println("Executing HTTP GET request for firmware...");
  modem.sendAT("+HTTPACTION=0");
  if (modem.waitResponse(60000L) != 1) { // 60 second timeout for firmware
    SerialMon.println("HTTP GET request failed");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return false;
  }
  
  // Get content length
  SerialMon.println("Getting content length...");
  modem.sendAT("+HTTPREAD");
  if (modem.waitResponse(10000L) != 1) {
    SerialMon.println("Failed to read HTTP response");
    modem.sendAT("+HTTPTERM");
    modem.waitResponse();
    return false;
  }
  
  // For now, we'll need to implement a different approach for firmware download
  // as the modem's HTTP client doesn't easily support large binary downloads
  // Let's return false for now and implement a proper solution later
  SerialMon.println("⚠️ Firmware download via modem HTTP client not yet implemented");
  SerialMon.println("This requires a different approach for large binary downloads");
  
  // Terminate HTTP service
  modem.sendAT("+HTTPTERM");
  modem.waitResponse();
  
  return false;
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
