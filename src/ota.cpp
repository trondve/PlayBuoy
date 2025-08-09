#include "ota.h"
#include "rtc_state.h"
#include <HTTPClient.h>
#include <Update.h>

#define SerialMon Serial

bool checkAndPerformOTA(const char* url) {
  if (rtcState.firmwareUpdateAttempted) {
    SerialMon.println("OTA already attempted this cycle. Skipping.");
    return false;
  }

  SerialMon.println("Attempting OTA update...");
  markFirmwareUpdateAttempted();

  HTTPClient http;
  http.begin(url); // Must be HTTP, not HTTPS without cert config

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
    // Fallback: reboot to previous firmware
    SerialMon.println("OTA failed, rebooting to previous firmware...");
    delay(1000);
    ESP.restart();
    return false;
  }

  if (!Update.isFinished()) {
    SerialMon.println("Update did not finish.");
    http.end();
    Update.abort();
    // Fallback: reboot to previous firmware
    SerialMon.println("OTA incomplete, rebooting to previous firmware...");
    delay(1000);
    ESP.restart();
    return false;
  }

  SerialMon.println("OTA update successful. Rebooting...");
  http.end();
  delay(1000);
  ESP.restart();  // Should not return
  return true;
}
