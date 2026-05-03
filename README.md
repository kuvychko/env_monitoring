# Indoor + Outdoor Air Quality Monitor

A full-stack IoT air quality monitoring system built around an Arduino Nano ESP32, four I²C sensors, a TFT display, and a Dockerized backend on a Raspberry Pi. Indoor readings arrive via MQTT; outdoor readings come from a PurpleAir node polled through their API. Everything lands in TimescaleDB and is visualized in Grafana.

---

## Origin

This started with curiosity about Infineon's XENSIV PAS CO₂ — a photoacoustic CO₂ sensor — which I bought to see how it worked. That experiment turned into a proper indoor air quality node, and the indoor node eventually got an outdoor counterpart when I added a PurpleAir sensor outside.

The enclosure is still very much TBD. The system is in the "works well enough that I don't want to disturb the breadboard" phase of product development.

![Sensor node on breadboard](docs/images/board.jpg)

---

## What It Measures

### Indoor — Arduino Nano ESP32

| Sensor | Manufacturer | Measurements |
|--------|--------------|-------------|
| XENSIV PAS CO₂ | Infineon | CO₂ (ppm) — photoacoustic |
| SPS30 | Sensirion | PM1.0, PM2.5, PM4.0, PM10 mass (µg/m³); particle number concentrations; typical size (µm) |
| BME280 | Bosch | Temperature (°C), humidity (%), pressure (hPa) |
| SGP40 | Sensirion | VOC index (1–500) |

All four sensors share a single I²C bus. The ESP32 also drives a 160×128 ST7735 TFT display over SPI as a live local dashboard.

### Outdoor — PurpleAir

A PurpleAir node (dual Plantower PMS3003) reports to the PurpleAir cloud at ~2-minute intervals. A backend service polls the PurpleAir history API every 120 seconds and stores the readings locally alongside the indoor data.

---

## System Architecture

```
┌─────────────────────────────────┐       ┌──────────────────────┐
│        Arduino Nano ESP32       │       │   PurpleAir node     │
│                                 │       │  (outdoor, cloud)    │
│  PAS CO₂ ─┐                    │       └──────────┬───────────┘
│  SPS30   ─┤─ I²C ─ firmware    │                  │ PurpleAir API
│  BME280  ─┤         │           │                  │ (HTTPS, 120s)
│  SGP40   ─┘         │           │                  │
│                   TFT display   │    ┌─────────────▼──────────┐
└─────────────┬───────────────────┘    │   ingest_purpleair     │
              │ MQTT / Wi-Fi (1/min)   │   (Python)             │
   ┌──────────▼───────────┐            └─────────────┬──────────┘
   │      Mosquitto        │                          │
   └──────────┬────────────┘           ┌─────────────▼──────────────────────┐
              │                        │           TimescaleDB               │
   ┌──────────▼───────────┐            │  iaq_readings | purpleair_readings  │
   │    ingest_mqtt        ├───────────►                                     │
   │    (Python)           │            └─────────────┬──────────────────────┘
   └───────────────────────┘                          │
                                           ┌──────────▼──────────┐
                                           │       Grafana        │
                                           └─────────────────────┘
```

All backend services run in Docker on a Raspberry Pi. The database is not exposed outside the Docker network.

---

## Dashboards

Four dashboards are auto-provisioned at startup.

![Grafana dashboard list](docs/images/dashboards.png)

| Dashboard | What it shows |
|-----------|---------------|
| **Overview** | CO₂ (color-coded at 800/1200 ppm), PM2.5 (EPA AQI thresholds), all PM sizes, temperature, humidity, pressure |
| **PM Deep Dive** | Number concentrations by particle size (NC0.5 through NC10), typical particle size, total mass |
| **Data Quality** | Wi-Fi RSSI, sensor uptime, reading intervals — for catching sensor faults or connectivity issues |
| **PurpleAir Outdoor** | Outdoor PM, temperature, humidity; indoor vs. outdoor PM2.5 overlay |

![Overview dashboard](docs/images/dashboard_overview.png)

![PurpleAir outdoor dashboard](docs/images/dashboard_purpleair.png)

The PM spike visible in the PurpleAir outdoor view is a soldering session — the indoor SPS30 catches it within seconds.

---

## Quick Start

### Firmware

```bash
cp firmware/nano-esp32-iaq/secrets.example.h firmware/nano-esp32-iaq/secrets.h
# Edit secrets.h: WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PASS, DEVICE_ID
```

Install these libraries via the Arduino Library Manager:

- `pas-co2-ino` (Infineon XENSIV PAS CO₂)
- `Adafruit BME280 Library`
- `Sensirion I2C SPS30` + `Sensirion Core`
- `Sensirion I2C SGP40` + `Sensirion Core`
- `Sensirion Gas Index Algorithm`
- `PubSubClient` (MQTT)
- `Adafruit ST7735 and ST7789 Library` + `Adafruit GFX Library`

