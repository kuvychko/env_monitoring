"""PurpleAir HTTP API client."""

import logging

import requests

logger = logging.getLogger(__name__)

BASE_URL = "https://api.purpleair.com/v1"

# Fields requested from the history endpoint. The response is columnar:
# {"fields": [...], "data": [[...], ...]}.
#
# Field cost rule (PurpleAir API pricing): averaged fields = 2 pt/row,
# channel-specific (*_a/_b) = 1 pt/row. We request only what dashboards display:
#   - temperature/humidity/pressure: outdoor environmental panels
#   - pm1.0_atm, pm10.0_atm: PM1/PM10 panel (averaged only — channels not displayed)
#   - pm2.5_cf_1: required for the EPA correction formula in Grafana
#       (0.524*cf_1 - 0.0862*humidity + 5.75)
#   - pm2.5_atm_a/_b: A/B agreement panel (sensor-health diagnostic)
#   - rssi, uptime: device-health stats
_HISTORY_FIELDS = ",".join([
    "temperature", "humidity", "pressure",
    "pm1.0_atm", "pm10.0_atm",
    "pm2.5_cf_1",
    "pm2.5_atm_a", "pm2.5_atm_b",
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
    average: int = 10,
) -> list[dict] | None:
    """
    Fetch historical readings since start_timestamp (Unix seconds).

    `average` selects the resampling interval in minutes. 0 = real-time (~2-min
    cadence, finest available); 10 = 10-minute averages; 60 = hourly. Larger
    values dramatically reduce point cost since rows-returned drops linearly.
    Returns a list of row dicts keyed by field name, or None on error.
    """
    url = f"{BASE_URL}/sensors/{sensor_index}/history"
    params: dict = {
        "fields": _HISTORY_FIELDS,
        "average": average,
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
