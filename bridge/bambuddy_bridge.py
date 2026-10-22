#!/usr/bin/env python3
"""Publish Bambuddy or direct Bambu printer state as retained MQTT display topics."""

from __future__ import annotations

import json
import logging
import os
import signal
import ssl
import tempfile
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import urljoin
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

import paho.mqtt.client as mqtt
import requests


def load_dotenv(path: str = ".env") -> None:
    """Load KEY=VALUE pairs from a .env file next to this script, without overriding real env vars."""
    env_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), path)
    if not os.path.exists(env_path):
        return
    with open(env_path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            key = key.strip()
            value = value.strip().strip('"').strip("'")
            if key and key not in os.environ:
                os.environ[key] = value


load_dotenv()

LOG_LEVEL = os.getenv("LOG_LEVEL", "INFO").upper()
logging.basicConfig(level=LOG_LEVEL, format="%(asctime)s %(levelname)s %(message)s")
LOGGER = logging.getLogger("bambuddy-bridge")


def env_bool(name: str, default: bool = False) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def env_float(name: str, default: float) -> float:
    raw = os.getenv(name)
    if raw is None or not raw.strip():
        return default
    try:
        return float(raw)
    except ValueError:
        LOGGER.warning("Invalid %s=%r, using %s", name, raw, default)
        return default


@dataclass(frozen=True)
class Config:
    # auto selects direct mode when DIRECT_PRINTER_HOST is set, otherwise Bambuddy.
    data_source: str = os.getenv("DATA_SOURCE", "auto").strip().lower()
    bambuddy_url: str = os.getenv("BAMBUDDY_URL", "http://localhost:8000")
    bambuddy_api_key: str = os.getenv("BAMBUDDY_API_KEY", "")
    h2c_printer_id: str = os.getenv("H2C_PRINTER_ID", "")
    h2c_serial: str = os.getenv("H2C_SERIAL", "")
    h2c_name: str = os.getenv("H2C_NAME", "H2C")
    direct_printer_host: str = os.getenv("DIRECT_PRINTER_HOST", "").strip()
    direct_printer_port: int = int(os.getenv("DIRECT_PRINTER_PORT", "8883"))
    direct_printer_serial: str = os.getenv("DIRECT_PRINTER_SERIAL", "").strip()
    direct_printer_access_code: str = os.getenv("DIRECT_PRINTER_ACCESS_CODE", "").strip()
    mqtt_host: str = os.getenv("MQTT_HOST", "localhost")
    mqtt_port: int = int(os.getenv("MQTT_PORT", "1883"))
    mqtt_username: str = os.getenv("MQTT_USERNAME", "")
    mqtt_password: str = os.getenv("MQTT_PASSWORD", "")
    mqtt_tls: bool = env_bool("MQTT_TLS", False)
    # Certificate checks are on unless a local broker uses a self-signed cert.
    mqtt_tls_insecure: bool = env_bool("MQTT_TLS_INSECURE", False)
    mqtt_client_id: str = os.getenv("MQTT_CLIENT_ID", "bambuddy-h2c-display-bridge")
    bambuddy_topic_prefix: str = os.getenv("BAMBUDDY_TOPIC_PREFIX", "bambuddy").strip("/")
    display_topic_prefix: str = os.getenv("DISPLAY_TOPIC_PREFIX", "display/amsview").strip("/")
    # ETA clock timezone (later configurable via web config). Default US Eastern (EST/EDT).
    display_tz: str = os.getenv("DISPLAY_TZ", "America/New_York")
    poll_seconds: int = int(os.getenv("POLL_SECONDS", "60"))
    # Republish after a live status message, but not more often than this.
    live_coalesce_seconds: float = env_float("LIVE_COALESCE_SECONDS", 5.0)
    ams2pro_count: int = int(os.getenv("AMS2PRO_COUNT", "4"))
    display_http_enabled: bool = env_bool("DISPLAY_HTTP_ENABLED", True)
    display_http_host: str = os.getenv("DISPLAY_HTTP_HOST", "0.0.0.0")
    display_http_port: int = int(os.getenv("DISPLAY_HTTP_PORT", "8090"))
    # Persists the device-selected printer id across restarts (see ctrl/printer topic).
    bridge_state_file: str = os.getenv(
        "BRIDGE_STATE_FILE",
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "bridge_state.json"),
    )


CONFIG = Config()
STOP_EVENT = threading.Event()
REFRESH_EVENT = threading.Event()
LATEST_STATUS: dict[str, Any] = {}
LATEST_STATUS_LOCK = threading.Lock()
LATEST_DISPLAY_STATE: dict[str, Any] = {}
LATEST_DISPLAY_STATE_LOCK = threading.Lock()
ACTIVE_PRINTER_ID: str = ""
ACTIVE_PRINTER_SERIAL: str = ""
# Bumped on every printer switch so an in-flight poll cannot republish the previous printer.
PRINTER_GENERATION = 0
ACTIVE_PRINTER_LOCK = threading.Lock()
# AMS slot/meta topics published last cycle, so we can clear (empty-retain) the
# ones that disappear when AMS units are unplugged or the monitored printer changes.
PUBLISHED_AMS_TOPICS: set[str] = set()
# Bambuddy printer topics currently subscribed. Retargeted when the monitored serial changes.
SUBSCRIBED_PRINTER_TOPICS: list[str] = []
SUBSCRIPTION_LOCK = threading.Lock()
PUBLISH_LOCK = threading.Lock()
# Last full REST status, plus inventory/queue captured with it. Live MQTT deltas overlay the snapshot.
REST_SNAPSHOT: dict[str, Any] = {}
INVENTORY_CACHE: dict[str, Any] = {
    "spools": [],
    "assignments": {},
    "queue_depth": 0,
    "next_job": "",
    "archive_id": None,
    "archive_grams": None,
    "printers": [],
    "printer_name": "",
    "printer_serial": "",
}
CACHE_LOCK = threading.Lock()
LIVE_PENDING = False
LIVE_LOCK = threading.Lock()
FORCE_REFRESH = False
FORCE_LOCK = threading.Lock()
BRIDGE_VERSION = "0.13.0"
SCHEMA_VERSION = 2
SPOOL_LOW_PERCENT = int(os.getenv("SPOOL_LOW_PERCENT", "30"))
SPOOL_CRITICAL_PERCENT = int(os.getenv("SPOOL_CRITICAL_PERCENT", "15"))


def normalize_hex_color(value: Any) -> str:
    if not value:
        return "#202020"
    text = str(value).strip()
    if text.startswith("#"):
        text = text[1:]
    if len(text) >= 6:
        return f"#{text[:6].upper()}"
    return "#202020"


def deep_get(payload: dict[str, Any], path: tuple[str, ...], default: Any = None) -> Any:
    current: Any = payload
    for key in path:
        if not isinstance(current, dict):
            return default
        current = current.get(key)
    return current if current is not None else default


def first_value(payload: dict[str, Any], keys: tuple[str, ...], default: Any = None) -> Any:
    for key in keys:
        value = payload.get(key)
        if value not in (None, ""):
            return value
    return default


def deep_merge_dict(target: dict[str, Any], update: dict[str, Any]) -> None:
    """Recursively merge printer delta reports without discarding sibling fields."""
    for key, value in update.items():
        current = target.get(key)
        if isinstance(current, dict) and isinstance(value, dict):
            deep_merge_dict(current, value)
        else:
            target[key] = value


def slot_topic(ams_id: int, slot_id: int) -> str:
    return f"{CONFIG.display_topic_prefix}/ams/{ams_id}/slot/{slot_id}"


def ams_meta_topic(ams_id: int) -> str:
    return f"{CONFIG.display_topic_prefix}/ams/{ams_id}/meta"


def printer_topic() -> str:
    return f"{CONFIG.display_topic_prefix}/printer"


def alerts_topic() -> str:
    return f"{CONFIG.display_topic_prefix}/alerts"


def overview_topic() -> str:
    return f"{CONFIG.display_topic_prefix}/ams/overview"


def bridge_status_topic() -> str:
    return f"{CONFIG.display_topic_prefix}/bridge/status"


def command_request_topic() -> str:
    return f"{CONFIG.display_topic_prefix}/command/request"


def command_response_topic() -> str:
    return f"{CONFIG.display_topic_prefix}/command/response"


def printers_topic() -> str:
    """Retained list of selectable printers for the on-device picker."""
    return f"{CONFIG.display_topic_prefix}/printers"


def printer_ctrl_topic() -> str:
    """Device -> bridge: which printer id to monitor (retained)."""
    return f"{CONFIG.display_topic_prefix}/ctrl/printer"


def load_selected_printer_id() -> str:
    try:
        with open(CONFIG.bridge_state_file, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        return str(data.get("printer_id") or "")
    except (OSError, ValueError):
        return ""


def save_selected_printer_id(printer_id: str) -> None:
    path = CONFIG.bridge_state_file
    directory = os.path.dirname(path) or "."
    handle = None
    tmp_path = ""
    try:
        handle = tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=directory, prefix=".bridge_state.", suffix=".tmp", delete=False)
        tmp_path = handle.name
        json.dump({"printer_id": str(printer_id)}, handle)
        handle.flush()
        os.fsync(handle.fileno())
        handle.close()
        handle = None
        os.replace(tmp_path, path)
        tmp_path = ""
    except OSError as exc:
        LOGGER.warning("Could not persist selected printer id: %s", exc)
    finally:
        if handle is not None:
            handle.close()
        if tmp_path:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass


