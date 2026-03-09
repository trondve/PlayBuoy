#pragma once
#include <Arduino.h>

// skipPreCycle: set true when modem is already powered (e.g. after GPS phase)
bool connectToNetwork(const char* apn, bool skipPreCycle = false);
bool testMultipleAPNs();
bool sendJsonToServer(const char* server, uint16_t port, const char* endpoint, const String& payload);
