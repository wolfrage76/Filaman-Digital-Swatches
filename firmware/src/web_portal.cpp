#include "web_portal.h"

#include <HTTPUpdateServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include <esp_heap_caps.h>

#include "app_config.h"
#include "display_poll.h"
#include "ota_update.h"
#include "printer_list.h"

namespace {

WebServer server(80);
HTTPUpdateServer httpUpdater;  // browser .bin upload -> inactive OTA partition, at /update
bool begun = false;

// Basic auth using the OTA/config password. Open (with an on-page warning) if
// no password is set.
bool authOK() {
  const String &pw = config().otaPassword;
  if (pw.length() == 0) {
    return true;
  }
  if (!server.authenticate("admin", pw.c_str())) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

String esc(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

String field(const char *label, const char *name, const String &value, const char *type) {
  String h = "<label>";
  h += label;
  h += "<input type='";
  h += type;
  h += "' name='";
  h += name;
  h += "' value='";
  h += esc(value);
  h += "'></label>";
  return h;
}

String pwField(const char *label, const char *name) {
  String h = "<label>";
  h += label;
  h += "<input type='password' name='";
  h += name;
  h += "' placeholder='(leave blank to keep)'></label>";
  return h;
}

void handleRoot() {
  if (!authOK()) {
    return;
  }
  const AppConfig &c = config();
  String h =
      "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>CrowPanel Config</title><style>"
      "body{font-family:sans-serif;max-width:520px;margin:1em auto;padding:0 1em;"
      "background:#0a0e14;color:#f2f5f8}label{display:block;margin:.6em 0}"
      "input{width:100%;padding:.5em;box-sizing:border-box;background:#182028;color:#f2f5f8;"
      "border:1px solid #2a3544;border-radius:6px}h2{color:#3ddc97}"
      "button{margin:1em 0;padding:.7em 1.4em;background:#3ddc97;color:#0a0e14;border:0;"
      "border-radius:6px;font-weight:bold;font-size:1em}"
      "fieldset{border:1px solid #2a3544;border-radius:8px;margin:1em 0}"
      "legend{color:#8b98a8}</style><h2>CrowPanel Config</h2>";
  if (c.otaPassword.length() == 0) {
    h += "<p style='color:#ff5c6a'>No password set - this page is unprotected. "
         "Set an OTA/config password below.</p>";
  }
  h += "<form method=POST action=/save>";
  h += "<fieldset><legend>WiFi</legend>";
  h += field("SSID", "wifiSsid", c.wifiSsid, "text");
  h += pwField("Password", "wifiPass");
  h += "</fieldset><fieldset><legend>MQTT</legend>";
  h += field("Host", "mqttHost", c.mqttHost, "text");
  h += field("Port", "mqttPort", String(c.mqttPort), "number");
  h += field("Username", "mqttUser", c.mqttUsername, "text");
  h += pwField("Password", "mqttPass");
  h += field("Topic prefix", "topicPfx", c.topicPrefix, "text");
  h += "</fieldset><fieldset><legend>FilaMan</legend>";
  h += field("Base URL", "fmUrl", c.filamanBaseUrl, "text");
  h += pwField("API key", "fmKey");
  h += field("Printer ID (0=auto)", "fmPrinter", String(c.filamanPrinterId), "number");
  h += "<label style='display:flex;align-items:center;gap:.5em'>";
  h += "<input type=checkbox name=fmTls ";
  h += (c.filamanTlsInsecure ? "checked" : "");
  h += " style='width:auto'>TLS insecure (LAN https)</label>";
  h += "</fieldset><fieldset><legend>Device &amp; Display</legend>";
  h += field("Hostname", "otaHost", c.otaHostname, "text");
  h += field("Timezone (POSIX TZ, e.g. EST5EDT,M3.2.0,M11.1.0)", "tz", c.displayTimezone, "text");
  h += field("Normal brightness % (50 / 75 / 100)", "brtFull", String(c.displayFullBrightPct), "number");
  h += field("Dimmed brightness % (10 / 20 / 35)", "brtDim", String(c.displayDimBrightPct), "number");
  h += field("Auto-return to AMS View sec (0 / 60 / 180)", "idleSec", String(c.idleReturnSec), "number");
  h += field("Low spool warning % (20 / 30 / 40)", "spLow", String(c.spoolLowPct), "number");
  h += field("Critical spool alert % (10 / 15 / 20)", "spCrit", String(c.spoolCritPct), "number");
  h += pwField("OTA / config password", "otaPass");
  h += "</fieldset>";
  h += "<button type=submit>Save &amp; Reboot</button></form>";

  // Monitored printer. FilaMan mode stores the id locally. MQTT mode publishes it.
  PrinterOption printers[MAX_PRINTER_OPTIONS];
  String activeId;
  const int pn = printer_list_get(printers, MAX_PRINTER_OPTIONS, activeId);
  h += "<form method=POST action=/printer><fieldset><legend>Monitored printer</legend>";
  if (pn == 0) {
    h += display_poll_active()
             ? "<p style='color:#8b98a8'>No printers yet. Check the FilaMan URL and API key.</p>"
             : "<p style='color:#8b98a8'>Waiting for the MQTT printer list.</p>";
  } else {
    for (int i = 0; i < pn; ++i) {
      const bool sel = printers[i].active || printers[i].id == activeId;
      h += "<label style='display:flex;align-items:center;gap:.5em'>";
      h += "<input type=radio name=pid value='";
      h += esc(printers[i].id);
      h += "'";
      h += (sel ? " checked" : "");
      h += " style='width:auto'>";
      h += esc(printers[i].name);
      if (printers[i].model.length()) {
        h += " <span style='color:#8b98a8'>(";
        h += esc(printers[i].model);
        h += ")</span>";
      }
      h += "</label>";
    }
    h += "<button type=submit>Monitor selected printer</button>";
  }
  h += "</fieldset></form>";

  h += "<p style='color:#8b98a8'>heap ";
  h += String(static_cast<uint32_t>(ESP.getFreeHeap()));
  h += " · ";
  h += display_poll_active() ? display_poll_status() : "mqtt mode";
  h += "</p>";
  h += "<form method=post action=/github-update style='margin-top:12px'><fieldset><legend>Firmware Update</legend>";
  h += "<p style='color:#8b98a8;margin:4px 0 8px'>Installed: v" + String(FIRMWARE_VERSION) + "</p>";
  h += "<button type=submit>Check &amp; Install Latest Release from GitHub</button>";
  h += "<p style='margin:10px 0 0'><a href='/update' style='color:#5aa7ff'>Or upload a local .bin file over WiFi</a></p>";
  h += "</fieldset></form>";
  server.send(200, "text/html", h);
}

// For password fields: blank submission keeps the stored value.
String keepIfBlank(const char *name, const String &current) {
  if (!server.hasArg(name)) {
    return current;
  }
  const String v = server.arg(name);
  return v.length() ? v : current;
}

void handleSave() {
  if (!authOK()) {
    return;
  }
  AppConfig c = config();  // start from current, override submitted fields
  if (server.hasArg("wifiSsid")) c.wifiSsid = server.arg("wifiSsid");
  c.wifiPassword = keepIfBlank("wifiPass", c.wifiPassword);
  if (server.hasArg("mqttHost")) c.mqttHost = server.arg("mqttHost");
  if (server.hasArg("mqttPort") && server.arg("mqttPort").length())
    c.mqttPort = server.arg("mqttPort").toInt();
  if (server.hasArg("mqttUser")) c.mqttUsername = server.arg("mqttUser");
  c.mqttPassword = keepIfBlank("mqttPass", c.mqttPassword);
  if (server.hasArg("topicPfx")) c.topicPrefix = server.arg("topicPfx");
  if (server.hasArg("fmUrl")) c.filamanBaseUrl = server.arg("fmUrl");
  c.filamanApiKey = keepIfBlank("fmKey", c.filamanApiKey);
  if (server.hasArg("fmPrinter")) c.filamanPrinterId = server.arg("fmPrinter").toInt();
  c.filamanTlsInsecure = server.hasArg("fmTls");  // unchecked box sends nothing
  if (server.hasArg("otaHost")) c.otaHostname = server.arg("otaHost");
  if (server.hasArg("tz") && server.arg("tz").length()) c.displayTimezone = server.arg("tz");
  if (server.hasArg("brtFull") && server.arg("brtFull").length())
    c.displayFullBrightPct = server.arg("brtFull").toInt();
  if (server.hasArg("brtDim") && server.arg("brtDim").length())
    c.displayDimBrightPct = server.arg("brtDim").toInt();
  if (server.hasArg("idleSec") && server.arg("idleSec").length())
    c.idleReturnSec = server.arg("idleSec").toInt();
  if (server.hasArg("spLow") && server.arg("spLow").length())
    c.spoolLowPct = server.arg("spLow").toInt();
  if (server.hasArg("spCrit") && server.arg("spCrit").length())
    c.spoolCritPct = server.arg("spCrit").toInt();
  c.otaPassword = keepIfBlank("otaPass", c.otaPassword);
  c.save();

  server.send(200, "text/html",
              "<meta http-equiv=refresh content='6;url=/'>"
              "<body style='font-family:sans-serif;background:#0a0e14;color:#3ddc97'>"
              "Saved. Rebooting to apply...");
  delay(600);
  ESP.restart();
}

void handlePrinter() {
  if (!authOK()) {
    return;
  }
  if (server.hasArg("pid") && server.arg("pid").length()) {
    printer_list_publish_selection(server.arg("pid"));
    server.send(200, "text/html",
                "<meta http-equiv=refresh content='4;url=/'>"
                "<body style='font-family:sans-serif;background:#0a0e14;color:#3ddc97'>"
                "Switching monitored printer... the display will update shortly.");
  } else {
    server.sendHeader("Location", "/");
    server.send(303);
  }
}

void handleGithubUpdate() {
  if (!authOK()) {
    return;
  }
  String ver, url, err;
  if (!ota_check_github_update(ver, url, err)) {
    server.send(500, "text/html",
                "<meta http-equiv=refresh content='6;url=/'>"
                "<body style='font-family:sans-serif;background:#0a0e14;color:#ff5c6a'>"
                "GitHub update check failed: " +
                    esc(err));
    return;
  }
  if (!ota_install_github_update(url, nullptr, err)) {
    server.send(500, "text/html",
                "<meta http-equiv=refresh content='6;url=/'>"
                "<body style='font-family:sans-serif;background:#0a0e14;color:#ff5c6a'>"
                "GitHub OTA failed: " +
                    esc(err));
    return;
  }
  server.send(200, "text/html",
              "<meta http-equiv=refresh content='8;url=/'>"
              "<body style='font-family:sans-serif;background:#0a0e14;color:#3ddc97'>"
              "Installed v" +
                  esc(ver) + " from GitHub. Rebooting...");
  delay(700);
  ESP.restart();
}

}  // namespace

void web_portal_begin() {
  if (begun) {
    return;
  }
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/printer", HTTP_POST, handlePrinter);
  server.on("/github-update", HTTP_POST, handleGithubUpdate);
  // Browser firmware upload over WiFi at /update (writes the inactive OTA slot).
  if (config().otaPassword.length()) {
    httpUpdater.setup(&server, "/update", "admin", config().otaPassword.c_str());
  } else {
    httpUpdater.setup(&server, "/update");
  }
  server.begin();
  begun = true;
}

void web_portal_loop() {
  if (begun) {
    server.handleClient();
  }
}
