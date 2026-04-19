#include "ota.h"
#include "config.h"
#include "rtc_state.h"
#include "battery.h"
#include <TinyGsmClient.h>
#include <Update.h>
#include "mbedtls/sha256.h"

#define SerialMon Serial

// Maximum time for firmware binary download (5 minutes).
// A 300KB binary over LTE-M at ~10KB/s should take ~30s.
// 5 minutes gives generous margin for slow links without draining the battery.
static const unsigned long OTA_DOWNLOAD_TIMEOUT_MS = 5UL * 60UL * 1000UL;

extern TinyGsm modem;
extern void powerOffModem();

static bool ensurePdpForHttp() {
  if (modem.isGprsConnected()) return true;
  return modem.gprsConnect(NETWORK_PROVIDER, "", "");
}

static String extractVersionFromBody(const String& body) {
  String trimmed = body; trimmed.trim();
  if (trimmed.length() == 0) return "";

  // Require strict MAJOR.MINOR.PATCH format: must start with a digit (no leading junk)
  if (trimmed[0] < '0' || trimmed[0] > '9') return "";

  String candidate;
  for (size_t i = 0; i < trimmed.length(); ++i) {
    char c = trimmed[i];
    if ((c >= '0' && c <= '9') || c == '.') candidate += c;
    else if (candidate.length() > 0) break;  // Stop at first non-digit/non-dot
  }

  // Verify format: must have exactly 2 dots for MAJOR.MINOR.PATCH
  int a = candidate.indexOf('.');
  int b = candidate.lastIndexOf('.');
  if (a > 0 && b > a && b < (int)candidate.length() - 1) {
    return candidate;
  }
  return "";
}

// Parse a hex SHA-256 string (64 chars) into 32 bytes. Returns true on success.
static bool parseHexSha256(const String& hex, uint8_t out[32]) {
  String h = hex; h.trim();
  if (h.length() < 64) return false;
  // Take only the first 64 hex chars (sha256sum output may have filename after)
  for (int i = 0; i < 32; ++i) {
    char hi = h[i * 2], lo = h[i * 2 + 1];
    auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int hn = nibble(hi), ln = nibble(lo);
    if (hn < 0 || ln < 0) return false;
    out[i] = (uint8_t)((hn << 4) | ln);
  }
  return true;
}

static int compareVersions(const String& a, const String& b) {
  auto parse = [](const String& v, int out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = 0;
    int idx = 0, val = 0; bool has = false;
    for (size_t i = 0; i < v.length() && idx < 3; ++i) {
      char c = v[i];
      if (c >= '0' && c <= '9') { has = true; val = val * 10 + (c - '0'); }
      else if (c == '.') { out[idx++] = has ? val : 0; val = 0; has = false; }
      else { break; }
    }
    if (idx < 3) out[idx] = has ? val : 0;
  };
  int va[3], vb[3]; parse(a, va); parse(b, vb);
  for (int i = 0; i < 3; ++i) { if (va[i] > vb[i]) return 1; if (va[i] < vb[i]) return -1; }
  return 0;
}

static String httpGetTinyGsm(const char* url) {
  String u(url), host, path; uint16_t port = 80;
  if (u.startsWith("http://")) u = u.substring(7);
  else if (u.startsWith("https://")) u = u.substring(8);
  int slash = u.indexOf('/');
  if (slash > 0) { host = u.substring(0, slash); path = u.substring(slash); }
  else { host = u; path = "/"; }
  int colon = host.indexOf(':');
  if (colon > 0) { port = (uint16_t)host.substring(colon + 1).toInt(); host = host.substring(0, colon); }

  SerialMon.printf("HTTP GET Host=%s Port=%u Path=%s\n", host.c_str(), port, path.c_str());
  if (!ensurePdpForHttp()) { SerialMon.println("No PDP"); return ""; }

  TinyGsmClient client(modem);
  if (!client.connect(host.c_str(), port)) { SerialMon.println("TCP connect failed"); return ""; }

  String req;
  req += String("GET ") + path + " HTTP/1.1\r\n";
  req += String("Host: ") + host + "\r\n";
  req += "User-Agent: TinyGSM-HTTP/1.0\r\n";
  req += "Accept: */*\r\n";
  req += "Connection: close\r\n\r\n";
  client.print(req);

  // Read entire response (headers + body) into one buffer with size cap to prevent OOM
  // Version/SHA files should be small (<100 bytes); cap at 4KB for safety
  const size_t MAX_RESPONSE_SIZE = 4096;
  String raw; raw.reserve(MAX_RESPONSE_SIZE);
  unsigned long t0 = millis();
  while (millis() - t0 < 20000) {
    while (client.available()) {
      if (raw.length() >= MAX_RESPONSE_SIZE) {
        SerialMon.printf("HTTP response exceeds %u bytes; aborting to prevent OOM\n", (unsigned)MAX_RESPONSE_SIZE);
        client.stop();
        return "";
      }
      raw += (char)client.read();
    }
    if (!client.connected()) break;
    delay(5);
  }
  client.stop();

  // Split at first \r\n\r\n to separate headers from body
  String body;
  int p = raw.indexOf("\r\n\r\n");
  if (p >= 0) body = raw.substring(p + 4);
  else body = raw; // no header terminator found, use entire response
  body.trim();
  SerialMon.printf("HTTP body: '%s'\n", body.c_str());
  return body;
}

