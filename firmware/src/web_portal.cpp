#include "web_portal.h"

#include <HTTPUpdateServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include <esp_heap_caps.h>
#include <miniz.h>

#include "app_config.h"
#include "boards/board.h"
#include "config.h"
#include "device_log.h"
#include "display_capture.h"
#include "display_poll.h"
#include "ota_update.h"
#include "printer_list.h"
#include "timezones.h"

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
    else if (c == '\'') o += "&#39;";
    else o += c;
  }
  return o;
}

String field(const char *label, const char *name, const String &value, const char *type,
             const char *hint = nullptr, const char *placeholder = nullptr) {
  String h = "<div class='field'><label class='field-label' for='f_";
  h += name;
  h += "'>";
  h += label;
  h += "</label><input id='f_";
  h += name;
  h += "' type='";
  h += type;
  h += "' name='";
  h += name;
  h += "' value='";
  h += esc(value);
  h += "'";
  if (placeholder && placeholder[0]) {
    h += " placeholder='";
    h += placeholder;
    h += "'";
  }
  h += ">";
  if (hint && hint[0]) {
    h += "<span class='field-hint'>";
    h += hint;
    h += "</span>";
  }
  h += "</div>";
  return h;
}

String pwField(const char *label, const char *name, const char *hint = nullptr) {
  String h = "<div class='field'><label class='field-label' for='f_";
  h += name;
  h += "'>";
  h += label;
  h += "</label><input id='f_";
  h += name;
  h += "' type='password' name='";
  h += name;
  h += "' placeholder='Leave blank to keep existing'>";
  if (hint && hint[0]) {
    h += "<span class='field-hint'>";
    h += hint;
    h += "</span>";
  }
  h += "</div>";
  return h;
}

String timezoneField(const String &current) {
  const int sel = timezone_index(current);
  String h =
      "<div class='field'><label class='field-label' for='f_tzPreset'>Time zone</label>"
      "<select id='f_tzPreset' name='tzPreset'>";
  if (sel < 0) {
    // A rule from secrets.h or an older firmware: keep it until a place is picked.
    h += "<option value='' selected>Keep current setting</option>";
  }
  for (int i = 0; i < kTimezoneCount; ++i) {
    h += "<option value='";
    h += i;
    h += "'";
    if (i == sel) {
      h += " selected";
    }
    h += ">";
    h += kTimezones[i].label;
    h += "</option>";
  }
  h += "</select><span class='field-hint'>Pick the place nearest you. "
       "Daylight saving is handled automatically.</span></div>";
  return h;
}

String toggleField(const char *title, const char *subtitle, const char *name, bool checked) {
  String h = "<label class='toggle-row' for='f_";
  h += name;
  h += "'><div class='toggle-copy'><span class='toggle-title'>";
  h += title;
  h += "</span>";
  if (subtitle && subtitle[0]) {
    h += "<span class='toggle-sub'>";
    h += subtitle;
    h += "</span>";
  }
  h += "</div><input class='toggle-input' id='f_";
  h += name;
  h += "' type='checkbox' name='";
  h += name;
  h += "'";
  if (checked) {
    h += " checked";
  }
  h += "><span class='toggle-track'><span class='toggle-thumb'></span></span></label>";
  return h;
}

// GitHub and MakerWorld buttons that close every console page. Marks are
// inline SVG: the panel often sits on a LAN with no route out, so nothing here
// is fetched. currentColor lets each mark follow its button's text colour.
#define FOOT_SVG "<svg viewBox='0 0 24 24' aria-hidden='true' focusable='false'><path d='"
#define FOOT_SVG_END "'/></svg>"

String footerLinks() {
  return F(
      "<style>.links{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;"
      "margin-top:18px}"
      ".links a{display:flex;align-items:center;justify-content:center;gap:8px;"
      "padding:11px 8px;border-radius:9px;text-decoration:none;font-size:13px;font-weight:600;"
      "background:#111823;color:#c7d2e0;border:1px solid #222f42;transition:background .14s}"
      ".links a:hover{background:#18222f}"
      ".links svg{width:15px;height:15px;flex:none;fill:currentColor}</style>"
      "<footer class='links'>"
      "<a href='" PROJECT_GITHUB_URL "' target='_blank' rel='noopener'>" FOOT_SVG
      "M12 .5C5.37.5 0 5.87 0 12.5c0 5.3 3.44 9.8 8.21 11.39.6.11.82-.26.82-.58"
      "v-2.03c-3.34.73-4.04-1.61-4.04-1.61-.55-1.39-1.34-1.76-1.34-1.76-1.09-.75."
      "08-.73.08-.73 1.2.08 1.84 1.24 1.84 1.24 1.07 1.84 2.81 1.31 3.5 1 .11-.78."
      "42-1.31.76-1.61-2.67-.3-5.47-1.34-5.47-5.96 0-1.32.47-2.39 1.24-3.23-.12-."
      "31-.54-1.53.12-3.18 0 0 1.01-.32 3.3 1.23a11.5 11.5 0 016.01 0c2.29-1.55 "
      "3.3-1.23 3.3-1.23.66 1.65.24 2.87.12 3.18.77.84 1.24 1.91 1.24 3.23 0 4.63"
      "-2.81 5.65-5.49 5.95.43.37.82 1.1.82 2.22v3.29c0 .32.22.7.83.58A12.01 12.01"
      " 0 0024 12.5C24 5.87 18.63.5 12 .5z" FOOT_SVG_END "GitHub</a>"
      "<a href='" PROJECT_MAKERWORLD_URL "' target='_blank' rel='noopener'>" FOOT_SVG
      "M12 2.5l8.5 4.75v9.5L12 21.5l-8.5-4.75v-9.5L12 2.5zm0 2.29L6.06 8.1 12 11.4"
      "l5.94-3.3L12 4.79zM5.5 9.79v5.79l5.5 3.07v-5.79L5.5 9.79zm13 0l-5.5 3.07v5."
      "79l5.5-3.07V9.79z" FOOT_SVG_END "MakerWorld</a>"
      "</footer>");
}

