"""PurpleAir HTTP API client."""

import logging

import requests

logger = logging.getLogger(__name__)

BASE_URL = "https://api.purpleair.com/v1"

# Fields requested from the history endpoint (average=0, real-time ~2-min data).
# The history endpoint uses a columnar response: {"fields": [...], "data": [[...], ...]}.
_HISTORY_FIELDS = ",".join([
    "temperature", "humidity", "pressure", "voc",
    # PM1.0 (µg/m³)
    "pm1.0_atm", "pm1.0_atm_a", "pm1.0_atm_b",
    "pm1.0_cf_1", "pm1.0_cf_1_a", "pm1.0_cf_1_b",
    # PM2.5 (µg/m³)
    "pm2.5_atm", "pm2.5_atm_a", "pm2.5_atm_b",
    "pm2.5_cf_1", "pm2.5_cf_1_a", "pm2.5_cf_1_b",
    "pm2.5_alt", "pm2.5_alt_a", "pm2.5_alt_b",
    # PM10 (µg/m³)
    "pm10.0_atm", "pm10.0_atm_a", "pm10.0_atm_b",
    "pm10.0_cf_1", "pm10.0_cf_1_a", "pm10.0_cf_1_b",
    # Device health (particle counts and confidence not available at average=0)
    "rssi", "uptime",
])

# Minimal fields for the live sensor endpoint (used once at startup for the name).
_SENSOR_FIELDS = "name,last_seen"


def fetch_sensor_meta(
    sensor_index: int, api_key: str, read_key: str | None = None
) -> dict | None:
    """Fetch sensor metadata (name, last_seen). Used once at startup."""
    url = f"{BASE_URL}/sensors/{sensor_index}"
    params: dict = {"fields": _SENSOR_FIELDS}
    if read_key:
        params["read_key"] = read_key
    try:
        resp = requests.get(
            url, headers={"X-API-Key": api_key}, params=params, timeout=30
        )
    except requests.RequestException as e:
        logger.error(f"PurpleAir API request failed: {e}")
        return None
    if resp.status_code != 200:
        logger.error(f"PurpleAir API returned HTTP {resp.status_code}: {resp.text[:200]}")
        return None
    try:
        return resp.json().get("sensor")
    except ValueError:
        return None


def fetch_history(
    sensor_index: int,
    api_key: str,
    start_timestamp: int,
    read_key: str | None = None,
) -> list[dict] | None:
    """
    Fetch historical readings since start_timestamp (Unix seconds).

    Uses average=0 (real-time, ~2-minute resolution — the finest PurpleAir stores).
    Returns a list of row dicts keyed by field name, or None on error.
    The response is columnar; this function converts it to a list of dicts.
    """
    url = f"{BASE_URL}/sensors/{sensor_index}/history"
    params: dict = {
        "fields": _HISTORY_FIELDS,
        "average": 0,
        "start_timestamp": start_timestamp,
    }
    if read_key:
        params["read_key"] = read_key

    try:
        resp = requests.get(
            url, headers={"X-API-Key": api_key}, params=params, timeout=60
        )
    except requests.RequestException as e:
        logger.error(f"PurpleAir history API request failed: {e}")
        return None

    if resp.status_code == 429:
        logger.warning("PurpleAir API rate limit exceeded (429) — will retry next interval")
        return None
    if resp.status_code != 200:
        logger.error(f"PurpleAir history API returned HTTP {resp.status_code}: {resp.text[:200]}")
        return None

    try:
        body = resp.json()
        fields = body["fields"]
        return [dict(zip(fields, row)) for row in body["data"]]
    except (ValueError, KeyError) as e:
        logger.error(f"Failed to parse PurpleAir history response: {e}")
        return None
