# Phase 2 S09 Delivery Summary

Date: 2026-07-18
Status: Implementation Complete; Target-Environment Validation Pending

## 1. Delivered

- Added tenant-scoped report analysis for monthly net worth and root-category, account, or tag breakdowns.
- Added explicit base currency, valuation time, report window, current/historical rate status, and all-or-nothing missing-rate behavior.
- Added a historical account-balance read model. Backdated entries use business occurrence time; account record creation time does not erase history.
- Added RFC 4180 UTF-8 CSV export with ledger filters, user-timezone timestamps, business amounts, formula-injection protection, a 366-day range, and a 10,000-row cap.
- Expanded Dashboard with account distribution and top expense categories.
- Added URL-driven Reports UI with lazy ECharts, accessible source tables, distinct loading/empty/historical/unavailable/error states, and server-named downloads.
- Kept complete cryptocurrency pricing outside the delivered boundary; reports fail explicitly when no complete historical conversion path exists.

## 2. Contracts

- `GET /api/v1/reports/analysis`
- `GET /api/v1/exports/transactions.csv`
- OpenAPI remains the generated frontend type source.
- Tag buckets intentionally overlap when one transaction has multiple tags; `dimensionOverlaps` exposes that fact.
- CSV reads stable 200-row pages. The 10,000-row cap keeps the response bounded; transport-level chunking remains benchmark-driven for S11.

## 3. Verification

- C++ Debug build and PostgreSQL adapter compile gate: PASS.
- Full CTest, including report, CSV, OpenAPI, and PostgreSQL structural checks: `375/375 PASS`.
- Vitest/MSW: `52/52 PASS`.
- Full Edge E2E: `29/29 PASS`; Desktop 1440x900 and Mobile 390x844 report workflow: `2/2 PASS`.
- E2E includes nonblank canvas pixels, URL filter changes, accessible tables, CSV download filename, axe, and page overflow checks.

## 4. Remaining Target Validation

- Real PostgreSQL historical-balance SQL, RLS pooled-connection isolation, DST fixtures, and NUMERIC round-trip.
- 10,000/100,000-row latency, first-byte, memory, and query-plan measurements.
- Reports route chunk is currently about 548 kB and excluded from the initial route; S11 owns the fixed bundle budget and optimization decision.
- Chromium, Firefox, WebKit, Linux/Docker, and same-origin production topology.