//
// PUBLIC FUNCTION: getServerFirmwareVersion
// Downloads and extracts version string from remote server.
// Endpoint must return plain text MAJOR.MINOR.PATCH on first line.
// Returns empty string on HTTP failure or parse error.
//
String getServerFirmwareVersion(const char* versionUrl) {
  SerialMon.println("CHECKING FOR FIRMWARE UPDATES");
  SerialMon.printf("Version URL: %s\n", versionUrl);
  SerialMon.printf("Current firmware version: %s\n", FIRMWARE_VERSION);
  SerialMon.println("----------------------------------------");
  String body = httpGetTinyGsm(versionUrl);
  if (body.length() == 0) return "";
  return extractVersionFromBody(body);
}

//
// PUBLIC FUNCTION: downloadAndCheckVersion
// Queries remote version, compares against current firmware, downloads if newer.
// Internal helper for checkForFirmwareUpdate().
// Returns true if download completed and device will restart, false if no update or error.
//
bool downloadAndCheckVersion(const char* versionUrl) {
  String serverVersion = getServerFirmwareVersion(versionUrl);
  if (serverVersion.length() == 0) { SerialMon.println("Could not retrieve server version"); return false; }
  SerialMon.printf("Server version retrieved: %s\n", serverVersion.c_str());
  int cmp = compareVersions(serverVersion, FIRMWARE_VERSION);
  SerialMon.printf("Version comparison: %s vs %s = %d\n", serverVersion.c_str(), FIRMWARE_VERSION, cmp);
  if (cmp > 0) { SerialMon.println("NEW FIRMWARE AVAILABLE!"); return true; }
  if (cmp == 0) SerialMon.println("Firmware is up to date"); else SerialMon.println("Server version is older");
  return false;
}

static bool parseHttpResponseHeaders(TinyGsmClient& client, int& statusCode, size_t& contentLength, String* outLocation = nullptr) {
  statusCode = -1;
  contentLength = 0;
  if (outLocation) outLocation->clear();

  String line;
  unsigned long t0 = millis();
  // Read status line
  bool gotStatus = false;
  while (!gotStatus && millis() - t0 < 15000) {
    while (client.available()) {
      char c = client.read();
      if (c == '\n') {
        line.trim();
        if (line.startsWith("HTTP/1.1 ") || line.startsWith("HTTP/1.0 ")) {
          int sp = line.indexOf(' ');
          if (sp > 0) statusCode = line.substring(sp + 1).toInt();
        }
        line = "";
        gotStatus = true;
        break;
      } else if (c != '\r') {
        line += c;
      }
    }
    if (!gotStatus) delay(5);
  }
  // Read headers until blank line
  bool sawHeaderEnd = false;
  t0 = millis();
  while (millis() - t0 < 15000) {
    while (client.available()) {
      char c = client.read();
      if (c == '\n') {
        String h = line; h.trim();
        if (h.length() == 0) {
          sawHeaderEnd = true;
          break;  // Exit inner while loop
        }
        if (h.startsWith("Content-Length:")) {
          String v = h.substring(strlen("Content-Length:")); v.trim();
          contentLength = (size_t)v.toInt();
        }
        // Capture Location header for 3xx redirects
        if (outLocation && h.startsWith("Location:")) {
          String loc = h.substring(strlen("Location:")); loc.trim();
          *outLocation = loc;
        }
        line = "";
      } else if (c != '\r') {
        line += c;
      }
    }
    if (sawHeaderEnd) break;  // Exit outer while loop
    delay(5);
  }
  return (statusCode > 0 && sawHeaderEnd);
}

