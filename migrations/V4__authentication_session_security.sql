-- Version: 4
-- Description: Complete authentication audit actions and session-family revocation

ALTER TYPE audit_action ADD VALUE IF NOT EXISTS 'register';
ALTER TYPE audit_action ADD VALUE IF NOT EXISTS 'login';
ALTER TYPE audit_action ADD VALUE IF NOT EXISTS 'logout';
ALTER TYPE audit_action ADD VALUE IF NOT EXISTS 'token_refresh';
ALTER TYPE audit_action ADD VALUE IF NOT EXISTS 'security_event';

-- A revoked refresh token remains in refresh_tokens for reuse detection. When a
-- revoked token is presented again, the whole sid is inserted here so every
-- outstanding access token in that session fails closed without enumerating
-- each jti individually.
CREATE TABLE revoked_sessions (
    session_id VARCHAR(64) PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    reason VARCHAR(64) NOT NULL,
    CONSTRAINT chk_revoked_sessions_expiry
        CHECK (expires_at > revoked_at)
);

CREATE INDEX idx_revoked_sessions_user
    ON revoked_sessions(user_id, revoked_at DESC);
CREATE INDEX idx_revoked_sessions_expires
    ON revoked_sessions(expires_at);

CREATE INDEX idx_refresh_tokens_active_session
    ON refresh_tokens(user_id, session_id, expires_at)
    WHERE revoked_at IS NULL;
