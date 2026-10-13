#!/usr/bin/env python3
"""Bridge Bambuddy printer/AMS state into simple retained MQTT display topics."""

from __future__ import annotations

import json
import logging
import os
import signal
import ssl
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


@dataclass(frozen=True)
class Config:
    bambuddy_url: str = os.getenv("BAMBUDDY_URL", "http://localhost:8000")
    bambuddy_api_key: str = os.getenv("BAMBUDDY_API_KEY", "")
    h2c_printer_id: str = os.getenv("H2C_PRINTER_ID", "")
    h2c_serial: str = os.getenv("H2C_SERIAL", "")
    h2c_name: str = os.getenv("H2C_NAME", "H2C")
    mqtt_host: str = os.getenv("MQTT_HOST", "localhost")
    mqtt_port: int = int(os.getenv("MQTT_PORT", "1883"))
    mqtt_username: str = os.getenv("MQTT_USERNAME", "")
    mqtt_password: str = os.getenv("MQTT_PASSWORD", "")
    mqtt_tls: bool = env_bool("MQTT_TLS", False)
    mqtt_client_id: str = os.getenv("MQTT_CLIENT_ID", "bambuddy-h2c-display-bridge")
    bambuddy_topic_prefix: str = os.getenv("BAMBUDDY_TOPIC_PREFIX", "bambuddy").strip("/")
    display_topic_prefix: str = os.getenv("DISPLAY_TOPIC_PREFIX", "display/amsview").strip("/")
    # ETA clock timezone (later configurable via web config). Default US Eastern (EST/EDT).
    display_tz: str = os.getenv("DISPLAY_TZ", "America/New_York")
    poll_seconds: int = int(os.getenv("POLL_SECONDS", "60"))
    ams2pro_count: int = int(os.getenv("AMS2PRO_COUNT", "4"))
    amsht_count: int = int(os.getenv("AMSHT_COUNT", "2"))
    amsht_max: int = int(os.getenv("AMSHT_MAX", "4"))
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
ACTIVE_PRINTER_LOCK = threading.Lock()
# AMS slot/meta topics published last cycle, so we can clear (empty-retain) the
# ones that disappear when AMS units are unplugged or the monitored printer changes.
PUBLISHED_AMS_TOPICS: set[str] = set()
BRIDGE_VERSION = "0.11.0"
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
    try:
        with open(CONFIG.bridge_state_file, "w", encoding="utf-8") as handle:
            json.dump({"printer_id": str(printer_id)}, handle)
    except OSError as exc:
        LOGGER.warning("Could not persist selected printer id: %s", exc)


