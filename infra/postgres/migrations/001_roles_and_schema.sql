-- 001_roles_and_schema.sql
-- Bootstrap the project's schema and least-privilege roles.
--
-- Idempotent: safe to re-run against a fresh bundled database (standalone
-- mode) or a live shared cluster (shared mode). Applied by the migration
-- runner (infra/postgres/migrate.sh), which supplies role passwords as psql
-- variables: iaq_owner_pw, iaq_rw_pw, iaq_ro_pw.
--
-- Roles:
--   iaq_owner  migrations/DDL only; owns the schema and its objects
--   iaq_rw     ingest services (INSERT/UPDATE/DELETE/SELECT)
--   iaq_ro     Grafana and other readers (SELECT only)

CREATE EXTENSION IF NOT EXISTS timescaledb;

-- Create roles only if absent (\gexec runs the generated DDL).
SELECT format('CREATE ROLE iaq_owner LOGIN PASSWORD %L', :'iaq_owner_pw')
WHERE NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'iaq_owner') \gexec
SELECT format('CREATE ROLE iaq_rw LOGIN PASSWORD %L', :'iaq_rw_pw')
WHERE NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'iaq_rw') \gexec
SELECT format('CREATE ROLE iaq_ro LOGIN PASSWORD %L', :'iaq_ro_pw')
WHERE NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'iaq_ro') \gexec

CREATE SCHEMA IF NOT EXISTS iaq AUTHORIZATION iaq_owner;

GRANT USAGE ON SCHEMA iaq TO iaq_rw, iaq_ro;
GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA iaq TO iaq_rw;
GRANT SELECT ON ALL TABLES IN SCHEMA iaq TO iaq_ro;

-- Tables created later by iaq_owner stay reachable without re-granting.
ALTER DEFAULT PRIVILEGES FOR ROLE iaq_owner IN SCHEMA iaq
    GRANT SELECT, INSERT, UPDATE, DELETE ON TABLES TO iaq_rw;
ALTER DEFAULT PRIVILEGES FOR ROLE iaq_owner IN SCHEMA iaq
    GRANT SELECT ON TABLES TO iaq_ro;
ALTER DEFAULT PRIVILEGES FOR ROLE iaq_owner IN SCHEMA iaq
    GRANT USAGE, SELECT ON SEQUENCES TO iaq_rw;

-- Unqualified table names resolve to the project schema for every role.
ALTER ROLE iaq_owner SET search_path = iaq;
ALTER ROLE iaq_rw    SET search_path = iaq;
ALTER ROLE iaq_ro    SET search_path = iaq;
