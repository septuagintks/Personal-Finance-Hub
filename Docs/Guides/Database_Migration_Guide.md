# Personal Finance Hub - Database Migration Guide

Version: 1.0  
Backend: C++23  
Database: PostgreSQL 16+  
Migration Tool: Flyway

---

## 1. Overview

This project uses **Flyway** for versioned database schema evolution. All schema changes must be expressed as **versioned SQL migration scripts** in the `migrations/` directory, never as manual SQL edits in production.

---

## 2. Migration File Naming Convention

Flyway enforces strict naming:

- **Versioned migrations**: `V<VERSION>__<description>.sql`
  - Example: `V1__initial_schema.sql`, `V2__seed_initial_currencies.sql`
  - Version must be numeric; dots allowed (e.g., `V2.1__add_user_email.sql`)
  - Description uses double underscore `__` separator
- **Repeatable migrations**: `R__<description>.sql`
  - Re-executed when checksum changes
  - Suitable for views, stored procedures, or reference data that may evolve
- **Undo migrations** (Flyway Teams/Enterprise only): `U<VERSION>__<description>.sql`

---

## 3. Current Migration Scripts

| Version | File                                   | Description                            |
| ------- | -------------------------------------- | -------------------------------------- |
| V1      | `V1__initial_schema.sql`               | Phase 1 core schema (all tables)       |
| V2      | `V2__seed_initial_currencies.sql`      | Seed fiat + controlled crypto metadata |
| V3      | `V3__seed_system_category_templates.sql` | Seed global category template pool     |

---

## 4. Local Development Setup

### 4.1 Prerequisites

- Docker + Docker Compose (recommended), or native PostgreSQL 16+
- Flyway CLI 10+ (optional; can also use Docker image)

### 4.2 Start Local PostgreSQL

```bash
# Start PostgreSQL container
docker-compose up -d postgres

# Wait for health check
docker-compose ps postgres
```

Default connection:
- **Host**: `localhost`
- **Port**: `5432`
- **Database**: `pfh_dev`
- **User**: `pfh_user`
- **Password**: `pfh_dev_password_REPLACE_ME` (override via `PFH_DB_PASSWORD` env var)

### 4.3 Run Migrations

**Option A: Docker Compose (recommended)**

```bash
# Run Flyway migration service
docker-compose up flyway

# Or run one-off migration command
docker-compose run --rm flyway migrate
```

**Option B: Local Flyway CLI**

```bash
# Set password via environment variable
export FLYWAY_PASSWORD=pfh_dev_password_REPLACE_ME

# Run migration
flyway migrate

# Check status
flyway info
```

**Option C: Manual psql (not recommended; bypasses Flyway tracking)**

```bash
psql -U pfh_user -d pfh_dev -h localhost -f migrations/V1__initial_schema.sql
```

---

## 5. Verifying Migration Status

```bash
flyway info
```

Expected output:

```text
+-----------+---------+------------------------------+------+---------------------+---------+
| Category  | Version | Description                  | Type | Installed On        | State   |
+-----------+---------+------------------------------+------+---------------------+---------+
|           | 1       | initial schema               | SQL  | 2026-07-09 12:00:00 | Success |
|           | 2       | seed initial currencies      | SQL  | 2026-07-09 12:00:05 | Success |
|           | 3       | seed system category templates | SQL | 2026-07-09 12:00:08 | Success |
+-----------+---------+------------------------------+------+---------------------+---------+
```

---

## 6. Adding New Migrations

### 6.1 Create New Migration Script

```bash
# Example: add user email field
touch migrations/V4__add_user_email.sql
```

```sql
-- V4__add_user_email.sql
ALTER TABLE users ADD COLUMN email VARCHAR(255);
CREATE UNIQUE INDEX idx_users_email ON users(email) WHERE email IS NOT NULL;
```

### 6.2 Apply Migration

```bash
flyway migrate
```

Flyway automatically detects new scripts and applies them in version order.

---

## 7. Migration Best Practices

### 7.1 Backward Compatibility

New migrations must be **backward compatible** with the currently deployed application:

- **Adding columns**: must have `DEFAULT` or allow `NULL`
- **Renaming columns**: use multi-phase migration:
  1. Add new column
  2. Dual-write to both (application change)
  3. Backfill old → new
  4. Drop old column (next release)
- **Removing columns**: mark as unused in code first, drop in next migration after deploy

### 7.2 Idempotency

Use defensive checks:

```sql
-- Good: safe to re-run
ALTER TABLE users ADD COLUMN IF NOT EXISTS email VARCHAR(255);

-- Avoid: fails on second run
ALTER TABLE users ADD COLUMN email VARCHAR(255);
```

### 7.3 Transaction Safety

Each Flyway migration runs in a **single transaction** by default. Avoid:

