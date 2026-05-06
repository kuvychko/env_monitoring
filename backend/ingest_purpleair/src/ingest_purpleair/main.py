"""Main entry point for the PurpleAir ingestion service."""

import logging
import os
import signal
import sys
import time
from datetime import datetime, timedelta, timezone

from . import api, db

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger(__name__)

_running = True


def _shutdown(signum, frame):
    global _running
    logger.info("Shutting down...")
    _running = False
    db.close_pool()
    sys.exit(0)


def main() -> None:
    api_key = os.environ.get("PURPLEAIR_API_KEY", "")
    sensor_index_str = os.environ.get("PURPLEAIR_SENSOR_INDEX", "")
    read_key = os.environ.get("PURPLEAIR_READ_KEY") or None
    poll_interval = int(os.environ.get("PURPLEAIR_POLL_INTERVAL", "600"))
    # Resampling interval in minutes (0=real-time, 10=10-min, 60=hourly).
    # Larger values cost fewer points; outdoor air rarely needs sub-10-min fidelity.
    average = int(os.environ.get("PURPLEAIR_AVERAGE", "10"))
    # How far back to fetch on the very first run (empty table)
    lookback_hours = int(os.environ.get("PURPLEAIR_LOOKBACK_HOURS", "24"))

    if not api_key:
        logger.error("PURPLEAIR_API_KEY is not set")
        sys.exit(1)
    if not sensor_index_str:
        logger.error("PURPLEAIR_SENSOR_INDEX is not set")
        sys.exit(1)

    sensor_index = int(sensor_index_str)

    signal.signal(signal.SIGINT, _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    # Fetch sensor name once for logging and DB annotation
    meta = api.fetch_sensor_meta(sensor_index, api_key, read_key)
    sensor_name = meta.get("name") if meta else None
    logger.info(
        f"Starting PurpleAir ingestion for sensor {sensor_index} "
        f"({sensor_name or 'unknown'}) — poll every {poll_interval}s, "
        f"average={average}min"
    )

    while _running:
        last_ts = db.get_last_timestamp(sensor_index)

        if last_ts is None:
            start_dt = datetime.now(timezone.utc) - timedelta(hours=lookback_hours)
            logger.info(
                f"No existing data — fetching last {lookback_hours}h of history "
                f"(since {start_dt.strftime('%Y-%m-%d %H:%M UTC')})"
            )
        else:
            # Stored time is the END of the previous averaging window (re-
            # stamped at insert), which equals the START of the next window
            # in API time. So for average>0 we can fetch from last_ts directly.
            # For average=0 (real-time), bump past the last actual measurement.
            start_dt = last_ts + timedelta(seconds=1 if average == 0 else 0)
            window_s = (datetime.now(timezone.utc) - start_dt).total_seconds()
            logger.info(
                f"Fetching history since {start_dt.strftime('%Y-%m-%d %H:%M:%S UTC')} "
                f"({window_s:.0f}s window)"
            )

        start_unix = int(start_dt.timestamp())
        rows = api.fetch_history(
            sensor_index, api_key, start_unix, read_key, average=average
        )

        if rows is not None:
            inserted = db.insert_readings(
                rows, sensor_index, sensor_name, time_offset_s=average * 60
            )
            logger.info(
                f"Fetched {len(rows)} rows from PurpleAir, inserted {inserted} new"
            )

        time.sleep(poll_interval)


if __name__ == "__main__":
    main()
