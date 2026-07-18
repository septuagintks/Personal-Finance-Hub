# Phase 2 S12 Delivery Summary

Date: 2026-07-18
Status: Windows Remediation Complete; Target-Environment Revalidation Pending

## 1. Current Validation Candidate

- Branch: `feature/phase2-product-experience`.
- Original S11 Release Candidate: `733a7836531014439f589c5995da87d1238ff7e6` (`feat: complete phase 2 S11 release candidate`).
- Current validation candidate: `b70c41a721a857b7eb887a7548227b2a45f14989` (`fix: harden phase 2 release boundaries`).
- The current candidate includes the macOS ABI/time/accessibility fixes, the V8/V9 migration correction, and the Windows release-boundary review fixes described below.
- Windows commits `cf13b6f67a118430a1174a99e1b52235e6c51568` and `b70c41a721a857b7eb887a7548227b2a45f14989` are reachable from the remote branch and verify as `Good signature` with key `36E772C15CA7CC4E`.

## 2. Windows Environment

- Windows 23H2 build `22631.6060`, AMD64; PowerShell `7.6.3`; China Standard Time.
- GCC `16.1.0`; CMake `4.3.2`; Ninja `1.13.2`; Python `3.14.6`; Git `2.55.0.windows.2`.
- Node.js `24.15.0`; pnpm `10.14.0`; Microsoft Edge `150.0.4078.65`.
- Docker, `psql`, and Flyway are not available on this machine.

## 3. Windows Results

- `quality_check.ps1`: PASS on the current validation candidate.
- Debug PostgreSQL OFF configure/build and CTest: `381/381 PASS`.
- Independent Release PostgreSQL OFF build and CTest: `381/381 PASS`.
- OpenAPI generated-type drift, TypeScript, ESLint, Prettier, Vitest/MSW `63/63`, production build, bundle budgets, 66-package runtime license policy, source-map policy, secret scan, and Markdown checks: PASS.
- `pnpm audit --prod --audit-level high`: no known vulnerabilities.
- Microsoft Edge E2E: `37/37 PASS`.
- Full browser configuration enumerates `111` tests in 7 files across Chromium, Firefox, and WebKit. Enumeration is PASS; actual three-browser execution is `NOT RUN` on Windows.

## 4. Delegated Target Matrix

The target side must validate the same RC lineage and return evidence for:

- Linux Debug and Release with `PFH_BUILD_POSTGRESQL=ON`, including real `postgresql_integration` and `drogon_runtime_integration` targets.
- PostgreSQL 16+ V1-V10 empty migration, info, validate, second no-op, role initialization, FORCE RLS, pooled isolation, repository, concurrency, NUMERIC, idempotency, maintenance, and Operator scenarios.
- Clean frontend install/build plus actual Chromium, Firefox, and WebKit `111`-test execution and manual WCAG keyboard, focus, contrast, zoom, and reduced-motion review.
- Daily 10,000-row and Stress 100,000-row fixed profiles, API/page/CSV budgets, memory/resource observations, and key query-plan summaries.
- Clean Backend and Web image builds, `docker compose config`, same-origin proxy and security headers, non-root/read-only runtime, health, Provider failover, Outbox/Scheduler, restart, SIGTERM, and sanitized logs.
- Backup/restore, previous-image rollback, and forward-only migration recovery rehearsal.

## 5. Phase Decision

Phase 2 remains `In Progress`. The target environment completed the independent Linux, frontend, browser, and image checks against the earlier candidate. Windows corrected the unpublished V8/V9 function reference and completed a release-boundary review, but the database-dependent runtime, full Compose, performance, backup/restore, and rollback gates have not yet run against the current candidate. Do not mark the Phase complete or merge it into `main` until that matrix passes.

## 6. macOS/Colima Target-Environment Results

- Validation time: 2026-07-18 17:21:21 +08:00 (Asia/Shanghai).
- Branch: `feature/phase2-product-experience`.
- Tested commit: `ccce0c99a5fe4fa7869e9149cdea2d11088cab60`, following signed fixes `1964616bf03b918390a2a17609fd36ade3c6c0f5` and `ccce0c99a5fe4fa7869e9149cdea2d11088cab60`. Both use the local default GPG key `81BFB01482975987` and verify as `Good signature`.
- Environment: macOS 26.5.2 / arm64; Colima 0.10.3; Ubuntu 24.04.4 / aarch64; GCC 13.3.0; CMake 3.28.3; Ninja 1.11.1; PostgreSQL 16.14; Drogon 1.8.7; OpenSSL 3.0.13; Argon2 20190702; libcurl 8.5.0; tzdata 2026b; Docker client 29.6.2 / server 29.5.2; Node 24.16.0; pnpm 11.14.0.

