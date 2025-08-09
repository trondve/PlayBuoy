#include "utils.h"
#include <Arduino.h>
#include "esp_sleep.h"

#define SerialMon Serial

void logWakeupReason() {
  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

  SerialMon.print("Wakeup reason: ");
  switch(reason) {
    case ESP_SLEEP_WAKEUP_EXT0: SerialMon.println("EXT0 (RTC_IO)"); break;
    case ESP_SLEEP_WAKEUP_EXT1: SerialMon.println("EXT1 (RTC_CNTL)"); break;
    case ESP_SLEEP_WAKEUP_TIMER: SerialMon.println("Timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: SerialMon.println("Touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: SerialMon.println("ULP Program"); break;
    case ESP_SLEEP_WAKEUP_GPIO: SerialMon.println("GPIO"); break;
    case ESP_SLEEP_WAKEUP_UART: SerialMon.println("UART"); break;
    case ESP_SLEEP_WAKEUP_WIFI: SerialMon.println("WiFi"); break;
    case ESP_SLEEP_WAKEUP_COCPU: SerialMon.println("Co-Processor"); break;
    case ESP_SLEEP_WAKEUP_ALL: SerialMon.println("All Wakeup Sources"); break;
    case ESP_SLEEP_WAKEUP_UNDEFINED: SerialMon.println("Undefined (Power On / Reset)"); break;
    default: SerialMon.println("Unknown"); break;
  }
}