void sendStatusPage(int httpCode, const char *badge, const String &title, const String &message,
                    int refreshSec, bool isError) {
  String h =
      "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='refresh' content='";
  h += String(refreshSec);
  h += ";url=/'><title>";
  h += title;
  h += " - Digital Swatch System</title><style>"
       ":root{--bg:#070a0f;--card:#10161f;--border:#222e3e;--text:#eef2f7;--muted:#8a99ad;"
       "--accent:#3ddc97;--danger:#ff5c6a}"
       "*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;align-items:center;"
       "justify-content:center;padding:24px;font-family:'Inter',-apple-system,BlinkMacSystemFont,"
       "'Segoe UI',system-ui,sans-serif;background:radial-gradient(ellipse 80% 50% at 50% -10%,"
       "rgba(61,220,151,.12),transparent),var(--bg);color:var(--text)}"
       ".box{max-width:440px;width:100%;background:var(--card);border:1px solid ";
  h += isError ? "rgba(255,92,106,.45)" : "rgba(61,220,151,.38)";
  h += ";border-radius:14px;padding:28px 26px;box-shadow:0 20px 50px rgba(0,0,0,.55);"
       "text-align:left}"
       ".pill{display:inline-flex;align-items:center;gap:6px;font-size:11px;font-weight:700;"
       "letter-spacing:.08em;text-transform:uppercase;padding:4px 10px;border-radius:999px;"
       "background:";
  h += isError ? "rgba(255,92,106,.14)" : "rgba(61,220,151,.14)";
  h += ";color:";
  h += isError ? "var(--danger)" : "var(--accent)";
  h += ";margin-bottom:14px}"
       "h1{margin:0 0 8px;font-size:20px;font-weight:650;letter-spacing:-.01em}"
       "p{margin:0 0 18px;color:var(--muted);font-size:14px;line-height:1.55}"
       ".bar{height:4px;width:100%;background:#182230;border-radius:99px;overflow:hidden;"
       "margin-bottom:14px}"
       ".fill{height:100%;width:100%;background:";
  h += isError ? "var(--danger)" : "var(--accent)";
  h += ";transform-origin:left;animation:prog ";
  h += String(refreshSec);
  h += "s linear forwards}@keyframes prog{from{transform:scaleX(0)}to{transform:scaleX(1)}}"
       ".back{color:var(--muted);font-size:12px;text-decoration:none}"
       ".back:hover{color:var(--text)}"
       "</style></head><body><div class='box'><span class='pill'>";
  h += badge;
  h += "</span><h1>";
  h += title;
  h += "</h1><p>";
  h += message;
  h += "</p><div class='bar'><div class='fill'></div></div>"
       "<a class='back' href='/'>Returning to console automatically...</a>"
       "</div></body></html>";
  server.send(httpCode, "text/html", h);
}