Results:

- Linux Debug and Release `PFH_BUILD_POSTGRESQL=ON` builds: `PASS`; the non-external CTest collection was `381/381 PASS` for each configuration. PostgreSQL OFF was independently `381/381 PASS`.
- Frontend clean install and quality gates: `PASS`; typecheck, ESLint, Prettier, production build, bundle/license/security gates, and Vitest `62/62` passed. `pnpm audit --prod --audit-level high` reported no known high-severity production vulnerabilities.
- Full Playwright matrix: `PASS`, `111/111` across Chromium, Firefox, and WebKit after the cross-engine reduced-motion and WCAG contrast fixes.
- Compose configuration: `PASS` with isolated Compose v5.1.4 binary because the host Docker CLI has no Compose plugin. No secret values were persisted.
- Backend image: `PASS`; clean arm64 build, image digest `sha256:8e16adc26337996d04ea365e82ebb8faa0cfb2ee30a18e7ca73bddbc418866b8`, 37,164,020 bytes, non-root `pfh`, built-in `/livez` healthcheck.
- Web image: `PASS`; clean arm64 build, image digest `sha256:ced3359c5713f10f3ca8a6c74dbeeb28a193ac48e3cd4e697ab6a2155c8bf889`, 22,453,077 bytes, non-root `pfh`, no source maps. A standalone static check under read-only rootfs, `cap_drop=ALL`, and `no-new-privileges` returned HTTP 200 with the configured CSP, cache, and security headers.

Database-dependent results:

- `BLOCKED`: Flyway V1-V7 applied to the disposable PostgreSQL 16.14 database, but V8 failed at `migrations/V8__transaction_corrections.sql:25` with SQLSTATE `42883`; V9 contains the same undefined function reference. V8 was rolled back.
- `NOT RUN`: V9/V10 migration completion, role initialization, real Repository/UoW fixture, pooled RLS/concurrency scenarios, Drogon API runtime, Provider/Outbox/Scheduler runtime, and full Compose topology. These were stopped at the migration boundary and were not represented as passing.
- `NOT RUN`: Daily/Stress performance profiles, backup/restore, image rollback, and forward-only migration recovery rehearsal.

The only project fixes in this target run are the two signed commits listed above. These results remain valid evidence for that tested commit, but they do not replace revalidation of the later migration, role, readiness, Provider-transport, and frontend error-boundary changes.

## 7. Windows Remediation And Review

- `cf13b6f67a118430a1174a99e1b52235e6c51568` corrected V8/V9 to use the canonical `pfh_current_user_id()` RLS function and added a static regression preventing the invalid alias from returning.
- `b70c41a721a857b7eb887a7548227b2a45f14989` hardened role initialization, startup role assertions, exact V10 readiness, same-origin Backend exposure, Provider URL-memory cleansing, and controlled JSON errors returned as Blob responses.
- `role-init` now rejects unsafe role reuse before mutation, keeps runtime passwords out of `psql` argv, restores request-role write defaults, limits Flyway history to read-only access, clears stale background grants, and scopes the 11-table FORCE RLS assertion to `public` relations.
- Windows reran Debug and Release CTest at `381/381 PASS`, Vitest/MSW at `63/63 PASS`, Chromium E2E at `37/37 PASS`, all static PostgreSQL/release/migration gates, frontend quality/build/security/license gates, and `git diff --check`.
- Bundle measurements remained within policy: initial JS gzip `61,558 B`, total JS gzip `314,552 B`, largest asynchronous chunk gzip `182,705 B`, and CSS gzip `9,076 B`.
- Real PostgreSQL, Flyway, Docker, production-ON runtime, Provider failover, Daily/Stress performance, backup/restore, and rollback were `NOT RUN` on Windows because the required local tools are unavailable.

## 8. Revalidation Required For Phase Signature

The target side must run the remaining matrix from a clean checkout of `b70c41a721a857b7eb887a7548227b2a45f14989`: V1-V10 migrate/info/validate/no-op, unsafe-role rejection and least-privilege role checks, real PostgreSQL/Drogon fixtures, complete same-origin Compose workflows, Provider/Outbox/Scheduler/restart/SIGTERM scenarios, Daily and Stress profiles, backup/restore, rollback, and sanitized runtime-artifact review. The rotated Provider key must be injected outside both repositories. Any required item that is not `PASS` keeps Phase 2 unsigned and unmerged.
