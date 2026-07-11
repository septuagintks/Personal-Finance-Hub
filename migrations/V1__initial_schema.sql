-- Version: 1
-- Description: Phase 1 initial schema for Personal Finance Hub
-- Backend: PostgreSQL 16+
--
-- Tables:
--   currencies, users, user_preferences, accounts, system_category_templates,
--   categories, transfer_groups, transactions, transaction_tags,
--   transaction_tag_relations, account_balance_cache, exchange_rates,
--   audit_logs, domain_events_outbox, refresh_tokens, revoked_access_tokens
--
-- Constraints:
--   - Amounts: NUMERIC(20,8)
--   - Exchange rates: NUMERIC(20,10)
--   - Cross-user data tables include user_id or enforce isolation via FK
--   - exchange_rates is append-only (INSERT only; no UPDATE/DELETE triggers optional)

-- =============================================================================
-- Enums
-- =============================================================================

CREATE TYPE theme_mode AS ENUM (
    'system',
    'light',
    'dark'
);

CREATE TYPE default_home_page AS ENUM (
    'dashboard',
    'transactions',
    'reports',
    'accounts'
);

CREATE TYPE report_period AS ENUM (
    'current_month',
    'last_month',
    'last_3_months',
    'current_year',
    'custom'
);

CREATE TYPE account_type AS ENUM (
    'cash',
    'savings',
    'credit',
    'digital_wallet',
    'investment',
    'crypto',
    'other'
);

CREATE TYPE account_category AS ENUM (
    'asset',
    'liability'
);

CREATE TYPE transaction_type AS ENUM (
    'income',
    'expense',
    'transfer',
    'adjustment'
);

CREATE TYPE category_source AS ENUM (
    'system',
    'user'
);

-- Category boards used by transactions. Fee-like adjustments use expense board.
CREATE TYPE category_board AS ENUM (
    'income',
    'expense'
);

CREATE TYPE audit_action AS ENUM (
    'create',
    'update',
    'archive',
    'delete',
    'dangerous_delete',
    'sync_import',
    'refresh'
);

CREATE TYPE outbox_status AS ENUM (
    'pending',
    'processing',
    'published',
    'failed',
    'dead_letter'
);

-- =============================================================================
-- currencies
-- Must be created before users/accounts because of FK references.
-- code is VARCHAR(10) to allow controlled crypto tickers (BTC, USDT, WBTC).
-- =============================================================================

CREATE TABLE currencies (
    code VARCHAR(10) PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    display_name VARCHAR(64) NOT NULL,
    symbol VARCHAR(16) NOT NULL,
    precision SMALLINT NOT NULL DEFAULT 2,
    is_crypto BOOLEAN NOT NULL DEFAULT FALSE,
    is_enabled BOOLEAN NOT NULL DEFAULT TRUE,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CHECK (precision >= 0 AND precision <= 12)
);

-- =============================================================================
-- users
-- =============================================================================

CREATE TABLE users (
    id BIGSERIAL PRIMARY KEY,
    username VARCHAR(64) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    base_currency_code VARCHAR(10) NOT NULL REFERENCES currencies(code),
    categories_initialized BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT uq_users_username UNIQUE (username)
);

-- =============================================================================
-- user_preferences
-- Domain UserPreference is composed from users.base_currency_code + this table.
-- =============================================================================

