# SGP40 VOC Sensor Upgrade Instructions

This document provides step-by-step instructions to upgrade the IAQ monitoring stack with SGP40 VOC sensor support.

## Prerequisites

- SSH access to the Raspberry Pi
- The code changes have been committed and pushed to git
- Arduino IDE installed locally (for firmware flashing)

## Overview of Changes

| Component | Change |
|-----------|--------|
| Firmware | Added SGP40 sensor reading with ring buffer averaging |
| Database | New columns: `voc_raw`, `voc_index` |
| Ingestion | Parse and store VOC fields |
| Grafana | New VOC Index panel, consolidated PM chart |

## Step 1: Deploy Backend Changes (Pi via SSH)

```bash
# SSH into your Pi
ssh pi@<your-pi-ip>

# Navigate to project directory
cd ~/env_monitoring

# Pull latest changes
git pull origin main

# Navigate to infra directory
cd infra
```

## Step 2: Apply Database Migration

The migration is safe to run on a live database - it only adds columns and doesn't modify existing data.

```bash
# Apply the migration
docker compose exec postgres psql -U iaq -d iaq -f /docker-entrypoint-initdb.d/003_add_voc.sql
```

If the migration file isn't mounted, run this instead:

```bash
docker compose exec postgres psql -U iaq -d iaq -c "
DO \$\$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_name = 'iaq_readings' AND column_name = 'voc_raw') THEN
        ALTER TABLE iaq_readings ADD COLUMN voc_raw INTEGER;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_name = 'iaq_readings' AND column_name = 'voc_index') THEN
        ALTER TABLE iaq_readings ADD COLUMN voc_index INTEGER;
    END IF;
END \$\$;
"
```

Verify columns were added:

```bash
docker compose exec postgres psql -U iaq -d iaq -c "\d iaq_readings"
```

You should see `voc_raw` and `voc_index` columns in the output.

## Step 3: Rebuild and Restart Ingestion Service

```bash
# Rebuild the ingestion service with new code
docker compose build ingest

# Restart only the ingestion service (keeps other services running)
docker compose up -d ingest

# Verify it started correctly
docker compose logs -f ingest --tail=30
```

Look for the log message showing successful connection to MQTT and database.

## Step 4: Reload Grafana Dashboards

Dashboards are provisioned from files, so a restart will reload them:

```bash
docker compose restart grafana
```

Alternatively, wait ~5 minutes for Grafana's file watcher to pick up changes.

## Step 5: Flash Arduino Firmware

On your local machine (Windows):

1. Open Arduino IDE
2. Open `firmware/nano-esp32-iaq/nano-esp32-iaq.ino`
3. Install required libraries via Library Manager:
   - Search "Sensirion I2C SGP40" and install
   - Search "Sensirion Gas Index Algorithm" and install
4. Connect Arduino Nano ESP32 via USB
5. Select correct board and port
6. Click Upload

## Step 6: Verify Everything Works

### Check Serial Monitor (Arduino IDE)

Look for:
```
Initializing SGP40...
SGP40 serial: XXXXXXXXXXXX
SGP40 OK (VOC Index needs ~60s conditioning)
```

### Check MQTT Messages

On the Pi:
```bash
docker compose exec mosquitto mosquitto_sub -t "iaq/+/telemetry" -v
```

Look for `voc_raw` and `voc_index` fields in the JSON payload.

### Check Database

```bash
docker compose exec postgres psql -U iaq -d iaq -c "
SELECT time, device_id, voc_raw, voc_index
FROM iaq_readings
ORDER BY time DESC
LIMIT 5;
"
```

### Check Grafana

1. Open Grafana at `http://<pi-ip>:3000`
2. Navigate to Overview dashboard
3. Verify:
   - VOC Index panel shows data (after ~2 minutes)
   - PM chart shows all sizes (PM1.0, PM2.5, PM4.0, PM10)
   - Historical data shows NULL for VOC (expected for old data)

## Troubleshooting

### SGP40 not detected
- Check I2C address: SGP40 should be at 0x59
- Run I2C scanner sketch to verify
- Check wiring

### VOC Index always 0 or unstable
- The VOC algorithm needs ~60 seconds of conditioning after power-on
- Values will stabilize after the first minute

### Database migration fails
- Check if columns already exist: `\d iaq_readings`
- The migration is idempotent and safe to re-run

### Ingestion service not parsing VOC fields
- Check logs: `docker compose logs ingest`
- Verify MQTT message contains `voc_raw` and `voc_index`
- Rebuild if needed: `docker compose build ingest && docker compose up -d ingest`

## VOC Index Interpretation

| VOC Index | Air Quality |
|-----------|-------------|
| 0-100     | Excellent   |
| 100-200   | Good        |
| 200-300   | Moderate    |
| 300-400   | Poor        |
| 400-500   | Bad         |

The VOC Index is computed using Sensirion's Gas Index Algorithm with temperature and humidity compensation from the BME280 sensor.
