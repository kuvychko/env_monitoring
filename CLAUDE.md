# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

IoT environmental monitoring system that collects **indoor** air quality (IAQ) data from Arduino Nano ESP32 sensors and **outdoor** air quality from a PurpleAir sensor, stores everything in TimescaleDB, and visualizes in Grafana.

**Indoor data flow:**
1. Arduino Nano ESP32 reads sensors via I²C (PAS CO₂, SPS30, BME280, SGP40)
2. ESP32 publishes MQTT messages (1/min) over Wi-Fi
3. `ingest_mqtt` Python service subscribes to MQTT and writes to `iaq_readings` table

**Outdoor data flow:**
1. PurpleAir sensor (sensor index <your-sensor-index>, "<your-sensor-name>") reports to PurpleAir cloud (~2 min intervals)
2. `ingest_purpleair` Python service polls PurpleAir history API every 600s with `average=10` (10-min averages)
3. New readings since last DB timestamp are batch-inserted into `purpleair_readings` table
4. `ON CONFLICT DO NOTHING` on `(sensor_index, time)` handles overlaps and restarts safely

**Shared:**
- TimescaleDB (Postgres) stores both tables
- Grafana queries both tables; dashboards include indoor-only, outdoor-only, and comparison views

## Project Structure

```
firmware/nano-esp32-iaq/        # Arduino ESP32 firmware
  nano-esp32-iaq.ino            # Main sketch (Wi-Fi, NTP, MQTT, sensors)
  secrets.example.h             # Template for credentials
  secrets.h                     # (gitignored) Actual credentials

backend/ingest_mqtt/            # Indoor: MQTT → Postgres
  Dockerfile
  pyproject.toml                # deps: paho-mqtt, psycopg[binary,pool]
  src/ingest_mqtt/
    main.py                     # MQTT client + signal handling
    mqtt.py                     # Subscribe, parse payload, map fields
    db.py                       # Connection pool + INSERT into iaq_readings

backend/ingest_purpleair/       # Outdoor: PurpleAir API → Postgres
  Dockerfile
  pyproject.toml                # deps: requests, psycopg[binary,pool]
  src/ingest_purpleair/
    main.py                     # Poll loop + signal handling
    api.py                      # fetch_sensor_meta(), fetch_history()
    db.py                       # Connection pool + batch INSERT into purpleair_readings

infra/
  docker-compose.yml            # All 5 services
  .env                          # Credentials (gitignored)
  mosquitto/                    # Mosquitto config + passwd
  postgres/init/                # SQL migrations (run in order on fresh DB)
    001_schema.sql              # iaq_readings hypertable
    002_add_co2_diagnostics.sql
    003_add_voc.sql
    004_add_purpleair.sql       # purpleair_readings hypertable
    005_purpleair_drop_unavailable_cols.sql  # cleanup migration
  grafana/
    provisioning/               # Auto-provision datasource + dashboards
    dashboards/
      general_overview.json     # Indoor: CO2, VOC, PM, temp, humidity, pressure
      pm_deep_dive.json         # Indoor: particle counts and size distribution
      data_quality.json         # Indoor: RSSI, uptime, reading intervals
      purpleair_outdoor.json    # Outdoor + indoor/outdoor comparisons

docs/                           # Setup and reference documentation
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

# Build and start only the PurpleAir ingest service
docker compose up -d --build ingest_purpleair

# View logs for a specific service
docker compose logs -f ingest_purpleair

# Stop services
docker compose down
```

## MQTT Contract (indoor sensor)

- Topic pattern: `iaq/<device_id>/telemetry`
- Status topic: `iaq/<device_id>/status`
- QoS: 1 (at-least-once)
- Payload fields:
  - `device_id`, `ts` (ISO8601 UTC)
  - `co2_ppm`, `temp_c`, `rh_pct`, `pressure_hpa`
  - `pm1_0_ugm3`, `pm2_5_ugm3`, `pm4_0_ugm3`, `pm10_ugm3`
  - `nc0_5_pcm3`, `nc1_0_pcm3`, `nc2_5_pcm3`, `nc4_0_pcm3`, `nc10_pcm3`, `typical_size_um`
  - `voc_raw`, `voc_index` (SGP40 VOC sensor)
  - `rssi_dbm`, `uptime_s`
  - `co2_age_s`, `co2_resets` (CO2 sensor diagnostics)

