#pragma once
#include <Arduino.h>

bool connectToNetwork(const char* apn);
bool testMultipleAPNs();
bool sendJsonToServer(const char* server, uint16_t port, const char* endpoint, const String& payload);
// Sync RTC time using network sources (HTTP Date header). Returns true on success
bool syncTimeFromNetwork();