void handleRoot() {
  if (!authOK()) {
    return;
  }
  const AppConfig &c = config();
  const String ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("AP / Offline");
  const uint32_t freeHeapKb = static_cast<uint32_t>(ESP.getFreeHeap()) / 1024u;
  const String dataMode = display_poll_active() ? display_poll_status() : String("MQTT Mode");
  const bool updateAvail = ota_update_available();
  const String latestVer = ota_cached_latest_version();

  String h;
  h.reserve(23000);
  h += "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
       "<meta name='viewport' content='width=device-width,initial-scale=1'>"
       "<title>Digital Swatch System · Console</title><style>"
       ":root{"
       "--bg:#070a0f;--bg-elev:#0d131c;--card:#111823;--card-2:#151f2d;"
       "--border:#222f42;--border-soft:#1a2433;--text:#eef2f7;--muted:#8a99ad;--dim:#59687b;"
       "--accent:#3ddc97;--accent-hover:#54e6a6;--accent-soft:rgba(61,220,151,.12);"
       "--info:#5aa7ff;--warn:#ffb443;--danger:#ff5c6a;"
       "--radius:12px;--mono:'JetBrains Mono',ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}"
       "*{box-sizing:border-box}"
       "body{margin:0;padding:0;background:radial-gradient(ellipse 90% 45% at 50% -10%,"
       "rgba(61,220,151,.10),transparent 70%),var(--bg);color:var(--text);"
       "font-family:'Inter',-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;"
       "font-size:14px;line-height:1.5;-webkit-font-smoothing:antialiased}"
       ".shell{max-width:1020px;margin:0 auto;padding:24px 18px 56px}"
       /* Top Header */
       ".topbar{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;"
       "gap:16px;padding:18px 22px;background:rgba(17,24,35,.88);border:1px solid var(--border);"
       "border-radius:14px;box-shadow:inset 0 1px 0 rgba(255,255,255,.04),0 14px 34px rgba(0,0,0,.35);"
       "margin-bottom:18px}"
       ".brand{display:flex;align-items:center;gap:14px}"
       ".swatch-mark{display:grid;grid-template-columns:repeat(4,6px);gap:3px;padding:7px;"
       "background:#0a0f16;border:1px solid var(--border);border-radius:10px;height:40px;align-items:stretch}"
       ".swatch-mark span{display:block;border-radius:2px}"
       ".sm-1{background:#3ddc97}.sm-2{background:#5aa7ff}.sm-3{background:#ffb443}.sm-4{background:#ff5c6a}"
       ".brand-title{margin:0;font-size:18px;font-weight:700;letter-spacing:-.02em;color:var(--text)}"
       ".brand-sub{margin:2px 0 0;font-size:12px;color:var(--muted)}"
       ".telemetry{display:flex;flex-wrap:wrap;align-items:center;gap:8px}"
       ".chip{display:inline-flex;align-items:center;gap:7px;padding:5px 11px;"
       "background:var(--bg-elev);border:1px solid var(--border);border-radius:999px;"
       "font-size:12px;color:var(--muted);font-family:var(--mono);font-variant-numeric:tabular-nums}"
       ".chip strong{color:var(--text);font-weight:600}"
       ".dot{width:7px;height:7px;border-radius:50%;background:var(--accent);"
       "box-shadow:0 0 8px var(--accent)}"
       ".dot-warn{background:var(--warn);box-shadow:0 0 8px var(--warn)}"
       ".tabs{display:flex;gap:6px;margin:-4px 0 18px;padding:5px;background:#0b1017;"
       "border:1px solid var(--border-soft);border-radius:10px;width:max-content}"
       ".tab{padding:7px 13px;border-radius:6px;color:var(--muted);font-size:12px;font-weight:650;"
       "text-decoration:none;transition:background .15s,color .15s}"
       ".tab:hover{color:var(--text);background:var(--card-2)}"
       ".tab.active{color:#07120d;background:var(--accent)}"
       /* Security Banner */
       ".banner-warn{display:flex;align-items:center;justify-content:space-between;gap:12px;"
       "padding:13px 18px;margin-bottom:18px;border-radius:11px;"
       "background:linear-gradient(90deg,rgba(255,92,106,.14),rgba(255,180,67,.08));"
       "border:1px solid rgba(255,92,106,.4);color:#ffd5d9;font-size:13px}"
       ".banner-warn strong{color:#fff;font-weight:650}"
       /* Layout Grids */
       ".grid-2{display:grid;grid-template-columns:1fr;gap:16px;margin-bottom:18px}"
       "@media(min-width:760px){.grid-2{grid-template-columns:repeat(2,minmax(0,1fr))}}"
       ".row-2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}"
       /* Cards */
       ".card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);"
       "padding:20px 22px;box-shadow:inset 0 1px 0 rgba(255,255,255,.03),0 8px 24px rgba(0,0,0,.25);"
       "display:flex;flex-direction:column;justify-content:space-between}"
       ".card-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;"
       "margin-bottom:16px;padding-bottom:12px;border-bottom:1px solid var(--border-soft)}"
       ".card-eyebrow{font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.1em;"
       "color:var(--accent);margin-bottom:3px;display:block}"
       ".card-title{margin:0;font-size:16px;font-weight:650;color:var(--text);letter-spacing:-.01em}"
       ".card-desc{margin:3px 0 0;font-size:12px;color:var(--muted)}"
       ".card-badge{font-family:var(--mono);font-size:11px;font-weight:600;padding:3px 9px;"
       "border-radius:6px;background:var(--card-2);border:1px solid var(--border);color:var(--muted);"
       "white-space:nowrap}"
       ".card-badge.live{background:var(--accent-soft);border-color:rgba(61,220,151,.35);color:var(--accent)}"
       /* Form Fields */
       ".field{margin-bottom:13px}"
       ".field:last-child{margin-bottom:0}"
       ".field-label{display:block;font-size:12px;font-weight:600;color:#c7d2e0;margin-bottom:5px}"
       ".field-hint{display:block;font-size:11px;color:var(--dim);margin-top:4px}"
       "input[type='text'],input[type='password'],input[type='number'],select{"
       "width:100%;padding:9px 12px;background:#0b1017;color:var(--text);"
       "border:1px solid var(--border);border-radius:8px;font-size:13px;font-family:inherit;"
       "transition:border-color .15s,box-shadow .15s;outline:none}"
       "select{appearance:auto;cursor:pointer}"
       "input[type='number']{font-family:var(--mono);font-variant-numeric:tabular-nums}"
       "input:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(61,220,151,.15)}"
       "input::placeholder{color:var(--dim)}"
       /* Toggle Switch */
       ".toggle-row{display:flex;align-items:center;justify-content:space-between;gap:14px;"
       "padding:11px 13px;margin-top:6px;background:#0b1017;border:1px solid var(--border-soft);"
       "border-radius:9px;cursor:pointer;user-select:none;transition:border-color .15s}"
       ".toggle-row:hover{border-color:var(--border)}"
       ".toggle-copy{display:flex;flex-direction:column;gap:2px}"
       ".toggle-title{font-size:13px;font-weight:600;color:var(--text)}"
       ".toggle-sub{font-size:11px;color:var(--muted)}"
       ".toggle-input{position:absolute;opacity:0;width:0;height:0}"
       ".toggle-track{position:relative;flex-shrink:0;width:40px;height:22px;background:#1c2635;"
       "border:1px solid var(--border);border-radius:999px;transition:background .2s,border-color .2s}"
       ".toggle-thumb{position:absolute;top:2px;left:2px;width:16px;height:16px;border-radius:50%;"
       "background:#8a99ad;transition:transform .2s,background .2s}"
       ".toggle-input:checked+.toggle-track{background:rgba(61,220,151,.22);border-color:var(--accent)}"
       ".toggle-input:checked+.toggle-track .toggle-thumb{transform:translateX(18px);background:var(--accent)}"
       /* Printer Radio List */
       ".printer-list{display:flex;flex-direction:column;gap:8px;margin-bottom:14px}"
       ".printer-item{display:flex;align-items:center;justify-content:space-between;gap:10px;"
       "padding:10px 13px;background:#0b1017;border:1px solid var(--border);border-radius:9px;"
       "cursor:pointer;transition:border-color .15s,background .15s}"
       ".printer-item:hover{border-color:#344863;background:#0e151f}"
       ".printer-item.active{border-color:var(--accent);background:rgba(61,220,151,.07)}"
       ".printer-left{display:flex;align-items:center;gap:10px;min-width:0}"
       ".printer-left input[type='radio']{accent-color:var(--accent);width:16px;height:16px;margin:0;cursor:pointer}"
       ".printer-name{font-weight:600;color:var(--text);font-size:13px;white-space:nowrap;"
       "overflow:hidden;text-overflow:ellipsis}"
       ".printer-model{font-family:var(--mono);font-size:11px;color:var(--muted);padding:2px 7px;"
       "background:var(--card-2);border:1px solid var(--border-soft);border-radius:5px}"
       ".empty-note{padding:16px;background:#0b1017;border:1px dashed var(--border);"
       "border-radius:9px;color:var(--muted);font-size:13px;text-align:center}"
       /* Buttons */
       ".btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;"
       "padding:10px 18px;border-radius:8px;font-size:13px;font-weight:650;font-family:inherit;"
       "cursor:pointer;border:1px solid transparent;text-decoration:none;transition:all .15s}"
       ".btn-primary{background:var(--accent);color:#06120c;border-color:var(--accent)}"
       ".btn-primary:hover{background:var(--accent-hover);box-shadow:0 4px 16px rgba(61,220,151,.28)}"
       ".btn-secondary{background:var(--card-2);color:var(--text);border-color:var(--border)}"
       ".btn-secondary:hover{background:#1c293b;border-color:#354a66}"
       ".btn-block{width:100%}"
       ".ota-actions{display:flex;flex-direction:column;gap:10px;margin-top:8px}"
       ".ota-meta{display:flex;align-items:center;justify-content:space-between;padding:10px 13px;"
       "background:#0b1017;border:1px solid var(--border-soft);border-radius:9px;margin-bottom:10px}"
       ".ota-ver{font-family:var(--mono);font-size:13px;font-weight:600;color:var(--text)}"
       ".fw-rows{margin-bottom:12px}"
       ".fw-row{display:flex;align-items:center;justify-content:space-between;gap:12px;"
       "padding:9px 2px;border-bottom:1px solid var(--border-soft);font-size:13px}"
       ".fw-row:last-child{border-bottom:0}"
       ".fw-row .k{color:var(--muted)}"
       ".fw-row .v{color:var(--text);text-align:right;font-variant-numeric:tabular-nums}"
       ".fw-row .v.mono{font-family:var(--mono);font-weight:600}"
       ".fw-row .v.new{color:var(--accent)}"
       ".fw-notes-btns{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:10px}"
       ".btn-quiet{background:transparent;color:var(--muted);border-color:var(--border);"
       "padding:7px 13px;font-size:12px}"
       ".btn-quiet:hover:not(:disabled){color:var(--text);border-color:#354a66}"
       ".btn-quiet:disabled{opacity:.45;cursor:default}"
       ".notes{display:none;max-height:300px;overflow:auto;margin:0 0 12px;padding:12px 14px;"
       "background:#0b1017;border:1px solid var(--border-soft);border-radius:9px;color:#b9c8d9;"
       "font:12px/1.55 var(--mono);white-space:pre-wrap;overflow-wrap:anywhere}"
       /* Bottom Save Bar */
       ".save-bar{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;"
       "gap:14px;padding:16px 22px;margin-top:4px;background:rgba(17,24,35,.92);"
       "border:1px solid var(--border);border-radius:13px;"
       "box-shadow:0 12px 32px rgba(0,0,0,.4)}"
       ".save-copy{font-size:13px;color:var(--muted)}"
       ".save-copy strong{color:var(--text);font-weight:600}"
       "</style></head><body><div class='shell'>";

  // 1. Top Command Header
  h += "<header class='topbar'><div class='brand'>"
       "<div class='swatch-mark'><span class='sm-1'></span><span class='sm-2'></span>"
       "<span class='sm-3'></span><span class='sm-4'></span></div>"
       "<div><h1 class='brand-title'>Digital Swatch System</h1>"
       "<p class='brand-sub'>" BOARD_MODEL " Hardware Configuration Console</p></div></div>"
       "<div class='telemetry'>"
       "<span class='chip'><span class='dot'></span><strong>";
  h += esc(dataMode);
  h += "</strong></span>"
       "<span class='chip'>FW <strong>v";
  h += esc(String(FIRMWARE_VERSION));
  h += "</strong></span>"
       "<span class='chip'>IP <strong>";
  h += esc(ipStr);
  h += "</strong></span>"
       "<span class='chip'>HEAP <strong>";
  h += String(freeHeapKb);
  h += " KB</strong></span></div></header>"
       "<nav class='tabs' aria-label='Console sections'>"
       "<a class='tab active' href='/'>Configuration</a>"
       "<a class='tab' href='/diagnostics'>Diagnostics</a>"
       "</nav>";

  // 2. Security Warning if no password is set
  if (c.otaPassword.length() == 0) {
    h += "<div class='banner-warn'><div><strong>Unprotected Console:</strong> "
         "No admin/OTA password is set. Anyone on your local network can modify settings or flash firmware.</div>"
         "<a href='#f_otaPass' class='btn btn-secondary' style='padding:6px 12px;font-size:12px'>"
         "Set Password</a></div>";
  }

  // 3. Top Quick-Action Cards: Monitored Printer + Firmware Update
  h += "<div class='grid-2'>";

  // Card A: Monitored Printer
  PrinterOption printers[MAX_PRINTER_OPTIONS];
  String activeId;
  const int pn = printer_list_get(printers, MAX_PRINTER_OPTIONS, activeId);
  h += "<form class='card' method='POST' action='/printer'><div>"
       "<div class='card-head'><div><span class='card-eyebrow'>Live Target</span>"
       "<h2 class='card-title'>Monitored Printer</h2>"
       "<p class='card-desc'>Select which printer feed drives the AMS swatch display.</p></div>"
       "<span class='card-badge'>";
  h += String(pn) + (pn == 1 ? " Printer" : " Printers");
  h += "</span></div>";
  if (pn == 0) {
    h += "<div class='empty-note'>";
    h += display_poll_active()
             ? "No printers reported yet. Verify your FilaMan URL and API Key below."
             : "Waiting for printer discovery payload over MQTT...";
    h += "</div></div></form>";
  } else {
    h += "<div class='printer-list'>";
    for (int i = 0; i < pn; ++i) {
      const bool sel = printers[i].active || printers[i].id == activeId;
      h += "<label class='printer-item";
      if (sel) h += " active";
      h += "'><div class='printer-left'><input type='radio' name='pid' value='";
      h += esc(printers[i].id);
      h += "'";
      if (sel) h += " checked";
      h += "><span class='printer-name'>";
      h += esc(printers[i].name);
      h += "</span></div><div style='display:flex;align-items:center;gap:6px'>";
      if (printers[i].model.length()) {
        h += "<span class='printer-model'>";
        h += esc(printers[i].model);
        h += "</span>";
      }
      if (sel) {
        h += "<span class='card-badge live'>ACTIVE</span>";
      }
      h += "</div></label>";
    }
    h += "</div></div>"
         "<button class='btn btn-primary btn-block' type='submit'>Switch Monitored Printer</button>"
         "</form>";
  }

  // Card B: Firmware & OTA Update
  h += "<form class='card' method='POST' action='/github-update'><div>"
       "<div class='card-head'><div><span class='card-eyebrow'>System Maintenance</span>"
       "<h2 class='card-title'>Firmware Update</h2>"
       "<p class='card-desc'>Install official GitHub releases or upload a local firmware binary.</p></div>";
  if (updateAvail && latestVer.length()) {
    h += "<span class='card-badge live'>NEW v" + esc(latestVer) + "</span>";
  } else {
    h += "<span class='card-badge'>OTA Ready</span>";
  }
  // Channel, dates and notes come from the GitHub API through the browser
  // (script below). The install time is the device's own record.
  h += "</div><div class='fw-rows'>"
       "<div class='fw-row'><span class='k'>Installed</span><span class='v mono'>v";
  h += esc(String(FIRMWARE_VERSION));
  h += "</span></div>"
       "<div class='fw-row'><span class='k'>Channel</span><span class='v' id='fwch'>&hellip;</span></div>"
       "<div class='fw-row'><span class='k'>Released</span><span class='v' id='fwrel'>&hellip;</span></div>"
       "<div class='fw-row'><span class='k'>Installed on</span><span class='v' id='fwsince' data-t='";
  h += String(ota_installed_at());
  h += "'>&hellip;</span></div>"
       "<div class='fw-row'><span class='k'>Latest on GitHub</span><span class='v mono";
  if (updateAvail) h += " new";
  h += "' id='ghlat'>";
  h += latestVer.length() ? "v" + esc(latestVer) : String("&hellip;");
  h += "</span></div>"
       "<div class='fw-row'><span class='k'>Latest released</span><span class='v' id='ghrel'>&hellip;</span></div>"
       "</div><div class='fw-notes-btns'>"
       "<button type='button' class='btn btn-quiet' id='fwnb' disabled>Release notes</button>"
       "<button type='button' class='btn btn-quiet' id='ghnb' disabled>What's new</button></div>"
       "<pre class='notes' id='fwn'></pre><pre class='notes' id='ghn'></pre>"
       "<p class='field-hint' style='margin:0 0 10px'>Release details are read from GitHub by this browser.</p>";
  h += "</div><div class='ota-actions'>"
       "<button class='btn btn-primary btn-block' type='submit'>"
       "Check &amp; Install Latest Release from GitHub</button>"
       "<a class='btn btn-secondary btn-block' href='/update'>"
       "Upload Local .bin Firmware Over Wi-Fi</a></div></form>";

  h += "</div>";  // end top .grid-2

  // 4. Main Configuration Form (/save)
  h += "<form method='POST' action='/save'><div class='grid-2'>";

  // Section 1: FilaMan API Integration
  h += "<div class='card'><div><div class='card-head'><div>"
       "<span class='card-eyebrow'>Primary Data Source</span>"
       "<h2 class='card-title'>FilaMan Integration</h2>"
       "<p class='card-desc'>Direct HTTP/HTTPS polling against your FilaMan spool inventory server.</p>"
       "</div><span class='card-badge";
  if (display_poll_active()) h += " live";
  h += "'>";
  h += display_poll_active() ? "Active" : "Standby";
  h += "</span></div>";
  h += field("FilaMan Base URL", "fmUrl", c.filamanBaseUrl, "text",
             "Example: http://192.168.1.50:8000 (leave blank to use MQTT only)",
             "http://192.168.1.50:8000");
  h += pwField("FilaMan API Key", "fmKey", "Bearer token for the FilaMan Display API");
  h += toggleField("Allow Self-Signed LAN TLS",
                   "Skip certificate verification when connecting to FilaMan over local HTTPS",
                   "fmTls", c.filamanTlsInsecure);
  h += "</div></div>";

  // Section 2: Wi-Fi & Network Identity
  h += "<div class='card'><div><div class='card-head'><div>"
       "<span class='card-eyebrow'>Connectivity</span>"
       "<h2 class='card-title'>Wi-Fi &amp; Network Identity</h2>"
       "<p class='card-desc'>2.4 GHz wireless credentials, OTA hostname, and local clock timezone.</p>"
       "</div><span class='card-badge'>";
  h += esc(c.wifiSsid.length() ? c.wifiSsid : String("No SSID"));
  h += "</span></div>";
  h += field("Wi-Fi SSID (2.4 GHz)", "wifiSsid", c.wifiSsid, "text");
  h += pwField("Wi-Fi Password", "wifiPass");
  {
    String hint = "The name your router shows, and the wireless update target. "
                  "Clear it to restore the default.";
    if (WiFi.status() == WL_CONNECTED) {
      hint += "<br>Reachable at: ";
      if (c.mdnsEnabled) {
        hint += "http://" + esc(c.otaHostname) + ".local/ &middot; ";
      }
      hint += "http://" + WiFi.localIP().toString() + "/";
    }
    h += field("Device Hostname", "otaHost", c.otaHostname, "text", hint.c_str());
  }
  h += toggleField("Answer to .local on the local network (mDNS)",
                   "Finds the panel by name with no DNS server involved. Turn off if your "
                   "network should carry no .local traffic; wireless updates then use the IP.",
                   "mdns", c.mdnsEnabled);
  h += timezoneField(c.displayTimezone);
  h += "</div></div>";

  // Section 3: Display, Spool Thresholds & Auto-Update
  h += "<div class='card'><div><div class='card-head'><div>"
       "<span class='card-eyebrow'>Panel UX</span>"
       "<h2 class='card-title'>Display, Alerts &amp; Updates</h2>"
       "<p class='card-desc'>Backlight dimming, idle navigation timeout, and spool remaining alerts.</p>"
       "</div><span class='card-badge'>800x480 RGB</span></div>";
  h += "<div class='row-2'>";
  h += field("Normal Brightness %", "brtFull", String(c.displayFullBrightPct), "number",
             "Recommended: 50 / 75 / 100");
  h += field("Dimmed Brightness %", "brtDim", String(c.displayDimBrightPct), "number",
             "Recommended: 10 / 20 / 35");
  h += "</div><div class='row-2'>";
  h += field("Low Spool Warning %", "spLow", String(c.spoolLowPct), "number",
             "Amber threshold (e.g. 20 / 30 / 40)");
  h += field("Critical Spool Alert %", "spCrit", String(c.spoolCritPct), "number",
             "Red alert threshold (e.g. 10 / 15 / 20)");
  h += "</div>";
  h += field("Auto-Return to AMS View (sec)", "idleSec", String(c.idleReturnSec), "number",
             "Seconds of inactivity before returning to AMS View (0 = disabled, 60, 180)");
  h += toggleField("Automatically Check for Firmware Updates",
                   "Check GitHub Releases hourly and show an update badge on the display menu",
                   "autoOta", c.autoCheckUpdates);
  h += "</div></div>";

  // Section 4: MQTT Broker & Security
  h += "<div class='card'><div><div class='card-head'><div>"
       "<span class='card-eyebrow'>Fallback &amp; Auth</span>"
       "<h2 class='card-title'>MQTT Broker &amp; Security</h2>"
       "<p class='card-desc'>Optional MQTT telemetry broker and HTTP/OTA administrator password.</p>"
       "</div><span class='card-badge'>";
  h += c.otaPassword.length() ? "Auth Locked" : "Unlocked";
  h += "</span></div>";
  h += "<div class='row-2'>";
  h += field("MQTT Broker Host", "mqttHost", c.mqttHost, "text", nullptr, "192.168.1.10");
  h += field("MQTT Port", "mqttPort", String(c.mqttPort), "number");
  h += "</div><div class='row-2'>";
  h += field("MQTT Username", "mqttUser", c.mqttUsername, "text");
  h += pwField("MQTT Password", "mqttPass");
  h += "</div>";
  h += field("MQTT Topic Prefix", "topicPfx", c.topicPrefix, "text",
             "Base topic subscribed by the panel (e.g. filaman/display)");
  h += pwField("Admin / OTA Password", "otaPass",
               "Protects this web console (user: admin) and wireless OTA flashing");
  h += "</div></div>";

  h += "</div>";  // end form .grid-2

  // 5. Sticky / Prominent Save & Reboot Action Bar
  h += "<div class='save-bar'><div class='save-copy'>"
       "<strong>Apply Configuration</strong> &mdash; Saving writes settings to NVS flash and reboots the display."
       "</div><button class='btn btn-primary' type='submit' style='min-width:210px'>"
       "Save Configuration &amp; Reboot</button></div></form>";

  h += footerLinks();
  h += "<script>const REPO='" PROJECT_GITHUB_REPO "',INST='";
  h += FIRMWARE_VERSION;
  h += "';";
  h += R"JS(
const $=id=>document.getElementById(id);
function day(s){return s?new Date(s).toLocaleDateString():'-';}
function cmp(a,b){a=a.replace(/^v/,'').split(/[.-]/);b=b.replace(/^v/,'').split(/[.-]/);
for(let i=0;i<Math.max(a.length,b.length);i++){const x=parseInt(a[i]||0),y=parseInt(b[i]||0);
if(x!==y)return x-y;}return 0;}
function gh(path){return fetch('https://api.github.com/repos/'+REPO+path,
{headers:{Accept:'application/vnd.github+json'}}).then(r=>r.ok?r.json():Promise.reject(r.status));}
function notes(btn,pre,shown,text){const b=$(btn),p=$(pre);b.disabled=!text;
b.onclick=()=>{const open=p.style.display==='block';p.textContent=text;
p.style.display=open?'none':'block';b.textContent=open?shown:'Hide';};}
(function(){const e=$('fwsince'),t=parseInt(e.dataset.t||'0');
e.textContent=t?new Date(t*1000).toLocaleString():'Waiting for network time';})();
gh('/releases/tags/v'+INST).then(d=>{$('fwch').textContent=d.prerelease?'Pre-release':'Release';
$('fwrel').textContent=day(d.published_at);notes('fwnb','fwn','Release notes',d.body||'');})
.catch(s=>{$('fwch').textContent=s===404?'Local build':'Unknown';$('fwrel').textContent='-';});
gh('/releases/latest').then(d=>{const l=$('ghlat');l.textContent=d.tag_name;
const newer=cmp(d.tag_name,INST)>0;l.classList.toggle('new',newer);
$('ghrel').textContent=day(d.published_at);
if(newer)notes('ghnb','ghn',"What's new",d.body||'');})
.catch(()=>{$('ghrel').textContent='-';});
</script></div></body></html>)JS";
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
  c.mdnsEnabled = server.hasArg("mdns");
  if (server.hasArg("tzPreset")) {
    // Index into kTimezones; empty means "Keep current setting".
    const String preset = server.arg("tzPreset");
    const int i = preset.toInt();
    if (preset.length() && i >= 0 && i < kTimezoneCount) {
      c.displayTimezone = kTimezones[i].posix;
    }
  } else if (server.hasArg("tz") && server.arg("tz").length()) {
    // Backward compatibility for older forms and scripted configuration.
    c.displayTimezone = server.arg("tz");
  }
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
  c.autoCheckUpdates = server.hasArg("autoOta");
  c.otaPassword = keepIfBlank("otaPass", c.otaPassword);
  c.save();

  sendStatusPage(200, "Configuration Saved", "Rebooting Display...",
                 "Your settings have been written to non-volatile storage. The panel will reconnect in a few seconds.",
                 6, false);
  delay(600);
  ESP.restart();
}

