Here’s a clean “dev on Windows, deploy on Raspberry Pi” architecture that scales nicely without getting fancy too early.

## Architecture (recommended)

**Data flow**

1. **Arduino Nano ESP32** reads sensors over I²C (PAS CO₂, SPS30, BME280)
2. ESP32 **publishes 1 message/min** via **MQTT** over Wi-Fi to a broker
3. A small **ingestion service** (Python) **subscribes** to MQTT and writes rows into **Postgres** (optionally with **TimescaleDB** extension)
4. **Grafana** reads from Postgres and provides dashboards

**Why this works well**

* ESP32 stays simple: read → publish.
* All “data engineering” complexity lives on the Pi, where you have CPU/storage.
* Identical stack runs on Windows (Docker Desktop) and on the Pi (Docker Compose).

---

## Containerized stack (Windows + Raspberry Pi)

Run these containers via Docker Compose:

* **mosquitto** (MQTT broker)
* **postgres** (DB; optional TimescaleDB)
* **grafana** (visualization)
* **ingest-mqtt** (your Python subscriber → DB writer)

You’ll end up with a single `docker compose up -d` for both dev and deployment.

---

## MQTT spec (topics + payload)

### Topics

* Telemetry (published once/min):

  * `iaq/<device_id>/telemetry`
* Status/health (optional, every ~5 min or on boot):

  * `iaq/<device_id>/status`

Example:

* `iaq/nanoesp32_office/telemetry`

### Payload (JSON)

Keep it flat and explicit:

```json
{
  "device_id": "nanoesp32_office",
  "ts": "2025-12-26T23:15:00Z",
  "co2_ppm": 612,
  "temp_c": 22.14,
  "rh_pct": 41.2,
  "pressure_hpa": 1013.6,

  "pm1_0_ugm3": 1.2,
  "pm2_5_ugm3": 2.1,
  "pm4_0_ugm3": 2.9,
  "pm10_ugm3": 3.4,

  "rssi_dbm": -58,
  "uptime_s": 123456
}
```

**Timestamping choice**

* Best: ESP32 syncs NTP and sends `ts` (UTC).
* Also fine: ingestion service stamps server time if `ts` missing.

**MQTT QoS**

* Use **QoS 1** for telemetry (at-least-once). Ingestion should be idempotent-friendly.

---

## Database design (Postgres, Timescale-friendly)

### Table

Single wide table is simplest:

* `iaq_readings`

  * `time timestamptz not null`
  * `device_id text not null`
  * sensor fields (co2/temp/rh/pressure/pm…)
  * optional metadata (rssi, uptime, fw_version)

Indexes:

* `(device_id, time desc)`

If you enable TimescaleDB later, this becomes a hypertable with a one-liner migration.

---

## Visualization layer (Grafana)

* Add Postgres data source
* Dashboard panels:

  * CO₂ (ppm) time series + thresholds
  * PM2.5 + PM10
  * Temp/RH/Pressure
  * “Air quality score” (optional derived query)
  * Device RSSI / uptime (debug reliability)

---

## Project repo structure (practical and clean)

```
co2-sensor-stack/
  README.md
  docs/
    architecture.md
    mqtt_contract.md
    db_schema.md
    deployment_pi.md

  firmware/
    nano-esp32-iaq/
      nano-esp32-iaq.ino
      secrets.example.h
      sensors/
        pasco2.cpp/h
        sps30.cpp/h
        bme280.cpp/h

  backend/
    ingest_mqtt/
      pyproject.toml
      src/ingest_mqtt/
        __init__.py
        main.py              # mqtt subscribe + parse + insert
        db.py                # connection pool, insert logic
        mqtt.py              # callbacks, reconnect, subscribe
        schema.py            # (optional) validation/parsing helpers
      tests/

  infra/
    docker-compose.yml
    mosquitto/
      mosquitto.conf
      passwd               # (optional) mounted secrets
    postgres/
      init/
        001_init.sql       # create tables (+ timescale ext if desired)
    grafana/
      provisioning/
        datasources/
          datasource.yml
        dashboards/
          dashboards.yml
      dashboards/
        iaq_dashboard.json

  scripts/
    dev_up.ps1
    dev_logs.ps1
    pi_install.sh
```

This keeps:

* Arduino code isolated in `firmware/`
* all Docker + deployment in `infra/`
* ingestion app in `backend/`
* dashboards version-controlled in `grafana/`

---

## Concrete specs to implement next

### Firmware requirements (ESP32)

* Wi-Fi connect with retry + backoff
* MQTT connect with LWT (last will) on `.../status`
* Publish telemetry every 60s
* Sensor polling schedule:

  * SPS30 is slowest; read it at its supported cadence (often 1s–10s internal), but publish averaged/latest at 60s
* Include:

  * `device_id`
  * `ts` (NTP)
  * `rssi_dbm`, `uptime_s`
* Optional: publish a short “capabilities” message at boot (which sensors present)

### Ingestion requirements (Python)

* Subscribe `iaq/+/telemetry`
* Validate/parse JSON
* Insert into `iaq_readings`
* Robustness:

  * reconnect MQTT
  * reconnect DB
  * log malformed payloads without crashing

### Infrastructure requirements

* Mosquitto with:

  * persistence enabled
  * optional username/password (recommended even on LAN)
* Postgres volume for durable storage
* Grafana volume for durable dashboards

---

## A starter `docker-compose.yml` (core idea)

```yaml
services:
  mosquitto:
    image: eclipse-mosquitto:2
    ports: ["1883:1883"]
    volumes:
      - ./mosquitto/mosquitto.conf:/mosquitto/config/mosquitto.conf
      - mosq_data:/mosquitto/data
      - mosq_log:/mosquitto/log

  postgres:
    image: postgres:16
    environment:
      POSTGRES_USER: iaq
      POSTGRES_PASSWORD: iaqpass
      POSTGRES_DB: iaq
    ports: ["5432:5432"]
    volumes:
      - pg_data:/var/lib/postgresql/data
      - ./postgres/init:/docker-entrypoint-initdb.d

  grafana:
    image: grafana/grafana:latest
    ports: ["3000:3000"]
    volumes:
      - grafana_data:/var/lib/grafana
      - ./grafana/provisioning:/etc/grafana/provisioning
      - ./grafana/dashboards:/var/lib/grafana/dashboards
    depends_on: [postgres]

  ingest:
    build: ../backend/ingest_mqtt
    environment:
      MQTT_HOST: mosquitto
      MQTT_PORT: "1883"
      MQTT_TOPIC: "iaq/+/telemetry"
      PG_HOST: postgres
      PG_PORT: "5432"
      PG_DB: iaq
      PG_USER: iaq
      PG_PASSWORD: iaqpass
    depends_on: [mosquitto, postgres]

volumes:
  mosq_data: {}
  mosq_log: {}
  pg_data: {}
  grafana_data: {}
```

---

## Suggested milestones (so it stays fun)

1. **MQTT working**: ESP32 publishes → Mosquitto receives (view with `mosquitto_sub`)
2. **DB ingest**: Python service writes rows to Postgres
3. **Grafana dashboard**: basic CO₂ + PM2.5 + temp/RH
4. Hardening: reconnect logic, auth, LWT, simple alerting (Grafana alerts)
5. Nice-to-haves: OTA firmware updates, enclosure, calibration notes, derived AQ metrics
