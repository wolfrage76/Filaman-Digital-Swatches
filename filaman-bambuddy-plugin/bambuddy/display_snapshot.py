"""Build CrowPanel-shaped display snapshots from Bambuddy status + FilaMan spools.

The Elecrow CrowPanel dashboard historically consumed MQTT topics produced by
bambuddy_bridge.py. This module reproduces that JSON schema so the panel can
poll FilaMan instead:

  POST /api/v1/printers/{id}/driver/action  {"action":"display_snapshot"}

Schema matches bridge GET /display/h2c/state (schema_version 2).
"""

from __future__ import annotations

import time
from datetime import datetime, timedelta, timezone
from typing import Any

SCHEMA_VERSION = 2
AMS2PRO_COUNT = 4
SPOOL_LOW_PERCENT = 30
SPOOL_CRITICAL_PERCENT = 15

SPEED_PRESETS = {
    1: "Silent",
    2: "Standard",
    3: "Sport",
    4: "Ludicrous",
}


def _first(payload: dict[str, Any], keys: tuple[str, ...], default: Any = None) -> Any:
    for key in keys:
        value = payload.get(key)
        if value not in (None, ""):
            return value
    return default


def _deep_get(payload: dict[str, Any], path: tuple[str, ...], default: Any = None) -> Any:
    current: Any = payload
    for key in path:
        if not isinstance(current, dict):
            return default
        current = current.get(key)
    return current if current is not None else default


def _as_float(value: Any) -> float | None:
    try:
        if value in (None, ""):
            return None
        return float(value)
    except (TypeError, ValueError):
        return None


def _as_int_temp(value: Any) -> int | None:
    f = _as_float(value)
    if f is None:
        return None
    return int(round(f))


def normalize_hex_color(value: Any) -> str:
    if not value:
        return "#202020"
    text = str(value).strip()
    if text.startswith("#"):
        text = text[1:]
    # Bambuddy often sends RRGGBBAA
    if len(text) >= 6:
        return f"#{text[:6].upper()}"
    return "#202020"


def extract_ams_units(status: dict[str, Any]) -> list[dict[str, Any]]:
    for candidate in (
        _deep_get(status, ("print", "ams", "ams"), None),
        _deep_get(status, ("ams", "ams"), None),
        status.get("ams"),
        _deep_get(status, ("print", "ams"), None),
    ):
        if isinstance(candidate, dict) and isinstance(candidate.get("ams"), list):
            candidate = candidate["ams"]
        if isinstance(candidate, list) and candidate:
            return [u for u in candidate if isinstance(u, dict)]
    return []


def extract_trays(ams_unit: dict[str, Any]) -> list[dict[str, Any]]:
    for key in ("tray", "trays", "slots"):
        trays = ams_unit.get(key)
        if isinstance(trays, list):
            return [t for t in trays if isinstance(t, dict)]
    return []


def is_ams_ht_unit(ams_unit: dict[str, Any]) -> bool:
    if ams_unit.get("is_ams_ht"):
        return True
    return int(_first(ams_unit, ("id", "ams_id"), 0) or 0) >= 128


def expected_ams_kind(ams_id: int) -> str:
    return "ams_ht" if ams_id >= AMS2PRO_COUNT else "ams2pro"


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


