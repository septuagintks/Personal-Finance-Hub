# Personal Finance Hub - Configuration Guide

## Configuration Files

### Development Environment

1. Copy `config.example.json` to `config.local.json`:
   ```powershell
   Copy-Item config/config.example.json config/config.local.json
   ```

2. Edit `config.local.json` with your actual values:
   - Database credentials
   - JWT secret key
   - Exchange rate API key

### Configuration Structure

#### Server Settings
- `host`: Server bind address (use `0.0.0.0` for all interfaces)
- `port`: HTTP port (default: 8080)
- `threads`: Number of Drogon event-loop threads
- `request_worker_threads`: Dedicated workers for synchronous application and database work (1-64)
- `request_queue_capacity`: Maximum queued ordinary HTTP requests before a retryable 503 (1-2048)
- `request_queue_byte_capacity`: Maximum total request-body bytes retained by accepted ordinary work (up to 256 MiB)
- `maximum_request_body_bytes`: Per-request body limit enforced before worker admission (up to 16 MiB)
- `auth_worker_threads`: Dedicated Argon2 worker count (1-8)
- `auth_queue_capacity`: Maximum queued login/register requests (1-256)
- `auth_queue_byte_capacity`: Body-byte budget reserved for login/register work (up to 16 MiB)
- `auth_rate_limit_attempts`: Fixed-window login/register attempts allowed per direct peer
- `auth_rate_limit_window_seconds`: Authentication rate-limit window
- `auth_rate_limit_sources`: Maximum peer counters retained in memory

#### Database Settings
- `host`: PostgreSQL server address
- `port`: PostgreSQL port (default: 5432)
- `name`: Database name
- `user`: Database user
- `password`: Database password (never commit this)
- `pool_size`: Connection pool size
- `connection_timeout`: Connection timeout in seconds

#### JWT Settings
- `secret`: JWT signing secret (must be strong, never commit this)
- `access_token_expiry_seconds`: Access token lifetime (default: 15 minutes)
- `refresh_token_expiry_seconds`: Refresh token lifetime (default: 30 days)

#### Logging Settings
- `level`: Log level (`trace`, `debug`, `info`, `warn`, `error`, `critical`)
- `output`: Output target (`console`, `file`, `both`)
- `file`: Log file path
- `maximum_file_size_bytes`: Maximum active/backup file size (64 KiB to 100 MiB; default 10 MiB)
- `maximum_file_count`: Total retained files including the active file (1-20; default 5)

#### Scheduler Settings
- `enabled`: Enables all Phase 1 recurring jobs
- `worker_threads`: Dedicated background worker count (1-64)
- `queue_capacity`: Maximum accepted tasks waiting for a worker (1-10000)
- `outbox_publish_interval_seconds`: Outbox poll interval
- `outbox_batch_size`: Maximum events claimed per publisher run
- `outbox_processing_timeout_seconds`: Time before an abandoned processing claim is recovered; must exceed the soft job deadline
- `exchange_rate_refresh_interval_minutes`: Exchange-rate refresh interval
- `session_cleanup_interval_minutes`: Expired authentication data cleanup interval
- `session_cleanup_batch_size`: Per-table deletion limit for one cleanup run
- `outbox_retention_interval_minutes`: Distributed-lease cleanup interval (at most seven days; default one day)
- `published_outbox_retention_days`: Published Outbox retention period (1-3650 days; default 30)
- `outbox_retention_batch_size`: Maximum published events deleted per cleanup run (1-10000; default 1000)
- `job_execution_timeout_seconds`: Soft execution deadline used for warning logs
- `job_lease_duration_seconds`: Distributed lease duration; must exceed the soft deadline

#### Exchange Rate Provider
- `provider`: Production requires `freecurrencyapi`; `exchangerate.fun` is the fixed no-key fallback
- `api_key`: FreeCurrencyAPI key; prefer `PFH_FREECURRENCYAPI_API_KEY`
- `request_timeout_seconds`: Per-provider HTTPS timeout; because failover is sequential, it must not exceed half the job execution timeout