void handlePrinter() {
  if (!authOK()) {
    return;
  }
  if (server.hasArg("pid") && server.arg("pid").length()) {
    printer_list_publish_selection(server.arg("pid"));
    sendStatusPage(200, "Printer Updated", "Switching Monitored Printer",
                   "The display is binding to the selected printer feed and will refresh momentarily.",
                   4, false);
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
    sendStatusPage(500, "Update Error", "GitHub Update Check Failed",
                   "Could not query the latest release from GitHub: " + esc(err), 6, true);
    return;
  }
  const int cmp = ota_compare_versions(ver, String(FIRMWARE_VERSION));
  device_logf("GitHub check: remote=v%s local=v%s cmp=%d", ver.c_str(), FIRMWARE_VERSION, cmp);
  // Only install when GitHub is strictly newer. Equal/older is success, not an error.
  if (cmp <= 0) {
    sendStatusPage(200, "Up to Date", "Already on v" + esc(String(FIRMWARE_VERSION)),
                   "GitHub reports latest v" + esc(ver) +
                       ". No install needed — your board is current.",
                   5, false);
    return;
  }
  device_logf("GitHub install: %s", url.c_str());
  if (!ota_install_github_update(url, nullptr, err)) {
    sendStatusPage(500, "OTA Failed", "Firmware Installation Failed",
                   "Error while flashing firmware from GitHub: " + esc(err), 6, true);
    return;
  }
  sendStatusPage(200, "Update Installed", "Flashed Firmware v" + esc(ver),
                 "The new firmware image was written to the OTA partition. Rebooting device now...",
                 8, false);
  delay(700);
  ESP.restart();
}

