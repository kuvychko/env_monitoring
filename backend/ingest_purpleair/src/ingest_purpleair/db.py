"""Database connection and insert logic for PurpleAir readings."""

import logging
import os
from contextlib import contextmanager
from datetime import datetime, timezone
from typing import Any

import psycopg
from psycopg_pool import ConnectionPool

logger = logging.getLogger(__name__)

DB_CONFIG = {
    "host": os.getenv("PG_HOST", "localhost"),
    "port": int(os.getenv("PG_PORT", "5432")),
    "dbname": os.getenv("PG_DB", "iaq"),
    "user": os.getenv("PG_USER", "iaq"),
    "password": os.getenv("PG_PASSWORD", "iaqpass"),
}

_pool: ConnectionPool | None = None


def get_pool() -> ConnectionPool:
    global _pool
    if _pool is None:
        conninfo = psycopg.conninfo.make_conninfo(**DB_CONFIG)
        _pool = ConnectionPool(conninfo, min_size=1, max_size=5, open=True)
        logger.info(
            f"Database pool created: {DB_CONFIG['host']}:{DB_CONFIG['port']}/{DB_CONFIG['dbname']}"
        )
    return _pool


def close_pool() -> None:
    global _pool
    if _pool is not None:
        _pool.close()
        _pool = None
        logger.info("Database pool closed")


@contextmanager
def get_connection():
    pool = get_pool()
    with pool.connection() as conn:
        yield conn


def _f(row: dict, key: str) -> float | None:
    val = row.get(key)
    if val is None:
        return None
    try:
        return float(val)
    except (TypeError, ValueError):
        return None


def _i(row: dict, key: str) -> int | None:
    val = row.get(key)
    if val is None:
        return None
    try:
        return int(val)
    except (TypeError, ValueError):
        return None


COLUMNS = [
    "time", "sensor_index", "name",
    "temp_c", "humidity", "pressure_hpa", "voc",
    "pm1_0_atm", "pm1_0_atm_a", "pm1_0_atm_b",
    "pm1_0_cf1", "pm1_0_cf1_a", "pm1_0_cf1_b",
    "pm2_5_atm", "pm2_5_atm_a", "pm2_5_atm_b",
    "pm2_5_cf1", "pm2_5_cf1_a", "pm2_5_cf1_b",
    "pm2_5_alt", "pm2_5_alt_a", "pm2_5_alt_b",
    "pm10_0_atm", "pm10_0_atm_a", "pm10_0_atm_b",
    "pm10_0_cf1", "pm10_0_cf1_a", "pm10_0_cf1_b",
    "rssi_dbm", "uptime_s",
]

# ON CONFLICT DO NOTHING handles overlap: the history endpoint always
# re-delivers the most recent point, and we may re-fetch after restarts.
INSERT_SQL = f"""
INSERT INTO purpleair_readings ({", ".join(COLUMNS)})
VALUES ({", ".join(f"%({c})s" for c in COLUMNS)})
ON CONFLICT (sensor_index, time) DO NOTHING
"""


def map_history_row(row: dict, sensor_index: int, name: str | None = None) -> dict[str, Any]:
    """Map one history API row dict to DB column values."""
    ts = row.get("time_stamp")
    time = (
        datetime.fromtimestamp(ts, tz=timezone.utc)
        if ts
        else datetime.now(timezone.utc)
    )
    temp_f = _f(row, "temperature")
    temp_c = (temp_f - 32) * 5 / 9 if temp_f is not None else None

    return {
        "time": time,
        "sensor_index": sensor_index,
        "name": name,
        "temp_c": temp_c,
        "humidity": _f(row, "humidity"),
        "pressure_hpa": _f(row, "pressure"),
        "voc": _f(row, "voc"),
        # PM1.0
        "pm1_0_atm":   _f(row, "pm1.0_atm"),
        "pm1_0_atm_a": _f(row, "pm1.0_atm_a"),
        "pm1_0_atm_b": _f(row, "pm1.0_atm_b"),
        "pm1_0_cf1":   _f(row, "pm1.0_cf_1"),
        "pm1_0_cf1_a": _f(row, "pm1.0_cf_1_a"),
        "pm1_0_cf1_b": _f(row, "pm1.0_cf_1_b"),
        # PM2.5
        "pm2_5_atm":   _f(row, "pm2.5_atm"),
        "pm2_5_atm_a": _f(row, "pm2.5_atm_a"),
        "pm2_5_atm_b": _f(row, "pm2.5_atm_b"),
        "pm2_5_cf1":   _f(row, "pm2.5_cf_1"),
        "pm2_5_cf1_a": _f(row, "pm2.5_cf_1_a"),
        "pm2_5_cf1_b": _f(row, "pm2.5_cf_1_b"),
        "pm2_5_alt":   _f(row, "pm2.5_alt"),
        "pm2_5_alt_a": _f(row, "pm2.5_alt_a"),
        "pm2_5_alt_b": _f(row, "pm2.5_alt_b"),
        # PM10
        "pm10_0_atm":   _f(row, "pm10.0_atm"),
        "pm10_0_atm_a": _f(row, "pm10.0_atm_a"),
        "pm10_0_atm_b": _f(row, "pm10.0_atm_b"),
        "pm10_0_cf1":   _f(row, "pm10.0_cf_1"),
        "pm10_0_cf1_a": _f(row, "pm10.0_cf_1_a"),
        "pm10_0_cf1_b": _f(row, "pm10.0_cf_1_b"),
        # Device health
        "rssi_dbm": _i(row, "rssi"),
        "uptime_s": _i(row, "uptime"),
    }


def get_last_timestamp(sensor_index: int) -> datetime | None:
    """Return the most recent ingested timestamp for this sensor, or None."""
    sql = "SELECT MAX(time) FROM purpleair_readings WHERE sensor_index = %(sensor_index)s"
    try:
        with get_connection() as conn:
            with conn.cursor() as cur:
                cur.execute(sql, {"sensor_index": sensor_index})
                row = cur.fetchone()
                return row[0] if row and row[0] else None
    except psycopg.Error as e:
        logger.error(f"Failed to query last timestamp: {e}")
        return None


def insert_readings(rows: list[dict], sensor_index: int, name: str | None = None) -> int:
    """
    Batch-insert history rows into the DB.

    Skips duplicates via ON CONFLICT DO NOTHING.
    Returns the number of rows actually inserted.
    """
    if not rows:
        return 0
    mapped = [map_history_row(r, sensor_index, name) for r in rows]
    try:
        with get_connection() as conn:
            with conn.cursor() as cur:
                cur.executemany(INSERT_SQL, mapped)
                inserted = cur.rowcount
            conn.commit()
        return inserted if inserted >= 0 else len(mapped)
    except psycopg.Error as e:
        logger.error(f"Database batch insert failed: {e}")
        return 0
