-- 005_purpleair_drop_unavailable_cols.sql
-- Remove columns that are never populated by the history endpoint (average=0).
-- Moving averages, per-bin particle counts, and extended device health fields
-- are only available from the live sensor snapshot endpoint, which we don't use.
--
-- To apply to a running container:
--   docker exec -i postgres psql -U iaq -d iaq < infra/postgres/init/005_purpleair_drop_unavailable_cols.sql

ALTER TABLE purpleair_readings
    DROP COLUMN IF EXISTS pm2_5_10min,
    DROP COLUMN IF EXISTS pm2_5_30min,
    DROP COLUMN IF EXISTS pm2_5_60min,
    DROP COLUMN IF EXISTS pm2_5_6hour,
    DROP COLUMN IF EXISTS pm2_5_24hour,
    DROP COLUMN IF EXISTS pm2_5_1week,
    DROP COLUMN IF EXISTS cnt_0_3_um_a,
    DROP COLUMN IF EXISTS cnt_0_5_um_a,
    DROP COLUMN IF EXISTS cnt_1_0_um_a,
    DROP COLUMN IF EXISTS cnt_2_5_um_a,
    DROP COLUMN IF EXISTS cnt_5_0_um_a,
    DROP COLUMN IF EXISTS cnt_10_0_um_a,
    DROP COLUMN IF EXISTS cnt_0_3_um_b,
    DROP COLUMN IF EXISTS cnt_0_5_um_b,
    DROP COLUMN IF EXISTS cnt_1_0_um_b,
    DROP COLUMN IF EXISTS cnt_2_5_um_b,
    DROP COLUMN IF EXISTS cnt_5_0_um_b,
    DROP COLUMN IF EXISTS cnt_10_0_um_b,
    DROP COLUMN IF EXISTS confidence,
    DROP COLUMN IF EXISTS channel_flags,
    DROP COLUMN IF EXISTS channel_state;