void handleDiagnostics() {
  if (!authOK()) {
    return;
  }
  const uint32_t uptimeSec = millis() / 1000UL;
  const uint32_t hours = uptimeSec / 3600UL;
  const uint32_t minutes = (uptimeSec % 3600UL) / 60UL;
  const String ip =
      WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("Offline");

  String h;
  h.reserve(8500);
  h +=
      "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Diagnostics - Digital Swatch System</title><style>"
      ":root{--bg:#070a0f;--card:#111823;--card2:#151f2d;--border:#222f42;"
      "--soft:#1a2433;--text:#eef2f7;--muted:#8a99ad;--dim:#59687b;"
      "--accent:#3ddc97;--danger:#ff5c6a;--mono:'JetBrains Mono',ui-monospace,"
      "SFMono-Regular,Menlo,Consolas,monospace}"
      "*{box-sizing:border-box}body{margin:0;background:radial-gradient(ellipse 90% 45% at 50% -10%,"
      "rgba(61,220,151,.10),transparent 70%),var(--bg);color:var(--text);"
      "font-family:'Inter',-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;"
      "font-size:14px;-webkit-font-smoothing:antialiased}"
      ".shell{max-width:1020px;margin:auto;padding:24px 18px 56px}"
      ".top{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:16px;"
      "padding:18px 22px;background:rgba(17,24,35,.9);border:1px solid var(--border);"
      "border-radius:14px;margin-bottom:18px;box-shadow:0 14px 34px rgba(0,0,0,.35)}"
      ".brand{display:flex;align-items:center;gap:13px}.mark{display:grid;"
      "grid-template-columns:repeat(4,6px);gap:3px;padding:7px;background:#0a0f16;"
      "border:1px solid var(--border);border-radius:10px;height:40px}.mark i{border-radius:2px}"
      ".mark i:nth-child(1){background:#3ddc97}.mark i:nth-child(2){background:#5aa7ff}"
      ".mark i:nth-child(3){background:#ffb443}.mark i:nth-child(4){background:#ff5c6a}"
      "h1{font-size:18px;margin:0;letter-spacing:-.02em}.sub{color:var(--muted);font-size:12px;margin-top:2px}"
      ".live{display:flex;align-items:center;gap:7px;font:12px var(--mono);color:var(--muted);"
      "padding:5px 11px;background:#0d131c;border:1px solid var(--border);border-radius:99px}"
      ".dot{width:7px;height:7px;border-radius:50%;background:var(--accent);box-shadow:0 0 8px var(--accent)}"
      ".tabs{display:flex;gap:6px;margin:-4px 0 18px;padding:5px;background:#0b1017;"
      "border:1px solid var(--soft);border-radius:10px;width:max-content}"
      ".tab{padding:7px 13px;border-radius:6px;color:var(--muted);font-size:12px;font-weight:650;"
      "text-decoration:none}.tab.active{color:#07120d;background:var(--accent)}"
      ".metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-bottom:16px}"
      "@media(min-width:700px){.metrics{grid-template-columns:repeat(4,minmax(0,1fr))}}"
      ".metric,.card{background:var(--card);border:1px solid var(--border);border-radius:12px;"
      "box-shadow:inset 0 1px 0 rgba(255,255,255,.03),0 8px 24px rgba(0,0,0,.22)}"
      ".metric{padding:14px 16px}.metric span{display:block;color:var(--dim);font-size:10px;"
      "font-weight:700;letter-spacing:.09em;text-transform:uppercase;margin-bottom:6px}"
      ".metric strong{font:600 16px var(--mono);color:var(--text)}"
      ".grid{display:grid;grid-template-columns:1fr;gap:16px}"
      "@media(min-width:760px){.grid{grid-template-columns:minmax(0,1.65fr) minmax(260px,.7fr)}}"
      ".card{padding:20px 22px}.head{display:flex;align-items:flex-start;justify-content:space-between;"
      "gap:12px;padding-bottom:12px;margin-bottom:14px;border-bottom:1px solid var(--soft)}"
      ".eyebrow{display:block;color:var(--accent);font-size:10px;font-weight:700;"
      "letter-spacing:.1em;text-transform:uppercase;margin-bottom:3px}"
      "h2{font-size:16px;margin:0}.desc{color:var(--muted);font-size:12px;margin:4px 0 0;line-height:1.5}"
      ".actions{display:flex;flex-wrap:wrap;gap:9px}.btn{display:inline-flex;justify-content:center;"
      "align-items:center;padding:9px 14px;border-radius:8px;border:1px solid var(--border);"
      "background:var(--card2);color:var(--text);font:650 12px inherit;text-decoration:none;cursor:pointer}"
      ".btn.primary{background:var(--accent);border-color:var(--accent);color:#07120d}"
      ".btn.danger{color:#ff9ca5;border-color:rgba(255,92,106,.4)}"
      "pre{height:390px;margin:0;padding:14px;overflow:auto;background:#070b10;"
      "border:1px solid var(--soft);border-radius:8px;color:#b9c8d9;font:11px/1.55 var(--mono);"
      "white-space:pre-wrap;overflow-wrap:anywhere}"
      ".shot{aspect-ratio:5/3;border:1px solid var(--soft);border-radius:9px;margin-bottom:13px;"
      "background:linear-gradient(135deg,#0c131c,#152335);display:flex;align-items:center;"
      "justify-content:center;color:var(--dim);font:12px var(--mono);text-align:center;padding:20px}"
      ".note{color:var(--muted);font-size:11px;line-height:1.5;margin:10px 0 0}"
      "</style></head><body><main class='shell'>"
      "<header class='top'><div class='brand'><div class='mark'><i></i><i></i><i></i><i></i></div>"
      "<div><h1>System Diagnostics</h1><div class='sub'>Digital Swatch System hardware console</div>"
      "</div></div><div class='live'><span class='dot'></span>LIVE &nbsp; v";
  h += esc(String(FIRMWARE_VERSION));
  h += "</div></header><nav class='tabs' aria-label='Console sections'>"
       "<a class='tab' href='/'>Configuration</a>"
       "<a class='tab active' href='/diagnostics'>Diagnostics</a></nav>"
       "<section class='metrics'><div class='metric'><span>Uptime</span><strong>";
  h += String(hours) + "h " + String(minutes) + "m";
  h += "</strong></div><div class='metric'><span>Free Heap</span><strong>";
  h += String(static_cast<uint32_t>(ESP.getFreeHeap()) / 1024U) + " KB";
  h += "</strong></div><div class='metric'><span>Free PSRAM</span><strong>";
  h += String(static_cast<uint32_t>(ESP.getFreePsram()) / 1024U) + " KB";
  h += "</strong></div><div class='metric'><span>Wi-Fi RSSI</span><strong>";
  h += WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : String("--");
  h += "</strong></div><div class='metric'><span>IP Address</span><strong>";
  h += esc(ip);
  h += "</strong></div><div class='metric'><span>Data Source</span><strong>";
  h += esc(display_poll_active() ? display_poll_status() : String("MQTT"));
  h += "</strong></div><div class='metric'><span>Min Free Heap</span><strong>";
  h += String(static_cast<uint32_t>(ESP.getMinFreeHeap()) / 1024U) + " KB";
  h += "</strong></div><div class='metric'><span>Update Status</span><strong>";
  h += ota_update_available() ? String("Available") : String("Current");
  h += "</strong></div></section>"
       "<div class='grid'><section class='card'><div class='head'><div>"
       "<span class='eyebrow'>Recent Events</span><h2>Firmware Log</h2>"
       "<p class='desc'>Connection, API polling, update, and recovery events from this boot.</p>"
       "</div><div class='actions'><button class='btn' id='pause'>Pause</button>"
       "<button class='btn danger' id='clear'>Clear</button></div></div>"
       "<pre id='log'>Loading log...</pre></section>"
       "<aside class='card'><div class='head'><div><span class='eyebrow'>Panel Output</span>"
       "<h2>Display Screenshot</h2><p class='desc'>Capture the current 800 x 480 framebuffer.</p>"
       "</div></div><div class='shot'>RGB565 framebuffer<br>800 x 480</div>"
       "<a class='btn primary' style='width:100%' href='/screenshot.png' download='filaman-display.png'>"
       "Download Current Screenshot</a>"
       "<p class='note'>The image reflects the exact screen currently visible on the panel. "
       "The download may take a few seconds while the framebuffer is encoded as PNG.</p></aside></div>"
       "<script>"
       "let paused=false;const log=document.getElementById('log'),pause=document.getElementById('pause');"
       "async function load(){if(paused)return;try{const r=await fetch('/diagnostics/logs',{cache:'no-store'});"
       "if(r.ok){const atBottom=log.scrollTop+log.clientHeight>=log.scrollHeight-24;"
       "log.textContent=await r.text();if(atBottom)log.scrollTop=log.scrollHeight;}}catch(e){}}"
       "pause.onclick=()=>{paused=!paused;pause.textContent=paused?'Resume':'Pause';};"
       "document.getElementById('clear').onclick=async()=>{await fetch('/diagnostics/logs',{method:'DELETE'});"
       "paused=false;pause.textContent='Pause';load();};load();setInterval(load,2500);"
       "</script>";
  h += footerLinks();
  h += "</main></body></html>";
  server.send(200, "text/html", h);
}

