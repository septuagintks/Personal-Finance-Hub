-- Version: 6
-- Description: Add reliable outbox leases, idempotent handler receipts,
--              system audit actors, and distributed scheduler leases
-- Runtime role boundary: these operational tables are non-RLS and are written
-- by the ordinary application role in unscoped transactions. The BYPASSRLS
-- background role remains read-only and is not used for these state changes.

CREATE TYPE audit_actor_type AS ENUM ('user', 'system');

ALTER TABLE audit_logs
    ALTER COLUMN operator_user_id DROP NOT NULL,
    ADD COLUMN actor_type audit_actor_type NOT NULL
        DEFAULT 'user'::audit_actor_type,
    ADD CONSTRAINT chk_audit_logs_actor
        CHECK (
            (actor_type = 'user'::audit_actor_type AND
             operator_user_id IS NOT NULL) OR
            (actor_type = 'system'::audit_actor_type AND
             operator_user_id IS NULL)
        );

CREATE INDEX idx_audit_logs_actor_time
    ON audit_logs(actor_type, occurred_at DESC);

ALTER TABLE domain_events_outbox
    ADD COLUMN locked_at TIMESTAMPTZ,
    ADD COLUMN locked_by VARCHAR(128),
    ADD COLUMN claim_token UUID,
    ADD COLUMN last_failed_handler VARCHAR(128),
    ADD COLUMN last_failed_at TIMESTAMPTZ;

-- A deployment can be interrupted while legacy code has rows in processing.
-- Those rows predate lease ownership, so make them retryable before enforcing
-- the V6 lease invariant. Also normalize unconstrained legacy retry counters.
UPDATE domain_events_outbox
SET retry_count = GREATEST(retry_count, 0),
    max_retry_count = GREATEST(max_retry_count, retry_count, 1);

UPDATE domain_events_outbox
SET retry_count = LEAST(retry_count + 1, max_retry_count),
    status = CASE
        WHEN retry_count + 1 >= max_retry_count
            THEN 'dead_letter'::outbox_status
        ELSE 'failed'::outbox_status
    END,
    next_retry_at = NOW(),
    last_error = 'processing lease invalidated by V6 migration',
    last_failed_handler = 'outbox-lease',
    last_failed_at = NOW()
WHERE status = 'processing'::outbox_status;

ALTER TABLE domain_events_outbox
    ADD CONSTRAINT chk_outbox_retry_counts
        CHECK (
            retry_count >= 0 AND
            max_retry_count > 0 AND
            retry_count <= max_retry_count
        ),
    ADD CONSTRAINT chk_outbox_processing_lease
        CHECK (
            (status = 'processing'::outbox_status AND
             locked_at IS NOT NULL AND
             locked_by IS NOT NULL AND
             claim_token IS NOT NULL) OR
            (status <> 'processing'::outbox_status AND
             locked_at IS NULL AND
             locked_by IS NULL AND
             claim_token IS NULL)
        );

CREATE INDEX idx_outbox_processing_lease
    ON domain_events_outbox(locked_at)
    WHERE status = 'processing'::outbox_status;

CREATE TABLE outbox_handler_receipts (
    outbox_id UUID NOT NULL
        REFERENCES domain_events_outbox(id) ON DELETE CASCADE,
    handler_name VARCHAR(128) NOT NULL,
    handled_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (outbox_id, handler_name)
);

CREATE INDEX idx_outbox_handler_receipts_time
    ON outbox_handler_receipts(handled_at DESC);

CREATE TABLE scheduled_job_leases (
    job_name VARCHAR(128) PRIMARY KEY,
    owner_id VARCHAR(128) NOT NULL,
    lease_token UUID NOT NULL,
    lease_until TIMESTAMPTZ NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT chk_scheduled_job_lease_owner
        CHECK (length(owner_id) > 0)
);

CREATE INDEX idx_scheduled_job_leases_expiry
    ON scheduled_job_leases(lease_until);
