# Phase 2 S11 Delivery Summary

Date: 2026-07-18
Status: Windows Release Candidate Complete; Target-Environment Validation Pending

## 1. Delivered

- Added repository-enforced bundle budgets, exact dependency and license checks, production source-map checks, and source/build secret scanning.
- Removed unused Element Plus, isolated ECharts in a Reports-only chunk, and upgraded Axios to the audited fixed `1.16.0` release.
- Added reduced-motion behavior, DST-offset presentation, long-text coverage, and Desktop/short-screen/Tablet/narrow-Mobile settings validation.
- Added an explicit Playwright Chromium/Firefox/WebKit release matrix while preserving the local Edge workflow.
- Added fixed Daily and Stress PostgreSQL fixture/benchmark tooling for transaction list, filtering, Dashboard, and CSV p50/p95 measurements.
- Added a lockfile-built Web image, same-origin Nginx proxy, strict response headers and cache policy, `/livez` healthchecks, and read-only least-privilege Compose services.
- Pinned the vcpkg baseline to an immutable commit and added an offline release-hardening CTest gate.

## 2. Security And Performance Boundaries

- The Web Edge emits CSP, frame denial, MIME sniffing denial, Referrer Policy, Permissions Policy, and COOP. Runtime header behavior still requires Docker validation.
- Production builds contain no source maps. The scanner covers tracked and not-ignored untracked sources plus generated HTML/JS/CSS/JSON output.
- `pnpm audit --prod --audit-level high` reports no known vulnerabilities. The 66-package runtime dependency graph uses only 0BSD, Apache-2.0, BSD-2-Clause, BSD-3-Clause, ISC, or MIT licenses.
- Bundle results are initial JS gzip `61,309 B`, total JS gzip `314,301 B`, largest async chunk gzip `182,705 B`, and CSS gzip `9,084 B`.
- Fixture seeding changes only a dedicated user's `PFH-PERF-*` facts, requires `--confirm-test-database`, isolates Daily and Stress profiles, and keeps the database URL out of process argv.

## 3. Windows Verification

- GCC 16.1 Debug PostgreSQL OFF build and CTest: `381/381 PASS`.
- GCC 16.1 Release PostgreSQL OFF build and CTest: `381/381 PASS`.
- OpenAPI drift, TypeScript, ESLint, Prettier, production build, bundle, dependency, license, source-map, and secret gates: PASS.
- Vitest/MSW: `62/62 PASS`.
- Microsoft Edge full E2E: `37/37 PASS` with axe, canvas-pixel, storage, long-text, overflow, reduced-motion, and workflow checks.
- Playwright release configuration enumerates `111` tests across Chromium, Firefox, and WebKit.

## 4. Target-Environment Validation

The following are intentionally `NOT RUN` on Windows and remain mandatory in P2-S12:

- Linux Debug/Release production ON, real Drogon/PostgreSQL, V1-V10 migration, RLS, role, concurrency, NUMERIC, Provider, Outbox, Scheduler, restart, and runtime-log validation.
- Daily 10,000-row and Stress 100,000-row PostgreSQL measurements, query plans, page performance, CSV memory/first-byte, and budget enforcement.
- Actual Chromium/Firefox/WebKit execution and manual WCAG keyboard, focus, contrast, and zoom review.
- `docker compose config`, clean Backend/Web image builds, same-origin runtime headers, non-root/read-only execution, health, SIGTERM, backup/restore, and rollback rehearsal.

Mock-contract, static, and PostgreSQL OFF results do not replace these target-environment conclusions.
