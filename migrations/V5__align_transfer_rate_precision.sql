-- Version: 5
-- Description: Align transfer snapshot rates with the Domain NUMERIC(20,10) boundary

DO $$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM transfer_groups
        WHERE exchange_rate IS NOT NULL
          AND (exchange_rate <= 0 OR ABS(exchange_rate) >= 10000000000)
    ) THEN
        RAISE EXCEPTION
            'transfer_groups contains exchange_rate values outside NUMERIC(20,10)';
    END IF;
END;
$$;

ALTER TABLE transfer_groups
    ALTER COLUMN exchange_rate TYPE NUMERIC(20, 10)
    USING exchange_rate::NUMERIC(20, 10);

ALTER TABLE transfer_groups
    ADD CONSTRAINT chk_transfer_groups_exchange_rate
        CHECK (exchange_rate IS NULL OR exchange_rate > 0);
