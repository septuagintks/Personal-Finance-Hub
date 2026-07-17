-- Append-only transfer aggregate correction relationship.
-- The original group remains as an audit fact while all of its member
-- transactions are soft-deleted and a replacement group is appended.

CREATE TABLE transfer_corrections (
    original_transfer_group_id BIGINT PRIMARY KEY,
    replacement_transfer_group_id BIGINT NOT NULL UNIQUE,
    user_id BIGINT NOT NULL REFERENCES users(id),
    corrected_at TIMESTAMPTZ NOT NULL,
    CONSTRAINT chk_transfer_correction_distinct
        CHECK (original_transfer_group_id <> replacement_transfer_group_id),
    CONSTRAINT fk_transfer_correction_original_same_user
        FOREIGN KEY (original_transfer_group_id, user_id)
        REFERENCES transfer_groups(id, user_id) ON DELETE CASCADE,
    CONSTRAINT fk_transfer_correction_replacement_same_user
        FOREIGN KEY (replacement_transfer_group_id, user_id)
        REFERENCES transfer_groups(id, user_id) ON DELETE CASCADE
);

CREATE INDEX idx_transfer_corrections_user_time
    ON transfer_corrections(user_id, corrected_at DESC);

ALTER TABLE transfer_corrections ENABLE ROW LEVEL SECURITY;
ALTER TABLE transfer_corrections FORCE ROW LEVEL SECURITY;
CREATE POLICY rls_transfer_corrections ON transfer_corrections
    USING (user_id = current_app_user_id())
    WITH CHECK (user_id = current_app_user_id());
