# Database Schema

## Overview

TimescaleDB (PostgreSQL + time-series extension) stores all IAQ sensor readings.

**Connection details:**
- Host: `localhost` (or `postgres` from Docker network)
- Port: `5432`
- Database: `iaq`
- User: `iaq`
- Password: `iaqpass`

## Table: `iaq_readings`

TimescaleDB hypertable for efficient time-series storage and queries.

| Column | Type | Description |
|--------|------|-------------|
| `time` | TIMESTAMPTZ | Timestamp (UTC) - hypertable partition key |
| `device_id` | TEXT | Sensor device identifier (e.g., `nanoesp32_office`) |
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

## Indexes

| Index | Columns | Purpose |
|-------|---------|---------|
| `idx_iaq_device_time` | `(device_id, time DESC)` | Efficient queries for recent readings by device |

## Example Queries

```sql
-- Latest reading per device
SELECT DISTINCT ON (device_id) *
FROM iaq_readings
ORDER BY device_id, time DESC;

-- Last hour of readings for a device
SELECT *
FROM iaq_readings
WHERE device_id = 'nanoesp32_office'
  AND time > NOW() - INTERVAL '1 hour'
ORDER BY time DESC;

-- Hourly averages
SELECT
    time_bucket('1 hour', time) AS hour,
    device_id,
    AVG(co2_ppm) AS avg_co2,
    AVG(temp_c) AS avg_temp,
    AVG(pm2_5_ugm3) AS avg_pm25
FROM iaq_readings
WHERE time > NOW() - INTERVAL '24 hours'
GROUP BY hour, device_id
ORDER BY hour DESC;
```

## Schema Location

The schema is defined in `infra/postgres/init/001_schema.sql` and auto-deploys when the Postgres container starts with an empty data volume.

## Migrations

For future schema changes:
1. Create `infra/postgres/init/002_description.sql` (or higher number)
2. Note: Init scripts only run on fresh database - for existing deployments, run manually or use a migration tool
