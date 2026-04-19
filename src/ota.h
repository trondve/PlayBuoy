#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

//
// Over-the-Air firmware updates for sealed buoys.
//
// DESIGN NOTES:
// - HTTP-only (TLS/HTTPS broken on SIM7000G modem — limitation, not choice)
// - Pre-flight battery gate: minimum 3.85V AND 50% SoC before attempting download
// - SHA-256 verification on downloaded image (graceful degradation if not provided)
// - 3xx redirect support (Location header parsing, up to 3 hops)
// - No version comparison trigger — relies on checkForFirmwareUpdate() endpoint response
//
// CRITICAL SAFETY RULES:
// - Never flash if modem voltage sags below 3.55V (SIM7000G minimum)
// - Never write to NVS without checking battery first (corruption risk on brownout)
// - Enforce HTTP header end detection (don't trust status code alone)
// - Always return true from download if restart triggered (don't add code after esp_restart)
//

//
// Main entry point — checks for available update and installs if found.
// Calls remote version endpoint, compares against current FIRMWARE_VERSION,
// downloads firmware if newer, and restarts device on completion.
// Returns: true if update started (device will restart), false if no update or error.
//
bool checkForFirmwareUpdate(const char* baseUrl);

//
// Low-level helper — downloads version string from endpoint.
// Endpoint must return plain text: MAJOR.MINOR.PATCH (first line).
// Returns: version string if successful, empty string on error.
// Example: "1.2.5"
//
String getServerFirmwareVersion(const char* versionUrl);

//
// Low-level helper — downloads and installs firmware image.
// Validates HTTP response headers, optionally verifies SHA-256, writes to flash.
// CRITICAL: Pre-check battery gate before calling this function.
// Returns: true if install successful and restart triggered, false on any error.
// NOTE: Device will NOT return on success (esp_restart called internally).
//
bool downloadAndInstallFirmware(const char* firmwareUrl, const uint8_t* expectedSha256 = nullptr);

//
// Internal helper — checks version string and initiates download if needed.
// Parses version from endpoint, compares against current, calls downloadAndInstallFirmware.
// Used internally by checkForFirmwareUpdate().
//
bool downloadAndCheckVersion(const char* versionUrl);

#endif // OTA_H
