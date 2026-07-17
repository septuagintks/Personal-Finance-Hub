# Phase 2 S10 Delivery Summary

Date: 2026-07-18
Status: Implementation Complete; Target-Environment Validation Pending

## 1. Delivered

- Added tenant-isolated user audit pagination with action, resource, and time filters. The response exposes only approved business facts and TraceId values; Operator and system audit entries are excluded.
- Added single-account and all-account balance-cache rebuilds. Rebuilds preserve the authoritative ledger balance, align `source_version` with `MAX(transaction.version)`, and commit user audit facts transactionally.
- Extended persistent request idempotency to account, category, and tag creation, including exact response replay and fingerprint-conflict rejection. A bounded scheduled cleanup removes only expired records.
- Added persistent `USER` and `OPERATOR` authorization. Registration always creates `USER`; login and refresh load the current database role; every protected request rejects a stale or forged JWT role.
- Added queue-independent liveness and data-minimized readiness probes, an Operator summary, authenticated Prometheus metrics, sanitized dead-letter pagination, and concurrent-safe idempotent retry with Operator audit.
- Added Maintenance and Operations Web views, role-aware navigation, an independent Operator route guard, and a stable forbidden view. Desktop and mobile operational tables remain keyboard accessible.

## 2. Contracts And Data Boundaries

- Flyway V10 adds the application role, Operator audit/retry facts, `outbox_retry_commands`, and bounded `SECURITY DEFINER` functions for expired idempotency cleanup and counting.
- `/livez` bypasses the bounded application queue. `/readyz` checks the request database, required migration version, and Scheduler startup without exposing role names, connection details, or internal failures.
- Dead-letter APIs never return event payloads, raw exception text, claim ownership, or database internals. Metrics and operations APIs require the current persistent `OPERATOR` role.
- User audit pagination is ordered by unique descending audit ID; occurrence time remains a filter and display fact, so equal timestamps cannot duplicate or skip rows.
- OpenAPI contains 42 paths and 54 unique operation IDs. Generated TypeScript remains the frontend contract source.

## 3. Verification

- Full C++ build, PostgreSQL adapter compile gate, and production bootstrap compile gate: PASS.
- Full CTest, including maintenance, authorization, operations, OpenAPI, and PostgreSQL structural contracts: `380/380 PASS`.
- OpenAPI parse/static route checks and Flyway enum-cast structural gate: PASS.
- Frontend TypeScript, ESLint, Prettier, generated-type drift, and production build: PASS.
- Vitest/MSW: `59/59 PASS`.
- Full Edge E2E: `34/34 PASS`; S10 Desktop/Mobile maintenance and operations slice: `5/5 PASS`, with axe and page-overflow checks.

## 4. Remaining Target Validation

- Execute V1-V10 migrate/info/validate/no-op and role initialization against a fresh PostgreSQL 16+ database.
- Re-run RLS, role grants, pooled-connection isolation, balance rebuild, idempotency cleanup, and concurrent dead-letter retry against real PostgreSQL.
- Run Linux production ON Debug/Release, Docker cold build, same-origin Drogon smoke, and Chromium/Firefox/WebKit validation.
- Complete the fixed 10,000/100,000-row performance and route-chunk work in P2-S11; the Reports route remains about 548 kB and outside the initial route.