CREATE TABLE user_preferences (
    user_id BIGINT PRIMARY KEY REFERENCES users(id),
    base_currency_code VARCHAR(10) NOT NULL REFERENCES currencies(code),
    locale VARCHAR(16) NOT NULL DEFAULT 'zh-CN',
    timezone VARCHAR(64) NOT NULL DEFAULT 'Asia/Shanghai',
    date_format VARCHAR(32) NOT NULL DEFAULT 'YYYY-MM-DD',
    number_format VARCHAR(32) NOT NULL DEFAULT '1,234.56',
    theme theme_mode NOT NULL DEFAULT 'system',
    default_home_page default_home_page NOT NULL DEFAULT 'dashboard',
    default_report_period report_period NOT NULL DEFAULT 'current_month',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- =============================================================================
-- accounts
-- version supports optimistic locking in Repository layer.
-- =============================================================================

CREATE TABLE accounts (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    name VARCHAR(128) NOT NULL,
    type account_type NOT NULL,
    subtype VARCHAR(64) NOT NULL,
    category account_category NOT NULL,
    currency_code VARCHAR(10) NOT NULL REFERENCES currencies(code),
    description TEXT,
    is_archived BOOLEAN NOT NULL DEFAULT FALSE,
    archived_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    version BIGINT NOT NULL DEFAULT 1,
    CONSTRAINT chk_accounts_archived_at
        CHECK ((is_archived = FALSE AND archived_at IS NULL)
            OR (is_archived = TRUE AND archived_at IS NOT NULL))
);

CREATE INDEX idx_accounts_user_id ON accounts(user_id);
CREATE INDEX idx_accounts_user_type ON accounts(user_id, type);
CREATE INDEX idx_accounts_user_subtype ON accounts(user_id, subtype);
CREATE INDEX idx_accounts_user_category ON accounts(user_id, category);

-- =============================================================================
-- system_category_templates (global template pool)
-- =============================================================================

CREATE TABLE system_category_templates (
    id BIGSERIAL PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    group_name VARCHAR(64) NOT NULL,
    parent_id BIGINT REFERENCES system_category_templates(id),
    default_board category_board,
    sort_order INT NOT NULL DEFAULT 0,
    is_selectable BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE NULLS NOT DISTINCT (group_name, parent_id, name)
);

CREATE INDEX idx_system_category_templates_parent ON system_category_templates(parent_id);

-- =============================================================================
-- categories (per-user tree)
-- =============================================================================

CREATE TABLE categories (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    name VARCHAR(128) NOT NULL,
    parent_id BIGINT REFERENCES categories(id),
    board category_board NOT NULL,
    source category_source NOT NULL DEFAULT 'user',
    template_id BIGINT REFERENCES system_category_templates(id),
    sort_order INT NOT NULL DEFAULT 0,
    deleted_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE NULLS NOT DISTINCT (user_id, board, parent_id, name)
);

CREATE INDEX idx_categories_user_id ON categories(user_id);
CREATE INDEX idx_categories_parent_id ON categories(parent_id);
CREATE INDEX idx_categories_user_board ON categories(user_id, board);
CREATE INDEX idx_categories_template_id ON categories(template_id);

-- =============================================================================
-- transfer_groups
-- Persistence metadata carrier for TransferAggregate; not a domain entity.
-- =============================================================================

CREATE TABLE transfer_groups (
    id UUID PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    note TEXT,
    transfer_mode SMALLINT NOT NULL,
    exchange_rate NUMERIC(30, 10),
    exchange_rate_provider VARCHAR(64),
    exchange_rate_snapshot_time TIMESTAMPTZ,
    CONSTRAINT chk_transfer_groups_mode CHECK (transfer_mode IN (1, 2, 3))
);

CREATE INDEX idx_transfer_groups_user_id ON transfer_groups(user_id);

-- =============================================================================
-- transactions
-- Source of truth. amount is signed NUMERIC(20,8).
-- transfer sides use type='transfer' and share transfer_group_id.
-- =============================================================================

CREATE TABLE transactions (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    account_id BIGINT NOT NULL REFERENCES accounts(id),
    category_id BIGINT REFERENCES categories(id),
    type transaction_type NOT NULL,
    amount NUMERIC(20, 8) NOT NULL,
    currency_code VARCHAR(10) NOT NULL REFERENCES currencies(code),
    description TEXT,
    transfer_group_id UUID REFERENCES transfer_groups(id),
    deleted_at TIMESTAMPTZ,
    transaction_time TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    version BIGINT NOT NULL DEFAULT 1,
    CONSTRAINT chk_transactions_amount_not_nan CHECK (amount = amount)
);

CREATE INDEX idx_transactions_account_id ON transactions(account_id);
CREATE INDEX idx_transactions_category_id ON transactions(category_id);
CREATE INDEX idx_transactions_time ON transactions(transaction_time);
CREATE INDEX idx_transactions_user_time ON transactions(user_id, transaction_time);
CREATE INDEX idx_transactions_user_category ON transactions(user_id, category_id);
CREATE INDEX idx_transactions_transfer_group ON transactions(transfer_group_id)
    WHERE transfer_group_id IS NOT NULL;
CREATE INDEX idx_transactions_user_active
    ON transactions(user_id, account_id)
    WHERE deleted_at IS NULL;

-- =============================================================================
-- transaction_tags
-- =============================================================================

CREATE TABLE transaction_tags (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    name VARCHAR(64) NOT NULL,
    deleted_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT uq_transaction_tags_user_name UNIQUE (user_id, name)
);

CREATE INDEX idx_transaction_tags_user_id ON transaction_tags(user_id);

CREATE TABLE transaction_tag_relations (
    transaction_id BIGINT NOT NULL REFERENCES transactions(id),
    tag_id BIGINT NOT NULL REFERENCES transaction_tags(id),
    PRIMARY KEY (transaction_id, tag_id)
);

CREATE INDEX idx_transaction_tag_relations_tag ON transaction_tag_relations(tag_id);

-- =============================================================================
-- account_balance_cache
-- Derived data; reconstructible from transactions.
-- =============================================================================

CREATE TABLE account_balance_cache (
    account_id BIGINT PRIMARY KEY REFERENCES accounts(id),
    balance NUMERIC(20, 8) NOT NULL,
    last_transaction_id BIGINT REFERENCES transactions(id),
    source_version BIGINT NOT NULL DEFAULT 0,
    cache_version BIGINT NOT NULL DEFAULT 1,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- =============================================================================
-- exchange_rates
-- Append-only historical rates. Application must never UPDATE/DELETE rows.
-- =============================================================================

CREATE TABLE exchange_rates (
    id BIGSERIAL PRIMARY KEY,
    base_currency_code VARCHAR(10) NOT NULL REFERENCES currencies(code),
    target_currency_code VARCHAR(10) NOT NULL REFERENCES currencies(code),
    rate NUMERIC(20, 10) NOT NULL,
    source VARCHAR(64) NOT NULL,
    fetched_at TIMESTAMPTZ NOT NULL,
    CONSTRAINT chk_exchange_rates_positive CHECK (rate > 0),
    CONSTRAINT chk_exchange_rates_pair CHECK (base_currency_code <> target_currency_code)
);

CREATE INDEX idx_exchange_rates_pair
    ON exchange_rates(base_currency_code, target_currency_code);

CREATE INDEX idx_exchange_rates_pair_time
    ON exchange_rates(base_currency_code, target_currency_code, fetched_at DESC);

-- Optional hard guard: reject UPDATE/DELETE on exchange_rates at DB level.
CREATE OR REPLACE FUNCTION forbid_exchange_rate_mutation()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
    RAISE EXCEPTION 'exchange_rates is append-only; UPDATE/DELETE is forbidden';
END;
$$;

CREATE TRIGGER trg_exchange_rates_no_update
    BEFORE UPDATE ON exchange_rates
    FOR EACH ROW
    EXECUTE FUNCTION forbid_exchange_rate_mutation();

CREATE TRIGGER trg_exchange_rates_no_delete
    BEFORE DELETE ON exchange_rates
    FOR EACH ROW
    EXECUTE FUNCTION forbid_exchange_rate_mutation();

-- =============================================================================
-- audit_logs
-- =============================================================================

CREATE TABLE audit_logs (
    id BIGSERIAL PRIMARY KEY,
    operator_user_id BIGINT NOT NULL REFERENCES users(id),
    action audit_action NOT NULL,
    resource_type VARCHAR(64) NOT NULL,
    resource_id VARCHAR(128) NOT NULL,
    before_value JSONB,
    after_value JSONB,
    metadata JSONB,
    occurred_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_audit_logs_operator_time
    ON audit_logs(operator_user_id, occurred_at DESC);
CREATE INDEX idx_audit_logs_resource
    ON audit_logs(resource_type, resource_id);
CREATE INDEX idx_audit_logs_action_time
    ON audit_logs(action, occurred_at DESC);

-- =============================================================================
-- domain_events_outbox
-- Transactional outbox; written in the same DB transaction as business facts.
-- =============================================================================

CREATE TABLE domain_events_outbox (
    id UUID PRIMARY KEY,
    event_name VARCHAR(128) NOT NULL,
    aggregate_type VARCHAR(64),
    aggregate_id VARCHAR(128),
    payload JSONB NOT NULL,
    status outbox_status NOT NULL DEFAULT 'pending',
    retry_count INT NOT NULL DEFAULT 0,
    max_retry_count INT NOT NULL DEFAULT 5,
    next_retry_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_error TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    published_at TIMESTAMPTZ
);

CREATE INDEX idx_outbox_pending
    ON domain_events_outbox(status, next_retry_at)
    WHERE status IN ('pending', 'failed');

CREATE INDEX idx_outbox_created
    ON domain_events_outbox(created_at);

-- =============================================================================
-- refresh_tokens / revoked_access_tokens (auth foundation for P1-S10)
-- =============================================================================

CREATE TABLE refresh_tokens (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    token_hash VARCHAR(64) NOT NULL,
    session_id VARCHAR(64) NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    revoked_at TIMESTAMPTZ,
    CONSTRAINT uq_refresh_tokens_hash UNIQUE (token_hash)
);

CREATE INDEX idx_refresh_tokens_user_session
    ON refresh_tokens(user_id, session_id);
CREATE INDEX idx_refresh_tokens_expires
    ON refresh_tokens(expires_at);

CREATE TABLE revoked_access_tokens (
    issuer VARCHAR(64) NOT NULL,
    jti VARCHAR(128) NOT NULL,
    session_id VARCHAR(64),
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (issuer, jti)
);

CREATE INDEX idx_revoked_access_tokens_expires
    ON revoked_access_tokens(expires_at);
CREATE INDEX idx_revoked_access_tokens_session
    ON revoked_access_tokens(session_id)
    WHERE session_id IS NOT NULL;
