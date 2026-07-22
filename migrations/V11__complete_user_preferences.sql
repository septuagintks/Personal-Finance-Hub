-- Version: 11
-- Description: Complete user preference invariants and bound operational counts

-- number_format was previously a free-form string. Preserve the only supported
-- values and normalize legacy values before installing the closed constraint.
UPDATE user_preferences
SET number_format = '1,234.56', updated_at = NOW()
WHERE number_format NOT IN ('1,234.56', '1.234,56', '1 234,56');

-- The old schema could persist custom without a range, and no released client
-- consumed it. Such rows have no recoverable meaning, so use the documented
-- default before introducing the range columns.
UPDATE user_preferences
SET default_report_period = 'current_month'::report_period,
    updated_at = NOW()
WHERE default_report_period = 'custom'::report_period;

ALTER TABLE user_preferences
    ADD COLUMN custom_report_start_month DATE,
    ADD COLUMN custom_report_end_month DATE,
    ADD CONSTRAINT chk_user_preferences_number_format
        CHECK (number_format IN ('1,234.56', '1.234,56', '1 234,56')),
    ADD CONSTRAINT chk_user_preferences_custom_report_months
        CHECK (
            (
                default_report_period = 'custom'::report_period
                AND custom_report_start_month IS NOT NULL
                AND custom_report_end_month IS NOT NULL
                AND EXTRACT(DAY FROM custom_report_start_month) = 1
                AND EXTRACT(DAY FROM custom_report_end_month) = 1
                AND custom_report_start_month <= custom_report_end_month
                AND custom_report_end_month <
                    custom_report_start_month + INTERVAL '120 months'
            ) OR (
                default_report_period <> 'custom'::report_period
                AND custom_report_start_month IS NULL
                AND custom_report_end_month IS NULL
            )
        );

-- Retention and bounded operations queries must not scan cumulative history.
CREATE INDEX idx_outbox_published_retention
    ON domain_events_outbox(published_at, id)
    WHERE status = 'published'::outbox_status;

CREATE INDEX idx_outbox_status_created
    ON domain_events_outbox(status, created_at DESC, id);

-- Return at most limit + 1 rows. 10001 means "at least 10001" and lets the
-- application expose saturation without counting the whole expired set.
CREATE OR REPLACE FUNCTION pfh_count_expired_request_idempotency()
RETURNS BIGINT
LANGUAGE sql
STABLE
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
    SELECT count(*)
    FROM (
        SELECT 1
        FROM public.request_idempotency
        WHERE expires_at <= statement_timestamp()
        ORDER BY expires_at
        LIMIT 10001
    ) AS bounded_expired;
$$;

REVOKE ALL ON FUNCTION pfh_count_expired_request_idempotency()
    FROM PUBLIC;
