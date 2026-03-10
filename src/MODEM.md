# Cellular Modem — Local Context

## Purpose
Connect to Telenor Norway via LTE-M (preferred) or NB-IoT (fallback), upload JSON telemetry via HTTP POST, and support OTA version checks. Uses SIM7000G integrated modem on UART1.

## What can go wrong
- **Brownout during TX**: SIM7000G draws up to 2A peak. If battery is marginal, modem TX causes voltage sag → ESP32 brownout → reset loop.
- **PWRKEY timing violation**: Power-on pulse <1000ms or power-off pulse <1200ms = modem ignores the command. Appears "stuck" in wrong power state.
- **Stale PDP context**: If PDP isn't torn down before GPS, GNSS engine won't start (radio shared). If PDP is left active before sleep, modem stays registered → drains battery.
- **NB-IoT flag never resets**: `static bool triedNBIoT` is local to `connectToNetwork()`. Once set, NB-IoT fallback is never retried in same boot — even after modem power-cycle.
- **No HTTP status check on upload**: Fixed — `sendJsonToServer()` now parses HTTP status line and only treats 2xx as success.

## Critical timings (from SIM7000G datasheet)
| Operation | Our value | Spec minimum | Margin |
|-----------|-----------|-------------|--------|
| PWRKEY on pulse | 2000ms | 1000ms | 2× |
| Post-power settle | 6000ms | ~5000ms | 1.2× |
| PWRKEY off pulse | 1300ms | 1200ms | 1.08× — tightest margin |
| CPOWD graceful off | 8000ms timeout | 2-4s typical | 2× |
| Network registration | 60s | varies | — |

## Key code paths
- `connectToNetwork(apn, skipPreCycle)`: Main entry. `skipPreCycle=true` after GPS phase saves ~14s by not power-cycling an already-warm modem.
- `sendJsonToServer()`: 3 retries with 2s backoff. Builds raw HTTP/1.1 request with `X-API-Key` header.
- `testMultipleAPNs()`: Tries "telenor" then "telenor.smart". Used as fallback.

## Rules
- Never reduce PWRKEY pulse widths below datasheet minimums
- Never power modem and GPS simultaneously
- Always call `tearDownPDP()` before starting GNSS
- Always call `powerOffModem()` before deep sleep
- `wakeModemForNetwork()` (DTR LOW) must be called before registration