class BambuddyClient:
    source_name = "bambuddy"
    uses_bambuddy_mqtt = True

    def __init__(self, config: Config) -> None:
        self.config = config
        self.session = requests.Session()
        self._lock = threading.Lock()
        if config.bambuddy_api_key:
            self.session.headers.update({"X-API-Key": config.bambuddy_api_key})

    def _url(self, path: str) -> str:
        return urljoin(self.config.bambuddy_url.rstrip("/") + "/", f"api/v1/{path.lstrip('/')}")

    def request_json(self, method: str, path: str, **kwargs: Any) -> Any:
        # The poll loop and the MQTT command handler share this session.
        with self._lock:
            response = self.session.request(method, self._url(path), timeout=10, **kwargs)
            response.raise_for_status()
            if response.content:
                return response.json()
            return None

    def list_printers(self) -> list[dict[str, Any]]:
        data = self.request_json("GET", "printers/")
        return data if isinstance(data, list) else []

    def printers_summary(self, active_id: str) -> list[dict[str, Any]]:
        """Compact {id,name,model,serial,active} list for the on-device picker."""
        summary: list[dict[str, Any]] = []
        for printer in self.list_printers():
            pid = str(first_value(printer, ("id", "printer_id"), ""))
            if not pid:
                continue
            summary.append(
                {
                    "id": pid,
                    "name": str(first_value(printer, ("name", "printer_name"), pid)),
                    "model": str(first_value(printer, ("model", "printer_model", "machine_type", "machine_model"), "")),
                    "serial": str(first_value(printer, ("serial_number", "serial", "dev_id"), "")),
                    "active": pid == str(active_id),
                }
            )
        return summary

    def resolve_printer_id(self) -> str:
        if self.config.h2c_printer_id:
            return self.config.h2c_printer_id

        printers = self.list_printers()
        # Serial wins even when an earlier printer happens to share H2C_NAME.
        if self.config.h2c_serial:
            for printer in printers:
                serial = str(first_value(printer, ("serial_number", "serial", "dev_id"), ""))
                if serial != self.config.h2c_serial:
                    continue
                printer_id = first_value(printer, ("id", "printer_id"), None)
                if printer_id not in (None, ""):
                    return str(printer_id)

        for printer in printers:
            name = str(printer.get("name", ""))
            if name != self.config.h2c_name:
                continue
            printer_id = first_value(printer, ("id", "printer_id"), None)
            if printer_id not in (None, ""):
                return str(printer_id)

        raise RuntimeError("Unable to resolve H2C printer id. Set H2C_PRINTER_ID or H2C_SERIAL.")

    def printer_status(self, printer_id: str) -> dict[str, Any]:
        data = self.request_json("GET", f"printers/{printer_id}/status")
        return data if isinstance(data, dict) else {}

    def refresh_status(self, printer_id: str) -> None:
        try:
            self.request_json("POST", f"printers/{printer_id}/refresh-status")
            LOGGER.info("Requested Bambuddy status refresh")
        except requests.HTTPError as exc:
            LOGGER.warning("Bambuddy refresh failed: %s", exc)

    def _queue_items(self, printer_id: str) -> list[dict[str, Any]]:
        try:
            data = self.request_json("GET", "queue/", params={"printer_id": printer_id})
        except requests.HTTPError:
            try:
                data = self.request_json("GET", "queue/")
            except requests.HTTPError:
                return []
        if isinstance(data, list):
            return [item for item in data if isinstance(item, dict)]
        if isinstance(data, dict):
            for key in ("items", "queue", "jobs", "data"):
                value = data.get(key)
                if isinstance(value, list):
                    return [item for item in value if isinstance(item, dict)]
        return []

    def queue_snapshot(self, printer_id: str) -> tuple[int, str]:
        """Pending count and the next job name from a single queue fetch."""
        depth = 0
        next_job = ""
        for item in self._queue_items(printer_id):
            item_printer = item.get("printer_id")
            if item_printer is not None and str(item_printer) != str(printer_id):
                continue
            status = str(item.get("status", "pending")).lower()
            if status not in {"pending", "queued", "waiting", ""}:
                continue
            depth += 1
            if next_job:
                continue
            name = first_value(item, ("name", "filename", "archive_name", "job_name"), None)
            if not name and isinstance(item.get("archive"), dict):
                name = first_value(item["archive"], ("name", "filename"), None)
            if name:
                next_job = str(name)
        return depth, next_job

    def send_printer_command(self, printer_id: str, action: str, arg: str | None = None) -> tuple[bool, str]:
        """Execute a safe printer control action via Bambuddy REST."""
        action = (action or "").strip().lower()
        try:
            if action == "pause":
                self.request_json("POST", f"printers/{printer_id}/print/pause")
                return True, "Pause sent"
            if action == "resume":
                self.request_json("POST", f"printers/{printer_id}/print/resume")
                return True, "Resume sent"
            if action == "refresh":
                self.request_json("POST", f"printers/{printer_id}/refresh-status")
                return True, "Refresh requested"
            if action == "light":
                on = str(arg or "").strip().lower() in {"1", "true", "on", "yes"}
                self.request_json("POST", f"printers/{printer_id}/chamber-light", params={"on": on})
                return True, f"Light {'on' if on else 'off'}"
            if action == "speed":
                mode = int(arg or "0")
                if mode not in (1, 2, 3, 4):
                    return False, "Speed mode must be 1-4"
                self.request_json("POST", f"printers/{printer_id}/print-speed", params={"mode": mode})
                return True, f"Speed mode {mode}"
            if action == "stop":
                return False, "Stop disabled until confirmed safeguards"
            return False, f"Unknown action: {action}"
        except requests.HTTPError as exc:
            detail = ""
            try:
                detail = exc.response.text[:200] if exc.response is not None else ""
            except Exception:
                detail = str(exc)
            LOGGER.warning("Command %s failed: %s %s", action, exc, detail)
            return False, f"Command failed: {exc.response.status_code if exc.response is not None else 'error'}"
        except requests.RequestException as exc:
            # Runs on the MQTT thread; paho re-raises callback errors and would stop its loop.
            LOGGER.warning("Command %s failed: %s", action, exc)
            return False, "Command failed: Bambuddy unreachable"
        except (TypeError, ValueError) as exc:
            return False, str(exc)

    def list_spools(self) -> list[dict[str, Any]]:
        data = self.request_json("GET", "inventory/spools")
        if isinstance(data, list):
            return data
        if isinstance(data, dict):
            for key in ("spools", "items", "data"):
                value = data.get(key)
                if isinstance(value, list):
                    return value
        return []

    def list_assignments(self, printer_id: str) -> dict[tuple[int, int], dict[str, Any]]:
        """Map (ams_id, tray_id) -> assigned spool details for the given printer."""
        try:
            data = self.request_json("GET", "inventory/assignments")
        except requests.HTTPError:
            return {}
        result: dict[tuple[int, int], dict[str, Any]] = {}
        if not isinstance(data, list):
            return result
        for entry in data:
            if not isinstance(entry, dict):
                continue
            if str(entry.get("printer_id")) != str(printer_id):
                continue
            spool = entry.get("spool")
            if not isinstance(spool, dict):
                continue
            ams_id = as_int(entry.get("ams_id"))
            tray_id = as_int(entry.get("tray_id"))
            if ams_id is None or tray_id is None:
                continue
            result[(ams_id, tray_id)] = spool
        return result

    def archive_filament_grams(self, archive_id: Any) -> float | None:
        """Sliced filament total (grams) for an archive id."""
        if archive_id in (None, ""):
            return None
        try:
            data = self.request_json("GET", f"archives/{archive_id}")
        except requests.HTTPError:
            return None
        if not isinstance(data, dict):
            return None
        return as_float(first_value(data, ("filament_used_grams",), first_value(data.get("extra_data") or {}, ("filament_used_grams",), None)))


class DirectPrinterClient:
    """Read one Bambu printer directly over its LAN MQTT broker."""

    source_name = "direct"
    uses_bambuddy_mqtt = False

    def __init__(self, config: Config) -> None:
        self.config = config
        self.serial = config.direct_printer_serial or config.h2c_serial
        self.name = config.h2c_name or self.serial
        self._lock = threading.Lock()
        self._raw_status: dict[str, Any] = {}
        self._connected = False
        self._sequence = 0
        self._client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"amsview-{self.serial[-8:]}",
        )
        self._client.username_pw_set("bblp", config.direct_printer_access_code)
        # Bambu printers use a self-signed v1 certificate on their LAN broker.
        self._client.tls_set(cert_reqs=ssl.CERT_NONE)
        self._client.tls_insecure_set(True)
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message
        self._client.reconnect_delay_set(min_delay=1, max_delay=30)

    @property
    def report_topic(self) -> str:
        return f"device/{self.serial}/report"

    @property
    def request_topic(self) -> str:
        return f"device/{self.serial}/request"

    def start(self) -> None:
        self._client.connect_async(
            self.config.direct_printer_host,
            self.config.direct_printer_port,
            keepalive=60,
        )
        self._client.loop_start()
        LOGGER.info(
            "Connecting directly to Bambu printer %s at %s:%d",
            self.serial,
            self.config.direct_printer_host,
            self.config.direct_printer_port,
        )

    def stop(self) -> None:
        self._client.loop_stop()
        try:
            self._client.disconnect()
        except (OSError, RuntimeError):
            pass

    def _next_sequence(self) -> str:
        with self._lock:
            self._sequence += 1
            return str(self._sequence)

    def _publish_request(self, section: str, command: str, **fields: Any) -> bool:
        request = {
            section: {
                "sequence_id": self._next_sequence(),
                "command": command,
                **fields,
            }
        }
        info = self._client.publish(
            self.request_topic,
            json.dumps(request, separators=(",", ":")),
            qos=1,
        )
        return info.rc == mqtt.MQTT_ERR_SUCCESS

    def _on_connect(
        self,
        client: mqtt.Client,
        _userdata: Any,
        _flags: Any,
        reason_code: Any,
        _properties: Any = None,
    ) -> None:
        if reason_code != 0:
            LOGGER.warning("Direct printer MQTT connection failed: %s", reason_code)
            return
        with self._lock:
            self._connected = True
        client.subscribe(self.report_topic)
        LOGGER.info("Connected directly to Bambu printer %s", self.serial)
        self.refresh_status(self.serial)

    def _on_disconnect(
        self,
        _client: mqtt.Client,
        _userdata: Any,
        _disconnect_flags: Any,
        reason_code: Any,
        _properties: Any = None,
    ) -> None:
        with self._lock:
            self._connected = False
            normalized = self._normalized_status_locked()
        with LATEST_STATUS_LOCK:
            LATEST_STATUS.clear()
            LATEST_STATUS.update(normalized)
        LOGGER.warning("Direct printer MQTT disconnected: %s", reason_code)
        note_live_update()

    def _normalized_status_locked(self) -> dict[str, Any]:
        raw = dict(self._raw_status)
        print_status = raw.get("print")
        normalized = dict(raw)
        if isinstance(print_status, dict):
            normalized.update(print_status)
        normalized["print"] = print_status if isinstance(print_status, dict) else {}
        normalized["connected"] = self._connected
        normalized["serial_number"] = self.serial
        normalized.setdefault("name", self.name)
        return normalized

    def merge_report(self, payload: dict[str, Any]) -> dict[str, Any]:
        """Merge one full or delta printer report and return the display-shaped status."""
        incoming_print = payload.get("print")
        full = isinstance(incoming_print, dict) and str(incoming_print.get("msg", "")) == "0"
        with self._lock:
            if full:
                self._raw_status = json.loads(json.dumps(payload))
            else:
                deep_merge_dict(self._raw_status, payload)
            return self._normalized_status_locked()

    def _on_message(self, _client: mqtt.Client, _userdata: Any, message: mqtt.MQTTMessage) -> None:
        if message.topic != self.report_topic:
            return
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            LOGGER.warning("Ignoring malformed direct printer report")
            return
        if not isinstance(payload, dict):
            return
        normalized = self.merge_report(payload)
        with LATEST_STATUS_LOCK:
            LATEST_STATUS.clear()
            LATEST_STATUS.update(normalized)
        note_live_update()

    def resolve_printer_id(self) -> str:
        return self.serial

    def printer_status(self, _printer_id: str) -> dict[str, Any]:
        with self._lock:
            return self._normalized_status_locked()

    def refresh_status(self, _printer_id: str) -> None:
        if not self._publish_request(
            "pushing",
            "pushall",
            version=1,
            push_target=1,
        ):
            LOGGER.warning("Could not request direct printer status")

    def printers_summary(self, active_id: str) -> list[dict[str, Any]]:
        status = self.printer_status(active_id)
        model = str(first_value(status, ("machine_type", "printer_model", "model"), ""))
        return [
            {
                "id": self.serial,
                "name": str(first_value(status, ("name", "device_name"), self.name)),
                "model": model,
                "serial": self.serial,
                "active": str(active_id) == self.serial,
            }
        ]

    def list_spools(self) -> list[dict[str, Any]]:
        return []

    def list_assignments(self, _printer_id: str) -> dict[tuple[int, int], dict[str, Any]]:
        return {}

    def queue_snapshot(self, _printer_id: str) -> tuple[int, str]:
        return 0, ""

    def archive_filament_grams(self, _archive_id: Any) -> float | None:
        return None

    def send_printer_command(
        self,
        _printer_id: str,
        action: str,
        arg: str | None = None,
    ) -> tuple[bool, str]:
        """Send safe controls. Printer writes require LAN Developer Mode."""
        action = (action or "").strip().lower()
        if action == "refresh":
            self.refresh_status(self.serial)
            return True, "Refresh requested"
        if action in {"pause", "resume"}:
            ok = self._publish_request("print", action, param="")
            return ok, f"{action.title()} sent" if ok else "Printer MQTT unavailable"
        if action == "light":
            on = str(arg or "").strip().lower() in {"1", "true", "on", "yes"}
            ok = self._publish_request(
                "system",
                "ledctrl",
                led_node="chamber_light",
                led_mode="on" if on else "off",
                led_on_time=500,
                led_off_time=500,
                loop_times=1,
            )
            return ok, f"Light {'on' if on else 'off'}" if ok else "Printer MQTT unavailable"
        if action == "speed":
            try:
                mode = int(arg or "0")
            except (TypeError, ValueError):
                return False, "Speed mode must be 1-4"
            if mode not in (1, 2, 3, 4):
                return False, "Speed mode must be 1-4"
            ok = self._publish_request("print", "print_speed", param=str(mode))
            return ok, f"Speed mode {mode}" if ok else "Printer MQTT unavailable"
        if action == "stop":
            return False, "Stop disabled until confirmed safeguards"
        return False, f"Unknown action: {action}"