def combine_slot_payload(
    ams_id: int,
    slot_id: int,
    tray: dict[str, Any],
    spool: dict[str, Any] | None,
) -> dict[str, Any]:
    spool = spool or {}
    tray_type = str(_first(tray, ("tray_type", "filament_type"), "") or "")
    empty = not tray_type.strip() and not spool

    material = str(
        _first(
            spool,
            ("material", "material_type", "filament_type"),
            _first(tray, ("tray_type", "filament_type"), ""),
        )
        or ""
    )
    color_name = str(
        _first(spool, ("color_name", "name"), _first(tray, ("tray_id_name", "tray_sub_brands"), ""))
        or ""
    )
    manufacturer = str(_first(spool, ("manufacturer", "brand", "vendor"), "") or "")
    color = normalize_hex_color(
        _first(spool, ("color", "color_hex", "rgba"), _first(tray, ("tray_color", "filament_color"), ""))
    )

    remaining_percent = spool.get("remaining_percent")
    remaining_source = "inventory" if remaining_percent is not None else "unknown"
    if remaining_percent is None:
        tray_remain = _as_float(tray.get("remain"))
        if tray_remain is not None and 0 <= tray_remain <= 100:
            remaining_percent = int(round(tray_remain))
            remaining_source = "ams"

    remaining_grams = spool.get("remaining_grams")
    if remaining_grams is not None:
        try:
            remaining_grams = int(round(float(remaining_grams)))
        except (TypeError, ValueError):
            remaining_grams = None

    tray_state = tray.get("state")
    active = bool(_first(tray, ("active", "is_active"), False)) or tray_state == 27

    rfid = False
    for source in (tray, spool):
        for key in ("tray_uuid", "tag_uid", "rfid_uid", "tag", "uuid"):
            value = source.get(key)
            if value not in (None, "", 0, "0"):
                rfid = True
                break

    return {
        "ams_id": ams_id,
        "ams_kind": expected_ams_kind(ams_id),
        "slot": slot_id,
        "empty": empty,
        "color": color,
        "color_name": color_name,
        "manufacturer": manufacturer,
        "material": material,
        "nozzle_min": _first(
            spool, ("nozzle_min", "nozzle_temp_min"), _first(tray, ("nozzle_temp_min",), None)
        ),
        "nozzle_max": _first(
            spool, ("nozzle_max", "nozzle_temp_max"), _first(tray, ("nozzle_temp_max",), None)
        ),
        "remaining_percent": remaining_percent,
        "remaining_grams": remaining_grams,
        "remaining_source": remaining_source,
        "cost_remaining": spool.get("cost_remaining"),
        "last_used": str(spool.get("last_used") or ""),
        "rfid": rfid,
        "backup_badge": "",
        "active": active,
        "schema_version": SCHEMA_VERSION,
        "filaman_spool_id": spool.get("filaman_spool_id"),
    }


def apply_tray_now(
    payloads: dict[tuple[int, int], dict[str, Any]], status: dict[str, Any]
) -> None:
    tray_now = _first(status, ("tray_now",), None)
    if tray_now in (None, "", 255, 254, -1):
        ams_block = status.get("ams")
        if isinstance(ams_block, dict):
            tray_now = ams_block.get("tray_now")
    if tray_now in (None, "", 255, 254, -1):
        return
    try:
        value = int(tray_now)
    except (TypeError, ValueError):
        return
    if value < 0 or value >= 254:
        return
    # Display ids for regular AMS match raw ids 0..3; HT remapped separately.
    raw_ams = value // 4
    slot_id = value % 4
    for payload in payloads.values():
        payload["active"] = False
    # Prefer matching by raw mapping into display payloads: regular AMS display_id == raw
    target = payloads.get((raw_ams, slot_id))
    if target and not target.get("empty"):
        target["active"] = True


