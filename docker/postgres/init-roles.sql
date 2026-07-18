\set ON_ERROR_STOP on
\getenv request_user PFH_REQUEST_DB_USER
\getenv request_password PFH_REQUEST_DB_PASSWORD
\getenv background_user PFH_BACKGROUND_DB_USER
\getenv background_password PFH_BACKGROUND_DB_PASSWORD

SELECT (
    :'request_user' <> :'background_user'
    AND :'request_user' <> current_user
    AND :'background_user' <> current_user
    AND NOT EXISTS (
        SELECT 1
        FROM pg_auth_members AS membership
        JOIN pg_roles AS member_role ON member_role.oid = membership.member
        WHERE member_role.rolname IN (:'request_user', :'background_user'))
    AND NOT EXISTS (
        SELECT 1
        FROM pg_roles
        WHERE rolname IN (:'request_user', :'background_user')
          AND (rolsuper OR rolcreatedb OR rolcreaterole OR rolreplication))
) AS pfh_role_preflight_ok
\gset

\if :pfh_role_preflight_ok
\else
\echo 'request/background roles must be distinct, dedicated, and non-administrative'
\quit 1
\endif

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
    'ALTER ROLE %I RESET default_transaction_read_only', :'request_user')
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
SELECT format('REVOKE CREATE ON SCHEMA public FROM %I', :'request_user')
\gexec
SELECT format('REVOKE CREATE ON SCHEMA public FROM %I', :'background_user')
\gexec
SELECT format(
    'GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA public TO %I',
    :'request_user')
\gexec
SELECT format(
    'REVOKE ALL PRIVILEGES ON TABLE public.flyway_schema_history FROM %I',
    :'request_user')
\gexec
SELECT format(
    'GRANT SELECT ON TABLE public.flyway_schema_history TO %I',
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
    'REVOKE ALL PRIVILEGES ON ALL TABLES IN SCHEMA public FROM %I',
    :'background_user')
\gexec
SELECT format(
    'REVOKE ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public FROM %I',
    :'background_user')
\gexec
SELECT format(
    'REVOKE ALL PRIVILEGES ON ALL FUNCTIONS IN SCHEMA public FROM %I',
    :'background_user')
\gexec
SELECT format(
    'GRANT SELECT (currency_code, is_archived) ON public.accounts TO %I',
    :'background_user')
\gexec
SELECT format(
    'GRANT SELECT (base_currency_code) ON public.users TO %I',
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
      AND relnamespace = 'public'::regnamespace
      AND relkind IN ('r', 'p')
      AND relrowsecurity
      AND relforcerowsecurity;
    IF tenant_tables <> 11 THEN
        RAISE EXCEPTION 'expected 11 ENABLE/FORCE RLS tenant tables, got %',
            tenant_tables;
    END IF;
END
$assertions$;
