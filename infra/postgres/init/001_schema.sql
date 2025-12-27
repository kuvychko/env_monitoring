-- 001_schema.sql
-- Initial schema for IAQ (Indoor Air Quality) readings
-- Auto-runs on first container start via docker-entrypoint-initdb.d

-- Enable TimescaleDB extension
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- Main readings table
CREATE TABLE iaq_readings (
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
    uptime_s        BIGINT
);

-- Convert to TimescaleDB hypertable for efficient time-series storage
SELECT create_hypertable('iaq_readings', 'time');

-- Index for efficient queries by device and time (DESC for recent-first)
CREATE INDEX idx_iaq_device_time ON iaq_readings (device_id, time DESC);
