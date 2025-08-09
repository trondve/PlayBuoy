#pragma once
#include <Arduino.h>

bool connectToNetwork(const char* apn);
bool sendJsonToServer(const char* server, uint16_t port, const char* endpoint, const String& payload);