- Long-running data migrations that lock tables for minutes
- DDL operations unsupported in PostgreSQL transactions (e.g., `CREATE INDEX CONCURRENTLY`)

For large data migrations:
- Split into smaller batches
- Use `CONCURRENTLY` with manual `--nontransactional` flag (Flyway Teams)
- Or run as separate background job, not in migration

### 7.4 Testing

Test migrations on a copy of production data:

```bash
# Restore production snapshot to local
pg_restore -U pfh_user -d pfh_test production_dump.sql

# Apply pending migrations
FLYWAY_URL=jdbc:postgresql://localhost:5432/pfh_test flyway migrate

# Run application tests
ctest
```

---

## 8. Rollback Strategy

### 8.1 Automatic Rollback (Failed Migration)

Flyway automatically rolls back the failed migration transaction:

```text
ERROR: Failed to execute migration V4__add_user_email.sql
Database state: unchanged (V3 still current)
```

### 8.2 Manual Rollback (Post-Deployment Issue)

**Scenario**: V4 deployed successfully but application breaks.

**Option 1: Forward fix** (preferred)

Create V5 to repair issue:

```sql
-- V5__fix_user_email_constraint.sql
ALTER TABLE users ALTER COLUMN email DROP NOT NULL;
```

**Option 2: Undo migration** (Flyway Teams/Enterprise only)

```bash
flyway undo
```

Executes `U4__remove_user_email.sql`:

```sql
-- U4__remove_user_email.sql
ALTER TABLE users DROP COLUMN email;
DROP INDEX idx_users_email;
```

**Option 3: Manual repair + `flyway repair`**

If migration partially applied:

```bash
# Manually fix database
psql -U pfh_user -d pfh_dev -c "DROP TABLE broken_table;"

# Mark migration as resolved
flyway repair
```

---

## 9. Production Deployment

### 9.1 CI/CD Pipeline Integration

Recommended flow:

1. **Build application** → run unit tests
2. **Run Flyway migrate** on staging database
3. **Run integration tests** on staging
4. **Deploy application** to staging
5. **Run smoke tests**
6. **Promote to production**: repeat steps 2-5

Example GitHub Actions:

```yaml
jobs:
  deploy:
    steps:
      - name: Run database migrations
        env:
          FLYWAY_URL: ${{ secrets.PROD_DB_URL }}
          FLYWAY_USER: ${{ secrets.PROD_DB_USER }}
          FLYWAY_PASSWORD: ${{ secrets.PROD_DB_PASSWORD }}
        run: |
          flyway migrate

      - name: Deploy application
        run: |
          kubectl apply -f k8s/deployment.yml
```

### 9.2 Blue-Green Deployment

1. Apply **forward-compatible** migration to production DB
2. Deploy new application version (green)
3. Gradually shift traffic from blue → green
4. Shut down blue once green is stable

### 9.3 Kubernetes Init Container

Run migration before starting application:

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: pfh-api
spec:
  template:
    spec:
      initContainers:
      - name: db-migration
        image: flyway/flyway:10
        command: ["flyway", "migrate"]
        env:
        - name: FLYWAY_URL
          value: "jdbc:postgresql://postgres:5432/pfh"
        - name: FLYWAY_USER
          valueFrom:
            secretKeyRef:
              name: db-credentials
              key: username
        - name: FLYWAY_PASSWORD
          valueFrom:
            secretKeyRef:
              name: db-credentials
              key: password
        volumeMounts:
        - name: migrations
          mountPath: /flyway/sql
      containers:
      - name: app
        image: pfh-api:latest
      volumes:
      - name: migrations
        configMap:
          name: flyway-migrations
```

---

## 10. Troubleshooting

### 10.1 Migration Checksum Mismatch

**Error**:

```text
Migration checksum mismatch for V2__seed_initial_currencies.sql
Expected: 1234567890
Found:    9876543210
```

**Cause**: Migration file was edited after being applied.

**Fix**:

```bash
# If edit was intentional and safe, update checksum
flyway repair

# Otherwise, revert file to original content
git checkout migrations/V2__seed_initial_currencies.sql
```

### 10.2 Failed Migration Stuck in `flyway_schema_history`

**Error**:

```text
Migration V4__add_user_email.sql failed
Status: Failed
```

**Fix**:

```bash
# Manually inspect and fix database
psql -U pfh_user -d pfh_dev

# Mark migration as resolved
flyway repair
```

### 10.3 Baseline Existing Database

If database already has tables before Flyway adoption:

```bash
flyway baseline -baselineVersion=1
```

This marks V1 as already applied without executing it.

---

## 11. References

- [Flyway Documentation](https://flywaydb.org/documentation/)
- [PostgreSQL 16 Release Notes](https://www.postgresql.org/docs/16/release-16.html)
- [PFH Database Design](../Architecture/02_Database_Design.md)
- [Phase 1 Detailed Plan](../Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md)