def indexed_spools(spools: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for spool in spools:
        for key in ("tray_uuid", "tag_uid"):
            value = spool.get(key)
            if value:
                result[f"{key}:{str(value).replace('-', '').upper()}"] = spool
    return result


def lookup_spool(index: dict[str, dict[str, Any]], tray: dict[str, Any]) -> dict[str, Any]:
    tray_uuid = first_value(tray, ("tray_uuid", "uuid"))
    tag_uid = first_value(tray, ("tag_uid", "tag"))
    for key_name, value in (("tray_uuid", tray_uuid), ("tag_uid", tag_uid)):
        if value:
            match = index.get(f"{key_name}:{str(value).replace('-', '').upper()}")
            if match:
                return match
    return {}


def extract_ams_units(status: dict[str, Any]) -> list[dict[str, Any]]:
    candidates = [
        deep_get(status, ("print", "ams", "ams"), []),
        deep_get(status, ("ams", "ams"), []),
        deep_get(status, ("ams",), []),
        deep_get(status, ("print", "ams"), []),
    ]
    for candidate in candidates:
        if isinstance(candidate, dict) and candidate.get("ams"):
            candidate = candidate["ams"]
        if isinstance(candidate, list) and candidate:
            return candidate
    return []


def extract_trays(ams_unit: dict[str, Any]) -> list[dict[str, Any]]:
    for key in ("tray", "trays", "slots"):
        trays = ams_unit.get(key)
        if isinstance(trays, list):
            return [tray for tray in trays if isinstance(tray, dict)]
    return []


def expected_ams_kind(ams_id: int) -> str:
    if ams_id < CONFIG.ams2pro_count:
        return "ams2pro"
    return "ams_ht"


def empty_slot_payload(ams_id: int, slot_id: int) -> dict[str, Any]:
    return {
        "ams_id": ams_id,
        "ams_kind": expected_ams_kind(ams_id),
        "slot": slot_id,
        "empty": True,
        "color": "#202020",
        "colors": [],
        "color_style": "",
        "finish": "",
        "color_name": "",
        "manufacturer": "",
        "material": "",
        "material_subgroup": "",
        "profile_base_name": "",
        "nozzle_min": None,
        "nozzle_max": None,
        "bed_temp": None,
        "density_g_cm3": None,
        "diameter_mm": None,
        "remaining_percent": None,
        "remaining_grams": None,
        "remaining_source": "unknown",
        "cost_remaining": None,
        "last_used": "",
        "rfid": False,
        "backup_badge": "",
        "active": False,
        "schema_version": SCHEMA_VERSION,
    }


def as_float(value: Any) -> float | None:
    if isinstance(value, bool) or value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def as_int(value: Any, default: int | None = None) -> int | None:
    if isinstance(value, bool) or value in (None, ""):
        return default
    try:
        return int(value)
    except (TypeError, ValueError):
        number = as_float(value)
        if number is None:
            return default
        return int(round(number))


def as_int_temp(value: Any) -> int | None:
    number = as_float(value)
    if number is None:
        return None
    return int(round(number))


def coerce_bool(value: Any) -> bool | None:
    if isinstance(value, bool):
        return value
    if value in (None, ""):
        return None
    if isinstance(value, (int, float)):
        return value != 0
    text = str(value).strip().lower()
    if text in {"1", "true", "yes", "on", "online", "connected"}:
        return True
    if text in {"0", "false", "no", "off", "offline", "disconnected"}:
        return False
    return None


def printer_is_connected(status: dict[str, Any]) -> bool:
    """Online only when the status says so. A missing key is not a connection."""
    for key in ("connected", "online", "is_connected"):
        if key not in status:
            continue
        parsed = coerce_bool(status.get(key))
        if parsed is not None:
            return parsed
    return False


def parse_epoch_seconds(value: Any) -> float | None:
    """Unix seconds from a number, digit string, or ISO-8601 timestamp."""
    if isinstance(value, bool) or value in (None, ""):
        return None
    if isinstance(value, (int, float)):
        ts = float(value)
    else:
        text = str(value).strip()
        if not text:
            return None
        try:
            ts = float(text)
        except ValueError:
            try:
                normalized = text.replace("Z", "+00:00")
                parsed = datetime.fromisoformat(normalized)
            except ValueError:
                return None
            if parsed.tzinfo is None:
                parsed = parsed.replace(tzinfo=timezone.utc)
            return parsed.timestamp()
    if ts > 10_000_000_000:  # milliseconds
        ts /= 1000.0
    return ts


def parse_hex_color(value: Any) -> str | None:
    if value in (None, ""):
        return None
    text = str(value).strip()
    if text.startswith("#"):
        text = text[1:]
    if len(text) < 6:
        return None
    digits = text[:6]
    if any(char not in "0123456789abcdefABCDEF" for char in digits):
        return None
    return f"#{digits.upper()}"


def ams_kind_of(payload: dict[str, Any], ams_id: int) -> str:
    kind = str(payload.get("ams_kind") or "")
    return kind or expected_ams_kind(ams_id)


def unit_short_name(ams_id: int, kind: str) -> str:
    """A / HT1. Overview uses the same HT numbering; AMS 2 Pro letters stay A-D."""
    if kind == "ams_ht":
        return f"HT{ams_id - CONFIG.ams2pro_count + 1}"
    if 0 <= ams_id < 26:
        return chr(ord("A") + ams_id)
    return str(ams_id)


def slot_short_name(ams_id: int, slot_id: int, kind: str) -> str:
    unit = unit_short_name(ams_id, kind)
    if kind == "ams_ht":
        if slot_id == 0:
            return unit
        return f"{unit}-{slot_id + 1}"
    return f"{unit}{slot_id + 1}"


_FINISH_BY_NAME = {
    "glow": "glow",
    "translucent": "translucent",
    "neon": "neon",
}
_SPARKLE_BY_NAME = {
    "sparkle": "sparkle",
    "galaxy": "galaxy",
    "glitter": "glitter",
}


def _first_text(sources: list[dict[str, Any]], keys: tuple[str, ...]) -> str:
    for source in sources:
        value = first_value(source, keys, None)
        if isinstance(value, str) and value.strip():
            return value.strip()
        if isinstance(value, dict):
            nested = first_value(value, ("name", "base_name", "profile"), None)
            if isinstance(nested, str) and nested.strip():
                return nested.strip()
    return ""


def _color_stops(sources: list[dict[str, Any]], primary: str | None) -> list[str]:
    raw: Any = None
    for source in sources:
        raw = first_value(source, ("extra_colors", "extra_colours", "colors"), None)
        if raw not in (None, "", []):
            break
    parts: list[Any]
    if isinstance(raw, str):
        parts = [piece.strip() for piece in raw.replace(";", ",").split(",")]
    elif isinstance(raw, list):
        parts = list(raw)
    else:
        parts = []
    colors: list[str] = []
    for part in parts:
        color = parse_hex_color(part)
        if color and color not in colors:
            colors.append(color)
    if primary and primary not in colors:
        colors.insert(0, primary)
    return colors[:8]


def _printer_param_temp(source: dict[str, Any], key: str) -> int | None:
    """Read one temperature from a FilaMan-style printer_params list or dict."""
    params = source.get("printer_params")
    if isinstance(params, dict):
        return as_int_temp(params.get(key))
    if not isinstance(params, list):
        return None
    for item in params:
        if not isinstance(item, dict):
            continue
        name = str(item.get("param_key") or item.get("key") or "")
        if name != key:
            continue
        temp = as_int_temp(item.get("param_value", item.get("value")))
        if temp is not None:
            return temp
    return None


def _profile_bed_temp_c(filament: dict[str, Any]) -> int | None:
    """filament_printer_profiles.bed_temp_c. Default profile, else lowest id."""
    profiles = filament.get("printer_profiles")
    if isinstance(profiles, dict):
        profiles = [profiles]
    if not isinstance(profiles, list):
        return None
    rows = [row for row in profiles if isinstance(row, dict)]
    rows.sort(key=lambda row: (not bool(row.get("is_default_for_printer")), row.get("id") or 0))
    for row in rows:
        temp = as_int_temp(row.get("bed_temp_c"))
        if temp is not None:
            return temp
    return None


def resolve_bed_temp(
    spool: dict[str, Any],
    filament: dict[str, Any],
    tray: dict[str, Any],
) -> int | None:
    """Slot bed temperature.

    Explicit setpoints win: ``bambu_bed_temp``, then ``bed_temp`` /
    ``bed_temperature``, spool before filament before tray. ``bed_temp_c``
    on a printer profile (or a flat ``bed_temp_c``) is the fallback.
    ``bed_temp_min`` remains the last resort.
    """
    sources = [source for source in (spool, filament, tray) if isinstance(source, dict)]
    for source in sources:
        temp = as_int_temp(first_value(source, ("bambu_bed_temp", "bed_temp", "bed_temperature"), None))
        if temp is None:
            temp = _printer_param_temp(source, "bambu_bed_temp")
        if temp is not None:
            return temp
    if isinstance(filament, dict):
        temp = _profile_bed_temp_c(filament)
        if temp is not None:
            return temp
    for source in sources:
        temp = as_int_temp(source.get("bed_temp_c"))
        if temp is not None:
            return temp
    for source in sources:
        temp = as_int_temp(source.get("bed_temp_min"))
        if temp is not None:
            return temp
    return None


def swatch_fields(
    spool: dict[str, Any],
    filament: dict[str, Any],
    tray: dict[str, Any],
    primary: str | None,
) -> dict[str, Any]:
    """Forward Bambuddy effect / extra-colour fields the panel already renders."""
    sources = [source for source in (spool, filament, tray) if isinstance(source, dict)]
    effect = _first_text(sources, ("effect_type", "effect", "finish")).lower()
    subtype = _first_text(sources, ("subtype", "material_subtype", "material_subgroup")).lower()
    finish = _FINISH_BY_NAME.get(effect) or _FINISH_BY_NAME.get(subtype) or ""
    if effect in _SPARKLE_BY_NAME:
        subgroup = _SPARKLE_BY_NAME[effect]
    elif subtype in _SPARKLE_BY_NAME:
        subgroup = _SPARKLE_BY_NAME[subtype]
    else:
        # CF/GF subtypes stay intact so the panel can mark fiber from the token.
        subgroup = subtype
    colors = _color_stops(sources, primary)
    profile = _first_text(
        sources,
        ("profile_base_name", "bambu_profile_base_name", "slicer_preset", "preset_name", "filament_profile"),
    )
    density = None
    diameter = None
    for source in sources:
        if density is None:
            density = as_float(first_value(source, ("density_g_cm3", "density"), None))
        if diameter is None:
            diameter = as_float(first_value(source, ("diameter_mm", "diameter"), None))
    return {
        "colors": colors,
        "color_style": "gradient" if len(colors) >= 2 else "",
        "finish": finish,
        "material_subgroup": subgroup,
        "profile_base_name": profile,
        "bed_temp": resolve_bed_temp(spool, filament, tray),
        "density_g_cm3": density,
        "diameter_mm": diameter,
    }


def spool_remaining_grams(spool: dict[str, Any]) -> float | None:
    """Bambuddy inventory: remaining = label_weight - weight_used."""
    if not spool:
        return None
    direct = as_float(first_value(spool, ("remaining_weight", "remaining")))
    if direct is not None:
        return max(0.0, direct)
    label = as_float(first_value(spool, ("label_weight", "initial_weight", "spool_weight", "total_weight", "start_weight")))
    used = as_float(first_value(spool, ("weight_used", "used_weight", "used")))
    if label is not None and used is not None:
        return max(0.0, label - used)
    return None


def spool_remaining_percent(
    spool: dict[str, Any], tray: dict[str, Any] | None = None
) -> tuple[int | None, str]:
    """Prefer inventory math; fall back to AMS tray.remain when 0-100."""
    if spool:
        for key in ("remaining_percent", "remaining_percentage", "percent", "percent_remaining"):
            value = as_float(spool.get(key))
            if value is not None:
                return max(0, min(100, int(round(value)))), "inventory"
        remaining = spool_remaining_grams(spool)
        label = as_float(
            first_value(
                spool,
                ("label_weight", "initial_weight", "spool_weight", "total_weight", "start_weight"),
            )
        )
        if remaining is not None and label is not None and label > 0:
            return max(0, min(100, int(round(100.0 * remaining / label)))), "inventory"

    if tray:
        tray_remain = as_float(tray.get("remain"))
        if tray_remain is not None and 0 <= tray_remain <= 100:
            return int(round(tray_remain)), "ams"
    return None, "unknown"


def spool_cost_remaining(spool: dict[str, Any], remaining_grams: float | None) -> str | None:
    if remaining_grams is None:
        return None
    price_per_kg = as_float(first_value(spool, ("cost_per_kg", "price_per_kg")))
    if price_per_kg is None:
        return None
    return f"{price_per_kg * (remaining_grams / 1000.0):.2f}"


def relative_age(value: Any) -> str:
    ts = parse_epoch_seconds(value)
    if ts is None:
        return "" if value in (None, "") else str(value).strip()[:16]
    age = max(0, int(time.time() - ts))
    if age < 3600:
        return f"{max(1, age // 60)}m ago"
    if age < 86400:
        return f"{age // 3600}h ago"
    days = age // 86400
    if days < 30:
        return f"{days}d ago"
    return f"{days // 30}mo ago"


def tray_has_rfid(tray: dict[str, Any], spool: dict[str, Any]) -> bool:
    for source in (tray, spool):
        for key in ("tray_uuid", "tag_uid", "tag", "uuid", "tray_info_idx"):
            value = source.get(key)
            if value not in (None, "", 0, "0"):
                return True
    return False


def combine_slot_payload(ams_id: int, slot_id: int, tray: dict[str, Any], spool: dict[str, Any]) -> dict[str, Any]:
    filament = spool.get("filament") if isinstance(spool.get("filament"), dict) else {}
    vendor = filament.get("vendor") if isinstance(filament.get("vendor"), dict) else {}

    material = first_value(
        spool,
        ("material", "filament_type", "type"),
        first_value(filament, ("material", "material_type", "name"), first_value(tray, ("tray_type", "filament_type", "type"), "")),
    )
    color_name = first_value(spool, ("color_name", "name"), first_value(filament, ("color_name", "name"), first_value(tray, ("tray_id_name", "tray_sub_brands"), "")))
    manufacturer = first_value(spool, ("brand", "manufacturer", "vendor"), first_value(vendor, ("name",), ""))
    color = first_value(spool, ("rgba", "color", "color_hex"), first_value(filament, ("color_hex", "color"), first_value(tray, ("tray_color", "filament_color", "color"), "")))

    tray_type = first_value(tray, ("tray_type", "filament_type"), "")
    empty = not str(tray_type or "").strip() and not spool
    remaining_grams = spool_remaining_grams(spool) if spool else None
    remaining, remaining_source = spool_remaining_percent(spool, tray)
    last_used = relative_age(first_value(spool, ("last_used", "updated_at", "last_used_at", "modified")))
    # state 27 is observed as the actively feeding tray on H2C AMS firmware.
    tray_state = as_int(tray.get("state"))
    active_flag = coerce_bool(first_value(tray, ("active", "is_active"), None))
    active = bool(active_flag) or tray_state == 27
    primary = parse_hex_color(color)
    swatch = swatch_fields(spool, filament, tray, primary)
    return {
        "ams_id": ams_id,
        "ams_kind": expected_ams_kind(ams_id),
        "slot": slot_id,
        "empty": empty,
        "color": normalize_hex_color(color),
        "colors": swatch["colors"],
        "color_style": swatch["color_style"],
        "finish": swatch["finish"],
        "color_name": str(color_name or ""),
        "manufacturer": str(manufacturer or ""),
        "material": str(material or ""),
        "material_subgroup": swatch["material_subgroup"],
        "profile_base_name": swatch["profile_base_name"],
        "nozzle_min": first_value(spool, ("nozzle_min", "nozzle_temp_min"), first_value(filament, ("nozzle_temp_min",), first_value(tray, ("nozzle_temp_min",), None))),
        "nozzle_max": first_value(spool, ("nozzle_max", "nozzle_temp_max"), first_value(filament, ("nozzle_temp_max",), first_value(tray, ("nozzle_temp_max",), None))),
        "bed_temp": swatch["bed_temp"],
        "density_g_cm3": swatch["density_g_cm3"],
        "diameter_mm": swatch["diameter_mm"],
        "remaining_percent": remaining,
        "remaining_grams": int(round(remaining_grams)) if remaining_grams is not None else None,
        "remaining_source": remaining_source,
        "cost_remaining": spool_cost_remaining(spool, remaining_grams) if spool else None,
        "last_used": last_used,
        "rfid": tray_has_rfid(tray, spool),
        "backup_badge": "",
        "active": active,
        "schema_version": SCHEMA_VERSION,
    }


def is_ams_ht_unit(ams_unit: dict[str, Any]) -> bool:
    """AMS-HT units report is_ams_ht=true and use raw ids of 128+."""
    if coerce_bool(ams_unit.get("is_ams_ht")) is True:
        return True
    return (as_int(first_value(ams_unit, ("id", "ams_id"), 0), 0) or 0) >= 128


def _unit_raw_id(ams_unit: dict[str, Any], default: int = 0) -> int:
    return as_int(first_value(ams_unit, ("id", "ams_id"), default), default) or 0


def build_slot_payloads(
    status: dict[str, Any],
    spools: list[dict[str, Any]],
    assignments: dict[tuple[int, int], dict[str, Any]] | None = None,
) -> tuple[dict[tuple[int, int], dict[str, Any]], dict[int, dict[str, Any]]]:
    payloads: dict[tuple[int, int], dict[str, Any]] = {}
    meta: dict[int, dict[str, Any]] = {}
    spool_index = indexed_spools(spools)
    assignments = assignments or {}

    ams_units = [unit for unit in extract_ams_units(status) if isinstance(unit, dict)]
    regular_units = sorted((u for u in ams_units if not is_ams_ht_unit(u)), key=_unit_raw_id)
    ht_units = sorted((u for u in ams_units if is_ams_ht_unit(u)), key=_unit_raw_id)

    # Display ids: AMS 2 Pro keep their raw ids (0-3); AMS-HT raw ids (128+) map to 4, 5, ...
    unit_map: list[tuple[int, dict[str, Any]]] = [(_unit_raw_id(u), u) for u in regular_units]
    unit_map += [(CONFIG.ams2pro_count + index, u) for index, u in enumerate(ht_units)]
    raw_to_display = {_unit_raw_id(ams_unit, display_id): display_id for display_id, ams_unit in unit_map}

    for display_id, ams_unit in unit_map:
        raw_id = _unit_raw_id(ams_unit, display_id)
        meta[display_id] = {
            "ams_id": display_id,
            "ams_kind": "ams_ht" if is_ams_ht_unit(ams_unit) else "ams2pro",
            "temperature": first_value(ams_unit, ("temp", "temperature"), None),
            "humidity": first_value(ams_unit, ("humidity", "humidity_raw"), None),
            "dry_status": first_value(ams_unit, ("dry_status",), None),
            "dry_target_temp": first_value(ams_unit, ("dry_target_temp",), None),
            "dry_time": first_value(ams_unit, ("dry_time",), None),
            "schema_version": SCHEMA_VERSION,
        }
        for tray in extract_trays(ams_unit):
            slot_id = as_int(first_value(tray, ("id", "tray_id", "slot"), 0), 0) or 0
            spool = assignments.get((raw_id, slot_id)) or lookup_spool(spool_index, tray)
            payloads[(display_id, slot_id)] = combine_slot_payload(display_id, slot_id, tray, spool)

    # Only actually-connected AMS units (from unit_map above) are published now;
    # we no longer pad to a fixed AMS 2 Pro / AMS-HT count. A connected AMS 2 Pro
    # still reports all 4 trays (empty or not), so fill any gaps in its slot range
    # so the detail page always shows 4 slots for a present unit.
    for display_id, ams_unit in unit_map:
        if is_ams_ht_unit(ams_unit):
            payloads.setdefault((display_id, 0), empty_slot_payload(display_id, 0))
        else:
            for slot_id in range(4):
                payloads.setdefault((display_id, slot_id), empty_slot_payload(display_id, slot_id))

    # Second pass: infer refill/backup peers now that all slots are known.
    for (ams_id, slot_id), payload in list(payloads.items()):
        if payload.get("empty"):
            continue
        # Rebuild badge using peer scan; spool fields already in payload.
        peers = {
            key: value
            for key, value in payloads.items()
            if key != (ams_id, slot_id)
        }
        material = str(payload.get("material") or "").lower()
        color = normalize_hex_color(payload.get("color"))
        if not material:
            continue
        for (other_ams, other_slot), other in peers.items():
            if other.get("empty"):
                continue
            if str(other.get("material") or "").lower() == material and normalize_hex_color(other.get("color")) == color:
                other_kind = ams_kind_of(other, other_ams)
                payload["backup_badge"] = f"Backup {unit_short_name(other_ams, other_kind)}-{other_slot + 1}"
                break

    apply_tray_now(payloads, status, raw_to_display)
    return payloads, meta


def extract_tray_now(status: dict[str, Any]) -> Any:
    for path in (("tray_now",), ("print", "tray_now"), ("ams", "tray_now"), ("print", "ams", "tray_now")):
        if len(path) == 1:
            if path[0] in status and status[path[0]] not in (None, ""):
                return status[path[0]]
            continue
        value = deep_get(status, path, None)
        if value not in (None, ""):
            return value
    return None


def decode_tray_now(value: int, raw_to_display: dict[int, int]) -> tuple[int, int] | None:
    """Map a Bambu global tray id onto a display AMS id and slot.

    254/255 are the external spool and are not decoded here. AMS 2 Pro uses
    raw_ams * 4 + slot. AMS-HT raw ids start at 128, so 128 itself and 512
    (128 * 4) both name the first HT unit. An id that matches no connected
    unit is left unresolved.
    """
    if value < 0 or value in (254, 255):
        return None
    raw_ams, slot = divmod(value, 4)
    if raw_ams in raw_to_display and 0 <= slot < 4:
        return raw_to_display[raw_ams], slot
    if value in raw_to_display:
        return raw_to_display[value], 0
    if value >= 256:
        hi, lo = (value >> 8) & 0xFF, value & 0xFF
        if hi >= 128 and hi in raw_to_display and 0 <= lo < 4:
            return raw_to_display[hi], lo
    return None


def apply_tray_now(
    payloads: dict[tuple[int, int], dict[str, Any]],
    status: dict[str, Any],
    raw_to_display: dict[int, int],
) -> None:
    """Mark the feeding tray when tray_now names a known slot.

    State 27 (already applied) stays in place when tray_now is the external
    spool or does not match a connected AMS. Clearing first would hide the
    only signal AMS-HT often provides.
    """
    value = as_int(extract_tray_now(status))
    if value is None or value < 0 or value in (254, 255):
        return
    decoded = decode_tray_now(value, raw_to_display)
    if decoded is None:
        return
    target = payloads.get(decoded)
    if not target or target.get("empty"):
        return
    for payload in payloads.values():
        payload["active"] = False
    target["active"] = True


SPEED_PRESETS = {
    1: "Silent",
    2: "Standard",
    3: "Sport",
    4: "Ludicrous",
}


def format_remaining_time_minutes(minutes: Any) -> str | None:
    """Bambuddy remaining_time is minutes on live H2C status."""
    if minutes in (None, ""):
        return None
    try:
        total_minutes = int(round(float(minutes)))
    except (TypeError, ValueError):
        return str(minutes)
    if total_minutes < 0:
        return None
    hours, mins = divmod(total_minutes, 60)
    if hours > 0:
        return f"{hours}h {mins:02d}m"
    return f"{total_minutes} min"


_TZ_CACHE: tuple[str, Any] | None = None


def display_timezone() -> Any:
    """Resolve the configured display timezone, falling back to UTC if unknown."""
    global _TZ_CACHE
    if _TZ_CACHE is not None and _TZ_CACHE[0] == CONFIG.display_tz:
        return _TZ_CACHE[1]
    try:
        zone: Any = ZoneInfo(CONFIG.display_tz)
    except (ZoneInfoNotFoundError, ValueError, KeyError):
        LOGGER.warning("Unknown DISPLAY_TZ %r, falling back to UTC", CONFIG.display_tz)
        zone = timezone.utc
    _TZ_CACHE = (CONFIG.display_tz, zone)
    return zone


def format_eta_clock(remaining_seconds: int | None) -> str:
    if remaining_seconds is None or remaining_seconds < 0:
        return ""
    zone = display_timezone()
    done = (datetime.now(timezone.utc) + timedelta(seconds=remaining_seconds)).astimezone(zone)
    now = datetime.now(zone)
    clock = done.strftime("%I:%M %p").lstrip("0")
    if done.date() == now.date():
        return f"done ~{clock}"
    return f"done ~{clock} {done.strftime('%a')}"


def format_hms(status: dict[str, Any]) -> str:
    hms = status.get("hms") or status.get("hms_errors") or status.get("hms_status")
    if hms in (None, "", "ok", "OK", []):
        return ""
    if isinstance(hms, str):
        return hms if hms.lower() != "ok" else ""
    if isinstance(hms, list):
        parts: list[str] = []
        for item in hms[:3]:
            if isinstance(item, dict):
                code = first_value(item, ("code", "attr", "module_id"), "")
                msg = first_value(item, ("msg", "message", "desc"), "")
                parts.append(str(msg or code or item))
            else:
                parts.append(str(item))
        return " | ".join(parts)
    return str(hms)


def decode_door(status: dict[str, Any]) -> str:
    explicit = first_value(status, ("door_open", "door", "door_status"), None)
    if explicit is True or str(explicit).lower() in {"open", "1", "true"}:
        return "open"
    if explicit is False or str(explicit).lower() in {"closed", "0", "false"}:
        return "closed"
    home_flag = status.get("home_flag")
    try:
        if home_flag is not None and (int(home_flag) >> 23) & 1:
            return "open"
        if home_flag is not None:
            return "closed"
    except (TypeError, ValueError):
        pass
    return "unknown"


def decode_filament_sensor(status: dict[str, Any], has_active_tray: bool) -> str:
    value = first_value(status, ("fila_switch", "hw_switch_state", "filament_sensor", "filam_sen"), None)
    if value is True or str(value).lower() in {"1", "true", "loaded", "on"}:
        return "loaded"
    if value is False or str(value).lower() in {"0", "false", "empty", "off"}:
        return "empty"
    try:
        if value is not None and str(value).strip() != "":
            return "loaded" if int(value) == 1 else "empty"
    except (TypeError, ValueError):
        pass
    if has_active_tray:
        return "AMS"
    state = str(first_value(status, ("state", "gcode_state"), "")).upper()
    if state in {"RUNNING", "PRINTING", "PAUSE", "PAUSED"}:
        return "AMS"
    return "idle"


def decode_chamber_light(status: dict[str, Any]) -> str:
    light = first_value(status, ("chamber_light",), None)
    if light is True or str(light).lower() in {"on", "1", "true"}:
        return "on"
    if light is False or str(light).lower() in {"off", "0", "false"}:
        return "off"
    reports = status.get("lights_report")
    if isinstance(reports, list):
        for entry in reports:
            if isinstance(entry, dict) and str(entry.get("node", "")).lower() == "chamber_light":
                mode = str(entry.get("mode", "")).lower()
                if mode in {"on", "flashing"}:
                    return "on"
                if mode == "off":
                    return "off"
    return "unknown"


def active_tray_summary(slot_payloads: dict[tuple[int, int], dict[str, Any]]) -> dict[str, Any]:
    for (ams_id, slot_id), payload in sorted(slot_payloads.items()):
        if payload.get("active") and not payload.get("empty"):
            kind = ams_kind_of(payload, ams_id)
            name = slot_short_name(ams_id, slot_id, kind)
            color_name = payload.get("color_name") or payload.get("material") or "Active"
            material = payload.get("material") or ""
            label = f"{name} / {color_name}"
            if material and material not in label:
                label += f" {material}"
            return {
                "active_color": payload.get("color") or "#202020",
                "active_material": material,
                "active_material_subgroup": payload.get("material_subgroup") or "",
                "active_label": label,
                "active_remaining": (
                    f"{payload.get('remaining_percent')}%"
                    if payload.get("remaining_percent") is not None
                    else (f"{payload.get('remaining_grams')}g" if payload.get("remaining_grams") is not None else "")
                ),
            }
    return {
        "active_color": "#202020",
        "active_material": "",
        "active_material_subgroup": "",
        "active_label": "",
        "active_remaining": "",
    }


def format_filament_used(status: dict[str, Any], archive_total_g: float | None) -> str | None:
    """Bambuddy status has no live filament_used; estimate from archive + progress."""
    direct = first_value(
        status,
        ("filament_used", "mc_filament_used", "weight"),
        None,
    )
    direct_f = as_float(direct)
    progress = as_float(first_value(status, ("progress", "mc_percent"), 0)) or 0.0
    progress = max(0.0, min(100.0, progress))

    if archive_total_g is not None and archive_total_g > 0:
        used = archive_total_g * (progress / 100.0)
        return f"{used:.0f}/{archive_total_g:.0f}"
    if direct_f is not None:
        return f"{direct_f:.0f}"
    return None


def build_printer_payload(
    status: dict[str, Any],
    slot_payloads: dict[tuple[int, int], dict[str, Any]] | None = None,
    next_job: str = "",
    archive_filament_g: float | None = None,
    queue_depth: int = 0,
    alert_count: int = 0,
    printer_name: str = "",
    printer_serial: str = "",
) -> dict[str, Any]:
    current_print = status.get("current_print") if isinstance(status.get("current_print"), dict) else {}
    # Live Bambuddy H2C reports remaining_time in minutes (e.g. 9 == 9m).
    remaining_minutes = first_value(
        status,
        ("remaining_time", "mc_remaining_time"),
        first_value(current_print, ("remaining_time",), None),
    )
    remaining_seconds = None
    remaining_minutes_int = None
    if remaining_minutes not in (None, ""):
        try:
            remaining_minutes_int = int(round(float(remaining_minutes)))
            remaining_seconds = remaining_minutes_int * 60
        except (TypeError, ValueError):
            remaining_seconds = None
            remaining_minutes_int = None

    layer = first_value(
        status,
        ("layer_num", "current_layer", "layer"),
        first_value(current_print, ("current_layer", "layer_num"), None),
    )
    total_layers = first_value(
        status,
        ("total_layers", "total_layer", "mc_total_layer", "total_layer_num"),
        first_value(current_print, ("total_layers",), None),
    )
    job_name = ""
    for key in ("subtask_name", "current_print", "gcode_file", "task_name"):
        value = status.get(key)
        if isinstance(value, str) and value.strip():
            job_name = value.strip()
            break
    if not job_name and isinstance(current_print, dict):
        nested = first_value(current_print, ("filename", "name", "subtask_name"), "")
        if isinstance(nested, str):
            job_name = nested.strip()

    progress_raw = first_value(status, ("progress", "mc_percent"), first_value(current_print, ("progress",), None))
    progress_int = None
    progress_f = as_float(progress_raw)
    if progress_f is not None:
        progress_int = max(0, min(100, int(round(progress_f))))

    filament_used = format_filament_used(status, archive_filament_g)
    speed_level = first_value(status, ("speed_level", "spd_lvl"), None)
    try:
        speed_int = int(speed_level) if speed_level is not None else None
    except (TypeError, ValueError):
        speed_int = None

    temps = status.get("temperatures") if isinstance(status.get("temperatures"), dict) else {}
    nozzle = as_int_temp(first_value(temps, ("nozzle",), first_value(status, ("nozzle_temper",), None)))
    nozzle_target = as_int_temp(first_value(temps, ("nozzle_target",), first_value(status, ("nozzle_target_temper",), None)))
    nozzle_2 = as_int_temp(first_value(temps, ("nozzle_2", "nozzle2"), first_value(status, ("nozzle_temper2", "nozzle2_temper"), None)))
    nozzle_2_target = as_int_temp(
        first_value(
            temps,
            ("nozzle_2_target", "nozzle2_target"),
            first_value(status, ("nozzle_target_temper2", "nozzle2_target_temper"), None),
        )
    )
    bed = as_int_temp(first_value(temps, ("bed",), first_value(status, ("bed_temper",), None)))
    bed_target = as_int_temp(first_value(temps, ("bed_target",), first_value(status, ("bed_target_temper",), None)))
    chamber = as_int_temp(first_value(temps, ("chamber",), first_value(status, ("chamber_temper",), None)))
    chamber_target = as_int_temp(first_value(temps, ("chamber_target",), None))

    if nozzle_target == 0:
        nozzle_target = None
    if nozzle_2_target == 0:
        nozzle_2_target = None
    if bed_target == 0:
        bed_target = None
    if chamber_target == 0:
        chamber_target = None

    active = active_tray_summary(slot_payloads or {})
    has_active = bool(active.get("active_label"))

    # Elapsed from a unix timestamp or an ISO-8601 start time.
    elapsed_minutes = None
    started = first_value(status, ("gcode_start_time", "print_start_time", "start_time"), None)
    started_ts = parse_epoch_seconds(started)
    if started_ts is not None:
        elapsed_minutes = max(0, int((time.time() - started_ts) / 60))

    serial = printer_serial or str(first_value(status, ("serial_number", "serial", "dev_id"), "") or "")
    if not serial:
        serial = CONFIG.h2c_serial

    return {
        "schema_version": SCHEMA_VERSION,
        # Name of the printer actually being polled (falls back to status/env).
        "name": printer_name or str(first_value(status, ("name", "printer_name", "device_name"), CONFIG.h2c_name)),
        "serial": serial,
        "connected": printer_is_connected(status),
        "state": first_value(status, ("state", "gcode_state", "status"), "unknown"),
        "job_name": str(job_name or ""),
        "progress": progress_int,
        "remaining_time": format_remaining_time_minutes(remaining_minutes_int),
        "remaining_minutes": remaining_minutes_int,
        "remaining_seconds": remaining_seconds,
        "elapsed_minutes": elapsed_minutes,
        "eta_clock": format_eta_clock(remaining_seconds),
        "layer": layer,
        "total_layers": total_layers,
        "filament_used": filament_used,
        "nozzle": nozzle,
        "nozzle_target": nozzle_target,
        "nozzle_2": nozzle_2,
        "nozzle_2_target": nozzle_2_target,
        "nozzle_heating": bool(temps.get("nozzle_heating", False)),
        "nozzle_2_heating": bool(temps.get("nozzle_2_heating", False)),
        "bed_heating": bool(temps.get("bed_heating", False)),
        "chamber_heating": bool(temps.get("chamber_heating", False)),
        "active_extruder": first_value(status, ("active_extruder",), 0),
        "bed": bed,
        "bed_target": bed_target,
        "chamber": chamber,
        "chamber_target": chamber_target,
        "speed_level": speed_int,
        "speed_preset": SPEED_PRESETS.get(speed_int or -1, ""),
        "fan_part": first_value(status, ("cooling_fan_speed",), None),
        "fan_aux": first_value(status, ("big_fan1_speed",), None),
        "fan_chamber": first_value(status, ("big_fan2_speed",), None),
        "wifi_signal": first_value(status, ("wifi_signal",), None),
        "hms": format_hms(status),
        "next_job": next_job,
        "queue_depth": queue_depth,
        "alert_count": alert_count,
        "chamber_light": decode_chamber_light(status),
        "door": decode_door(status),
        "filament_sensor": decode_filament_sensor(status, has_active),
        "active_color": active["active_color"],
        "active_material": active["active_material"],
        "active_material_subgroup": active.get("active_material_subgroup") or "",
        "active_label": active["active_label"],
        "active_remaining": active.get("active_remaining") or "",
        "updated_at": int(time.time()),
    }


LAST_PUBLISHED: dict[str, str] = {}


def payload_fingerprint(payload: dict[str, Any]) -> str:
    """Compare payloads ignoring always-changing timestamps."""
    filtered = {k: v for k, v in payload.items() if k != "updated_at"}
    return json.dumps(filtered, sort_keys=True, separators=(",", ":"), default=str)


def publish_json(
    client: mqtt.Client,
    topic: str,
    payload: dict[str, Any],
    retain: bool = True,
    *,
    force: bool = False,
    pace: bool = True,
) -> bool:
    body = json.dumps(payload, separators=(",", ":"), default=str)
    fingerprint = payload_fingerprint(payload)
    with PUBLISH_LOCK:
        if not force and LAST_PUBLISHED.get(topic) == fingerprint:
            return False
        client.publish(topic, body, retain=retain)
        LAST_PUBLISHED[topic] = fingerprint
    # Pace retained floods so the CrowPanel RGB panel isn't blanked by MQTT bursts.
    # Command replies and the connect callback run on the MQTT thread and must not block it.
    if pace and threading.current_thread() is threading.main_thread():
        STOP_EVENT.wait(0.25)
    return True


def clear_retained(client: mqtt.Client, topic: str) -> None:
    """Delete a retained topic (empty payload) so stale AMS units drop off the panel."""
    with PUBLISH_LOCK:
        client.publish(topic, "", retain=True)
        LAST_PUBLISHED.pop(topic, None)
    if threading.current_thread() is threading.main_thread():
        STOP_EVENT.wait(0.05)


def build_alerts(
    status: dict[str, Any],
    slot_payloads: dict[tuple[int, int], dict[str, Any]],
) -> list[dict[str, Any]]:
    alerts: list[dict[str, Any]] = []
    connected = printer_is_connected(status)
    state = str(first_value(status, ("state", "gcode_state", "status"), "")).upper()

    if not connected:
        alerts.append(
            {
                "id": "printer-offline",
                "severity": "danger",
                "title": "Printer offline",
                "detail": "The configured data source reports the printer is disconnected",
                "source": "printer",
                "acknowledged": False,
            }
        )

    if state in {"PAUSE", "PAUSED"}:
        alerts.append(
            {
                "id": "print-paused",
                "severity": "warn",
                "title": "Print paused",
                "detail": str(first_value(status, ("subtask_name", "gcode_file"), "Job paused")),
                "source": "printer",
                "acknowledged": False,
            }
        )
    if state in {"FAILED", "FAIL", "ERROR"}:
        alerts.append(
            {
                "id": "print-failed",
                "severity": "danger",
                "title": "Print failed",
                "detail": str(first_value(status, ("subtask_name", "gcode_file"), "Check printer")),
                "source": "printer",
                "acknowledged": False,
            }
        )

    door = decode_door(status)
    if door == "open" and state in {"RUNNING", "PRINTING", "PAUSE", "PAUSED"}:
        alerts.append(
            {
                "id": "door-open",
                "severity": "warn",
                "title": "Door open",
                "detail": "Chamber door is open during a job",
                "source": "printer",
                "acknowledged": False,
            }
        )

    hms = status.get("hms") or status.get("hms_errors") or []
    if isinstance(hms, list):
        for index, item in enumerate(hms[:5]):
            if isinstance(item, dict):
                code = first_value(item, ("code", "full_code", "attr"), f"hms-{index}")
                msg = first_value(item, ("msg", "message", "desc"), "HMS alert")
                alerts.append(
                    {
                        "id": f"hms-{code}",
                        "severity": "danger",
                        "title": f"HMS {code}",
                        "detail": str(msg),
                        "source": "hms",
                        "acknowledged": False,
                    }
                )

    for (ams_id, slot_id), payload in sorted(slot_payloads.items()):
        if payload.get("empty"):
            continue
        rem = payload.get("remaining_percent")
        if rem is None:
            continue
        try:
            rem_i = int(rem)
        except (TypeError, ValueError):
            continue
        unit = slot_short_name(ams_id, slot_id, ams_kind_of(payload, ams_id))
        label = unit
        material = payload.get("material") or payload.get("color_name") or "spool"
        if rem_i <= SPOOL_CRITICAL_PERCENT:
            alerts.append(
                {
                    "id": f"spool-critical-{ams_id}-{slot_id}",
                    "severity": "danger",
                    "title": f"{label} critically low",
                    "detail": f"{material} at {rem_i}%",
                    "source": "ams",
                    "acknowledged": False,
                }
            )
        elif rem_i <= SPOOL_LOW_PERCENT and payload.get("active"):
            alerts.append(
                {
                    "id": f"spool-low-{ams_id}-{slot_id}",
                    "severity": "warn",
                    "title": f"{label} low",
                    "detail": f"Active {material} at {rem_i}%",
                    "source": "ams",
                    "acknowledged": False,
                }
            )

    # Deduplicate by id while preserving order.
    seen: set[str] = set()
    unique: list[dict[str, Any]] = []
    for alert in alerts:
        alert_id = str(alert.get("id"))
        if alert_id in seen:
            continue
        seen.add(alert_id)
        unique.append(alert)
    return unique[:12]


def build_ams_overview(
    slot_payloads: dict[tuple[int, int], dict[str, Any]],
    meta_payloads: dict[int, dict[str, Any]],
) -> dict[str, Any]:
    units: list[dict[str, Any]] = []
    # Only units actually present this cycle (meta or slots), not a fixed range.
    present_ids = sorted(set(meta_payloads.keys()) | {ams_id for (ams_id, _slot) in slot_payloads.keys()})
    for ams_id in present_ids:
        meta = meta_payloads.get(ams_id, {})
        kind = meta.get("ams_kind") or expected_ams_kind(ams_id)
        if kind == "ams_ht":
            label = f"HT{ams_id - CONFIG.ams2pro_count + 1}"
            slot_ids = [0]
        else:
            label = f"AMS {chr(ord('A') + ams_id)}"
            slot_ids = list(range(4))
        slots = []
        for slot_id in slot_ids:
            payload = slot_payloads.get((ams_id, slot_id), empty_slot_payload(ams_id, slot_id))
            slots.append(
                {
                    "slot": slot_id,
                    "empty": bool(payload.get("empty")),
                    "active": bool(payload.get("active")),
                    "color": payload.get("color") or "#202020",
                    "colors": payload.get("colors") or [],
                    "color_style": payload.get("color_style") or "",
                    "finish": payload.get("finish") or "",
                    "color_name": payload.get("color_name") or "",
                    "manufacturer": payload.get("manufacturer") or "",
                    "material": payload.get("material") or "",
                    "material_subgroup": payload.get("material_subgroup") or "",
                    "rfid": bool(payload.get("rfid")),
                    "remaining_percent": payload.get("remaining_percent"),
                    "remaining_grams": payload.get("remaining_grams"),
                }
            )
        # Skip empty HT placeholders with no climate and empty slot.
        if kind == "ams_ht":
            has_climate = meta.get("temperature") not in (None, "") or meta.get("humidity") not in (None, "")
            has_filament = any(not s.get("empty") for s in slots)
            if not has_climate and not has_filament:
                continue
        units.append(
            {
                "ams_id": ams_id,
                "ams_kind": kind,
                "label": label,
                "temperature": meta.get("temperature"),
                "humidity": meta.get("humidity"),
                "slots": slots,
            }
        )
    return {
        "schema_version": SCHEMA_VERSION,
        "updated_at": int(time.time()),
        "units": units,
    }


def build_display_state(
    printer_payload: dict[str, Any],
    alerts: list[dict[str, Any]],
    overview: dict[str, Any],
    slot_payloads: dict[tuple[int, int], dict[str, Any]],
    meta_payloads: dict[int, dict[str, Any]],
    printer_id: str,
) -> dict[str, Any]:
    """Single snapshot for GET /display/h2c/state (panel can poll instead of MQTT flood)."""
    slots = [slot_payloads[key] for key in sorted(slot_payloads.keys())]
    metas = [meta_payloads[key] for key in sorted(meta_payloads.keys())]
    return {
        "schema_version": SCHEMA_VERSION,
        "bridge_version": BRIDGE_VERSION,
        "updated_at": int(time.time()),
        "printer_id": str(printer_id),
        "bridge": {
            "online": True,
            "status": "online",
            "version": BRIDGE_VERSION,
            "schema_version": SCHEMA_VERSION,
        },
        "printer": printer_payload,
        "alerts": alerts,
        "overview": overview,
        "slots": slots,
        "ams_meta": metas,
    }


def store_display_state(state: dict[str, Any]) -> None:
    with LATEST_DISPLAY_STATE_LOCK:
        LATEST_DISPLAY_STATE.clear()
        LATEST_DISPLAY_STATE.update(state)


def get_display_state() -> dict[str, Any]:
    with LATEST_DISPLAY_STATE_LOCK:
        if not LATEST_DISPLAY_STATE:
            return {
                "schema_version": SCHEMA_VERSION,
                "bridge_version": BRIDGE_VERSION,
                "updated_at": 0,
                "bridge": {"online": False, "status": "starting", "version": BRIDGE_VERSION},
                "printer": {},
                "alerts": [],
                "overview": {"units": []},
                "slots": [],
                "ams_meta": [],
            }
        return dict(LATEST_DISPLAY_STATE)


class DisplayHttpHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format: str, *args: Any) -> None:
        LOGGER.debug("HTTP %s - %s", self.address_string(), format % args)

    def _send_json(self, status: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, separators=(",", ":"), default=str).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        state_route = f"/{CONFIG.display_topic_prefix}/state"
        stat_route = f"/{CONFIG.display_topic_prefix}/stat"
        if path in {state_route, stat_route, "/display/amsview/state", "/display/h2c/state", "/display/h2c/stat", "/state", "/stat"}:
            self._send_json(200, get_display_state())
            return
        if path in {"/health", "/"}:
            state = get_display_state()
            self._send_json(
                200,
                {
                    "ok": True,
                    "bridge_version": BRIDGE_VERSION,
                    "updated_at": state.get("updated_at", 0),
                    "state_path": state_route,
                },
            )
            return
        self._send_json(404, {"ok": False, "error": "not_found", "path": path})


