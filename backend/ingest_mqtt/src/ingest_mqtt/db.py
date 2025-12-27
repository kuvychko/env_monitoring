"""Database connection and insert logic for IAQ readings."""

import logging
import os
from contextlib import contextmanager
from typing import Any

import psycopg
from psycopg_pool import ConnectionPool

logger = logging.getLogger(__name__)

# Database configuration from environment
DB_CONFIG = {
    "host": os.getenv("PG_HOST", "localhost"),
    "port": int(os.getenv("PG_PORT", "5432")),
    "dbname": os.getenv("PG_DB", "iaq"),
    "user": os.getenv("PG_USER", "iaq"),
    "password": os.getenv("PG_PASSWORD", "iaqpass"),
}

# Connection pool (lazy initialized)
_pool: ConnectionPool | None = None


def get_pool() -> ConnectionPool:
    """Get or create the connection pool."""
    global _pool
    if _pool is None:
        conninfo = psycopg.conninfo.make_conninfo(**DB_CONFIG)
        _pool = ConnectionPool(conninfo, min_size=1, max_size=5, open=True)
        logger.info(f"Database pool created: {DB_CONFIG['host']}:{DB_CONFIG['port']}/{DB_CONFIG['dbname']}")
    return _pool


def close_pool() -> None:
    """Close the connection pool."""
    global _pool
    if _pool is not None:
        _pool.close()
        _pool = None
        logger.info("Database pool closed")


@contextmanager
def get_connection():
    """Get a connection from the pool."""
    pool = get_pool()
    with pool.connection() as conn:
        yield conn


# Column order must match INSERT statement
COLUMNS = [
    "time", "device_id",
    "co2_ppm", "temp_c", "rh_pct", "pressure_hpa",
    "pm1_0_ugm3", "pm2_5_ugm3", "pm4_0_ugm3", "pm10_ugm3",
    "nc0_5_pcm3", "nc1_0_pcm3", "nc2_5_pcm3", "nc4_0_pcm3", "nc10_pcm3",
    "typical_size_um", "rssi_dbm", "uptime_s",
    "co2_age_s", "co2_resets",  # CO2 sensor diagnostics
]

INSERT_SQL = f"""
INSERT INTO iaq_readings ({", ".join(COLUMNS)})
VALUES ({", ".join(f"%({c})s" for c in COLUMNS)})
"""


def insert_reading(data: dict[str, Any]) -> bool:
    """
    Insert a single IAQ reading into the database.

    Args:
        data: Dictionary with keys matching COLUMNS

    Returns:
        True if successful, False otherwise
    """
    try:
        with get_connection() as conn:
            with conn.cursor() as cur:
                cur.execute(INSERT_SQL, data)
            conn.commit()
        logger.debug(f"Inserted reading for device {data.get('device_id')}")
        return True
    except psycopg.Error as e:
        logger.error(f"Database insert failed: {e}")
        return False
