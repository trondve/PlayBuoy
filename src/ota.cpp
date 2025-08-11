#define TINY_GSM_MODEM_SIM7000
#include "ota.h"
#include "config.h"
#include <TinyGsmClient.h>

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
  SerialMon.println("CHECKING FOR FIRMWARE UPDATES");
  SerialMon.printf("Version URL: %s\n", versionUrl);
  SerialMon.printf("Current firmware version: %s\n", FIRMWARE_VERSION);
  SerialMon.println("----------------------------------------");
  String serverVersion = getServerFirmwareVersion(versionUrl);
  if (serverVersion.length() == 0) { SerialMon.println("Could not retrieve server version"); return false; }
  SerialMon.printf("Server version retrieved: %s\n", serverVersion.c_str());
  int cmp = compareVersions(serverVersion, FIRMWARE_VERSION);
  SerialMon.printf("Version comparison: %s vs %s = %d\n", serverVersion.c_str(), FIRMWARE_VERSION, cmp);
  if (cmp > 0) { SerialMon.println("NEW FIRMWARE AVAILABLE!"); return true; }
  if (cmp == 0) SerialMon.println("Firmware is up to date"); else SerialMon.println("Server version is older");
  return false;
}

bool checkForFirmwareUpdate(const char* baseUrl) {
  String versionUrl = String(baseUrl);
  if (versionUrl.endsWith(".bin")) versionUrl = versionUrl.substring(0, versionUrl.length() - 4) + ".version";
  else versionUrl += ".version";
  (void)downloadAndCheckVersion(versionUrl.c_str());
  return false; // Version check only; install not implemented here
}