def start_display_http_server() -> ThreadingHTTPServer | None:
    if not CONFIG.display_http_enabled:
        LOGGER.info("Display HTTP API disabled")
        return None
    server = ThreadingHTTPServer((CONFIG.display_http_host, CONFIG.display_http_port), DisplayHttpHandler)
    thread = threading.Thread(target=server.serve_forever, name="display-http", daemon=True)
    thread.start()
    LOGGER.info(
        "Display HTTP API on http://%s:%d/%s/state",
        CONFIG.display_http_host,
        CONFIG.display_http_port,
        CONFIG.display_topic_prefix,
    )
    return server


def current_printer() -> tuple[str, int]:
    with ACTIVE_PRINTER_LOCK:
        return ACTIVE_PRINTER_ID, PRINTER_GENERATION


def still_current(printer_id: str, generation: int) -> bool:
    with ACTIVE_PRINTER_LOCK:
        return ACTIVE_PRINTER_ID == printer_id and PRINTER_GENERATION == generation


def note_live_update() -> None:
    global LIVE_PENDING
    with LIVE_LOCK:
        LIVE_PENDING = True
    REFRESH_EVENT.set()


def live_is_pending() -> bool:
    with LIVE_LOCK:
        return LIVE_PENDING


def consume_live() -> None:
    global LIVE_PENDING
    with LIVE_LOCK:
        LIVE_PENDING = False


