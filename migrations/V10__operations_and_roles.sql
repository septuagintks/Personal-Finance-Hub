-- Version: 10
-- Description: Persistent authorization roles and controlled outbox retry facts

CREATE TYPE user_role AS ENUM ('user', 'operator');

ALTER TABLE users
    ADD COLUMN role user_role NOT NULL DEFAULT 'user'::user_role;

CREATE INDEX idx_users_role ON users(role, id);

ALTER TYPE audit_actor_type ADD VALUE IF NOT EXISTS 'operator';
ALTER TYPE audit_action ADD VALUE IF NOT EXISTS 'retry';

ALTER TABLE audit_logs
    DROP CONSTRAINT chk_audit_logs_actor,
    ADD COLUMN trace_id VARCHAR(128),
    ADD CONSTRAINT chk_audit_logs_actor
        CHECK (
            (actor_type::text = 'user' AND operator_user_id IS NOT NULL) OR
            (actor_type::text = 'operator' AND operator_user_id IS NOT NULL) OR
            (actor_type::text = 'system' AND operator_user_id IS NULL)
        ),
    ADD CONSTRAINT chk_audit_logs_trace
        CHECK (trace_id IS NULL OR length(trace_id) BETWEEN 1 AND 128);

CREATE INDEX idx_audit_logs_user_view
    ON audit_logs(operator_user_id, id DESC)
    INCLUDE (action, resource_type, resource_id, trace_id, occurred_at)
    WHERE actor_type = 'user'::audit_actor_type;

CREATE TABLE outbox_retry_commands (
    operator_user_id BIGINT NOT NULL REFERENCES users(id),
    idempotency_key VARCHAR(128) NOT NULL,
    outbox_id UUID NOT NULL REFERENCES domain_events_outbox(id),
    trace_id VARCHAR(128) NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (operator_user_id, idempotency_key),
    CONSTRAINT chk_outbox_retry_key
        CHECK (length(idempotency_key) BETWEEN 1 AND 128),
    CONSTRAINT chk_outbox_retry_trace
        CHECK (length(trace_id) BETWEEN 1 AND 128)
);

CREATE INDEX idx_outbox_retry_commands_event
    ON outbox_retry_commands(outbox_id, created_at DESC);

CREATE OR REPLACE FUNCTION pfh_cleanup_expired_request_idempotency(
    requested_limit INTEGER)
RETURNS INTEGER
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
DECLARE
    deleted_count INTEGER;
BEGIN
    IF requested_limit < 1 OR requested_limit > 10000 THEN
        RAISE EXCEPTION 'idempotency cleanup limit is invalid';
    END IF;

    WITH doomed AS (
        SELECT ctid
        FROM public.request_idempotency
        WHERE expires_at <= clock_timestamp()
        ORDER BY expires_at, user_id, operation, idempotency_key
        LIMIT requested_limit
        FOR UPDATE SKIP LOCKED
    )
    DELETE FROM public.request_idempotency AS record
    USING doomed
    WHERE record.ctid = doomed.ctid;

    GET DIAGNOSTICS deleted_count = ROW_COUNT;
    RETURN deleted_count;
END;
$$;

REVOKE ALL ON FUNCTION pfh_cleanup_expired_request_idempotency(INTEGER)
    FROM PUBLIC;

CREATE OR REPLACE FUNCTION pfh_count_expired_request_idempotency()
RETURNS BIGINT
LANGUAGE sql
STABLE
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
    SELECT count(*)
    FROM public.request_idempotency
    WHERE expires_at <= statement_timestamp();
$$;

REVOKE ALL ON FUNCTION pfh_count_expired_request_idempotency()
    FROM PUBLIC;
