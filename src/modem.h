#pragma once
#include <Arduino.h>

//
// Cellular (LTE-M/NB-IoT) network connectivity via SIM7000G modem.
// Handles modem power sequencing, network registration, and HTTP uploads.
//
// POWER SEQUENCING:
// - Call ensureModemReady() before any AT commands (powers on if needed, ~8 seconds)
// - Modem stays powered after GPS phase to save time on cellular connection
// - Call powerOffModem() at end of cycle before sleep
// - PDP context should be torn down before sleep (sendAT calls for CNACT/CGACT/CGATT/CIPSHUT)
//
// FREQUENCY BANDS:
// - LTE-M preferred (lower power, better range in rural areas)
// - NB-IoT fallback if LTE-M unavailable
// - APN tested sequentially (Telenor, Telia, Netcom for Norway)
//

//
// Establishes data connection to cellular network.
// Registers with network, obtains IP address, ready for HTTP requests.
// skipPreCycle: if true, skips the initial 6s power-up wait (used when modem
//   already powered from GPS phase). If false, waits full 8s for modem readiness.
// Returns: true if connected and ready for HTTP, false if registration/connection failed.
//
bool connectToNetwork(const char* apn, bool skipPreCycle = false);

//
// Tests multiple known APNs sequentially if initial connection fails.
// Tries: Telenor (Norway), Telia, Netcom (fallbacks for roaming/other carriers).
// Returns: true if any APN succeeded, false if all failed.
//
bool testMultipleAPNs();

//
// Sends JSON payload to API server via HTTP POST.
// Constructs request with X-API-Key header, manages TinyGsm socket,
// parses HTTP response status code.
// Parameters:
//   server: hostname or IP (e.g. "playbuoyapi.no")
//   port: HTTP port (typically 80, not 443 due to TLS limitation)
//   endpoint: path on server (e.g. "/upload")
//   payload: JSON string (will be POST body)
// Returns: true if HTTP 200-299 received, false on connection error or non-2xx status.
//
bool sendJsonToServer(const char* server, uint16_t port, const char* endpoint, const String& payload);

//
// Helper — ensures modem is powered and ready for AT commands.
// Called internally before connectToNetwork(), also used by GPS module.
// Powers on modem if not already powered (includes full ~8s settle time).
//
void ensureModemReady();