def request_force_refresh() -> None:
    global FORCE_REFRESH
    with FORCE_LOCK:
        FORCE_REFRESH = True
    REFRESH_EVENT.set()


def consume_force() -> bool:
    global FORCE_REFRESH
    with FORCE_LOCK:
        pending = FORCE_REFRESH
        FORCE_REFRESH = False
        return pending


def http_status(exc: requests.HTTPError) -> int | None:
    response = getattr(exc, "response", None)
    if response is None:
        return None
    return response.status_code


def snapshot_ready() -> bool:
    with LATEST_STATUS_LOCK:
        return bool(REST_SNAPSHOT)


def reset_printer_cache() -> None:
    with LATEST_STATUS_LOCK:
        LATEST_STATUS.clear()
        REST_SNAPSHOT.clear()
    with CACHE_LOCK:
        INVENTORY_CACHE.update(
            {
                "spools": [],
                "assignments": {},
                "queue_depth": 0,
                "next_job": "",
                "archive_id": None,
                "archive_grams": None,
                "printers": [],
                "printer_name": "",
                "printer_serial": "",
            }
        )


def copy_inventory() -> dict[str, Any]:
    with CACHE_LOCK:
        return {
            "spools": list(INVENTORY_CACHE["spools"]),
            "assignments": dict(INVENTORY_CACHE["assignments"]),
            "queue_depth": INVENTORY_CACHE["queue_depth"],
            "next_job": INVENTORY_CACHE["next_job"],
            "archive_id": INVENTORY_CACHE["archive_id"],
            "archive_grams": INVENTORY_CACHE["archive_grams"],
            "printers": list(INVENTORY_CACHE["printers"]),
            "printer_name": INVENTORY_CACHE["printer_name"],
            "printer_serial": INVENTORY_CACHE["printer_serial"],
        }


