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
#include "services/mdns_service.h"
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

// Preset buttons matching the panel's Display & Alerts pane. AppConfig snaps
// stored values to these presets on load, so one option is always checked.
String choiceField(const char *label, const char *hint, const char *name, const int *values,
                   const char *const *labels, int count, int current) {
  (void)hint;
  String h = "<div class='set-row'><span class='field-label'>";
  h += label;
  h += "</span><div class='seg' role='radiogroup'>";
  for (int i = 0; i < count; ++i) {
    h += "<label><input type='radio' name='";
    h += name;
    h += "' value='";
    h += values[i];
    h += "'";
    if (values[i] == current) {
      h += " checked";
    }
    h += "><span>";
    h += labels[i];
    h += "</span></label>";
  }
  h += "</div></div>";
  return h;
}

String timezoneField(const String &current) {
  const int sel = timezone_index(current);
  String h = "<div class='set-row'><label class='field-label' for='f_tzPreset'>Time zone</label>"
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
  h += "</select></div>";
  return h;
}

// formId ties a switch to a form it does not sit inside. The firmware checks
// live beside the version, while the rest of the settings post from /save.
String toggleField(const char *title, const char *subtitle, const char *name, bool checked,
                   const char *formId = nullptr) {
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
  if (formId && formId[0]) {
    h += " form='";
    h += formId;
    h += "'";
  }
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
      "<style>.links{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;"
      "margin-top:20px}"
      ".links a{display:flex;align-items:center;justify-content:center;gap:8px;"
      "padding:12px 8px;border-radius:8px;text-decoration:none;font-size:14px;font-weight:500;"
      "background:#121820;color:#f2f5f8;border:1px solid #2a3544}"
      ".links a:hover{border-color:#3ddc97;color:#3ddc97}"
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
       ":root{--bg:#0a0e14;--card:#121820;--border:#2a3544;--text:#f2f5f8;--muted:#8b98a8;"
       "--accent:#3ddc97;--danger:#ff5c6a}"
       "*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;align-items:center;"
       "justify-content:center;padding:24px;font-family:system-ui,sans-serif;"
       "background:var(--bg);color:var(--text)}"
       ".box{max-width:440px;width:100%;background:var(--card);border:1px solid ";
  h += isError ? "rgba(255,92,106,.45)" : "rgba(61,220,151,.38)";
  h += ";border-radius:8px;padding:28px 26px;text-align:left}"
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
  const uint32_t uptimeSec = millis() / 1000UL;
  const uint32_t upHours = uptimeSec / 3600UL;
  const uint32_t upMinutes = (uptimeSec % 3600UL) / 60UL;

  String h;
  h.reserve(48000);
  h += "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
       "<meta name='viewport' content='width=device-width,initial-scale=1'>"
       "<title>Digital Swatch System</title><style>";
  h += R"CSS(
:root{--bg:#0a0e14;--card:#121820;--card-2:#182028;--inset:#0d1219;--border:#2a3544;--border-soft:#1e2834;--text:#f2f5f8;--muted:#8b98a8;--dim:#5c6b7a;--accent:#3ddc97;--warn:#ffb020;--danger:#ff5c6a;--radius:8px}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,sans-serif;font-size:15px;line-height:1.45}
.shell{max-width:920px;margin:0 auto;padding:20px 16px 88px}
.topbar{display:flex;flex-wrap:wrap;align-items:flex-end;justify-content:space-between;gap:16px;padding:0 0 16px;margin:0 0 0;border:0;border-bottom:1px solid var(--border);border-radius:0;background:none;box-shadow:none}
.brand{display:flex;align-items:center;gap:14px}
.swatch-mark{display:flex;flex-direction:column;width:10px;height:40px;gap:2px;padding:0;background:none;border:0;border-radius:0}
.swatch-mark span{display:block;flex:1;border-radius:1px}
.sm-1{background:#6b7c32}.sm-2{background:#f3ef6a}.sm-3{background:#f07a1a}.sm-4{background:#9aa0a6}
.brand-title{margin:0;font-size:22px;font-weight:500;letter-spacing:0}
.brand-sub{margin:2px 0 0;font-size:13px;color:var(--muted)}
.telemetry{display:flex;flex-wrap:wrap;gap:14px;align-items:center}
.chip{display:inline-flex;align-items:center;gap:6px;padding:0;background:none;border:0;border-radius:0;font:13px system-ui,sans-serif;color:var(--muted)}
.chip strong{color:var(--text);font-weight:500}
.dot{width:8px;height:8px;border-radius:50%;background:var(--accent);box-shadow:none}
.dot-warn{background:var(--warn)}
.tabs{display:flex;flex-wrap:wrap;gap:0;margin:16px 0 22px;padding:0;background:none;border:0;border-bottom:1px solid var(--border);border-radius:0;width:100%;max-width:none}
.tab{padding:10px 14px 12px;margin-bottom:-1px;border-bottom:2px solid transparent;border-radius:0;color:var(--muted);font-size:14px;font-weight:500;text-decoration:none}
.tab:hover{color:var(--text);background:none}
.tab.active{color:var(--text);background:none;border-bottom-color:var(--accent)}
.tabs-on .panel{display:none}
.tabs-on #panel-status{display:block}
.tabs-on .panel.on{display:block}
.tabs-on.hide-status #panel-status{display:none}
.tabs-on #saveBar{display:none}
.tabs-on.save-on #saveBar{display:flex}
.panel-head{display:flex;align-items:center;justify-content:space-between;gap:16px;margin:0 0 14px}
.panel-head .band-lead{margin:0;min-width:0}
.band-lead,.card-desc,.group-lead,.field-hint,.toggle-sub,.note,.save-copy{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.band-lead{margin:0 0 14px;color:var(--muted);font-size:14px}
.metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:0;margin:0 0 16px;border-top:1px solid var(--border);border-bottom:1px solid var(--border)}
@media(min-width:760px){.metrics{grid-template-columns:repeat(4,minmax(0,1fr))}}
.metric{background:none;border:0;border-radius:0;box-shadow:none;padding:12px 12px 12px 0}
.metric span{display:block;color:var(--dim);font-size:12px;font-weight:500;letter-spacing:0;text-transform:none;margin-bottom:2px}
.metric strong{font:500 18px system-ui,sans-serif;font-variant-numeric:tabular-nums;color:var(--text)}
.card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:16px;box-shadow:none;margin-bottom:16px}
.card-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:12px;padding-bottom:0;border-bottom:0}
.card-title{margin:0;font-size:16px;font-weight:500}
.card-desc{margin:4px 0 0;font-size:13px;color:var(--muted)}
.card-badge{font-size:12px;font-weight:500;padding:2px 8px;border-radius:4px;background:var(--inset);border:1px solid var(--border);color:var(--muted);white-space:nowrap}
.card-badge.live{background:#13261d;border-color:var(--accent);color:var(--accent)}
.card-eyebrow{display:none}
.thread{box-shadow:none}
.grid-2{display:grid;grid-template-columns:1fr;gap:16px}
@media(min-width:760px){.grid-2{grid-template-columns:1fr 1fr}}
.row-2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.field{margin-bottom:12px}
.field:last-child{margin-bottom:0}
.field-label{display:block;font-size:13px;font-weight:500;color:var(--text);margin-bottom:6px}
.field-hint{display:block;font-size:12px;color:var(--dim);margin-top:4px}
input[type=text],input[type=password],input[type=number],select{width:100%;padding:10px 12px;background:var(--inset);color:var(--text);border:1px solid var(--border);border-radius:4px;font:14px system-ui,sans-serif;outline:none}
input[type=number]{font-variant-numeric:tabular-nums}
input:focus,select:focus{border-color:var(--accent);box-shadow:none}
input::placeholder{color:var(--dim)}
.host-line{display:flex;align-items:stretch}
.host-line input{border-top-right-radius:0;border-bottom-right-radius:0;width:auto;flex:1;min-width:0}
.host-suffix{display:flex;align-items:center;padding:0 12px;border:1px solid var(--border);border-left:0;border-radius:0 4px 4px 0;background:var(--inset);color:var(--muted);font-size:14px}
.host-line.solo input{border-radius:4px}
.host-line.solo .host-suffix{display:none}
.host-line:focus-within input,.host-line:focus-within .host-suffix{border-color:var(--accent)}
.toggle-row{display:flex;align-items:center;justify-content:space-between;gap:14px;padding:10px 0;margin-top:4px;background:none;border:0;border-radius:0;cursor:pointer}
.toggle-copy{display:flex;flex-direction:column;gap:2px}
.toggle-title{font-size:14px;font-weight:500}
.toggle-sub{font-size:12px;color:var(--muted)}
.toggle-input{position:absolute;opacity:0;width:0;height:0}
.toggle-track{position:relative;flex:none;width:40px;height:22px;background:#1c2635;border:1px solid var(--border);border-radius:999px}
.toggle-thumb{position:absolute;top:2px;left:2px;width:16px;height:16px;border-radius:50%;background:var(--muted)}
.toggle-input:checked+.toggle-track{background:#13261d;border-color:var(--accent)}
.toggle-input:checked+.toggle-track .toggle-thumb{transform:translateX(18px);background:var(--accent)}
.printer-list{display:flex;flex-direction:column;gap:8px}
.printer-item{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:12px;background:var(--inset);border:1px solid var(--border);border-radius:8px;cursor:pointer}
.printer-item.active{border-color:var(--accent);background:#13261d}
.printer-left{display:flex;align-items:center;gap:10px;min-width:0}
.printer-left input{accent-color:var(--accent);width:16px;height:16px;margin:0}
.printer-name{font-weight:500;font-size:15px}
.printer-model{font-size:12px;color:var(--muted)}
.empty-note{padding:16px;border:1px dashed var(--border);border-radius:8px;color:var(--muted);text-align:center}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;padding:10px 16px;border-radius:8px;font:500 14px system-ui,sans-serif;cursor:pointer;border:1px solid transparent;text-decoration:none}
.btn-primary{background:var(--accent);color:#0a0e14}
.btn-primary:hover{filter:brightness(1.05)}
.btn-secondary{background:var(--card-2);color:var(--text);border-color:var(--border)}
.btn-secondary:hover{border-color:var(--muted)}
.btn-danger{color:var(--danger);border-color:var(--danger);background:transparent}
.btn-quiet{background:transparent;color:var(--muted);border-color:var(--border);padding:8px 12px;font-size:13px}
.btn-quiet:disabled{opacity:.4;cursor:default}
.btn-block{width:100%;margin-top:10px}
.actions{display:flex;gap:8px}
.diag-grid{display:grid;grid-template-columns:1fr;gap:16px}
@media(min-width:760px){.diag-grid{grid-template-columns:minmax(0,1.6fr) minmax(240px,.7fr)}}
#log{height:320px;margin:0;padding:12px;overflow:auto;background:var(--inset);border:1px solid var(--border-soft);border-radius:4px;color:#c5d0dc;font:12px/1.5 ui-monospace,monospace;white-space:pre-wrap}
.shot{display:block;width:100%;height:auto;aspect-ratio:5/3;object-fit:contain;background:#0a0e14;border:1px solid var(--border);border-radius:4px;margin-bottom:8px}
.shot[hidden]{display:none}
.note{color:var(--muted);font-size:12px;margin:8px 0 0}
.banner-warn{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:10px 14px;margin:0 0 16px;border-radius:8px;background:#2a1618;border:1px solid var(--danger);color:#ffd0d4;font-size:14px;white-space:nowrap}
.banner-warn div{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.banner-warn .btn{flex:none}
.banner-warn strong{color:#fff}
.fw-split{display:grid;grid-template-columns:1fr;gap:16px}
.split-2{display:grid;grid-template-columns:1fr;gap:8px}
@media(min-width:760px){.fw-split,.split-2{grid-template-columns:1fr 1fr;align-items:start}}
.fw-rows{margin-bottom:12px}
.fw-row{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-bottom:1px solid var(--border-soft);font-size:14px}
.fw-row .k{color:var(--muted)}
.fw-row .v{text-align:right;font-variant-numeric:tabular-nums}
.fw-row .v.mono{font-weight:500}
.fw-row .v.new{color:var(--accent)}
.fw-side{display:flex;flex-direction:column;gap:8px}
.subhead{margin:16px 0 8px;font-size:13px;font-weight:500;letter-spacing:0;text-transform:none;color:var(--text)}
.group .subhead:first-child,.fw-side>.subhead{margin-top:0}
.group-lead{margin:0 0 10px;font-size:13px;color:var(--muted)}
.install{display:flex;flex-direction:column;gap:8px;margin-top:8px}
.after-split{margin-top:8px}
.set-row{display:flex;align-items:center;gap:16px;padding:8px 0;border-bottom:1px solid var(--border-soft)}
.set-row:last-child{border-bottom:0}
.set-row .field-label{margin:0;width:132px;flex:none;white-space:nowrap}
.set-row input,.set-row select{width:auto;flex:1;min-width:0}
.set-row .host-line{flex:1;min-width:0}
.set-row .toggle-row{flex:1;margin:0;padding:0}
.set-row .seg{flex:1}
.seg{display:grid;grid-auto-flow:column;grid-auto-columns:minmax(0,1fr);gap:6px}
.seg label{position:relative;cursor:pointer}
.seg input{position:absolute;opacity:0;width:0;height:0}
.seg span{display:block;padding:10px 4px;text-align:center;font-size:14px;font-weight:500;font-variant-numeric:tabular-nums;color:var(--text);background:var(--inset);border:1px solid var(--border);border-radius:4px}
.seg input:checked+span{background:var(--accent);border-color:var(--accent);color:#0a0e14}
.seg input:focus-visible+span{outline:2px solid var(--accent);outline-offset:2px}
.seg input:disabled+span{color:var(--dim);border-style:dashed}
.notes{display:none;max-height:280px;overflow:auto;margin:8px 0;padding:12px;background:var(--inset);border:1px solid var(--border-soft);border-radius:4px;color:#c5d0dc;font:12px/1.5 ui-monospace,monospace;white-space:pre-wrap}
.fw-notes-btns{display:flex;gap:8px;margin-bottom:8px}
.save-bar{position:sticky;bottom:12px;display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:12px;padding:12px 14px;background:#121820;border:1px solid var(--border);border-radius:8px}
.save-copy{font-size:13px;color:var(--muted);min-width:0}
.save-copy strong{color:var(--text);font-weight:500}
#f_otaPass{scroll-margin-top:24px}
)CSS";
  h += "</style><script>document.documentElement.classList.add('tabs-on')</script>"
       "</head><body><div class='shell'>";

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
       "<a class='tab active' href='#panel-status'>Status</a>"
       "<a class='tab' href='#panel-firmware'>Firmware</a>"
       "<a class='tab' href='#panel-data'>Data</a>"
       "<a class='tab' href='#panel-network'>Network</a>"
       "<a class='tab' href='#panel-display'>Display</a>"
       "</nav>";

  // 2. Security Warning if no password is set
  if (c.otaPassword.length() == 0) {
    h += "<div class='banner-warn'><div><strong>Unprotected.</strong> "
         "Anyone on this network can change settings or flash firmware.</div>"
         "<a href='#panel-network' class='btn btn-secondary' data-focus='f_otaPass' "
         "style='padding:6px 12px;font-size:12px'>Set Password</a></div>";
  }

  // Firmware is its own band. The two update-check switches post with the
  // settings form (id=cfg) via the form attribute, so the install button
  // does not save them.
  h += "<section id='panel-firmware' class='panel'><div class='card thread'><div class='fw-split'><div>";
  h += "<div class='fw-rows'>"
       "<div class='fw-row'><span class='k'>Installed</span><span class='v mono'>v";
  h += esc(String(FIRMWARE_VERSION));
  h += "</span></div>"
       "<div class='fw-row'><span class='k'>Channel</span><span class='v' id='fwch'>&hellip;</span></div>"
       "<div class='fw-row'><span class='k'>Released</span><span class='v' id='fwrel'>&hellip;</span></div>"
       "<div class='fw-row'><span class='k'>Installed on</span><span class='v' id='fwsince' data-t='";
  h += String(ota_installed_at());
  h += "'>&hellip;</span></div>"
       "<div class='fw-row'><span class='k'>GitHub</span><span class='v mono";
  if (updateAvail) h += " new";
  h += "' id='ghlat'>";
  h += latestVer.length() ? "v" + esc(latestVer) : String("&hellip;");
  h += "</span></div>"
       "<div class='fw-row'><span class='k'>GitHub released</span><span class='v' id='ghrel'>&hellip;</span></div>"
       "</div><div class='fw-notes-btns'>"
       "<button type='button' class='btn btn-quiet' id='fwnb' disabled>Release notes</button>"
       "<button type='button' class='btn btn-quiet' id='ghnb' disabled>What's new</button></div>"
       "<pre class='notes' id='fwn'></pre><pre class='notes' id='ghn'></pre>"
       "</div><div class='fw-side'>";
  h += toggleField("Check automatically", nullptr, "autoOta", c.autoCheckUpdates, "cfg");
  h += toggleField("Include betas", nullptr, "beta", c.betaUpdates, "cfg");
  h += "<form method='POST' action='/github-update'>"
       "<button class='btn btn-primary btn-block' type='submit'>Install latest</button></form>"
       "<a class='btn btn-secondary btn-block' href='/update'>Upload .bin</a>"
       "</div></div></div></section>";

  // Printer selection posts on its own and must not nest inside the save form.
  // Diagnostics lives here too: this is the first tab, so there is no separate page.
  PrinterOption printers[MAX_PRINTER_OPTIONS];
  String activeId;
  const int pn = printer_list_get(printers, MAX_PRINTER_OPTIONS, activeId);
  h += "<section id='panel-status' class='panel'><div class='panel-head'>"
       "<p class='band-lead'>Which printer the swatches follow, and how this panel is running.</p>"
       "</div><section class='metrics'>"
       "<div class='metric'><span>Uptime</span><strong>";
  h += String(upHours) + "h " + String(upMinutes) + "m";
  h += "</strong></div><div class='metric'><span>Free Heap</span><strong>";
  h += String(static_cast<uint32_t>(ESP.getFreeHeap()) / 1024U) + " KB";
  h += "</strong></div><div class='metric'><span>Free PSRAM</span><strong>";
  h += String(static_cast<uint32_t>(ESP.getFreePsram()) / 1024U) + " KB";
  h += "</strong></div><div class='metric'><span>Wi-Fi RSSI</span><strong>";
  h += WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : String("--");
  h += "</strong></div><div class='metric'><span>IP Address</span><strong>";
  h += esc(ipStr);
  h += "</strong></div><div class='metric'><span>Data Source</span><strong>";
  h += esc(dataMode);
  h += "</strong></div><div class='metric'><span>Min Free Heap</span><strong>";
  h += String(static_cast<uint32_t>(ESP.getMinFreeHeap()) / 1024U) + " KB";
  h += "</strong></div><div class='metric'><span>Update Status</span><strong>";
  h += updateAvail ? String("Available") : String("Current");
  h += "</strong></div></section>";
  h += "<form class='card' method='POST' action='/printer'>";
  if (pn == 0) {
    h += "<div class='empty-note'>";
    h += display_poll_active()
             ? "No printers reported yet. Check the FilaMan URL and API key on the Data tab."
             : "Waiting for printer discovery payload over MQTT...";
    h += "</div></form>";
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
    h += "</div>"
         "<button class='btn btn-primary btn-block' type='submit'>Switch Monitored Printer</button>"
         "</form>";
  }
  h += "<div class='diag-grid'><section class='card'><div class='card-head'><div>"
       "<h3 class='card-title'>Firmware log</h3>"
       "<p class='card-desc'>Connection, polling, update, and recovery events from this boot.</p>"
       "</div><div class='actions'><button type='button' class='btn btn-secondary' id='pause'>Pause</button>"
       "<button type='button' class='btn btn-danger' id='clear'>Clear</button></div></div>"
       "<pre id='log'>Loading log...</pre></section>"
       "<aside class='card'><div class='card-head'><div>"
       "<h3 class='card-title'>Display screenshot</h3>"
       "<p class='card-desc'>Capture the current 800 x 480 framebuffer.</p></div></div>"
       "<img class='shot' id='shot' alt='The panel, 800 by 480' hidden>"
       "<button type='button' class='btn btn-secondary btn-block' id='shotBtn'>Show the screen</button>"
       "<a class='btn btn-primary btn-block' href='/screenshot.png' download='filaman-display.png'>"
       "Download PNG</a>"
       "<p class='note'>PNG of the screen right now. Encoding takes a few seconds.</p></aside></div></section>";

  h += "<form id='cfg' method='POST' action='/save'>";

  h += "<div id='panel-data' class='panel'><div class='panel-head'>"
       "<p class='band-lead'>FilaMan is used when a URL is set. Otherwise the panel listens on MQTT.</p>"
       "</div><div class='grid-2'>";
  h += "<div class='card'><div class='card-head'><div>"
       "<h3 class='card-title'>FilaMan</h3>"
       "<p class='card-desc'>HTTP or HTTPS polling of the spool inventory.</p>"
       "</div><span class='card-badge";
  if (display_poll_active()) h += " live";
  h += "'>";
  h += display_poll_active() ? "Active" : "Standby";
  h += "</span></div>";
  h += field("FilaMan Base URL", "fmUrl", c.filamanBaseUrl, "text",
             "Blank uses MQTT only",
             "http://192.168.1.50:8000");
  h += pwField("FilaMan API Key", "fmKey", "Display API key");
  h += toggleField("Allow self-signed LAN TLS",
                   "Skip certificate checks for local HTTPS",
                   "fmTls", c.filamanTlsInsecure);
  h += "</div>";

  h += "<div class='card'><div class='card-head'><div>"
       "<h3 class='card-title'>MQTT</h3>"
       "<p class='card-desc'>Broker, account, and the topic this panel subscribes to.</p>"
       "</div></div>";
  h += "<div class='row-2'>";
  h += field("MQTT Broker Host", "mqttHost", c.mqttHost, "text", nullptr, "192.168.1.10");
  h += field("MQTT Port", "mqttPort", String(c.mqttPort), "number");
  h += "</div><div class='row-2'>";
  h += field("MQTT Username", "mqttUser", c.mqttUsername, "text");
  h += pwField("MQTT Password", "mqttPass");
  h += "</div>";
  h += field("MQTT Topic Prefix", "topicPfx", c.topicPrefix, "text",
             "Topic this panel subscribes to");
  h += "</div></div></div>";

  h += "<div id='panel-network' class='panel'><div class='card'>"
       "<div class='set-row'><label class='field-label' for='f_wifiSsid'>Wi-Fi SSID</label>"
       "<input id='f_wifiSsid' type='text' name='wifiSsid' value='";
  h += esc(c.wifiSsid);
  h += "'></div>"
       "<div class='set-row'><label class='field-label' for='f_wifiPass'>Wi-Fi password</label>"
       "<input id='f_wifiPass' type='password' name='wifiPass' placeholder='Leave blank to keep existing'></div>"
       "<div class='set-row'><label class='field-label' for='f_otaPass'>Admin password</label>"
       "<input id='f_otaPass' type='password' name='otaPass' placeholder='Leave blank to keep existing'></div>";
  {
    String hostShown = c.otaHostname;
    if (hostShown.endsWith(".local")) {
      hostShown = hostShown.substring(0, hostShown.length() - 6);
    }
    h += "<div class='set-row'><label class='field-label' for='f_otaHost'>Hostname</label>"
         "<div class='host-line";
    if (!c.mdnsEnabled) {
      h += " solo";
    }
    h += "' id='hostLine'";
    if (WiFi.status() == WL_CONNECTED) {
      h += " data-ip='";
      h += WiFi.localIP().toString();
      h += "'";
    }
    h += "><input id='f_otaHost' type='text' name='otaHost' value='";
    h += esc(hostShown);
    h += "'><span class='host-suffix'>.local</span></div></div>"
         "<p class='field-hint' id='hostHint'></p>";
  }
  h += toggleField(".local name", nullptr, "mdns", c.mdnsEnabled);
  h += timezoneField(c.displayTimezone);
  h += "</div></div>";

  h += "<div id='panel-display' class='panel'><div class='card'>";
  {
    static constexpr int kFullVals[] = {50, 75, 100};
    static constexpr const char *kFullLbls[] = {"50%", "75%", "100%"};
    static constexpr int kDimVals[] = {10, 20, 35};
    static constexpr const char *kDimLbls[] = {"10%", "20%", "35%"};
    static constexpr int kTimeoutVals[] = {5, 10, 15, 30};
    static constexpr const char *kTimeoutLbls[] = {"5m", "10m", "15m", "30m"};
    static constexpr int kOffTimeoutVals[] = {5, 10, 15, 30, kDisplayOffNever};
    static constexpr const char *kOffTimeoutLbls[] = {"5m", "10m", "15m", "30m", "Never"};
    static constexpr int kIdleVals[] = {0, 60, 180};
    static constexpr const char *kIdleLbls[] = {"Off", "1 min", "3 min"};
    static constexpr int kLowVals[] = {20, 30, 40};
    static constexpr const char *kLowLbls[] = {"20%", "30%", "40%"};
    static constexpr int kCritVals[] = {10, 15, 20};
    static constexpr const char *kCritLbls[] = {"10%", "15%", "20%"};
    h += choiceField("Awake", nullptr, "brtFull", kFullVals, kFullLbls, 3, c.displayFullBrightPct);
    h += choiceField("Dimmed", nullptr, "brtDim", kDimVals, kDimLbls, 3, c.displayDimBrightPct);
    h += choiceField("Dim after", nullptr, "dimMin", kTimeoutVals, kTimeoutLbls, 4, c.displayDimMin);
    h += choiceField("Off after", nullptr, "offMin", kOffTimeoutVals, kOffTimeoutLbls, 5,
                     c.displayOffMin);
    h += choiceField("Auto-return", nullptr, "idleSec", kIdleVals, kIdleLbls, 3, c.idleReturnSec);
    h += choiceField("Low spool", nullptr, "spLow", kLowVals, kLowLbls, 3, c.spoolLowPct);
    h += choiceField("Critical", nullptr, "spCrit", kCritVals, kCritLbls, 3, c.spoolCritPct);
  }
  h += toggleField("Flip 180&deg;", nullptr, "flip", c.displayFlipped);
  h += "</div></div>";

  h += "<div class='save-bar' id='saveBar'><div class='save-copy'>"
       "<strong>Apply.</strong> Saves these settings, then reboots."
       "</div><button class='btn btn-primary' type='submit' style='min-width:210px'>"
       "Save and reboot</button></div></form>";

  h += footerLinks();
  h += "<script>const REPO='" PROJECT_GITHUB_REPO "',INST='";
  h += FIRMWARE_VERSION;
  h += "',BETA=";
  h += c.betaUpdates ? "true" : "false";
  h += ";";
  h += R"JS(
const $=id=>document.getElementById(id);
function day(s){return s?new Date(s).toLocaleDateString():'-';}
function cmp(a,b){const p=v=>{v=v.replace(/^v/,'');const i=v.indexOf('-');
return[v.slice(0,i<0?v.length:i).split('.').map(Number),i<0?'':v.slice(i+1)];};
const[x,xs]=p(a),[y,ys]=p(b);for(let i=0;i<3;i++){const d=(x[i]||0)-(y[i]||0);if(d)return d;}
if(xs===ys)return 0;if(!xs)return 1;if(!ys)return -1;
return xs.localeCompare(ys,undefined,{numeric:true});}
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
function showLatest(d){const l=$('ghlat');l.textContent=d.tag_name;
const newer=cmp(d.tag_name,INST)>0;l.classList.toggle('new',newer);
$('ghrel').textContent=day(d.published_at);
if(newer)notes('ghnb','ghn',"What's new",d.body||'');}
const stableP=gh('/releases/latest').catch(()=>null);
const tagsP=BETA?gh('/tags?per_page=30').catch(()=>null):Promise.resolve(null);
Promise.all([stableP,tagsP]).then(([s,tags])=>{
 let best=null;
 if(Array.isArray(tags))for(const t of tags){const n=t.name||'';
  if(n.indexOf('-')<0)continue;if(!best||cmp(n,best)>0)best=n;}
 if(best&&(!s||cmp(best,s.tag_name)>0))
  return gh('/releases/tags/'+best).then(b=>b&&b.prerelease&&!b.draft?b:s).catch(()=>s);
 return s;
}).then(d=>{if(!d)throw 0;showLatest(d);}).catch(()=>{$('ghrel').textContent='-';});
document.querySelectorAll('a[href^="#panel-"]').forEach(a=>{
 a.addEventListener('click',e=>{
  const id=a.getAttribute('href').slice(1);
  if(!$(id))return;
  e.preventDefault();
  show(id,true);
  if(a.dataset.focus){const f=$(a.dataset.focus);if(f)f.focus();}
 });
});
function show(id,push){
 document.querySelectorAll('.panel').forEach(p=>p.classList.toggle('on',p.id===id));
 const settings=id!=='panel-status';
 document.documentElement.classList.toggle('hide-status',settings);
 document.documentElement.classList.toggle('save-on',settings);
 document.querySelectorAll('nav .tab').forEach(t=>t.classList.toggle('active',t.getAttribute('href')==='#'+id));
 if(push)history.replaceState(null,'','#'+id);
}
const start=(location.hash||'#panel-status').slice(1);
if($(start))show(start,false);
const hostLine=$('hostLine'),hostHint=$('hostHint');
function syncHost(){
 const on=$('f_mdns').checked;
 hostLine.classList.toggle('solo',!on);
 const name=($('f_otaHost').value||'').trim();
 const ip=hostLine.dataset.ip||'';
 let t='';
 if(on&&name)t='http://'+name+'.local/';
 else if(ip)t='http://'+ip+'/';
 hostHint.textContent=t;
}
$('f_mdns').addEventListener('change',syncHost);
$('f_otaHost').addEventListener('input',syncHost);
syncHost();
const TO=[5,10,15,30];
function syncTimers(e){
 const pick=n=>document.querySelector('input[name='+n+']:checked');
 const d=pick('dimMin'),o=pick('offMin');
 if(d&&o&&+o.value!==0&&+d.value>=+o.value){
  if(e&&e.target.name==='offMin'){const v=TO.filter(x=>x<+o.value).pop();
   document.querySelector('input[name=dimMin][value="'+v+'"]').checked=true;}
  else{const v=TO.find(x=>x>+d.value);
   document.querySelector('input[name=offMin][value="'+(v==null?0:v)+'"]').checked=true;}
 }
 document.querySelectorAll('input[name=dimMin]').forEach(i=>i.disabled=false);
 document.querySelectorAll('input[name=offMin]').forEach(i=>i.disabled=+i.value!==0&&+i.value<=TO[0]);
}
document.querySelectorAll('input[name=dimMin],input[name=offMin]').forEach(i=>i.addEventListener('change',syncTimers));
syncTimers();
$('shotBtn').onclick=()=>{const i=$('shot');i.hidden=false;i.removeAttribute('src');
i.src='/screenshot.png?t='+Date.now();$('shotBtn').textContent='Refreshing…';
i.onload=()=>{$('shotBtn').textContent='Refresh the screen';};};
let paused=false;
const log=$('log');
async function loadLog(){
 if(paused||document.documentElement.classList.contains('hide-status'))return;
 try{const r=await fetch('/diagnostics/logs',{cache:'no-store'});
  if(!r.ok)return;
  const atBottom=log.scrollTop+log.clientHeight>=log.scrollHeight-24;
  log.textContent=await r.text();
  if(atBottom)log.scrollTop=log.scrollHeight;
 }catch(e){}
}
$('pause').onclick=()=>{paused=!paused;$('pause').textContent=paused?'Resume':'Pause';};
$('clear').onclick=async()=>{await fetch('/diagnostics/logs',{method:'DELETE'});
 paused=false;$('pause').textContent='Pause';loadLog();};
loadLog();setInterval(loadLog,2500);
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
  if (server.hasArg("otaHost")) {
    String host = server.arg("otaHost");
    host.trim();
    // .local is the mDNS suffix shown beside the field, not part of the name.
    if (host.endsWith(".local")) {
      host = host.substring(0, host.length() - 6);
    }
    c.otaHostname = host;
  }
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
  if (server.hasArg("dimMin") && server.arg("dimMin").length())
    c.displayDimMin = server.arg("dimMin").toInt();
  if (server.hasArg("offMin") && server.arg("offMin").length())
    c.displayOffMin = server.arg("offMin").toInt();
  if (server.hasArg("spLow") && server.arg("spLow").length())
    c.spoolLowPct = server.arg("spLow").toInt();
  if (server.hasArg("spCrit") && server.arg("spCrit").length())
    c.spoolCritPct = server.arg("spCrit").toInt();
  c.displayFlipped = server.hasArg("flip");
  c.autoCheckUpdates = server.hasArg("autoOta");
  c.betaUpdates = server.hasArg("beta");
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
  // The diagnostics view is the Status tab on the console.
  server.sendHeader("Location", "/#panel-status");
  server.send(303);
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

  // A flipped panel holds the picture upside down; save it the way it reads.
  const bool flipped = config().displayFlipped;
  for (int y = 0; ok && png.ok && y < SCREEN_HEIGHT; ++y) {
    row[0] = 0;  // filter None
    const uint16_t *src =
        framebuffer + static_cast<size_t>(flipped ? SCREEN_HEIGHT - 1 - y : y) * SCREEN_WIDTH;
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
      const uint16_t pixel = src[flipped ? SCREEN_WIDTH - 1 - x : x];
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

bool web_portal_running() { return begun; }

void web_portal_loop() {
  if (begun) {
    server.handleClient();
  }
}
