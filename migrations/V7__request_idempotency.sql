-- Version: 7
-- Description: Add tenant-scoped request idempotency for financial writes

CREATE TYPE request_idempotency_status AS ENUM ('in_progress', 'completed');

CREATE TABLE request_idempotency (
    user_id BIGINT NOT NULL REFERENCES users(id),
    operation VARCHAR(64) NOT NULL,
    idempotency_key VARCHAR(128) NOT NULL,
    request_fingerprint CHAR(64) NOT NULL,
    status request_idempotency_status NOT NULL DEFAULT 'in_progress',
    response_values JSONB,
    created_at TIMESTAMPTZ NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (user_id, operation, idempotency_key),
    CONSTRAINT chk_request_idempotency_key
        CHECK (length(idempotency_key) BETWEEN 1 AND 128),
    CONSTRAINT chk_request_idempotency_fingerprint
        CHECK (request_fingerprint ~ '^[0-9a-f]{64}$'),
    CONSTRAINT chk_request_idempotency_expiry
        CHECK (expires_at > created_at),
    CONSTRAINT chk_request_idempotency_response
        CHECK (
            (status = 'in_progress'::request_idempotency_status AND
             response_values IS NULL) OR
            (status = 'completed'::request_idempotency_status AND
             jsonb_typeof(response_values) = 'object')
        )
);

CREATE INDEX idx_request_idempotency_expires
    ON request_idempotency(expires_at);

ALTER TABLE request_idempotency ENABLE ROW LEVEL SECURITY;
ALTER TABLE request_idempotency FORCE ROW LEVEL SECURITY;
CREATE POLICY rls_request_idempotency ON request_idempotency
    USING (user_id = pfh_current_user_id())
    WITH CHECK (user_id = pfh_current_user_id());
