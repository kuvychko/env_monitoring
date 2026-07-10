-- 002_tables.sql
-- Current-state tables in the `iaq` schema (consolidates the legacy
-- public-schema init scripts 001-005). Idempotent; instance-agnostic.

SET ROLE iaq_owner;
-- Session search_path for this script: TimescaleDB's functions live in public.
SET search_path = iaq, public;

-- Indoor readings from the ESP32 sensor node (via MQTT)
CREATE TABLE IF NOT EXISTS iaq.iaq_readings (
    time            TIMESTAMPTZ NOT NULL,
    device_id       TEXT NOT NULL,

    -- Environmental sensors
    co2_ppm         INTEGER,
    temp_c          REAL,
    rh_pct          REAL,
    pressure_hpa    REAL,

    -- PM mass concentrations (µg/m³)
    pm1_0_ugm3      REAL,
    pm2_5_ugm3      REAL,
    pm4_0_ugm3      REAL,
    pm10_ugm3       REAL,

    -- PM number concentrations (#/cm³)
    nc0_5_pcm3      REAL,
    nc1_0_pcm3      REAL,
    nc2_5_pcm3      REAL,
    nc4_0_pcm3      REAL,
    nc10_pcm3       REAL,
    typical_size_um REAL,

    -- Device metadata
    rssi_dbm        INTEGER,
    uptime_s        BIGINT,

    -- CO2 sensor diagnostics
    co2_age_s       INTEGER,
    co2_resets      INTEGER,

    -- VOC sensor (SGP40): raw signal and computed VOC Index (1-500)
    voc_raw         INTEGER,
    voc_index       INTEGER
);

SELECT create_hypertable('iaq.iaq_readings', 'time', if_not_exists => TRUE);

CREATE INDEX IF NOT EXISTS idx_iaq_device_time
    ON iaq.iaq_readings (device_id, time DESC);

-- PurpleAir outdoor sensor readings (history endpoint, average=0).
-- Columns limited to what that endpoint actually returns.
CREATE TABLE IF NOT EXISTS iaq.purpleair_readings (
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

    -- Device health (rssi and uptime available at average=0)
    rssi_dbm            INTEGER,
    uptime_s            BIGINT,

    -- Deduplication: history endpoint re-delivers the latest point each poll
    UNIQUE (sensor_index, time)
);

SELECT create_hypertable('iaq.purpleair_readings', 'time', if_not_exists => TRUE);

CREATE INDEX IF NOT EXISTS idx_purpleair_sensor_time
    ON iaq.purpleair_readings (sensor_index, time DESC);

RESET ROLE;
