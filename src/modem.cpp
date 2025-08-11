#define TINY_GSM_MODEM_SIM7000

#include "config.h"
#include "modem.h"
#include <TinyGsmClient.h>
#include <IPAddress.h>
#include "esp_task_wdt.h"  // For watchdog timer

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