Upload to the Arduino Nano ESP32.

### Backend

```bash
cd infra
cp .env.example .env
# Edit .env — see comments in the file for each variable
docker compose up -d
```

Grafana is at `http://<pi-ip>:3000`. Credentials are whatever you set in `.env`.

For remote access, Tailscale works well without any port forwarding — see [docs/security.md](docs/security.md).

### PurpleAir (optional)

If you have a PurpleAir sensor, set `PURPLEAIR_API_KEY`, `PURPLEAIR_SENSOR_INDEX`, and `PURPLEAIR_READ_KEY` in `.env`. On first start, `ingest_purpleair` backfills the last 24 hours automatically. Inserts use `ON CONFLICT DO NOTHING`, so restarts are safe.

You can run without PurpleAir — just remove or comment out the `ingest_purpleair` service in `docker-compose.yml` and ignore the outdoor dashboards.

---

## Hardware Notes

**CO₂ sensor power:** The PAS CO₂ needs both 12V (for the IR emitter) and 3.3V (for logic). The 12V rail was the first surprise when getting it running.

**I²C addresses:** BME280 at `0x77`, SPS30 at `0x69`, PAS CO₂ at `0x28`, SGP40 at `0x59`. All four share one bus at 100 kHz.

**TFT display:** ST7735 (160×128) on SPI. Shows a live summary with color-coded CO₂ and VOC thresholds. Uses a flicker-free update approach that redraws only changed values.

**Firmware design:** Non-blocking polling with independent sensor intervals (SPS30 every 1s, BME280/SGP40 every 6s, CO₂ every 15s, publish every 60s). Ring-buffer averages smooth out noise. Stale readings map to `-1` in the payload, which the ingest service converts to `NULL` before inserting, so bad samples don't corrupt aggregates.

---

## Project Structure

```
firmware/nano-esp32-iaq/        # Arduino firmware
  nano-esp32-iaq.ino
  secrets.example.h             # Copy to secrets.h and fill in

backend/ingest_mqtt/            # MQTT → TimescaleDB
backend/ingest_purpleair/       # PurpleAir API → TimescaleDB

infra/
  docker-compose.yml
  .env.example                  # Copy to .env and fill in
  mosquitto/                    # Broker config + hashed password file
  postgres/init/                # SQL migrations (applied in order on fresh DB)
  grafana/
    provisioning/               # Auto-provision datasource
    dashboards/                 # 4 JSON dashboard definitions

docs/                           # Architecture, schema, deployment, security
```

---

## Observations

**CO₂ as an occupancy signal.** CO₂ is a surprisingly good proxy for what's happening in a room. You can see someone enter, leave, open a door, or crack a window — each event has a recognizable shape in the time series.

**Soldering spikes PM hard.** The SPS30 reacts within seconds to electronics work. A brief soldering session produces a spike that looks far more alarming on the dashboard than it feels at the bench. The session below was done ~3 ft from the sensor, with a fume extractor running — it still pushed indoor PM2.5 to ~240 µg/m³ (well into the "hazardous" AQI range) before recovering in a few minutes.

![PM2.5 soldering spike](docs/images/PM25_soldering_spike.png)

**Indoor and outdoor PM sensors don't agree by default.** PurpleAir uses dual Plantower PMS3003 sensors; the SPS30 is a more expensive, factory-calibrated unit with a different optical geometry and correction factors. Getting them onto the same scale is an interesting measurement problem in itself.

**The value is in correlation.** CO₂ vs. occupancy, indoor PM2.5 vs. outdoor AQI, soldering events vs. recovery time, ventilation changes vs. CO₂ decay rate — none of this is visible until you have the data.

---

## Notes and Caveats

**VOC sensor:** The SGP40 is wired up and the index is stored, but I wouldn't add it again. The VOC index is noisy and hard to interpret without knowing what's actually in the air. Don't expect it to tell you much.

**PM4 and PM10 from the SPS30** are estimates extrapolated from the measured size distribution, not directly counted — the SPS30's optical geometry tops out around 10 µm but the larger bin values are modeled.

**Number concentrations are cumulative:** NC10 ≥ NC4 ≥ NC2.5 ≥ NC1 ≥ NC0.5.

---

## Further Reading

- [Architecture](docs/architecture.md)
- [Sensor details and field reference](docs/sensors.md)
- [Database schema](docs/db_schema.md)
- [Deployment on Raspberry Pi](docs/deployment_raspberry_pi.md)
- [Deployment on Windows (dev)](docs/deployment_windows.md)
- [Security model](docs/security.md)