def build_slot_payloads(
    status: dict[str, Any],
    spool_by_slot: dict[str, dict[str, Any]],
) -> tuple[dict[tuple[int, int], dict[str, Any]], dict[int, dict[str, Any]]]:
    """spool_by_slot keys are Bambuddy raw keys: '{ams_id}-{tray_id}'."""
    payloads: dict[tuple[int, int], dict[str, Any]] = {}
    meta: dict[int, dict[str, Any]] = {}

    ams_units = [u for u in extract_ams_units(status) if isinstance(u, dict)]
    regular = sorted(
        (u for u in ams_units if not is_ams_ht_unit(u)),
        key=lambda u: int(_first(u, ("id", "ams_id"), 0) or 0),
    )
    ht = sorted(
        (u for u in ams_units if is_ams_ht_unit(u)),
        key=lambda u: int(_first(u, ("id", "ams_id"), 0) or 0),
    )

    unit_map: list[tuple[int, dict[str, Any]]] = [
        (int(_first(u, ("id", "ams_id"), 0) or 0), u) for u in regular
    ]
    unit_map += [(AMS2PRO_COUNT + i, u) for i, u in enumerate(ht)]

    for display_id, ams_unit in unit_map:
        raw_id = int(_first(ams_unit, ("id", "ams_id"), display_id) or display_id)
        meta[display_id] = {
            "ams_id": display_id,
            "ams_kind": "ams_ht" if is_ams_ht_unit(ams_unit) else "ams2pro",
            "temperature": _first(ams_unit, ("temp", "temperature"), None),
            "humidity": _first(ams_unit, ("humidity", "humidity_raw"), None),
            "dry_status": _first(ams_unit, ("dry_status",), None),
            "dry_target_temp": _first(ams_unit, ("dry_target_temp",), None),
            "dry_time": _first(ams_unit, ("dry_time",), None),
            "schema_version": SCHEMA_VERSION,
        }
        for tray in extract_trays(ams_unit):
            slot_id = int(_first(tray, ("id", "tray_id", "slot"), 0) or 0)
            spool = spool_by_slot.get(f"{raw_id}-{slot_id}") or {}
            payloads[(display_id, slot_id)] = combine_slot_payload(
                display_id, slot_id, tray, spool
            )

    for display_id, ams_unit in unit_map:
        if is_ams_ht_unit(ams_unit):
            payloads.setdefault((display_id, 0), empty_slot_payload(display_id, 0))
        else:
            for slot_id in range(4):
                payloads.setdefault((display_id, slot_id), empty_slot_payload(display_id, slot_id))

    # Backup badges: same material+color elsewhere
    for (ams_id, slot_id), payload in list(payloads.items()):
        if payload.get("empty"):
            continue
        material = str(payload.get("material") or "").lower()
        color = normalize_hex_color(payload.get("color"))
        if not material:
            continue
        for (other_ams, other_slot), other in payloads.items():
            if (other_ams, other_slot) == (ams_id, slot_id) or other.get("empty"):
                continue
            if (
                str(other.get("material") or "").lower() == material
                and normalize_hex_color(other.get("color")) == color
            ):
                unit = chr(ord("A") + other_ams) if 0 <= other_ams < 26 else str(other_ams)
                payload["backup_badge"] = f"Backup {unit}-{other_slot + 1}"
                break

    apply_tray_now(payloads, status)
    return payloads, meta


def build_ams_overview(
    slot_payloads: dict[tuple[int, int], dict[str, Any]],
    meta_payloads: dict[int, dict[str, Any]],
) -> dict[str, Any]:
    units: list[dict[str, Any]] = []
    present_ids = sorted(
        set(meta_payloads.keys()) | {ams_id for (ams_id, _) in slot_payloads.keys()}
    )
    for ams_id in present_ids:
        meta = meta_payloads.get(ams_id, {})
        kind = meta.get("ams_kind") or expected_ams_kind(ams_id)
        if kind == "ams_ht":
            label = f"HT{ams_id - AMS2PRO_COUNT + 1}"
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
        if kind == "ams_ht":
            has_climate = meta.get("temperature") not in (None, "") or meta.get(
                "humidity"
            ) not in (None, "")
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


def _format_remaining_time_minutes(minutes: Any) -> str | None:
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


def _format_eta_clock(remaining_seconds: int | None) -> str:
    if remaining_seconds is None or remaining_seconds < 0:
        return ""
    done = datetime.now(timezone.utc) + timedelta(seconds=remaining_seconds)
    return done.astimezone().strftime("done ~%I:%M %p").replace(" 0", " ").replace("~0", "~")


def _format_hms(status: dict[str, Any]) -> str:
    hms = status.get("hms") or status.get("hms_errors") or status.get("hms_status")
    if hms in (None, "", "ok", "OK", []):
        return ""
    if isinstance(hms, str):
        return hms if hms.lower() != "ok" else ""
    if isinstance(hms, list):
        parts: list[str] = []
        for item in hms[:3]:
            if isinstance(item, dict):
                code = _first(item, ("code", "full_code", "attr"), "")
                msg = _first(item, ("msg", "message", "description", "desc"), "")
                parts.append(str(msg or code or item))
            else:
                parts.append(str(item))
        return " | ".join(parts)
    return str(hms)


def _decode_chamber_light(status: dict[str, Any]) -> str:
    explicit = _first(status, ("chamber_light", "light"), None)
    if explicit is True or str(explicit).lower() in {"on", "1", "true"}:
        return "on"
    if explicit is False or str(explicit).lower() in {"off", "0", "false"}:
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


def _decode_door(status: dict[str, Any]) -> str:
    explicit = _first(status, ("door_open", "door", "door_status"), None)
    if explicit is True or str(explicit).lower() in {"open", "1", "true"}:
        return "open"
    if explicit is False or str(explicit).lower() in {"closed", "0", "false"}:
        return "closed"
    return "unknown"