HTTP event-loop callbacks validate request size before copying the body and use task-count plus retained-body-byte admission. Login and registration use a separate, smaller Argon2 pool and a bounded direct-peer rate limiter, so unauthenticated password work cannot consume ordinary API workers. Scheduler callbacks use a third bounded background pool. `job_execution_timeout_seconds` is a soft deadline because C++ worker threads are not terminated unsafely; external HTTP has its own hard timeout. Both `outbox_processing_timeout_seconds` and `job_lease_duration_seconds` must be longer than the soft deadline. FreeCurrencyAPI and exchangerate.fun may run sequentially, so twice `request_timeout_seconds` must fit within the soft deadline.

Operator observability exposes the effective HTTP capacities and monotonic rejection counters through `/api/v1/operations/summary` and the `pfh_http_admission_capacity` / `pfh_http_admission_rejections_total` Prometheus families. Report limits are compile-time safety boundaries rather than operator-tunable values: 100,000 aggregate rows, 10,000 detailed/CSV rows, 64 MiB report input, 32 MiB CSV output, 10,000 breakdown buckets, 100,000 tag expansions, 1,024 historical-rate points, 120 cash-flow months and a 366-day CSV range. They are exposed as `reportResources` and `pfh_report_resource_*` metrics; changing them requires code review and renewed memory/pressure validation.

The Outbox retention job deletes only published events older than the configured period. Pending, processing, failed, and dead-letter events are retained. Handler receipts cascade with deleted events; retry-command facts are removed in the same transaction before their event. The request-role database client performs these writes and the other Outbox transitions, exchange-rate appends, supplemental audit writes, token cleanup and job lease updates against non-RLS tables. The background BYPASSRLS/default-read-only client is reserved exclusively for the cross-tenant active-currency query.

## Environment Variable Overlay

Configuration is loaded in this order: environment variables, JSON file, then built-in defaults. The `PFH_*` names are preferred; legacy unprefixed names remain supported for compatibility.

| Configuration | Preferred environment variable |
| ------------- | ------------------------------ |
| Runtime environment | `PFH_ENVIRONMENT` |
| JWT secret | `PFH_JWT_SECRET` |
| Password pepper | `PFH_PASSWORD_PEPPER` |
| Database host | `PFH_DB_HOST` |
| Database port | `PFH_DB_PORT` |
| Database name | `PFH_DB_NAME` |
| Database user | `PFH_DB_USER` |
| Database password | `PFH_DB_PASSWORD` |
| Background database host | `PFH_BACKGROUND_DB_HOST` |
| Background database port | `PFH_BACKGROUND_DB_PORT` |
| Background database name | `PFH_BACKGROUND_DB_NAME` |
| Background database user | `PFH_BACKGROUND_DB_USER` |
| Background database password | `PFH_BACKGROUND_DB_PASSWORD` |
| FreeCurrencyAPI key | `PFH_FREECURRENCYAPI_API_KEY` |

`PFH_DB_PORT` and `PFH_BACKGROUND_DB_PORT` must be integers from 1 through 65535. Invalid environment values fail configuration loading instead of silently falling back to JSON. Environment-provided secrets are subject to the same placeholder and strength validation as JSON values. `PFH_FREECURRENCYAPI_API_KEY` takes precedence over the legacy `PFH_EXCHANGE_RATE_API_KEY` / `EXCHANGE_RATE_API_KEY` aliases. `PFH_PASSWORD_PEPPER` is optional: leave it empty or provide a real secret; values beginning with `REPLACE_WITH_` are rejected. The request and background roles must be distinct; the background role is reserved for explicitly approved read-only cross-tenant jobs and is never injected into request handlers.

Server settings, logging settings, JWT expiry values, connection-pool sizes, and provider selection currently remain JSON-configured. `.env.example` lists only overlays implemented by `JsonConfigLoader`.

## Environment-Specific Configs

- `config.example.json`: Template with placeholder values (committed to git)
- `config.local.json`: Local development config (ignored by git)
- `config.test.json`: Test environment config (can be committed)
- `config.prod.json`: Production config template (committed without secrets)

## Security Notes

**NEVER commit files containing:**
- Database passwords
- JWT secrets
- API keys
- Any other sensitive credentials

All `.local.*` files are automatically ignored by `.gitignore`.
