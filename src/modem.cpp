#define TINY_GSM_MODEM_SIM7000

#include "config.h"
#include "modem.h"
#include <TinyGsmClient.h>
#include <IPAddress.h>
#include "esp_task_wdt.h"  // For watchdog timer
#include <time.h>
#include <stdlib.h>
#include <string.h>

#define SerialMon Serial

// Declare external modem from main.cpp
extern TinyGsm modem;
extern HardwareSerial Serial1;  // SerialAT is defined as Serial1 in main.cpp

// Add extern declarations for modem power control
extern void powerOnModem();
extern void powerOffModem();

// Connection timeout
static const unsigned long NETWORK_TIMEOUT_MS = 30000;

//
// Connect to NB-IoT or LTE-M network using given APN
//
bool connectToNetwork(const char* apn) {
  const int maxRetries = 3;
  for (int attempt = 0; attempt < maxRetries; ++attempt) {
    SerialMon.printf("Connecting to cellular network (attempt %d/%d)...\n", attempt + 1, maxRetries);

    // Initialize modem
    SerialMon.println("Initializing modem...");
    modem.init();
    
    // Test basic communication first
    SerialMon.println("Testing AT communication...");
    if (!modem.testAT()) {
      SerialMon.println(" AT communication failed");
      if (attempt < maxRetries - 1) {
        SerialMon.println("Power-cycling modem...");
        powerOffModem();
        delay(2000);
        powerOnModem();
        delay(3000);
      }
      continue;
    }
    SerialMon.println(" AT communication successful");

    // Wait for network registration
    SerialMon.println("Waiting for network registration...");
    esp_task_wdt_reset(); // Reset watchdog before network wait
    if (!modem.waitForNetwork(NETWORK_TIMEOUT_MS)) {
      SerialMon.println(" Network registration failed.");
      SerialMon.println("Signal quality: " + String(modem.getSignalQuality()));
      SerialMon.println("Operator: " + modem.getOperator());
      
      if (attempt < maxRetries - 1) {
        SerialMon.println("Power-cycling modem...");
        powerOffModem();
        delay(2000);
        powerOnModem();
        delay(3000);
      }
      continue;
    }

    SerialMon.println(" Network registered.");
    SerialMon.println("Signal quality: " + String(modem.getSignalQuality()));
    SerialMon.println("Operator: " + modem.getOperator());

    // Check if network is connected
    if (!modem.isNetworkConnected()) {
      SerialMon.println(" Network not connected.");
      if (attempt < maxRetries - 1) {
        SerialMon.println("Power-cycling modem...");
        powerOffModem();
        delay(2000);
        powerOnModem();
        delay(3000);
      }
      continue;
    }

    SerialMon.println(" Network connected.");

    // Connect to GPRS/APN
    SerialMon.printf("Connecting to APN: %s\n", apn);
    esp_task_wdt_reset(); // Reset watchdog before APN connection
    if (!modem.gprsConnect(apn, "", "")) {
      SerialMon.println(" APN connection failed.");
      SerialMon.println("Trying to get IP address...");
      IPAddress ip = modem.localIP();
      SerialMon.printf("IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
      
      if (attempt < maxRetries - 1) {
        SerialMon.println("Power-cycling modem...");
        powerOffModem();
        delay(2000);
        powerOnModem();
        delay(3000);
      }
      continue;
    }

    SerialMon.println(" Cellular network connected.");
    IPAddress localIP = modem.localIP();
    SerialMon.printf("Local IP: %d.%d.%d.%d\n", localIP[0], localIP[1], localIP[2], localIP[3]);
    return true;
  }
  return false;
}

//
// Test multiple APNs to find one that works
//
bool testMultipleAPNs() {
  const char* apns[] = {"telenor", "telenor.smart"};
  const int numApns = sizeof(apns) / sizeof(apns[0]);

  SerialMon.println("Trying known APNs...");

  // Ensure we are registered before trying APNs
  if (!modem.isNetworkConnected()) {
    SerialMon.println(" Waiting for network registration...");
    if (!modem.waitForNetwork(NETWORK_TIMEOUT_MS)) {
      SerialMon.println(" Network registration failed.");
      return false;
    }
  }

  for (int i = 0; i < numApns; ++i) {
    const char* apn = apns[i];
    SerialMon.printf(" APN: %s\n", apn);

    // Disconnect any existing PDP before switching APN
    modem.gprsDisconnect();
    delay(300);

    if (modem.gprsConnect(apn, "", "")) {
      IPAddress ip = modem.localIP();
      SerialMon.printf(" Connected. IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
      return true;
    }

    SerialMon.println(" Failed. Trying next...");
    delay(500);
  }

  SerialMon.println(" No known APN worked");
  return false;
}

//
// Send JSON payload to server using HTTP POST
//
bool sendJsonToServer(const char* server, uint16_t port, const char* endpoint, const String& payload) {
  const int maxRetries = 3;
  for (int attempt = 0; attempt < maxRetries; ++attempt) {
    TinyGsmClient client(modem);

    if (!client.connect(server, port)) {
      SerialMon.printf("Connection to server failed (attempt %d/%d).\n", attempt + 1, maxRetries);
      delay(2000);
      continue;
    }

    // Build HTTP request
    String request =
      String("POST ") + endpoint + " HTTP/1.1\r\n" +
      "Host: " + server + "\r\n" +
      "Content-Type: application/json\r\n" +
      "X-API-Key: " + String(API_KEY) + "\r\n" +
      "Connection: close\r\n" +
      "Content-Length: " + payload.length() + "\r\n\r\n" +
      payload;

    client.print(request);

    unsigned long timeout = millis();
    bool gotResponse = false;
    while (client.connected() && millis() - timeout < 10000) {
      if (client.available()) {
        String line = client.readStringUntil('\n');
        SerialMon.println(line);
        gotResponse = true;
      }
    }

    client.stop();

    if (gotResponse) {
      return true;
    } else {
      SerialMon.printf("No response from server (attempt %d/%d).\n", attempt + 1, maxRetries);
      delay(2000);
    }
  }
  return false;
}

// Try to sync time using an HTTP HEAD to retrieve Date header (avoids HTTPS)
bool syncTimeFromNetwork() {
  // Must already have PDP up
  if (!modem.isGprsConnected()) return false;

  TinyGsmClient client(modem);
  // Use API server as a time source (any HTTP server with Date header works)
  if (!client.connect(API_SERVER, API_PORT)) {
    SerialMon.println("Time sync: TCP connect failed");
    return false;
  }
  String req = String("HEAD ") + "/" + " HTTP/1.1\r\n" +
               "Host: " + API_SERVER + "\r\n" +
               "Connection: close\r\n\r\n";
  client.print(req);

  // Parse headers, look for 'Date:'
  String line;
  unsigned long t0 = millis();
  time_t parsed = 0;
  while (millis() - t0 < 10000) {
    while (client.available()) {
      char c = client.read();
      if (c == '\n') {
        line.trim();
        if (line.length() == 0) { // end of headers
          client.stop();
          if (parsed > 0) {
            struct timeval tv; tv.tv_sec = parsed; tv.tv_usec = 0; settimeofday(&tv, nullptr);
            // Also print human-readable local time (with timezone/DST)
            time_t now = parsed; struct tm lt; localtime_r(&now, &lt);
            char buf[64]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &lt);
            SerialMon.printf("Time sync: set RTC from HTTP Date to %lu (%s)\n", (unsigned long)parsed, buf);
            return true;
          }
          return false;
        }
        if (line.startsWith("Date:")) {
          // Example: Date: Sun, 11 Aug 2025 20:18:30 GMT
          // We parse minimal fields
          // Skip "Date: "
          String ds = line.substring(5); ds.trim();
          // Very simple parse using tm structure
          struct tm t = {};
          // Day of week is ignored; format: Ddd, DD Mon YYYY HH:MM:SS GMT
          // Extract components
          // Find day
          int comma = ds.indexOf(',');
          String rest = (comma >= 0) ? ds.substring(comma + 1) : ds;
          rest.trim();
          // Day
          int sp1 = rest.indexOf(' ');
          int day = rest.substring(0, sp1).toInt();
          // Month
          int sp2 = rest.indexOf(' ', sp1 + 1);
          String monStr = rest.substring(sp1 + 1, sp2);
          const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
          int mon = 0; for (; mon < 12; ++mon) { if (monStr.equalsIgnoreCase(String(months + mon * 3).substring(0,3))) break; }
          // Year
          int sp3 = rest.indexOf(' ', sp2 + 1);
          int year = rest.substring(sp2 + 1, sp3).toInt();
          // Time HH:MM:SS
          int sp4 = rest.indexOf(' ', sp3 + 1);
          String timeStr = rest.substring(sp3 + 1, sp4);
          int c1 = timeStr.indexOf(':'); int c2 = timeStr.indexOf(':', c1 + 1);
          int hh = timeStr.substring(0, c1).toInt();
          int mm = timeStr.substring(c1 + 1, c2).toInt();
          int ss = timeStr.substring(c2 + 1).toInt();
          t.tm_mday = day; t.tm_mon = mon; t.tm_year = year - 1900; t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss; t.tm_isdst = 0;
          // HTTP Date is GMT/UTC; convert to epoch using UTC timezone
          // Provide a local timegm replacement by temporarily switching TZ to UTC
          char oldTz[64]; oldTz[0] = '\0';
          const char* envTz = getenv("TZ");
          if (envTz) { strncpy(oldTz, envTz, sizeof(oldTz) - 1); oldTz[sizeof(oldTz) - 1] = '\0'; }
          setenv("TZ", "UTC0", 1); tzset();
          parsed = mktime(&t);
          if (oldTz[0] != '\0') { setenv("TZ", oldTz, 1); } else { unsetenv("TZ"); }
          tzset();
        }
        line = "";
      } else if (c != '\r') {
        line += c;
      }
    }
    if (!client.connected()) break;
    delay(5);
  }
  client.stop();
  return false;
}