class BambuddyClient:
    def __init__(self, config: Config) -> None:
        self.config = config
        self.session = requests.Session()
        if config.bambuddy_api_key:
            self.session.headers.update({"X-API-Key": config.bambuddy_api_key})

    def _url(self, path: str) -> str:
        return urljoin(self.config.bambuddy_url.rstrip("/") + "/", f"api/v1/{path.lstrip('/')}")

    def request_json(self, method: str, path: str, **kwargs: Any) -> Any:
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

        for printer in self.list_printers():
            serial = str(printer.get("serial_number", ""))
            name = str(printer.get("name", ""))
            if self.config.h2c_serial and serial == self.config.h2c_serial:
                return str(printer["id"])
            if name == self.config.h2c_name:
                return str(printer["id"])

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

    def queue_depth(self, printer_id: str) -> int:
        try:
            data = self.request_json("GET", "queue/", params={"printer_id": printer_id})
        except requests.HTTPError:
            try:
                data = self.request_json("GET", "queue/")
            except requests.HTTPError:
                return 0
        items: list[dict[str, Any]] = []
        if isinstance(data, list):
            items = [item for item in data if isinstance(item, dict)]
        elif isinstance(data, dict):
            for key in ("items", "queue", "jobs", "data"):
                value = data.get(key)
                if isinstance(value, list):
                    items = [item for item in value if isinstance(item, dict)]
                    break
        count = 0
        for item in items:
            item_printer = item.get("printer_id")
            if item_printer is not None and str(item_printer) != str(printer_id):
                continue
            status = str(item.get("status", "pending")).lower()
            if status in {"pending", "queued", "waiting", ""}:
                count += 1
        return count

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
            return False, f"Command failed: {exc.response.status_code if exc.response else 'error'}"
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
            if isinstance(spool, dict):
                result[(int(entry.get("ams_id", -1)), int(entry.get("tray_id", -1)))] = spool
        return result

    def next_queue_job(self, printer_id: str) -> str:
        """Return a short label for the next pending queue item for this printer."""
        try:
            data = self.request_json("GET", "queue/", params={"printer_id": printer_id})
        except requests.HTTPError:
            try:
                data = self.request_json("GET", "queue/")
            except requests.HTTPError:
                return ""
        items: list[dict[str, Any]] = []
        if isinstance(data, list):
            items = [item for item in data if isinstance(item, dict)]
        elif isinstance(data, dict):
            for key in ("items", "queue", "jobs", "data"):
                value = data.get(key)
                if isinstance(value, list):
                    items = [item for item in value if isinstance(item, dict)]
                    break
        for item in items:
            item_printer = item.get("printer_id")
            if item_printer is not None and str(item_printer) != str(printer_id):
                continue
            status = str(item.get("status", "pending")).lower()
            if status not in {"pending", "queued", "waiting", ""}:
                continue
            name = first_value(item, ("name", "filename", "archive_name", "job_name"), None)
            if not name and isinstance(item.get("archive"), dict):
                name = first_value(item["archive"], ("name", "filename"), None)
            if name:
                return str(name)
        return ""

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
        "color_name": "",
        "manufacturer": "",
        "material": "",
        "nozzle_min": None,
        "nozzle_max": None,
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
    if value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def as_int_temp(value: Any) -> int | None:
    number = as_float(value)
    if number is None:
        return None
    return int(round(number))


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
    if value in (None, ""):
        return ""
    text = str(value).strip()
    ts: int | None = None
    try:
        if text.isdigit():
            ts = int(text)
            if ts > 10_000_000_000:  # ms
                ts //= 1000
        else:
            from datetime import datetime, timezone

            normalized = text.replace("Z", "+00:00")
            dt = datetime.fromisoformat(normalized)
            if dt.tzinfo is None:
                dt = dt.replace(tzinfo=timezone.utc)
            ts = int(dt.timestamp())
    except Exception:
        return text[:16]
    if ts is None:
        return text[:16]
    age = max(0, int(time.time()) - ts)
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
    tray_state = tray.get("state")
    active = bool(first_value(tray, ("active", "is_active"), False)) or tray_state == 27
    return {
        "ams_id": ams_id,
        "ams_kind": expected_ams_kind(ams_id),
        "slot": slot_id,
        "empty": empty,
        "color": normalize_hex_color(color),
        "color_name": str(color_name or ""),
        "manufacturer": str(manufacturer or ""),
        "material": str(material or ""),
        "nozzle_min": first_value(spool, ("nozzle_min", "nozzle_temp_min"), first_value(filament, ("nozzle_temp_min",), first_value(tray, ("nozzle_temp_min",), None))),
        "nozzle_max": first_value(spool, ("nozzle_max", "nozzle_temp_max"), first_value(filament, ("nozzle_temp_max",), first_value(tray, ("nozzle_temp_max",), None))),
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
    if ams_unit.get("is_ams_ht"):
        return True
    return int(first_value(ams_unit, ("id", "ams_id"), 0)) >= 128


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
    regular_units = sorted((u for u in ams_units if not is_ams_ht_unit(u)), key=lambda u: int(first_value(u, ("id", "ams_id"), 0)))
    ht_units = sorted((u for u in ams_units if is_ams_ht_unit(u)), key=lambda u: int(first_value(u, ("id", "ams_id"), 0)))

    # Display ids: AMS 2 Pro keep their raw ids (0-3); AMS-HT raw ids (128+) map to 4, 5, ...
    unit_map: list[tuple[int, dict[str, Any]]] = [(int(first_value(u, ("id", "ams_id"), 0)), u) for u in regular_units]
    unit_map += [(CONFIG.ams2pro_count + index, u) for index, u in enumerate(ht_units)]

    for display_id, ams_unit in unit_map:
        raw_id = int(first_value(ams_unit, ("id", "ams_id"), display_id))
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
            slot_id = int(first_value(tray, ("id", "tray_id", "slot"), 0))
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
                unit = chr(ord("A") + other_ams) if 0 <= other_ams < 26 else str(other_ams)
                payload["backup_badge"] = f"Backup {unit}-{other_slot + 1}"
                break

    apply_tray_now(payloads, status)
    return payloads, meta


def apply_tray_now(payloads: dict[tuple[int, int], dict[str, Any]], status: dict[str, Any]) -> None:
    """Mark the feeding tray using Bambuddy tray_now (ams*4 + slot)."""
    tray_now = first_value(status, ("tray_now",), None)
    if tray_now in (None, "", 255, 254, -1):
        return
    try:
        value = int(tray_now)
    except (TypeError, ValueError):
        return
    if value < 0 or value >= 254:
        return
    ams_id = value // 4
    slot_id = value % 4
    for payload in payloads.values():
        payload["active"] = False
    target = payloads.get((ams_id, slot_id))
    if target and not target.get("empty"):
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


def display_timezone() -> Any:
    """Resolve the configured display timezone, falling back to UTC if unknown."""
    try:
        return ZoneInfo(CONFIG.display_tz)
    except (ZoneInfoNotFoundError, ValueError, KeyError):
        LOGGER.warning("Unknown DISPLAY_TZ %r, falling back to UTC", CONFIG.display_tz)
        return timezone.utc


def format_eta_clock(remaining_seconds: int | None) -> str:
    if remaining_seconds is None or remaining_seconds < 0:
        return ""
    done = datetime.now(timezone.utc) + timedelta(seconds=remaining_seconds)
    done = done.astimezone(display_timezone())
    return done.strftime("done ~%I:%M %p").replace(" 0", " ").replace("~0", "~")


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
            unit = chr(ord("A") + ams_id) if 0 <= ams_id < 26 else str(ams_id)
            color_name = payload.get("color_name") or payload.get("material") or "Active"
            material = payload.get("material") or ""
            label = f"{unit}{slot_id + 1} / {color_name}"
            if material and material not in label:
                label += f" {material}"
            return {
                "active_color": payload.get("color") or "#202020",
                "active_material": material,
                "active_label": label,
                "active_remaining": (
                    f"{payload.get('remaining_percent')}%"
                    if payload.get("remaining_percent") is not None
                    else (f"{payload.get('remaining_grams')}g" if payload.get("remaining_grams") is not None else "")
                ),
            }
    return {"active_color": "#202020", "active_material": "", "active_label": "", "active_remaining": ""}


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
    job_name = first_value(
        status,
        ("subtask_name", "current_print", "gcode_file", "task_name"),
        first_value(current_print, ("filename", "name", "subtask_name"), ""),
    )
    if isinstance(job_name, dict):
        job_name = first_value(job_name, ("filename", "name", "subtask_name"), "")

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

    # Elapsed estimate from archive start if available; otherwise leave null.
    elapsed_minutes = None
    started = first_value(status, ("gcode_start_time", "print_start_time", "start_time"), None)
    if started not in (None, ""):
        try:
            start_ts = float(started)
            if start_ts > 1_000_000_000_000:
                start_ts /= 1000.0
            elapsed_minutes = max(0, int((time.time() - start_ts) / 60))
        except (TypeError, ValueError):
            elapsed_minutes = None

    return {
        "schema_version": SCHEMA_VERSION,
        # Name of the printer actually being polled (falls back to status/env).
        "name": printer_name or str(first_value(status, ("name", "printer_name", "device_name"), CONFIG.h2c_name)),
        "serial": CONFIG.h2c_serial,
        "connected": bool(status.get("connected", True)),
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
        "fan_part": first_value(status, ("cooling_fan_speed", "heatbreak_fan_speed"), None),
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
) -> bool:
    body = json.dumps(payload, separators=(",", ":"), default=str)
    fingerprint = payload_fingerprint(payload)
    if not force and LAST_PUBLISHED.get(topic) == fingerprint:
        return False
    client.publish(topic, body, retain=retain)
    LAST_PUBLISHED[topic] = fingerprint
    # Pace retained floods so the CrowPanel RGB panel isn't blanked by MQTT bursts.
    time.sleep(0.25)
    return True


def clear_retained(client: mqtt.Client, topic: str) -> None:
    """Delete a retained topic (empty payload) so stale AMS units drop off the panel."""
    client.publish(topic, "", retain=True)
    LAST_PUBLISHED.pop(topic, None)
    time.sleep(0.05)


def build_alerts(
    status: dict[str, Any],
    slot_payloads: dict[tuple[int, int], dict[str, Any]],
) -> list[dict[str, Any]]:
    alerts: list[dict[str, Any]] = []
    connected = bool(status.get("connected", True))
    state = str(first_value(status, ("state", "gcode_state", "status"), "")).upper()

    if not connected:
        alerts.append(
            {
                "id": "printer-offline",
                "severity": "danger",
                "title": "Printer offline",
                "detail": "Bambuddy reports the printer is disconnected",
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
        unit = chr(ord("A") + ams_id) if 0 <= ams_id < 26 else str(ams_id)
        label = f"{unit}{slot_id + 1}"
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
                    "material": payload.get("material") or "",
                    "remaining_percent": payload.get("remaining_percent"),
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


def refresh_and_publish(bambuddy: BambuddyClient, mqtt_client: mqtt.Client, printer_id: str, force_refresh: bool = False) -> None:
    if force_refresh:
        bambuddy.refresh_status(printer_id)
        time.sleep(1)

    rest_status = bambuddy.printer_status(printer_id)
    with LATEST_STATUS_LOCK:
        merged_status = {**LATEST_STATUS, **rest_status}

    # Resolve the display name/list of the printer we're actually polling.
    printers: list[dict[str, Any]] = []
    active_name = ""
    try:
        printers = bambuddy.printers_summary(printer_id)
        active_name = next((p["name"] for p in printers if p["id"] == str(printer_id)), "")
    except requests.HTTPError as exc:
        LOGGER.warning("Could not list printers: %s", exc)

    spools = bambuddy.list_spools()
    assignments = bambuddy.list_assignments(printer_id)
    slot_payloads, meta_payloads = build_slot_payloads(merged_status, spools, assignments)
    next_job = bambuddy.next_queue_job(printer_id)
    archive_filament = bambuddy.archive_filament_grams(merged_status.get("current_archive_id"))
    queue_depth = bambuddy.queue_depth(printer_id)
    alerts = build_alerts(merged_status, slot_payloads)
    overview = build_ams_overview(slot_payloads, meta_payloads)

    printer_payload = build_printer_payload(
        merged_status,
        slot_payloads,
        next_job,
        archive_filament,
        queue_depth=queue_depth,
        alert_count=len(alerts),
        printer_name=active_name,
    )
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
    publish_json(mqtt_client, printer_topic(), printer_payload)
    publish_json(
        mqtt_client,
        alerts_topic(),
        {"schema_version": SCHEMA_VERSION, "updated_at": int(time.time()), "alerts": alerts},
    )
    publish_json(mqtt_client, overview_topic(), overview)
    published_slots = 0
    published_meta = 0
    for (ams_id, slot_id), payload in sorted(slot_payloads.items()):
        if publish_json(mqtt_client, slot_topic(ams_id, slot_id), payload):
            published_slots += 1
    for ams_id, payload in sorted(meta_payloads.items()):
        if publish_json(mqtt_client, ams_meta_topic(ams_id), payload):
            published_meta += 1

    # Clear retained topics for AMS units/slots that are no longer present, so the
    # panel drops their nav buttons instead of showing stale placeholders.
    global PUBLISHED_AMS_TOPICS
    current_ams_topics = {slot_topic(a, s) for (a, s) in slot_payloads} | {
        ams_meta_topic(a) for a in meta_payloads
    }
    for stale_topic in PUBLISHED_AMS_TOPICS - current_ams_topics:
        clear_retained(mqtt_client, stale_topic)
    PUBLISHED_AMS_TOPICS = current_ams_topics

    # Selectable-printer list for the on-device picker (fetched up top).
    if printers:
        publish_json(
            mqtt_client,
            printers_topic(),
            {
                "schema_version": SCHEMA_VERSION,
                "updated_at": int(time.time()),
                "active_id": str(printer_id),
                "printers": printers,
            },
        )

    publish_json(
        mqtt_client,
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
    )

    LOGGER.info(
        "Published printer/alerts/overview; slots %d/%d changed, meta %d/%d changed",
        published_slots,
        len(slot_payloads),
        published_meta,
        len(meta_payloads),
    )


def publish_command_response(client: mqtt.Client, request_id: str, action: str, ok: bool, message: str) -> None:
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
    )


def handle_command_request(bambuddy: BambuddyClient, client: mqtt.Client, payload: dict[str, Any]) -> None:
    global ACTIVE_PRINTER_ID
    request_id = str(payload.get("id") or f"cmd-{int(time.time())}")
    action = str(payload.get("action") or "")
    arg = payload.get("arg")
    arg_text = None if arg is None else str(arg)
    if not ACTIVE_PRINTER_ID:
        publish_command_response(client, request_id, action, False, "Printer id not ready")
        return
    ok, message = bambuddy.send_printer_command(ACTIVE_PRINTER_ID, action, arg_text)
    publish_command_response(client, request_id, action, ok, message)
    if ok:
        REFRESH_EVENT.set()


def handle_printer_switch(client: mqtt.Client, new_id: str) -> None:
    """Device selected a different printer to monitor: switch, persist, re-poll."""
    global ACTIVE_PRINTER_ID, PUBLISHED_AMS_TOPICS
    with ACTIVE_PRINTER_LOCK:
        if new_id == ACTIVE_PRINTER_ID:
            return
        LOGGER.info("Switching monitored printer %s -> %s", ACTIVE_PRINTER_ID, new_id)
        ACTIVE_PRINTER_ID = new_id
    save_selected_printer_id(new_id)
    # Drop stale state from the previous printer so it can't bleed into the merge.
    with LATEST_STATUS_LOCK:
        LATEST_STATUS.clear()
    for stale_topic in list(PUBLISHED_AMS_TOPICS):
        clear_retained(client, stale_topic)
    PUBLISHED_AMS_TOPICS = set()
    REFRESH_EVENT.set()


def on_connect(client: mqtt.Client, _userdata: Any, _flags: Any, reason_code: Any, _properties: Any = None) -> None:
    LOGGER.info("Connected to MQTT broker: %s", reason_code)
    serial_filter = CONFIG.h2c_serial if CONFIG.h2c_serial else "+"
    client.subscribe(f"{CONFIG.bambuddy_topic_prefix}/printers/{serial_filter}/status")
    client.subscribe(f"{CONFIG.bambuddy_topic_prefix}/printers/{serial_filter}/ams/changed")
    client.subscribe(command_request_topic())
    client.subscribe(printer_ctrl_topic())
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
    )
    REFRESH_EVENT.set()


def on_message(client: mqtt.Client, userdata: Any, message: mqtt.MQTTMessage) -> None:
    topic = message.topic
    LOGGER.debug("Received %s", topic)
    try:
        payload = json.loads(message.payload.decode("utf-8"))
    except json.JSONDecodeError:
        payload = {}

    if topic == command_request_topic() and isinstance(payload, dict):
        bambuddy = userdata.get("bambuddy") if isinstance(userdata, dict) else None
        if isinstance(bambuddy, BambuddyClient):
            handle_command_request(bambuddy, client, payload)
        return

    if topic == printer_ctrl_topic():
        new_id = ""
        if isinstance(payload, dict):
            new_id = str(first_value(payload, ("id", "printer_id"), ""))
        elif isinstance(payload, (str, int)):
            new_id = str(payload)
        if new_id:
            handle_printer_switch(client, new_id)
        return

    if topic.endswith("/status") and isinstance(payload, dict) and "/command/" not in topic:
        with LATEST_STATUS_LOCK:
            LATEST_STATUS.update(payload)
        return
    REFRESH_EVENT.set()


def build_mqtt_client(bambuddy: BambuddyClient) -> mqtt.Client:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=CONFIG.mqtt_client_id)
    client.user_data_set({"bambuddy": bambuddy})
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
        client.tls_set(cert_reqs=ssl.CERT_NONE)
        client.tls_insecure_set(True)
    return client


