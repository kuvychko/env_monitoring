# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

IoT environmental monitoring system that collects indoor air quality (IAQ) data from Arduino Nano ESP32 sensors and stores it for visualization.

**Data Flow:**
1. Arduino Nano ESP32 reads sensors via I²C (PAS CO₂, SPS30, BME280, SGP40)
2. ESP32 publishes MQTT messages (1/min) over Wi-Fi
3. Python ingestion service subscribes to MQTT and writes to Postgres
4. Grafana provides dashboards

## Project Structure

```
firmware/nano-esp32-iaq/     # Arduino ESP32 firmware
  nano-esp32-iaq.ino         # Main sketch (Wi-Fi, NTP, MQTT, sensors)
  secrets.example.h          # Template for credentials
  secrets.h                  # (gitignored) Actual credentials

docs/                        # Setup documentation
  docker_setup_windows.md
  docker_setup_pi.md

backend/ingest_mqtt/         # (planned) Python MQTT→Postgres ingestion
infra/                       # (planned) Docker Compose stack
```

## Firmware Setup

1. Copy `secrets.example.h` to `secrets.h`
2. Fill in Wi-Fi credentials and MQTT broker address
3. Install required Arduino libraries:
   - pas-co2-ino (Infineon XENSIV PAS CO2)
   - Adafruit BME280 Library
   - Sensirion I2C SPS30 (+ Sensirion Core)
   - Sensirion I2C SGP40 (+ Sensirion Core)
   - Sensirion Gas Index Algorithm
   - PubSubClient (MQTT)
4. Upload to Arduino Nano ESP32

## Build & Run Commands

```bash
# Start all services (from infra/ directory)
docker compose up -d

# View logs
docker compose logs -f

# Stop services
docker compose down
```

## MQTT Contract

- Topic pattern: `iaq/<device_id>/telemetry`
- Status topic: `iaq/<device_id>/status`
- QoS: 1 (at-least-once)
- Payload fields:
  - `device_id`, `ts` (ISO8601 UTC)
  - `co2_ppm`, `temp_c`, `rh_pct`, `pressure_hpa`
  - `pm1_0_ugm3`, `pm2_5_ugm3`, `pm4_0_ugm3`, `pm10_ugm3`
  - `nc0_5_pcm3`, `nc1_0_pcm3`, `nc2_5_pcm3`, `nc4_0_pcm3`, `nc10_pcm3`, `typical_size_um`
  - `voc_raw`, `voc_index` (SGP40 VOC sensor - raw SRAW signal and computed index 1-500)
  - `rssi_dbm`, `uptime_s`
  - `co2_age_s`, `co2_resets` (CO2 sensor diagnostics)

## Database

Single table `iaq_readings` in Postgres with index on `(device_id, time desc)`. TimescaleDB-ready.

## Environment Configuration

The ingestion service uses these environment variables:
- `MQTT_HOST`, `MQTT_PORT`, `MQTT_TOPIC`
- `PG_HOST`, `PG_PORT`, `PG_DB`, `PG_USER`, `PG_PASSWORD`