def _active_tray_summary(
    slot_payloads: dict[tuple[int, int], dict[str, Any]],
) -> dict[str, Any]:
    for (ams_id, slot_id), payload in sorted(slot_payloads.items()):
        if payload.get("active") and not payload.get("empty"):
            unit = chr(ord("A") + ams_id) if 0 <= ams_id < 26 else str(ams_id)
            color_name = payload.get("color_name") or payload.get("material") or "Active"
            material = payload.get("material") or ""
            label = f"{unit}{slot_id + 1} / {color_name}"
            if material and material not in label:
                label += f" {material}"
            rem = ""
            if payload.get("remaining_percent") is not None:
                rem = f"{payload.get('remaining_percent')}%"
            elif payload.get("remaining_grams") is not None:
                rem = f"{payload.get('remaining_grams')}g"
            return {
                "active_color": payload.get("color") or "#202020",
                "active_material": material,
                "active_label": label,
                "active_remaining": rem,
            }
    return {
        "active_color": "#202020",
        "active_material": "",
        "active_label": "",
        "active_remaining": "",
    }


def build_printer_payload(
    status: dict[str, Any],
    slot_payloads: dict[tuple[int, int], dict[str, Any]],
    *,
    printer_name: str = "",
    alert_count: int = 0,
) -> dict[str, Any]:
    current_print = (
        status.get("current_print") if isinstance(status.get("current_print"), dict) else {}
    )
    remaining_minutes = _first(
        status,
        ("remaining_time", "mc_remaining_time"),
        _first(current_print, ("remaining_time",), None),
    )
    remaining_seconds = None
    remaining_minutes_int = None
    if remaining_minutes not in (None, ""):
        try:
            remaining_minutes_int = int(round(float(remaining_minutes)))
            remaining_seconds = remaining_minutes_int * 60
        except (TypeError, ValueError):
            remaining_minutes_int = None

    layer = _first(
        status,
        ("layer_num", "current_layer", "layer"),
        _first(current_print, ("current_layer", "layer_num"), None),
    )
    total_layers = _first(
        status,
        ("total_layers", "total_layer", "total_layer_num"),
        _first(current_print, ("total_layers",), None),
    )
    job_name = _first(
        status,
        ("subtask_name", "gcode_file", "task_name"),
        _first(current_print, ("filename", "name", "subtask_name"), ""),
    )
    if isinstance(job_name, dict):
        job_name = _first(job_name, ("filename", "name", "subtask_name"), "")

    progress_f = _as_float(
        _first(status, ("progress", "mc_percent"), _first(current_print, ("progress",), None))
    )
    progress_int = (
        max(0, min(100, int(round(progress_f)))) if progress_f is not None else None
    )

    speed_level = _first(status, ("speed_level", "spd_lvl"), None)
    try:
        speed_int = int(speed_level) if speed_level is not None else None
    except (TypeError, ValueError):
        speed_int = None

    temps = status.get("temperatures") if isinstance(status.get("temperatures"), dict) else {}
    nozzle = _as_int_temp(_first(temps, ("nozzle",), _first(status, ("nozzle_temper",), None)))
    nozzle_target = _as_int_temp(
        _first(temps, ("nozzle_target",), _first(status, ("nozzle_target_temper",), None))
    )
    bed = _as_int_temp(_first(temps, ("bed",), _first(status, ("bed_temper",), None)))
    bed_target = _as_int_temp(
        _first(temps, ("bed_target",), _first(status, ("bed_target_temper",), None))
    )
    chamber = _as_int_temp(
        _first(temps, ("chamber",), _first(status, ("chamber_temper",), None))
    )
    chamber_target = _as_int_temp(_first(temps, ("chamber_target",), None))

    active = _active_tray_summary(slot_payloads)
    return {
        "schema_version": SCHEMA_VERSION,
        "name": printer_name
        or str(_first(status, ("name", "printer_name", "device_name"), "Printer")),
        "connected": bool(status.get("connected", True)),
        "state": _first(status, ("state", "gcode_state", "status"), "unknown"),
        "job_name": str(job_name or ""),
        "progress": progress_int,
        "remaining_time": _format_remaining_time_minutes(remaining_minutes_int),
        "remaining_minutes": remaining_minutes_int,
        "remaining_seconds": remaining_seconds,
        "elapsed_minutes": None,
        "eta_clock": _format_eta_clock(remaining_seconds),
        "layer": layer,
        "total_layers": total_layers,
        "filament_used": "",
        "nozzle": nozzle,
        "nozzle_target": nozzle_target if nozzle_target else None,
        "nozzle_2": None,
        "nozzle_2_target": None,
        "nozzle_heating": bool(temps.get("nozzle_heating", False)),
        "nozzle_2_heating": False,
        "bed_heating": bool(temps.get("bed_heating", False)),
        "chamber_heating": bool(temps.get("chamber_heating", False)),
        "active_extruder": _first(status, ("active_extruder",), 0),
        "bed": bed,
        "bed_target": bed_target if bed_target else None,
        "chamber": chamber,
        "chamber_target": chamber_target if chamber_target else None,
        "speed_level": speed_int,
        "speed_preset": SPEED_PRESETS.get(speed_int or -1, ""),
        "fan_part": _first(status, ("cooling_fan_speed", "heatbreak_fan_speed"), None),
        "fan_aux": _first(status, ("big_fan1_speed",), None),
        "fan_chamber": _first(status, ("big_fan2_speed",), None),
        "wifi_signal": _first(status, ("wifi_signal",), None),
        "hms": _format_hms(status),
        "next_job": "",
        "queue_depth": 0,
        "alert_count": alert_count,
        "chamber_light": _decode_chamber_light(status),
        "door": _decode_door(status),
        "filament_sensor": "unknown",
        "active_color": active["active_color"],
        "active_material": active["active_material"],
        "active_label": active["active_label"],
        "active_remaining": active.get("active_remaining") or "",
        "updated_at": int(time.time()),
    }