//
// PUBLIC FUNCTION: downloadAndInstallFirmware
// Downloads and installs firmware image from URL via HTTP.
// Validates battery (H-04: minimum 3.85V + 50% SoC), handles 3xx redirects (H-07).
// Verifies SHA-256 hash if provided (graceful degradation if not).
// Writes image to flash partition, initiates reboot on success.
// CRITICAL: Device will NOT return on success (esp_restart() called internally).
// Returns: true if install initiated and restart triggered, false on any error.
//
bool downloadAndInstallFirmware(const char* firmwareUrl, const uint8_t* expectedSha256) {
  SerialMon.printf("Downloading firmware from: %s\n", firmwareUrl);
  if (!ensurePdpForHttp()) {
    SerialMon.println("No PDP for firmware download");
    return false;
  }

  const int MAX_REDIRECTS = 3;
  String currentUrl(firmwareUrl);

  for (int redirectCount = 0; redirectCount <= MAX_REDIRECTS; redirectCount++) {
    if (redirectCount > 0) {
      SerialMon.printf("Following redirect %d/%d to: %s\n", redirectCount, MAX_REDIRECTS, currentUrl.c_str());
    }

    // Parse URL
    String u(currentUrl), host, path; uint16_t port = 80;
    if (u.startsWith("http://")) u = u.substring(7);
    else if (u.startsWith("https://")) u = u.substring(8);
    int slash = u.indexOf('/');
    if (slash > 0) { host = u.substring(0, slash); path = u.substring(slash); } else { host = u; path = "/"; }
    int colon = host.indexOf(':');
    if (colon > 0) { port = (uint16_t)host.substring(colon + 1).toInt(); host = host.substring(0, colon); }

    TinyGsmClient client(modem);
    if (!client.connect(host.c_str(), port)) { SerialMon.println("TCP connect failed"); return false; }

    // Send request
    String req;
    req += String("GET ") + path + " HTTP/1.1\r\n";
    req += String("Host: ") + host + "\r\n";
    req += "User-Agent: TinyGSM-HTTP/1.0\r\n";
    req += "Accept: application/octet-stream\r\n";
    req += "Connection: close\r\n\r\n";
    client.print(req);

    int status = -1; size_t contentLength = 0;
    String location;
    if (!parseHttpResponseHeaders(client, status, contentLength, &location)) { client.stop(); return false; }
    SerialMon.printf("HTTP status: %d, Content-Length: %u\n", status, (unsigned)contentLength);

    // Handle redirects
    if ((status >= 300 && status < 400) && location.length() > 0) {
      client.stop();
      if (redirectCount >= MAX_REDIRECTS) {
        SerialMon.printf("Too many redirects (max %d)\n", MAX_REDIRECTS);
        return false;
      }
      currentUrl = location;
      continue;  // Try next redirect
    }

    // Only proceed if we got 200 OK
    if (status != 200) { client.stop(); return false; }

    size_t updateSize = (contentLength > 0) ? contentLength : (size_t)UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(updateSize)) {
      SerialMon.printf("Update.begin failed: %s\n", Update.errorString());
      client.stop();
      return false;
    }

    // Initialize SHA-256 context for integrity verification
    mbedtls_sha256_context sha256ctx;
    bool verifySha = (expectedSha256 != nullptr);
    if (verifySha) {
      mbedtls_sha256_init(&sha256ctx);
      mbedtls_sha256_starts(&sha256ctx, 0);  // 0 = SHA-256 (not SHA-224)
    }

    size_t written = 0; uint8_t buf[1024];
    unsigned long lastLog = millis();
    unsigned long downloadStart = millis();
    bool timedOut = false;

    while (client.connected()) {
      // Wall-clock timeout: abort if download takes too long (prevents stalled TCP
      // from draining battery — the 45-min WDT is too late for OTA safety)
      if (millis() - downloadStart > OTA_DOWNLOAD_TIMEOUT_MS) {
        SerialMon.printf("OTA download timeout after %lu ms (%u bytes received)\n",
                         millis() - downloadStart, (unsigned)written);
        timedOut = true;
        break;
      }

      int n = client.readBytes(buf, sizeof(buf));
      if (n < 0) break;
      if (n == 0) { delay(5); continue; }

      // Hash the raw bytes before writing to flash
      if (verifySha) {
        mbedtls_sha256_update(&sha256ctx, buf, (size_t)n);
      }

      size_t w = Update.write(buf, (size_t)n);
      if (w != (size_t)n) {
        SerialMon.printf("Update.write mismatch (w=%u n=%d) err=%s\n", (unsigned)w, n, Update.errorString());
        client.stop();
        if (verifySha) mbedtls_sha256_free(&sha256ctx);
        Update.abort();
        return false;
      }
      written += w;
      if (millis() - lastLog > 2000) { SerialMon.printf("Downloaded %u bytes\n", (unsigned)written); lastLog = millis(); }
      if (contentLength > 0 && written >= contentLength) break;
    }
    client.stop();

    if (timedOut) {
      if (verifySha) mbedtls_sha256_free(&sha256ctx);
      Update.abort();
      return false;
    }

    if (contentLength > 0 && written != contentLength) {
      SerialMon.printf("Short read: expected %u, got %u\n", (unsigned)contentLength, (unsigned)written);
      if (verifySha) mbedtls_sha256_free(&sha256ctx);
      Update.abort();
      return false;
    }

    // Verify SHA-256 before committing the update
    if (verifySha) {
      uint8_t computedHash[32];
      mbedtls_sha256_finish(&sha256ctx, computedHash);
      mbedtls_sha256_free(&sha256ctx);

      if (memcmp(computedHash, expectedSha256, 32) != 0) {
        SerialMon.println("SHA-256 MISMATCH — firmware corrupted, aborting OTA!");
        SerialMon.print("  Expected: ");
        for (int i = 0; i < 32; ++i) SerialMon.printf("%02x", expectedSha256[i]);
        SerialMon.println();
        SerialMon.print("  Computed: ");
        for (int i = 0; i < 32; ++i) SerialMon.printf("%02x", computedHash[i]);
        SerialMon.println();
        Update.abort();
        return false;
      }
      SerialMon.println("SHA-256 verified OK");
    }

    if (!Update.end(true)) { // true => set boot partition, image pending verify
      SerialMon.printf("Update.end failed: %s\n", Update.errorString());
      return false;
    }
    SerialMon.printf("OTA image written (%u bytes); rebooting into pending verify\n", (unsigned)written);
    return true;
  }
  // If we exhausted redirects without getting 200, return failure
  return false;
}

