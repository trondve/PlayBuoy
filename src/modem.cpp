#define TINY_GSM_MODEM_SIM7000

#include "modem.h"
#include <TinyGsmClient.h>

#define SerialMon Serial

// Declare external modem from main.cpp
extern TinyGsm modem;
extern HardwareSerial SerialAT;

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

    modem.init();
    if (!modem.waitForNetwork(NETWORK_TIMEOUT_MS)) {
      SerialMon.println("Network registration failed.");
      // Power-cycle modem if not last attempt
      if (attempt < maxRetries - 1) {
        SerialMon.println("Power-cycling modem...");
        powerOffModem();
        delay(2000);
        powerOnModem();
        delay(3000);
      }
      continue;
    }

    if (!modem.isNetworkConnected()) {
      SerialMon.println("Network not connected.");
      if (attempt < maxRetries - 1) {
        SerialMon.println("Power-cycling modem...");
        powerOffModem();
        delay(2000);
        powerOnModem();
        delay(3000);
      }
      continue;
    }

    SerialMon.println("Network registered.");

    if (!modem.gprsConnect(apn, "", "")) {
      SerialMon.println("APN connection failed.");
      if (attempt < maxRetries - 1) {
        SerialMon.println("Power-cycling modem...");
        powerOffModem();
        delay(2000);
        powerOnModem();
        delay(3000);
      }
      continue;
    }

    SerialMon.println("Cellular network connected.");
    return true;
  }
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