def build_alerts(
    status: dict[str, Any],
    slot_payloads: dict[tuple[int, int], dict[str, Any]],
) -> list[dict[str, Any]]:
    alerts: list[dict[str, Any]] = []
    connected = bool(status.get("connected", True))
    state = str(_first(status, ("state", "gcode_state", "status"), "")).upper()

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
                "detail": str(_first(status, ("subtask_name", "gcode_file"), "Job paused")),
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
                "detail": str(_first(status, ("subtask_name", "gcode_file"), "Check printer")),
                "source": "printer",
                "acknowledged": False,
            }
        )

    hms = status.get("hms") or status.get("hms_errors") or []
    if isinstance(hms, list):
        for index, item in enumerate(hms[:5]):
            if isinstance(item, dict):
                code = _first(item, ("code", "full_code", "attr"), f"hms-{index}")
                msg = _first(item, ("msg", "message", "description", "desc"), "HMS alert")
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

    seen: set[str] = set()
    unique: list[dict[str, Any]] = []
    for alert in alerts:
        alert_id = str(alert.get("id"))
        if alert_id in seen:
            continue
        seen.add(alert_id)
        unique.append(alert)
    return unique[:12]


def build_display_snapshot(
    status: dict[str, Any],
    spool_by_slot: dict[str, dict[str, Any]],
    *,
    filaman_printer_id: int,
    printer_name: str = "",
    plugin_version: str = "1.3.7",
) -> dict[str, Any]:
    """Full CrowPanel /display/h2c/state-compatible payload."""
    slots_map, meta_map = build_slot_payloads(status, spool_by_slot)
    alerts = build_alerts(status, slots_map)
    printer = build_printer_payload(
        status, slots_map, printer_name=printer_name, alert_count=len(alerts)
    )
    overview = build_ams_overview(slots_map, meta_map)
    slots = [slots_map[key] for key in sorted(slots_map.keys())]
    metas = [meta_map[key] for key in sorted(meta_map.keys())]
    return {
        "schema_version": SCHEMA_VERSION,
        "source": "filaman-bambuddy",
        "plugin_version": plugin_version,
        "updated_at": int(time.time()),
        "printer_id": str(filaman_printer_id),
        "bridge": {
            "online": True,
            "status": "online",
            "version": plugin_version,
            "schema_version": SCHEMA_VERSION,
        },
        "printer": printer,
        "alerts": alerts,
        "overview": overview,
        "slots": slots,
        "ams_meta": metas,
    }
