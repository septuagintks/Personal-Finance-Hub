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

## Environment-Specific Configs

- `config.example.json`: Template with placeholder values (committed to git)
- `config.local.json`: Local development config (ignored by git)
- `config.test.json`: Test environment config (can be committed)
- `config.prod.json`: Production config template (committed without secrets)

## Security Notes

⚠️ **NEVER commit files containing:**
- Database passwords
- JWT secrets
- API keys
- Any other sensitive credentials

All `.local.*` files are automatically ignored by `.gitignore`.
