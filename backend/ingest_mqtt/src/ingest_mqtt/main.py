"""Main entry point for the MQTT ingestion service."""

import logging
import signal
import sys

from . import db, mqtt

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger(__name__)


def handle_message(data: dict) -> None:
    """Handle a parsed MQTT message by inserting into the database."""
    success = db.insert_reading(data)
    if success:
        logger.info(f"Ingested reading from {data['device_id']} at {data['time']}")


def main() -> None:
    """Main entry point."""
    logger.info("Starting IAQ MQTT ingestion service...")

    # Create MQTT client
    client = mqtt.create_client(on_message=handle_message)

    # Handle graceful shutdown
    def shutdown(signum, frame):
        logger.info("Shutting down...")
        client.disconnect()
        db.close_pool()
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # Run the MQTT client (blocking)
    try:
        mqtt.run_client(client)
    except KeyboardInterrupt:
        shutdown(None, None)
    except Exception as e:
        logger.error(f"Fatal error: {e}")
        db.close_pool()
        sys.exit(1)


if __name__ == "__main__":
    main()
