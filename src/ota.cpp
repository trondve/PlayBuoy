#define TINY_GSM_MODEM_SIM7000
#include "ota.h"
#include "config.h"
#include <TinyGsmClient.h>
#include <Update.h>

#define SerialMon Serial

extern TinyGsm modem;

static bool ensurePdpForHttp() {
  if (modem.isGprsConnected()) return true;
  return modem.gprsConnect(NETWORK_PROVIDER, "", "");
}

static String extractVersionFromBody(const String& body) {
  String trimmed = body; trimmed.trim();
  if (trimmed.length() == 0) return "";
  String candidate;
  for (size_t i = 0; i < trimmed.length(); ++i) {
    char c = trimmed[i];
    if ((c >= '0' && c <= '9') || c == '.') candidate += c;
    else if (candidate.length() > 0) break;
  }
  int a = candidate.indexOf('.');
  int b = candidate.lastIndexOf('.');
  if (a > 0 && b > a) return candidate;
  return "";
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

  unsigned long t0 = millis(); String headers;
  while (millis() - t0 < 15000 && client.connected()) {
    while (client.available()) {
      char c = client.read();
      headers += c;
      if (headers.endsWith("\r\n\r\n")) goto headers_done;
    }
    delay(5);
  }
headers_done:
  String body; unsigned long t1 = millis();
  while (millis() - t1 < 20000) {
    while (client.available()) body += (char)client.read();
    if (!client.connected()) break; delay(5);
  }
  client.stop();
  int p = body.indexOf("\r\n\r\n"); if (p >= 0) body = body.substring(p + 4);
  body.trim();
  SerialMon.printf("HTTP body: '%s'\n", body.c_str());
  return body;
}

String getServerFirmwareVersion(const char* versionUrl) {
  SerialMon.println("CHECKING FOR FIRMWARE UPDATES");
  SerialMon.printf("Version URL: %s\n", versionUrl);
  SerialMon.printf("Current firmware version: %s\n", FIRMWARE_VERSION);
  SerialMon.println("----------------------------------------");
  String body = httpGetTinyGsm(versionUrl);
  if (body.length() == 0) return "";
  return extractVersionFromBody(body);
}

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

static bool parseHttpResponseHeaders(TinyGsmClient& client, int& statusCode, size_t& contentLength) {
  statusCode = -1;
  contentLength = 0;
  String line;
  unsigned long t0 = millis();
  // Read status line
  while (millis() - t0 < 15000) {
    while (client.available()) {
      char c = client.read();
      if (c == '\n') {
        line.trim();
        if (line.startsWith("HTTP/1.1 ") || line.startsWith("HTTP/1.0 ")) {
          int sp = line.indexOf(' ');
          if (sp > 0) statusCode = line.substring(sp + 1).toInt();
        }
        line = "";
        goto read_headers;
      } else if (c != '\r') {
        line += c;
      }
    }
    delay(5);
  }
read_headers:;
  // Read headers until blank line
  t0 = millis();
  while (millis() - t0 < 15000) {
    while (client.available()) {
      char c = client.read();
      if (c == '\n') {
        String h = line; h.trim();
        if (h.length() == 0) return (statusCode > 0);
        if (h.startsWith("Content-Length:")) {
          String v = h.substring(strlen("Content-Length:")); v.trim();
          contentLength = (size_t)v.toInt();
        }
        line = "";
      } else if (c != '\r') {
        line += c;
      }
    }
    delay(5);
  }
  return (statusCode > 0);
}

bool downloadAndInstallFirmware(const char* firmwareUrl) {
  SerialMon.printf("Downloading firmware from: %s\n", firmwareUrl);
  if (!ensurePdpForHttp()) {
    SerialMon.println("No PDP for firmware download");
    return false;
  }

  // Parse URL
  String u(firmwareUrl), host, path; uint16_t port = 80;
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
  if (!parseHttpResponseHeaders(client, status, contentLength)) { client.stop(); return false; }
  SerialMon.printf("HTTP status: %d, Content-Length: %u\n", status, (unsigned)contentLength);
  if (status != 200) { client.stop(); return false; }

  size_t updateSize = (contentLength > 0) ? contentLength : (size_t)UPDATE_SIZE_UNKNOWN;
  if (!Update.begin(updateSize)) {
    SerialMon.printf("Update.begin failed: %s\n", Update.errorString());
    client.stop();
    return false;
  }

  size_t written = 0; uint8_t buf[1024]; unsigned long lastLog = millis();
  while (client.connected()) {
    int n = client.readBytes(buf, sizeof(buf));
    if (n < 0) break;
    if (n == 0) { delay(5); continue; }
    size_t w = Update.write(buf, (size_t)n);
    if (w != (size_t)n) {
      SerialMon.printf("Update.write mismatch (w=%u n=%d) err=%s\n", (unsigned)w, n, Update.errorString());
      client.stop();
      Update.abort();
      return false;
    }
    written += w;
    if (millis() - lastLog > 2000) { SerialMon.printf("Downloaded %u bytes\n", (unsigned)written); lastLog = millis(); }
    if (contentLength > 0 && written >= contentLength) break;
  }
  client.stop();

  if (contentLength > 0 && written != contentLength) {
    SerialMon.printf("Short read: expected %u, got %u\n", (unsigned)contentLength, (unsigned)written);
    Update.abort();
    return false;
  }

  if (!Update.end(true)) { // true => set boot partition, image pending verify
    SerialMon.printf("Update.end failed: %s\n", Update.errorString());
    return false;
  }
  SerialMon.println("OTA image written; rebooting into pending verify");
  return true;
}

bool checkForFirmwareUpdate(const char* baseUrl) {
  String versionUrl = String(baseUrl);
  if (versionUrl.endsWith(".bin")) versionUrl = versionUrl.substring(0, versionUrl.length() - 4) + ".version";
  else versionUrl += ".version";
  bool hasNew = downloadAndCheckVersion(versionUrl.c_str());
  if (!hasNew) return false;

  String firmwareUrl = String(baseUrl);
  if (!firmwareUrl.endsWith(".bin")) firmwareUrl += ".bin";
  if (downloadAndInstallFirmware(firmwareUrl.c_str())) {
    SerialMon.println("OTA update successful. Rebooting...");
    delay(500);
    ESP.restart();
    return true;
  }
  SerialMon.println("OTA update failed.");
  return false;
}


