-- 002_add_co2_diagnostics.sql
-- Add CO2 sensor diagnostic fields
-- Run manually on existing databases: docker exec postgres psql -U iaq -d iaq -f /docker-entrypoint-initdb.d/002_add_co2_diagnostics.sql

-- Add columns if they don't exist
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name = 'iaq_readings' AND column_name = 'co2_age_s') THEN
        ALTER TABLE iaq_readings ADD COLUMN co2_age_s INTEGER;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_name = 'iaq_readings' AND column_name = 'co2_resets') THEN
        ALTER TABLE iaq_readings ADD COLUMN co2_resets INTEGER;
    END IF;
END $$;