def printer_status_topic(serial: str) -> str:
    return f"{CONFIG.bambuddy_topic_prefix}/printers/{serial}/status"


def printer_ams_topic(serial: str) -> str:
    return f"{CONFIG.bambuddy_topic_prefix}/printers/{serial}/ams/changed"


def serial_from_printer_topic(topic: str) -> str:
    prefix = f"{CONFIG.bambuddy_topic_prefix}/printers/"
    if not topic.startswith(prefix):
        return ""
    return topic[len(prefix) :].split("/", 1)[0]


def retarget_printer_subscription(client: mqtt.Client, serial: str) -> None:
    serial = serial.strip()
    if not serial or any(char in serial for char in "+#/"):
        return
    topics = [printer_status_topic(serial), printer_ams_topic(serial)]
    with SUBSCRIPTION_LOCK:
        global SUBSCRIBED_PRINTER_TOPICS
        if topics == SUBSCRIBED_PRINTER_TOPICS:
            return
        previous = list(SUBSCRIBED_PRINTER_TOPICS)
        SUBSCRIBED_PRINTER_TOPICS = topics
    for old in previous:
        if old not in topics:
            client.unsubscribe(old)
    for topic in topics:
        client.subscribe(topic)
    LOGGER.info("Watching Bambuddy MQTT for serial %s", serial)


