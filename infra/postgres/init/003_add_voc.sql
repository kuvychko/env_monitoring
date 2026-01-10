-- Migration: Add VOC sensor columns
-- SGP40 provides raw signal and computed VOC Index (1-500)

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_name = 'iaq_readings' AND column_name = 'voc_raw') THEN
        ALTER TABLE iaq_readings ADD COLUMN voc_raw INTEGER;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_name = 'iaq_readings' AND column_name = 'voc_index') THEN
        ALTER TABLE iaq_readings ADD COLUMN voc_index INTEGER;
    END IF;
END $$;
