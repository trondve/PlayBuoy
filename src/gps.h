#pragma once

#include <Arduino.h>

// Holds a GPS fix result
typedef struct {
  bool success;
  float latitude;
  float longitude;
  float accuracy;
  uint32_t fixTimeEpoch;  // GPS-provided time (optional, if supported)
} GpsFixResult;

void gpsBegin();                         // Enables GPS module
GpsFixResult getGpsFix(uint16_t timeoutSec = 1800);  // Attempts fix within timeout (30 minutes default)
void gpsEnd();                           // Optional: powers down GPS to save power

// Dynamic GPS timeout functions
uint16_t getGpsFixTimeout(bool isFirstFix);        // Returns timeout based on battery and first fix
GpsFixResult getGpsFixDynamic(bool isFirstFix);    // Gets GPS fix with dynamic timeout

// Test function for extended GPS debugging
void testGpsExtended(uint16_t timeoutSec = 1800);  // 30 minutes default

// XTRA/AGPS functions for faster cold starts
bool downloadXtraData();                 // Downloads XTRA assistance data via LTE
bool configureAgps();                    // Configures AGPS settings
bool enableXtraMode();                   // Enables XTRA mode for faster fixes
void forceXtraDownload();                // Forces XTRA download (for testing)
void testGpsWithXtra(uint16_t timeoutSec = 1800);   // Test GPS with XTRA assistance (30 minutes default)
void checkXtraStatus();                  // Check and report XTRA/AGPS status
void quickXtraTest(uint16_t timeoutSec = 300);      // Quick XTRA test with short timeout
void checkModemTime();                   // Check and report modem time status
void comprehensiveGpsTest(uint16_t timeoutSec = 1800);  // Comprehensive GPS test with all optimizations
void testNetworkFirstApproach(uint16_t timeoutSec = 600);  // Test network-first approach
void diagnoseGpsAtCommands();  // Simple GPS AT command diagnostic
void comprehensiveGpsDiagnostic();  // Comprehensive GPS diagnostic (non-blocking)
void monitorGpsFix(uint16_t timeoutSec = 1800);  // Monitor GPS fix with periodic diagnostics
void runPeriodicGpsStatus(uint32_t elapsedSeconds);  // Run periodic status checks during GPS fix
void testImprovedGps(uint16_t timeoutSec = 600);  // Test improved GPS with periodic status checks
void verifyXtraEffectiveness(uint16_t timeoutSec = 300);  // Test XTRA effectiveness by comparing fix times
void verifyTimeSyncEffectiveness(uint16_t timeoutSec = 300);  // Test time sync effectiveness by comparing fix times
