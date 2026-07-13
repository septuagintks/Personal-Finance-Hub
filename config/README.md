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
- `threads`: Number of worker threads

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

#### Scheduler Settings
- `exchange_rate_refresh_interval_minutes`: How often to refresh exchange rates

#### Exchange Rate Provider
- `provider`: Exchange rate data provider (`mock` for testing, other providers TBD)
- `api_key`: Provider API key (if required)

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
| Exchange-rate API key | `PFH_EXCHANGE_RATE_API_KEY` |

`PFH_DB_PORT` and `PFH_BACKGROUND_DB_PORT` must be integers from 1 through 65535. Invalid environment values fail configuration loading instead of silently falling back to JSON. Environment-provided secrets are subject to the same placeholder and strength validation as JSON values. `PFH_PASSWORD_PEPPER` is optional: leave it empty or provide a real secret; values beginning with `REPLACE_WITH_` are rejected. The request and background roles must be distinct; the background role is reserved for explicitly approved read-only cross-tenant jobs and is never injected into request handlers.

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