def remember_printer_serial(client: mqtt.Client, printer_id: str, generation: int, serial: str) -> None:
    global ACTIVE_PRINTER_SERIAL
    serial = serial.strip()
    if not serial:
        return
    with ACTIVE_PRINTER_LOCK:
        if ACTIVE_PRINTER_ID != printer_id or PRINTER_GENERATION != generation:
            return
        ACTIVE_PRINTER_SERIAL = serial
    retarget_printer_subscription(client, serial)


def refresh_inventory(source: Any, printer_id: str, generation: int, archive_id: Any) -> dict[str, Any]:
    """Fetch spools, assignments, and the queue once. Keep the last good copy on failure."""
    cached = copy_inventory()
    try:
        spools = source.list_spools()
        cached["spools"] = spools
    except requests.RequestException as exc:
        LOGGER.warning("Spool inventory failed: %s", exc)
    try:
        cached["assignments"] = source.list_assignments(printer_id)
    except requests.RequestException as exc:
        LOGGER.warning("Spool assignments failed: %s", exc)
    try:
        depth, next_job = source.queue_snapshot(printer_id)
        cached["queue_depth"] = depth
        cached["next_job"] = next_job
    except requests.RequestException as exc:
        LOGGER.warning("Queue fetch failed: %s", exc)
    if archive_id != cached["archive_id"]:
        try:
            cached["archive_grams"] = source.archive_filament_grams(archive_id)
            cached["archive_id"] = archive_id
        except requests.RequestException as exc:
            LOGGER.warning("Archive filament lookup failed: %s", exc)
    try:
        printers = source.printers_summary(printer_id)
        cached["printers"] = printers
        active = next((item for item in printers if item["id"] == str(printer_id)), None)
        if active:
            cached["printer_name"] = str(active.get("name") or "")
            cached["printer_serial"] = str(active.get("serial") or "")
    except requests.RequestException as exc:
        LOGGER.warning("Could not list printers: %s", exc)
    with ACTIVE_PRINTER_LOCK:
        if ACTIVE_PRINTER_ID != printer_id or PRINTER_GENERATION != generation:
            return cached
        with CACHE_LOCK:
            INVENTORY_CACHE.update(cached)
    return cached


def publish_display(
    client: mqtt.Client,
    printer_id: str,
    generation: int,
    *,
    merged_status: dict[str, Any],
    inventory: dict[str, Any],
    heartbeat: bool,
) -> bool:
    global PUBLISHED_AMS_TOPICS
    if not still_current(printer_id, generation):
        return False
    slot_payloads, meta_payloads = build_slot_payloads(
        merged_status,
        inventory["spools"],
        inventory["assignments"],
    )
    alerts = build_alerts(merged_status, slot_payloads)
    overview = build_ams_overview(slot_payloads, meta_payloads)
    printer_payload = build_printer_payload(
        merged_status,
        slot_payloads,
        inventory["next_job"],
        inventory["archive_grams"],
        queue_depth=inventory["queue_depth"],
        alert_count=len(alerts),
        printer_name=inventory["printer_name"],
        printer_serial=inventory["printer_serial"],
    )
    if not still_current(printer_id, generation):
        return False
    store_display_state(
        build_display_state(
            printer_payload,
            alerts,
            overview,
            slot_payloads,
            meta_payloads,
            printer_id,
        )
    )

    def publish_one(topic: str, payload: dict[str, Any], *, force: bool = False) -> bool | None:
        if not still_current(printer_id, generation):
            return None
        return publish_json(client, topic, payload, force=force)

    if publish_one(printer_topic(), printer_payload) is None:
        return False
    if (
        publish_one(
            alerts_topic(),
            {"schema_version": SCHEMA_VERSION, "updated_at": int(time.time()), "alerts": alerts},
        )
        is None
    ):
        return False
    if publish_one(overview_topic(), overview) is None:
        return False
    published_slots = 0
    published_meta = 0
    for (ams_id, slot_id), payload in sorted(slot_payloads.items()):
        changed = publish_one(slot_topic(ams_id, slot_id), payload)
        if changed is None:
            return False
        if changed:
            published_slots += 1
    for ams_id, payload in sorted(meta_payloads.items()):
        changed = publish_one(ams_meta_topic(ams_id), payload)
        if changed is None:
            return False
        if changed:
            published_meta += 1

    current_ams_topics = {slot_topic(a, s) for (a, s) in slot_payloads} | {ams_meta_topic(a) for a in meta_payloads}
    ams_prefix = f"{CONFIG.display_topic_prefix}/ams/"
    with PUBLISH_LOCK:
        known = set(PUBLISHED_AMS_TOPICS)
        # Only per-unit slot/meta topics. The overview also lives under ams/ and must stay retained.
        known.update(
            topic
            for topic in LAST_PUBLISHED
            if topic.startswith(ams_prefix) and ("/slot/" in topic or topic.endswith("/meta"))
        )
    for stale_topic in known - current_ams_topics:
        if not still_current(printer_id, generation):
            return False
        clear_retained(client, stale_topic)
    with ACTIVE_PRINTER_LOCK:
        if ACTIVE_PRINTER_ID != printer_id or PRINTER_GENERATION != generation:
            return False
        PUBLISHED_AMS_TOPICS = current_ams_topics

    if inventory["printers"] and publish_one(
        printers_topic(),
        {
            "schema_version": SCHEMA_VERSION,
            "updated_at": int(time.time()),
            "active_id": str(printer_id),
            "printers": inventory["printers"],
        },
    ) is None:
        return False
    if heartbeat and publish_one(
        bridge_status_topic(),
        {
            "online": True,
            "status": "online",
            "version": BRIDGE_VERSION,
            "schema_version": SCHEMA_VERSION,
            "updated_at": int(time.time()),
            "printer_id": str(printer_id),
        },
        force=True,
    ) is None:
        return False
    LOGGER.info(
        "Published printer/alerts/overview; slots %d/%d changed, meta %d/%d changed",
        published_slots,
        len(slot_payloads),
        published_meta,
        len(meta_payloads),
    )
    return True


def replace_missing_printer(source: Any, client: mqtt.Client, printer_id: str) -> None:
    LOGGER.warning("Bambuddy has no printer %s; resolving from configuration", printer_id)
    try:
        replacement = str(source.resolve_printer_id())
    except (requests.RequestException, RuntimeError) as exc:
        LOGGER.warning("No replacement printer: %s", exc)
        return
    if replacement == str(printer_id):
        LOGGER.warning("Configured printer %s is still missing", printer_id)
        return
    handle_printer_switch(client, replacement)


def refresh_and_publish(
    source: Any,
    mqtt_client: mqtt.Client,
    printer_id: str,
    force_refresh: bool = False,
    *,
    live_only: bool = False,
    heartbeat: bool = False,
) -> bool:
    """Read the configured source and publish; return False when it should retry sooner."""
    _printer, generation = current_printer()
    if not printer_id or not still_current(printer_id, generation):
        return False
    if live_only and not force_refresh and snapshot_ready():
        with LATEST_STATUS_LOCK:
            merged_status = {**REST_SNAPSHOT, **LATEST_STATUS}
        inventory = copy_inventory()
        if inventory["printer_serial"] and source.uses_bambuddy_mqtt:
            remember_printer_serial(mqtt_client, printer_id, generation, inventory["printer_serial"])
        return publish_display(
            mqtt_client,
            printer_id,
            generation,
            merged_status=merged_status,
            inventory=inventory,
            heartbeat=False,
        )

    if force_refresh:
        try:
            source.refresh_status(printer_id)
        except requests.RequestException as exc:
            LOGGER.warning("%s refresh failed: %s", source.source_name, exc)
        if STOP_EVENT.wait(1):
            return False
        if not still_current(printer_id, generation):
            return False

    try:
        rest_status = source.printer_status(printer_id)
    except requests.HTTPError as exc:
        if http_status(exc) == 404:
            replace_missing_printer(source, mqtt_client, printer_id)
        else:
            LOGGER.warning("Printer status failed: %s", exc)
        return False
    except requests.RequestException as exc:
        LOGGER.warning("Printer status failed: %s", exc)
        return False
    if not isinstance(rest_status, dict):
        rest_status = {}

    with ACTIVE_PRINTER_LOCK:
        if ACTIVE_PRINTER_ID != printer_id or PRINTER_GENERATION != generation:
            return False
        with LATEST_STATUS_LOCK:
            delta = dict(LATEST_STATUS)
            LATEST_STATUS.clear()
            merged_status = {**delta, **rest_status}
            REST_SNAPSHOT.clear()
            REST_SNAPSHOT.update(rest_status)

    inventory = refresh_inventory(source, printer_id, generation, merged_status.get("current_archive_id"))
    if not still_current(printer_id, generation):
        return False
    with LATEST_STATUS_LOCK:
        if LATEST_STATUS:
            merged_status = {**merged_status, **LATEST_STATUS}
    if inventory["printer_serial"] and source.uses_bambuddy_mqtt:
        remember_printer_serial(mqtt_client, printer_id, generation, inventory["printer_serial"])
    return publish_display(
        mqtt_client,
        printer_id,
        generation,
        merged_status=merged_status,
        inventory=inventory,
        heartbeat=heartbeat,
    )