void handleDiagnosticLogs() {
  if (!authOK()) {
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  if (server.method() == HTTP_DELETE) {
    device_log_clear();
    server.send(200, "text/plain", "Diagnostics log cleared\n");
    return;
  }
  server.send(200, "text/plain", device_log_snapshot());
}

void putBe32(uint8_t *p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value >> 24);
  p[1] = static_cast<uint8_t>(value >> 16);
  p[2] = static_cast<uint8_t>(value >> 8);
  p[3] = static_cast<uint8_t>(value);
}

uint32_t pngCrc(const uint8_t *type4, const uint8_t *data, size_t len) {
  uint32_t crc = mz_crc32(MZ_CRC32_INIT, type4, 4);
  if (data && len) {
    crc = mz_crc32(crc, data, len);
  }
  return crc;
}

struct PngBuf {
  uint8_t *data = nullptr;
  size_t len = 0;
  size_t cap = 0;
  bool ok = true;
};

bool pngAppend(PngBuf &b, const void *src, size_t n) {
  if (!b.ok || n == 0) {
    return b.ok;
  }
  if (b.len + n > b.cap) {
    size_t ncap = b.cap ? b.cap * 2 : 128 * 1024;
    while (ncap < b.len + n) {
      ncap *= 2;
    }
    auto *nd = static_cast<uint8_t *>(
        heap_caps_realloc(b.data, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!nd) {
      b.ok = false;
      return false;
    }
    b.data = nd;
    b.cap = ncap;
  }
  memcpy(b.data + b.len, src, n);
  b.len += n;
  return true;
}

bool pngAppendChunk(PngBuf &b, const char type[4], const uint8_t *payload, size_t payloadLen) {
  uint8_t hdr[8];
  putBe32(hdr, static_cast<uint32_t>(payloadLen));
  hdr[4] = static_cast<uint8_t>(type[0]);
  hdr[5] = static_cast<uint8_t>(type[1]);
  hdr[6] = static_cast<uint8_t>(type[2]);
  hdr[7] = static_cast<uint8_t>(type[3]);
  if (!pngAppend(b, hdr, sizeof(hdr))) {
    return false;
  }
  if (payloadLen && !pngAppend(b, payload, payloadLen)) {
    return false;
  }
  uint8_t crcBuf[4];
  putBe32(crcBuf, pngCrc(hdr + 4, payload, payloadLen));
  return pngAppend(b, crcBuf, 4);
}

mz_bool pngIdatPut(const void *buf, int len, void *user) {
  auto *out = static_cast<PngBuf *>(user);
  if (!out || !out->ok || len <= 0) {
    return MZ_TRUE;
  }
  const bool ok =
      pngAppendChunk(*out, "IDAT", static_cast<const uint8_t *>(buf), static_cast<size_t>(len));
  yield();
  return ok ? MZ_TRUE : MZ_FALSE;
}

bool writeAll(WiFiClient &client, const uint8_t *data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    if (!client.connected()) {
      return false;
    }
    const size_t n = client.write(data + sent, len - sent);
    if (n == 0) {
      delay(1);
      yield();
      continue;
    }
    sent += n;
    yield();
  }
  return true;
}