## PurpleAir Integration

- **Sensor**: index <your-sensor-index>, name "<your-sensor-name>", private (requires read key)
- **API endpoint**: `GET https://api.purpleair.com/v1/sensors/{sensor_index}/history`
- **Authentication**: `X-API-Key` header (account API key) + `read_key` query param (per-sensor key)
- **Average parameter**: `10` = 10-minute averages (default; tunable via `PURPLEAIR_AVERAGE`)
- **Polling**: every 600s; always fetches history since `MAX(time)` in DB
- **Timestamp convention**: at insert time, averaged rows are re-stamped to the END of their averaging window (PurpleAir labels them with the START). This halves the apparent dashboard lag and matches the "as of" convention. The cursor logic in `main.py` accounts for this: when `average>0`, `start_dt = last_ts` directly (no `+ interval` skip needed, because `last_ts` already equals the next API window's start)
- **First run / gap recovery**: if DB is empty or stale, fetches last `PURPLEAIR_LOOKBACK_HOURS` (default 24h)
- **API points are billed for owner queries too** — every call counts against your purchased point balance, even when polling your own sensor with your own keys. The implementation is tuned to minimize draw (~78k pt/month). See README §"PurpleAir API Costs".

**Fields actually requested** (10 fields, ~16 pt/row at standard pricing):
- Environmental (averaged): `temperature`, `humidity`, `pressure`
- PM (averaged): `pm1.0_atm`, `pm10.0_atm`, `pm2.5_cf_1` (cf_1 is required for the EPA correction in Grafana)
- PM channels: `pm2.5_atm_a`, `pm2.5_atm_b` (for the A/B agreement panel)
- Device: `rssi`, `uptime`

**Fields populated in DB but unused / always NULL** (kept in the schema for backwards compat — see `db.py`):
- `voc`; PM1/PM10 channels and CF=1 variants; PM2.5 CF=1 channels and ALT (averaged + channels); `pm2_5_atm` (the multi-algorithm comparison panel that read it was removed).

## Database

Two TimescaleDB hypertables in the `iaq` database:

| Table | Source | Key | Description |
|-------|--------|-----|-------------|
| `iaq_readings` | MQTT (indoor) | `(device_id, time DESC)` | Indoor sensor readings |
| `purpleair_readings` | PurpleAir API | `UNIQUE(sensor_index, time)`, index `(sensor_index, time DESC)` | Outdoor PurpleAir readings |

**Applying migrations to an existing (running) database:**
```bash
# From infra/ directory on the Pi
docker exec -i postgres psql -U iaq -d iaq < postgres/init/004_add_purpleair.sql
docker exec -i postgres psql -U iaq -d iaq < postgres/init/005_purpleair_drop_unavailable_cols.sql
```

**Backup before schema changes:**
```bash
docker exec postgres pg_dump -U iaq iaq > iaq_backup_$(date +%Y%m%d_%H%M%S).sql
```

## Environment Configuration

All credentials live in `infra/.env` (gitignored). Required variables:

```bash
# PostgreSQL / TimescaleDB
POSTGRES_PASSWORD=...

# Grafana
GRAFANA_ADMIN_PASSWORD=...

# MQTT (indoor sensor broker)
MQTT_USER=iaq
MQTT_PASSWORD=...

# PurpleAir (outdoor sensor)
PURPLEAIR_API_KEY=...          # Account-level read API key from develop.purpleair.com
PURPLEAIR_SENSOR_INDEX=<your-sensor-index>  # Numeric sensor ID
PURPLEAIR_READ_KEY=...         # Per-sensor key (required for private sensors)
```

Optional PurpleAir tuning (set in docker-compose.yml):
- `PURPLEAIR_POLL_INTERVAL` — seconds between history fetches (default: 600)
- `PURPLEAIR_AVERAGE` — minutes of averaging at the API (0=real-time ~2min, 10=10min, 60=hourly; default: 10). Larger values reduce point cost since rows-returned drops linearly.
- `PURPLEAIR_LOOKBACK_HOURS` — how far back to fetch on first run (default: 24)