def main() -> None:
    global ACTIVE_PRINTER_ID
    bambuddy = BambuddyClient(CONFIG)
    mqtt_client = build_mqtt_client(bambuddy)
    # Device-selected printer (persisted) wins over env/auto-resolve.
    selected = load_selected_printer_id()
    printer_id = selected or bambuddy.resolve_printer_id()
    ACTIVE_PRINTER_ID = str(printer_id)
    LOGGER.info(
        "Monitoring Bambuddy printer id %s%s (bridge %s schema %s)",
        printer_id,
        " (device-selected)" if selected else "",
        BRIDGE_VERSION,
        SCHEMA_VERSION,
    )

    def handle_signal(_signum: int, _frame: Any) -> None:
        STOP_EVENT.set()
        REFRESH_EVENT.set()

    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    mqtt_client.connect(CONFIG.mqtt_host, CONFIG.mqtt_port, keepalive=60)
    mqtt_client.loop_start()
    http_server = start_display_http_server()

    try:
        next_poll = 0.0
        while not STOP_EVENT.is_set():
            now = time.monotonic()
            should_poll = now >= next_poll
            should_refresh = REFRESH_EVENT.wait(timeout=max(0.1, min(1.0, next_poll - now)))
            REFRESH_EVENT.clear()

            if should_poll or should_refresh:
                with ACTIVE_PRINTER_LOCK:
                    active_id = ACTIVE_PRINTER_ID
                refresh_and_publish(bambuddy, mqtt_client, active_id, force_refresh=should_refresh)
                next_poll = time.monotonic() + CONFIG.poll_seconds
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
        )
        mqtt_client.loop_stop()
        mqtt_client.disconnect()


if __name__ == "__main__":
    main()