void handleScreenshot() {
  if (!authOK()) {
    return;
  }
  const uint16_t *framebuffer = display_framebuffer_rgb565();
  if (!framebuffer) {
    server.send(503, "text/plain", "Display framebuffer unavailable");
    return;
  }

  constexpr size_t kRowBytes = 1 + SCREEN_WIDTH * 3;  // filter byte + RGB
  auto *comp = static_cast<tdefl_compressor *>(
      heap_caps_malloc(sizeof(tdefl_compressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto *row = static_cast<uint8_t *>(
      heap_caps_malloc(kRowBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!comp || !row) {
    heap_caps_free(comp);
    heap_caps_free(row);
    server.send(507, "text/plain", "Not enough memory for PNG encode");
    return;
  }

  device_log("Web screenshot capture started");

  PngBuf png;
  static const uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  bool ok = pngAppend(png, kSig, sizeof(kSig));

  uint8_t ihdr[13] = {};
  putBe32(ihdr + 0, SCREEN_WIDTH);
  putBe32(ihdr + 4, SCREEN_HEIGHT);
  ihdr[8] = 8;   // bit depth
  ihdr[9] = 2;   // RGB
  ihdr[10] = 0;  // compression
  ihdr[11] = 0;  // filter
  ihdr[12] = 0;  // interlace
  ok = ok && pngAppendChunk(png, "IHDR", ihdr, sizeof(ihdr));

  // Huffman-only keeps the compressor small/fast and avoids deep LZ probes.
  const int flags = TDEFL_WRITE_ZLIB_HEADER | TDEFL_HUFFMAN_ONLY;
  tdefl_status st = tdefl_init(comp, pngIdatPut, &png, flags);
  ok = ok && (st == TDEFL_STATUS_OKAY);
  if (!ok) {
    device_logf("PNG encode init failed (tdefl=%d)", static_cast<int>(st));
  }

  for (int y = 0; ok && png.ok && y < SCREEN_HEIGHT; ++y) {
    row[0] = 0;  // filter None
    const uint16_t *src = framebuffer + static_cast<size_t>(y) * SCREEN_WIDTH;
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
      const uint16_t pixel = src[x];
      row[1 + x * 3 + 0] = static_cast<uint8_t>(((pixel >> 11) & 0x1F) * 255 / 31);
      row[1 + x * 3 + 1] = static_cast<uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63);
      row[1 + x * 3 + 2] = static_cast<uint8_t>((pixel & 0x1F) * 255 / 31);
    }
    st = tdefl_compress_buffer(comp, row, kRowBytes, TDEFL_NO_FLUSH);
    ok = st >= TDEFL_STATUS_OKAY;
    if ((y & 0x0F) == 0) {
      yield();
    }
  }
  if (ok && png.ok) {
    st = tdefl_compress_buffer(comp, nullptr, 0, TDEFL_FINISH);
    ok = st == TDEFL_STATUS_DONE;
  }
  ok = ok && png.ok && pngAppendChunk(png, "IEND", nullptr, 0);

  heap_caps_free(comp);
  heap_caps_free(row);

  if (!ok || !png.data || png.len < 100) {
    device_logf("Web screenshot capture failed (ok=%d len=%u)", ok ? 1 : 0,
                static_cast<unsigned>(png.len));
    heap_caps_free(png.data);
    if (!server.client().connected()) {
      return;
    }
    server.send(500, "text/plain", "PNG encode failed");
    return;
  }

  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "attachment; filename=filaman-display.png");
  server.setContentLength(png.len);
  server.send(200, "image/png", "");
  const bool sent = writeAll(server.client(), png.data, png.len);
  heap_caps_free(png.data);
  device_log(sent ? "Web screenshot capture completed"
                  : "Web screenshot capture send failed");
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
  server.on("/diagnostics", HTTP_GET, handleDiagnostics);
  server.on("/diagnostics/logs", HTTP_GET, handleDiagnosticLogs);
  server.on("/diagnostics/logs", HTTP_DELETE, handleDiagnosticLogs);
  server.on("/screenshot.png", HTTP_GET, handleScreenshot);
  server.on("/screenshot.bmp", HTTP_GET, handleScreenshot);  // legacy alias
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
