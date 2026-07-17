\set ON_ERROR_STOP on

SELECT format(
    'CREATE ROLE %I LOGIN PASSWORD %L NOSUPERUSER NOCREATEDB NOCREATEROLE NOINHERIT NOBYPASSRLS',
    :'request_user', :'request_password')
WHERE NOT EXISTS (
    SELECT 1 FROM pg_roles WHERE rolname = :'request_user')
\gexec

SELECT format(
    'ALTER ROLE %I LOGIN PASSWORD %L NOSUPERUSER NOCREATEDB NOCREATEROLE NOINHERIT NOBYPASSRLS',
    :'request_user', :'request_password')
\gexec

SELECT format(
    'CREATE ROLE %I LOGIN PASSWORD %L NOSUPERUSER NOCREATEDB NOCREATEROLE NOINHERIT BYPASSRLS',
    :'background_user', :'background_password')
WHERE NOT EXISTS (
    SELECT 1 FROM pg_roles WHERE rolname = :'background_user')
\gexec

SELECT format(
    'ALTER ROLE %I LOGIN PASSWORD %L NOSUPERUSER NOCREATEDB NOCREATEROLE NOINHERIT BYPASSRLS',
    :'background_user', :'background_password')
\gexec

SELECT format(
    'ALTER ROLE %I SET default_transaction_read_only = on',
    :'background_user')
\gexec

SELECT format(
    'GRANT CONNECT ON DATABASE %I TO %I', current_database(), :'request_user')
\gexec
SELECT format(
    'GRANT CONNECT ON DATABASE %I TO %I', current_database(), :'background_user')
\gexec
SELECT format('GRANT USAGE ON SCHEMA public TO %I', :'request_user')
\gexec
SELECT format('GRANT USAGE ON SCHEMA public TO %I', :'background_user')
\gexec
SELECT format(
    'GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA public TO %I',
    :'request_user')
\gexec
SELECT format(
    'GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO %I',
    :'request_user')
\gexec
SELECT format(
    'GRANT EXECUTE ON FUNCTION public.pfh_cleanup_expired_request_idempotency(INTEGER) TO %I',
    :'request_user')
\gexec
SELECT format(
    'GRANT EXECUTE ON FUNCTION public.pfh_count_expired_request_idempotency() TO %I',
    :'request_user')
\gexec
SELECT format(
    'GRANT SELECT ON public.accounts, public.users TO %I',
    :'background_user')
\gexec

DO $assertions$
DECLARE
    tenant_tables integer;
BEGIN
    SELECT count(*) INTO tenant_tables
    FROM pg_class
    WHERE relname = ANY(ARRAY[
        'accounts', 'categories', 'transactions', 'transfer_groups',
        'transaction_tags', 'transaction_tag_relations',
        'account_balance_cache', 'user_preferences', 'request_idempotency',
        'transaction_corrections', 'transfer_corrections'])
      AND relrowsecurity
      AND relforcerowsecurity;
    IF tenant_tables <> 11 THEN
        RAISE EXCEPTION 'expected 11 ENABLE/FORCE RLS tenant tables, got %',
            tenant_tables;
    END IF;
END
$assertions$;
