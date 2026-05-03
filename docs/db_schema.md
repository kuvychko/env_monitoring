# Database Schema

## Overview

TimescaleDB (PostgreSQL + time-series extension) stores all sensor readings in two hypertables:

| Table | Source | Description |
|-------|--------|-------------|
| `iaq_readings` | Indoor Arduino/MQTT sensor | CO₂, PM, VOC, temperature, humidity, pressure |
| `purpleair_readings` | Outdoor PurpleAir API | PM1/2.5/10, temperature, humidity, pressure |

**Connection details:**
- Host: `localhost` (or `postgres` from Docker network)
- Port: `5432`
- Database: `iaq`
- User: `iaq`

---

## Table: `iaq_readings`

TimescaleDB hypertable. Partitioned by `time`. Data arrives via MQTT ~1/min from the indoor Arduino Nano ESP32.

| Column | Type | Description |
|--------|------|-------------|
| `time` | TIMESTAMPTZ | Timestamp (UTC) — hypertable partition key |
| `device_id` | TEXT | Sensor identifier (e.g. `nanoesp32_office`) |
| `co2_ppm` | INTEGER | CO₂ concentration (ppm) |
| `temp_c` | REAL | Temperature (°C) |
| `rh_pct` | REAL | Relative humidity (%) |
| `pressure_hpa` | REAL | Barometric pressure (hPa) |
| `pm1_0_ugm3` | REAL | PM1.0 mass concentration (µg/m³) |
| `pm2_5_ugm3` | REAL | PM2.5 mass concentration (µg/m³) |
| `pm4_0_ugm3` | REAL | PM4.0 mass concentration (µg/m³) |
| `pm10_ugm3` | REAL | PM10 mass concentration (µg/m³) |
| `nc0_5_pcm3` | REAL | Particles >0.5µm (#/cm³) |
| `nc1_0_pcm3` | REAL | Particles >1.0µm (#/cm³) |
| `nc2_5_pcm3` | REAL | Particles >2.5µm (#/cm³) |
| `nc4_0_pcm3` | REAL | Particles >4.0µm (#/cm³) |
| `nc10_pcm3` | REAL | Particles >10µm (#/cm³) |
| `typical_size_um` | REAL | Typical particle size (µm) |
| `rssi_dbm` | INTEGER | WiFi signal strength (dBm) |
| `uptime_s` | BIGINT | Device uptime (seconds) |
| `co2_age_s` | INTEGER | Age of CO₂ reading (seconds) |
| `co2_resets` | INTEGER | CO₂ sensor soft reset count |
| `voc_raw` | INTEGER | SGP40 raw SRAW signal (0–65535) |
| `voc_index` | INTEGER | SGP40 VOC index (1–500) |

**Index:** `idx_iaq_device_time ON (device_id, time DESC)`

---

## Table: `purpleair_readings`

TimescaleDB hypertable. Partitioned by `time`. Data arrives via PurpleAir HTTP history API (~2-min resolution). Unique constraint on `(sensor_index, time)` enables safe idempotent inserts (`ON CONFLICT DO NOTHING`).

Only columns available from the `average=0` (real-time) history endpoint are included. Moving averages, per-bin particle counts, and extended device health fields are absent from this endpoint and intentionally omitted from the schema.

### Environmental

| Column | Type | Description |
|--------|------|-------------|
| `time` | TIMESTAMPTZ | Reading timestamp (UTC) — from `last_seen`, hypertable key |
| `sensor_index` | INTEGER | PurpleAir numeric sensor ID |
| `name` | TEXT | Sensor name (e.g. "MyBackyard") |
| `temp_c` | REAL | Temperature (°C) — converted from °F on ingest |
| `humidity` | REAL | Relative humidity (%) |
| `pressure_hpa` | REAL | Barometric pressure (hPa) |
| `voc` | REAL | VOC index (if sensor has VOC capability) |

### PM1.0 (µg/m³)

| Column | Description |
|--------|-------------|
| `pm1_0_atm` | Atmospheric correction, channel average |
| `pm1_0_atm_a` / `pm1_0_atm_b` | Per-channel A and B |
| `pm1_0_cf1` | CF=1 correction (EPA indoor standard), channel average |
| `pm1_0_cf1_a` / `pm1_0_cf1_b` | Per-channel A and B |

### PM2.5 (µg/m³)

| Column | Description |
|--------|-------------|
| `pm2_5_atm` | Atmospheric correction — **recommended for outdoor AQI** |
| `pm2_5_atm_a` / `pm2_5_atm_b` | Per-channel A and B |
| `pm2_5_cf1` | CF=1 correction (EPA indoor standard) |
| `pm2_5_cf1_a` / `pm2_5_cf1_b` | Per-channel A and B |
| `pm2_5_alt` | Alternative correction |
| `pm2_5_alt_a` / `pm2_5_alt_b` | Per-channel A and B |

### PM10 (µg/m³)

| Column | Description |
|--------|-------------|
| `pm10_0_atm` | Atmospheric correction, channel average |
| `pm10_0_atm_a` / `pm10_0_atm_b` | Per-channel A and B |
| `pm10_0_cf1` | CF=1 correction, channel average |
| `pm10_0_cf1_a` / `pm10_0_cf1_b` | Per-channel A and B |

### Device Health

| Column | Type | Description |
|--------|------|-------------|
| `rssi_dbm` | INTEGER | WiFi signal strength (dBm) |
| `uptime_s` | BIGINT | Sensor uptime (seconds) |

**Indexes:**
- `UNIQUE (sensor_index, time)` — deduplication
- `idx_purpleair_sensor_time ON (sensor_index, time DESC)` — efficient recent-data queries

---

## Migrations

Init scripts in `infra/postgres/init/` run automatically on a **fresh** database container. For an **existing** running database, apply manually:

```bash
# From infra/ on the Pi
docker exec -i postgres psql -U iaq -d iaq < postgres/init/004_add_purpleair.sql
docker exec -i postgres psql -U iaq -d iaq < postgres/init/005_purpleair_drop_unavailable_cols.sql
```

| File | Description |
|------|-------------|
| `001_schema.sql` | `iaq_readings` hypertable |
| `002_add_co2_diagnostics.sql` | CO₂ diagnostic columns |
| `003_add_voc.sql` | VOC columns |
| `004_add_purpleair.sql` | `purpleair_readings` hypertable |
| `005_purpleair_drop_unavailable_cols.sql` | Remove columns not populated by history API |

---

## Example Queries

```sql
-- Latest indoor reading
SELECT DISTINCT ON (device_id) time, device_id, co2_ppm, temp_c, pm2_5_ugm3
FROM iaq_readings
ORDER BY device_id, time DESC;

-- Latest outdoor reading
SELECT time, pm2_5_atm, temp_c, humidity
FROM purpleair_readings
ORDER BY time DESC LIMIT 1;

-- Hourly indoor averages (TimescaleDB)
SELECT
    time_bucket('1 hour', time) AS hour,
    AVG(co2_ppm)    AS avg_co2,
    AVG(temp_c)     AS avg_temp,
    AVG(pm2_5_ugm3) AS avg_pm25
FROM iaq_readings
WHERE time > NOW() - INTERVAL '24 hours'
GROUP BY hour ORDER BY hour DESC;

-- Indoor vs outdoor PM2.5 comparison
SELECT
    o.time,
    o.pm2_5_atm   AS outdoor_pm25,
    i.pm2_5_ugm3  AS indoor_pm25
FROM purpleair_readings o
JOIN iaq_readings i
  ON i.time BETWEEN o.time - INTERVAL '2 minutes'
                AND o.time + INTERVAL '2 minutes'
WHERE o.time > NOW() - INTERVAL '6 hours'
ORDER BY o.time DESC;

-- Dual-channel agreement check (flag readings where A and B diverge >20%)
SELECT time, pm2_5_atm_a, pm2_5_atm_b,
       ABS(pm2_5_atm_a - pm2_5_atm_b) / NULLIF((pm2_5_atm_a + pm2_5_atm_b) / 2, 0) AS divergence
FROM purpleair_readings
WHERE time > NOW() - INTERVAL '24 hours'
  AND ABS(pm2_5_atm_a - pm2_5_atm_b) / NULLIF((pm2_5_atm_a + pm2_5_atm_b) / 2, 0) > 0.2
ORDER BY time DESC;
```