//
// PUBLIC FUNCTION: checkForFirmwareUpdate
// Main OTA entry point — checks remote for new firmware, downloads and installs if available.
// Enforces pre-flight battery gate (H-04): 3.85V minimum + 50% SoC (prevents bricking).
// Calls downloadAndCheckVersion(versionUrl) to query and compare versions.
// Returns: true if update initiated and device will restart, false if no update or error.
//
bool checkForFirmwareUpdate(const char* baseUrl) {
  // Battery safety check: OTA download and flash write draw significant power (modem TX + flash ops).
  // If battery dies mid-flash on a sealed buoy, the device is permanently bricked.
  // Low battery sag under TX current can brown out NVS write, corrupting state.
  float voltage = getStableBatteryVoltage();
  int pct = estimateBatteryPercent(voltage);
  const float MIN_OTA_VOLTAGE = 3.85f;  // Provides headroom under modem TX current
  const int MIN_OTA_SOC_PCT = 50;       // Ensures recovery even if battery dips during download

  if (voltage < MIN_OTA_VOLTAGE || pct < MIN_OTA_SOC_PCT) {
    SerialMon.printf("OTA skipped: insufficient battery (%.2fV / %d%%). Need >=%.2fV AND >=%d%%.\n",
                     voltage, pct, MIN_OTA_VOLTAGE, MIN_OTA_SOC_PCT);
    return false;
  }

  String versionUrl = String(baseUrl);
  if (versionUrl.endsWith(".bin")) versionUrl = versionUrl.substring(0, versionUrl.length() - 4) + ".version";
  else versionUrl += ".version";
  bool hasNew = downloadAndCheckVersion(versionUrl.c_str());
  if (!hasNew) return false;

  String firmwareUrl = String(baseUrl);
  if (!firmwareUrl.endsWith(".bin")) firmwareUrl += ".bin";

  // Download SHA-256 hash for integrity verification.
  // File format: 64 hex chars (optionally followed by filename, like sha256sum output).
  // If .sha256 file is unavailable, proceed without verification (graceful degradation).
  String sha256Url = firmwareUrl.substring(0, firmwareUrl.length() - 4) + ".sha256";
  String sha256Body = httpGetTinyGsm(sha256Url.c_str());
  uint8_t expectedHash[32];
  bool haveSha = parseHexSha256(sha256Body, expectedHash);
  if (haveSha) {
    SerialMon.print("SHA-256 from server: ");
    for (int i = 0; i < 32; ++i) SerialMon.printf("%02x", expectedHash[i]);
    SerialMon.println();
  } else {
    SerialMon.println("No .sha256 file available — proceeding without integrity check");
  }

  if (downloadAndInstallFirmware(firmwareUrl.c_str(), haveSha ? expectedHash : nullptr)) {
    SerialMon.println("OTA update successful. Saving state and rebooting...");
    markFirmwareUpdateAttempted();
    saveStateToNvs();
    powerOffModem();
    delay(500);
    ESP.restart();
    return true;
  }
  SerialMon.println("OTA update failed.");
  return false;
}