def publish_command_response(client: mqtt.Client, request_id: str, action: str, ok: bool, message: str) -> None:
    # force=True: a repeated pause must still produce a reply. updated_at is not part of the fingerprint.
    publish_json(
        client,
        command_response_topic(),
        {
            "id": request_id,
            "action": action,
            "ok": ok,
            "message": message,
            "updated_at": int(time.time()),
        },
        retain=False,
        force=True,
        pace=False,
    )


def handle_command_request(source: Any, client: mqtt.Client, payload: dict[str, Any]) -> None:
    request_id = str(payload.get("id") or f"cmd-{int(time.time())}")
    action = str(payload.get("action") or "")
    arg = payload.get("arg")
    arg_text = None if arg is None else str(arg)
    with ACTIVE_PRINTER_LOCK:
        printer_id = ACTIVE_PRINTER_ID
    if not printer_id:
        publish_command_response(client, request_id, action, False, "Printer id not ready")
        return
    ok, message = source.send_printer_command(printer_id, action, arg_text)
    publish_command_response(client, request_id, action, ok, message)
    if ok:
        request_force_refresh()


def handle_printer_switch(client: mqtt.Client, new_id: str) -> None:
    """Device selected a different printer to monitor: switch, persist, re-poll."""
    global ACTIVE_PRINTER_ID, ACTIVE_PRINTER_SERIAL, PRINTER_GENERATION, PUBLISHED_AMS_TOPICS
    with ACTIVE_PRINTER_LOCK:
        if new_id == ACTIVE_PRINTER_ID:
            return
        LOGGER.info("Switching monitored printer %s -> %s", ACTIVE_PRINTER_ID, new_id)
        ACTIVE_PRINTER_ID = new_id
        ACTIVE_PRINTER_SERIAL = ""
        PRINTER_GENERATION += 1
        stale_topics = list(PUBLISHED_AMS_TOPICS)
        PUBLISHED_AMS_TOPICS = set()
        # Hold this lock across the cache clear so an in-flight poll cannot
        # write the previous printer back after the generation bump.
        reset_printer_cache()
    save_selected_printer_id(new_id)
    store_display_state(
        {
            "schema_version": SCHEMA_VERSION,
            "bridge_version": BRIDGE_VERSION,
            "updated_at": int(time.time()),
            "printer_id": str(new_id),
            "bridge": {
                "online": True,
                "status": "online",
                "version": BRIDGE_VERSION,
                "schema_version": SCHEMA_VERSION,
            },
            "printer": {},
            "alerts": [],
            "overview": {"schema_version": SCHEMA_VERSION, "units": []},
            "slots": [],
            "ams_meta": [],
        }
    )
    for stale_topic in stale_topics:
        clear_retained(client, stale_topic)
    request_force_refresh()


def on_connect(client: mqtt.Client, _userdata: Any, _flags: Any, reason_code: Any, _properties: Any = None) -> None:
    LOGGER.info("Connected to MQTT broker: %s", reason_code)
    client.subscribe(command_request_topic())
    client.subscribe(printer_ctrl_topic())
    with SUBSCRIPTION_LOCK:
        topics = list(SUBSCRIBED_PRINTER_TOPICS)
    for topic in topics:
        client.subscribe(topic)
    publish_json(
        client,
        bridge_status_topic(),
        {
            "online": True,
            "status": "online",
            "version": BRIDGE_VERSION,
            "schema_version": SCHEMA_VERSION,
            "updated_at": int(time.time()),
        },
        pace=False,
    )
    request_force_refresh()


def on_message(client: mqtt.Client, userdata: Any, message: mqtt.MQTTMessage) -> None:
    topic = message.topic
    LOGGER.debug("Received %s", topic)
    try:
        payload = json.loads(message.payload.decode("utf-8")) if message.payload else {}
    except (json.JSONDecodeError, UnicodeDecodeError):
        payload = {}

    if topic == command_request_topic() and isinstance(payload, dict):
        source = userdata.get("source") if isinstance(userdata, dict) else None
        if source is not None and hasattr(source, "send_printer_command"):
            handle_command_request(source, client, payload)
        return

    if topic == printer_ctrl_topic():
        source = userdata.get("source") if isinstance(userdata, dict) else None
        if source is not None and not source.uses_bambuddy_mqtt:
            # Direct mode represents one fixed printer; ignore retained selections
            # left over from a previous Bambuddy setup.
            return
        new_id = ""
        if isinstance(payload, dict):
            new_id = str(first_value(payload, ("id", "printer_id"), ""))
        elif isinstance(payload, (str, int)) and not isinstance(payload, bool):
            new_id = str(payload)
        if new_id:
            handle_printer_switch(client, new_id)
        return

    printer_prefix = f"{CONFIG.bambuddy_topic_prefix}/printers/"
    if not topic.startswith(printer_prefix):
        return
    serial = serial_from_printer_topic(topic)
    with ACTIVE_PRINTER_LOCK:
        active_serial = ACTIVE_PRINTER_SERIAL
    if not serial or serial != active_serial:
        return
    if topic.endswith("/status") and isinstance(payload, dict):
        with LATEST_STATUS_LOCK:
            LATEST_STATUS.update(payload)
        note_live_update()
        return
    if topic.endswith("/ams/changed"):
        note_live_update()


def build_mqtt_client(source: Any) -> mqtt.Client:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=CONFIG.mqtt_client_id)
    client.user_data_set({"source": source})
    client.on_connect = on_connect
    client.on_message = on_message
    offline = json.dumps(
        {"online": False, "status": "offline", "version": BRIDGE_VERSION, "schema_version": SCHEMA_VERSION},
        separators=(",", ":"),
    )
    client.will_set(bridge_status_topic(), offline, retain=True)
    if CONFIG.mqtt_username:
        client.username_pw_set(CONFIG.mqtt_username, CONFIG.mqtt_password)
    if CONFIG.mqtt_tls:
        if CONFIG.mqtt_tls_insecure:
            LOGGER.warning("MQTT TLS certificate verification is disabled")
            client.tls_set(cert_reqs=ssl.CERT_NONE)
            client.tls_insecure_set(True)
        else:
            client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
    return client


def initial_printer_id(source: Any) -> tuple[str, str]:
    """Return (printer id, how it was chosen). A deleted saved id falls back to env."""
    if not source.uses_bambuddy_mqtt:
        return str(source.resolve_printer_id()), "direct"
    selected = load_selected_printer_id()
    if selected:
        try:
            source.printer_status(selected)
            return selected, "device-selected"
        except requests.HTTPError as exc:
            if http_status(exc) != 404:
                LOGGER.warning("Could not verify saved printer %s: %s", selected, exc)
                return selected, "device-selected"
            LOGGER.warning("Saved printer %s is not in Bambuddy", selected)
        except requests.RequestException as exc:
            LOGGER.warning("Could not verify saved printer %s: %s", selected, exc)
            return selected, "device-selected"
    try:
        resolved = str(source.resolve_printer_id())
    except (requests.RequestException, RuntimeError) as exc:
        LOGGER.warning("Could not resolve a printer yet: %s", exc)
        return "", ""
    if selected and selected != resolved:
        save_selected_printer_id(resolved)
    return resolved, "configured"


def build_data_source(config: Config) -> Any:
    mode = config.data_source
    if mode not in {"auto", "bambuddy", "direct"}:
        raise RuntimeError("DATA_SOURCE must be auto, bambuddy, or direct")
    use_direct = mode == "direct" or (mode == "auto" and bool(config.direct_printer_host))
    if not use_direct:
        return BambuddyClient(config)
    missing = []
    if not config.direct_printer_host:
        missing.append("DIRECT_PRINTER_HOST")
    if not (config.direct_printer_serial or config.h2c_serial):
        missing.append("DIRECT_PRINTER_SERIAL (or H2C_SERIAL)")
    if not config.direct_printer_access_code:
        missing.append("DIRECT_PRINTER_ACCESS_CODE")
    if missing:
        raise RuntimeError(f"Direct printer mode requires: {', '.join(missing)}")
    return DirectPrinterClient(config)


def main() -> None:
    global ACTIVE_PRINTER_ID
    source = build_data_source(CONFIG)
    mqtt_client = build_mqtt_client(source)
    printer_id, origin = initial_printer_id(source)
    ACTIVE_PRINTER_ID = str(printer_id)
    if printer_id and source.uses_bambuddy_mqtt and origin == "configured" and CONFIG.h2c_serial:
        # Live status from the first second, before the printer list confirms the serial.
        remember_printer_serial(mqtt_client, ACTIVE_PRINTER_ID, PRINTER_GENERATION, CONFIG.h2c_serial)
    if printer_id:
        LOGGER.info(
            "Monitoring %s printer id %s (%s, bridge %s schema %s)",
            source.source_name,
            printer_id,
            origin,
            BRIDGE_VERSION,
            SCHEMA_VERSION,
        )
    else:
        LOGGER.warning("No printer resolved yet; retrying (bridge %s)", BRIDGE_VERSION)

    def handle_signal(_signum: int, _frame: Any) -> None:
        STOP_EVENT.set()
        REFRESH_EVENT.set()

    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    if isinstance(source, DirectPrinterClient):
        source.start()
    mqtt_client.connect(CONFIG.mqtt_host, CONFIG.mqtt_port, keepalive=60)
    mqtt_client.loop_start()
    http_server = start_display_http_server()

    retry_seconds = min(15, CONFIG.poll_seconds)
    try:
        next_poll = 0.0
        next_live = 0.0
        while not STOP_EVENT.is_set():
            now = time.monotonic()
            wait_for = 1.0
            if now < next_poll:
                wait_for = min(wait_for, max(0.1, next_poll - now))
            if live_is_pending() and now < next_live:
                wait_for = min(wait_for, max(0.1, next_live - now))
            REFRESH_EVENT.wait(timeout=wait_for)
            REFRESH_EVENT.clear()
            if STOP_EVENT.is_set():
                break

            now = time.monotonic()
            poll_due = now >= next_poll
            force = consume_force()
            live_pending = live_is_pending()
            live_due = live_pending and now >= next_live
            if not poll_due and not force and not live_due:
                continue
            if live_pending and (live_due or poll_due or force):
                consume_live()

            with ACTIVE_PRINTER_LOCK:
                active_id = ACTIVE_PRINTER_ID
            if not active_id:
                try:
                    found = str(source.resolve_printer_id())
                except (requests.RequestException, RuntimeError) as exc:
                    LOGGER.warning("Could not resolve a printer yet: %s", exc)
                    next_poll = time.monotonic() + retry_seconds
                    continue
                with ACTIVE_PRINTER_LOCK:
                    if not ACTIVE_PRINTER_ID:
                        ACTIVE_PRINTER_ID = found
                    active_id = ACTIVE_PRINTER_ID

            full = poll_due or force or not snapshot_ready()
            try:
                ok = refresh_and_publish(
                    source,
                    mqtt_client,
                    active_id,
                    force_refresh=force,
                    live_only=not full,
                    heartbeat=poll_due or force,
                )
            except Exception:
                LOGGER.exception("Poll failed")
                ok = False

            now = time.monotonic()
            if poll_due or force:
                next_poll = now + (CONFIG.poll_seconds if ok else retry_seconds)
            elif not ok:
                next_poll = min(next_poll, now + retry_seconds)
            if ok:
                next_live = now + max(0.5, CONFIG.live_coalesce_seconds)
            elif live_due or force:
                next_live = now + retry_seconds
    finally:
        if http_server is not None:
            http_server.shutdown()
        publish_json(
            mqtt_client,
            bridge_status_topic(),
            {
                "online": False,
                "status": "offline",
                "version": BRIDGE_VERSION,
                "schema_version": SCHEMA_VERSION,
                "updated_at": int(time.time()),
            },
            pace=False,
        )
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        if isinstance(source, DirectPrinterClient):
            source.stop()


if __name__ == "__main__":
    main()
