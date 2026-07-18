-- Append-only transaction correction relationship.
-- A correction creates a replacement transaction and soft-deletes the
-- original; neither financial fact is edited in place.

CREATE TABLE transaction_corrections (
    original_transaction_id BIGINT PRIMARY KEY,
    replacement_transaction_id BIGINT NOT NULL UNIQUE,
    user_id BIGINT NOT NULL REFERENCES users(id),
    corrected_at TIMESTAMPTZ NOT NULL,
    CONSTRAINT chk_transaction_correction_distinct
        CHECK (original_transaction_id <> replacement_transaction_id),
    CONSTRAINT fk_transaction_correction_original_same_user
        FOREIGN KEY (original_transaction_id, user_id)
        REFERENCES transactions(id, user_id) ON DELETE CASCADE,
    CONSTRAINT fk_transaction_correction_replacement_same_user
        FOREIGN KEY (replacement_transaction_id, user_id)
        REFERENCES transactions(id, user_id) ON DELETE CASCADE
);

CREATE INDEX idx_transaction_corrections_user_time
    ON transaction_corrections(user_id, corrected_at DESC);

ALTER TABLE transaction_corrections ENABLE ROW LEVEL SECURITY;
ALTER TABLE transaction_corrections FORCE ROW LEVEL SECURITY;
CREATE POLICY rls_transaction_corrections ON transaction_corrections
    USING (user_id = pfh_current_user_id())
    WITH CHECK (user_id = pfh_current_user_id());
