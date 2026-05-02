-- 004_add_purpleair.sql
-- PurpleAir outdoor sensor readings table.
-- Safe to run against an existing DB (uses IF NOT EXISTS throughout).
--
-- To apply to an already-running container:
--   docker exec -i postgres psql -U iaq -d iaq < infra/postgres/init/004_add_purpleair.sql
--
-- Columns limited to what the history endpoint (average=0) actually returns.
-- Moving averages, per-bin particle counts, and extended device health fields
-- are not available at real-time resolution and are omitted intentionally.

CREATE TABLE IF NOT EXISTS purpleair_readings (
    time                TIMESTAMPTZ NOT NULL,
    sensor_index        INTEGER NOT NULL,
    name                TEXT,

    -- Environmental (temperature converted from °F → °C on ingest)
    temp_c              REAL,
    humidity            REAL,
    pressure_hpa        REAL,
    voc                 REAL,

    -- PM1.0 mass concentration (µg/m³), dual-channel A/B + average
    pm1_0_atm           REAL,
    pm1_0_atm_a         REAL,
    pm1_0_atm_b         REAL,
    pm1_0_cf1           REAL,
    pm1_0_cf1_a         REAL,
    pm1_0_cf1_b         REAL,

    -- PM2.5 mass concentration (µg/m³)
    pm2_5_atm           REAL,
    pm2_5_atm_a         REAL,
    pm2_5_atm_b         REAL,
    pm2_5_cf1           REAL,
    pm2_5_cf1_a         REAL,
    pm2_5_cf1_b         REAL,
    pm2_5_alt           REAL,
    pm2_5_alt_a         REAL,
    pm2_5_alt_b         REAL,

    -- PM10.0 mass concentration (µg/m³)
    pm10_0_atm          REAL,
    pm10_0_atm_a        REAL,
    pm10_0_atm_b        REAL,
    pm10_0_cf1          REAL,
    pm10_0_cf1_a        REAL,
    pm10_0_cf1_b        REAL,

    -- Device health (rssi and uptime available at average=0; confidence/flags are not)
    rssi_dbm            INTEGER,
    uptime_s            BIGINT,

    -- Deduplication: history endpoint re-delivers the latest point each poll
    UNIQUE (sensor_index, time)
);

SELECT create_hypertable('purpleair_readings', 'time', if_not_exists => TRUE);

CREATE INDEX IF NOT EXISTS idx_purpleair_sensor_time
    ON purpleair_readings (sensor_index, time DESC);
