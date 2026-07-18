# Phase 2 S12 Delivery Summary

Date: 2026-07-19
Status: Target-Environment Matrix Passed; Ready for Windows Review

## 1. Current Validation Candidate

- Branch: `feature/phase2-product-experience`.
- Windows delegated baseline: `ce6468f3411de6fa47072974252a02b8e705ff53` (`docs: prepare phase 2 target revalidation`).
- Final target-environment candidate: `f3abd40c96379775c422811e4b826889b5ea7af3` (`fix: make ledger scroll region keyboard accessible`).
- The local branch and `origin/feature/phase2-product-experience` both pointed to the final candidate before this summary update.
- The original S11 Release Candidate remains `733a7836531014439f589c5995da87d1238ff7e6`.

The macOS target run produced the following linear commits after the delegated baseline:

| Commit                                     | Purpose                                                                                                      | Signature                    |
| ------------------------------------------ | ------------------------------------------------------------------------------------------------------------ | ---------------------------- |
| `466b952b78c325d7c9a339e3ca5ad7390cd2cd7d` | Harden role initialization, UTC timestamp binding, NOWAIT conflict mapping, and runtime idempotency fixtures | Good, key `81BFB01482975987` |
| `0fc53c268e75d72c417ae920a7ef5a97f174f45e` | Preserve forwarded same-origin scheme and Drogon 1.8.7 refresh cookies                                       | Good, key `81BFB01482975987` |
| `6c2da63b570c0cbc72faabfc21c424732460d5f9` | Type Transfer performance fixture `category_id` values as `BIGINT`                                           | Good, key `81BFB01482975987` |
| `944ac2dd03ab9580d3a0cd21df5e3284f8cff7b3` | Keep the Daily CSV benchmark inside the API range contract                                                   | Good, key `81BFB01482975987` |
| `f3abd40c96379775c422811e4b826889b5ea7af3` | Make the horizontally scrolling transaction ledger keyboard accessible                                       | Good, key `81BFB01482975987` |

## 2. Validation Environments

Windows baseline evidence was produced on Windows 23H2 build `22631.6060`, AMD64, with GCC `16.1.0`, CMake `4.3.2`, Ninja `1.13.2`, Python `3.14.6`, Node.js `24.15.0`, pnpm `10.14.0`, and Edge `150.0.4078.65`. Docker, PostgreSQL, and Flyway were unavailable there, so Windows did not represent those gates as passing.

The final target matrix ran with:

- macOS `26.5.2`, Darwin `25.5.0`, arm64; Colima `0.10.3`.
- Ubuntu `24.04.4`, aarch64; GCC `13.3.0`; CMake `3.28.3`; Ninja `1.11.1`; tzdata `2026b`.
- PostgreSQL/psql `16.14`; Flyway OSS `10.22.0`; Drogon `1.8.7`; OpenSSL `3.0.13`; Argon2 `20190702`; libcurl `8.5.0`.
- Docker client `29.6.2`, server `29.5.2`; isolated Compose binary `v5.1.4`.
- Node.js `24.16.0`; pnpm `10.14.0`; Playwright `1.61.1`; Chromium `149`, Firefox `151`, and WebKit `26.5`.
- `PFH_FREECURRENCYAPI_API_KEY`: `SET` outside the repositories. Its value and derived metadata were not read into evidence.

## 3. Linux And PostgreSQL Results

Final reruns after all candidate fixes:

| Configuration                      | Build | CTest          | External runtime                                                   |
| ---------------------------------- | ----- | -------------- | ------------------------------------------------------------------ |
| Debug, `PFH_BUILD_POSTGRESQL=ON`   | PASS  | `384/384 PASS` | `postgresql_integration` and `drogon_runtime_integration` executed |
| Release, `PFH_BUILD_POSTGRESQL=ON` | PASS  | `384/384 PASS` | `postgresql_integration` and `drogon_runtime_integration` executed |
| Debug, `PFH_BUILD_POSTGRESQL=OFF`  | PASS  | `382/382 PASS` | Not registered by design                                           |

Database and authorization validation:

- Disposable PostgreSQL 16.14 empty database: V1-V10 `migrate`, `info`, `validate`, and second no-op all `PASS`; V8/V9 use the canonical `pfh_current_user_id()` function.
- Normal request/background role initialization and all three rejection cases `PASS`. Reused role names, administrator reuse, and pre-existing membership fail before role mutation with nonzero exit status.
- Request and background roles are distinct `LOGIN NOINHERIT` roles. The request role is non-BYPASSRLS with write defaults; the background role is BYPASSRLS with read-only defaults and only approved column grants.
- Flyway history access, cleanup-function denial, and all 11 `public` tenant tables with ENABLE/FORCE RLS: `PASS`.
- Repository/UoW, pooled tenant rebinding, two-user isolation, NUMERIC boundaries, idempotency cleanup, NOWAIT conflicts, maintenance, Operator/dead-letter, and exact V10 readiness scenarios: `PASS`.

## 4. Compose And Runtime Results

- Compose configuration, cold Backend build, and cold Web build: `PASS`.
- Final Backend image: `sha256:315d2e9572741cecae4d1a19b77976df8721c6cd93f7f4eab6d7a21fa6fe49f7`, `37,160,648` bytes, immutable tag `pfh-s12r-app-verified:f3abd40`.
- Final Web image: `sha256:cb80dfc0b92ba6c70243fabb49b65992c9694e9ff0ed771f1643182d993bb753`, `22,453,938` bytes, immutable tag `pfh-s12r-web-verified:f3abd40`.
- Both arm64 images run as non-root user `pfh` with read-only root filesystems, `cap_drop=ALL`, and `no-new-privileges`. The Backend exposes no host port.
- Same-origin Web and Bearer API matrices: `PASS`. Coverage includes registration, login, refresh rotation, logout clearing, accounts, categories, tags, preferences, transactions, transfers, corrections, reports, analytics, CSV, maintenance, USER/OPERATOR authorization, dead-letter retry, Cookie attributes, TraceId, ETag, Blob JSON errors, CSP, cache policy, and security headers.
- FreeCurrencyAPI primary batch, exchangerate.fun whole-batch fallback, and both historical-complete and historical-incomplete dual-failure paths: `PASS`. Successful and degraded Outbox events were published with handler receipts; no mixed or partial Provider batch was persisted.
- Scheduler lease exclusion, competing worker skip, timed-out Outbox ownership recovery after restart, and SIGTERM shutdown: `PASS`. All tested application instances exited `0` within the grace period with no OOM.
- Refined post-rollback App/Web log scans found no authorization value, Cookie value, JWT, Provider credential, database URL, full Provider URL, response body, or host-private path. Normal `refresh-token` job names and lowercase `/api/v1/users/` routes were not treated as credential or macOS path matches.

## 5. Frontend And Accessibility Results

- Frozen clean install, OpenAPI generated-type drift, typecheck, ESLint, Prettier, production build, bundle, runtime license, dependency security, source-map, and secret gates: `PASS`.
- Vitest/MSW: `63/63 PASS`; `pnpm audit --prod --audit-level high`: no known high-severity production vulnerability.
- Full Playwright execution: `111/111 PASS` across Chromium, Firefox, and WebKit after the final ledger accessibility fix.
- Keyboard, visible focus, contrast, 200% zoom-equivalent short viewport, reduced motion, Desktop, Tablet, and narrow-screen review: `PASS`.
- The transaction ledger exposes a named focusable region. Real Tab navigation entered the region, the focus indicator remained visible, and keyboard horizontal scrolling moved it by `40 px` without overlapping adjacent content.

## 6. Performance Results

Both fixed profiles seeded successfully and passed `tools/phase2_performance.py benchmark --enforce` against the same-origin Compose runtime.

| Profile | Fixture          | First page p95 | Filter p95 | Dashboard p95 | CSV first-byte p95 |
| ------- | ---------------- | -------------- | ---------- | ------------- | ------------------ |
| Daily   | `10,000/20/200`  | `2.02 ms`      | `1.99 ms`  | `7.57 ms`     | `65.05 ms`         |
| Stress  | `100,000/50/500` | `3.21 ms`      | `3.10 ms`  | `20.01 ms`    | `95.79 ms`         |

Page measurements:

| Profile | Filter interactive | Public LCP | INP     | CLS      | axe |
| ------- | ------------------ | ---------- | ------- | -------- | --- |
| Daily   | `55.86 ms`         | `64 ms`    | `24 ms` | `0.0116` | `0` |
| Stress  | `54.72 ms`         | `88 ms`    | `24 ms` | `0.0116` | `0` |

- The measured primary-action contrast ratio was `7.38:1`; reduced motion and visible focus remained active during the page checks.
- Stress observation: App `37.03% CPU / 134 MiB`; PostgreSQL `55.40% CPU / 120 MiB`.
- Warm shared-buffer query plans had zero reads. First-page/filter plans were approximately `0.217 ms`, the 4,813-row monthly range was `1.398 ms`, and the 3,960-row account aggregate was `7.105 ms`.
- Full benchmark JSON, raw plans, screenshots, and resource captures remained temporary evidence and were not added to either repository.

## 7. Recovery And Rollback Results

- A custom-format `pg_dump` of `2,416,534` bytes restored into an independent PostgreSQL 16 tmpfs instance.
- Source and restored counts matched: `8` users, `59` accounts, `100,004` transactions, `37` Outbox events, `7` exchange rates, and `10` migrations.
- The restored Backend became healthy; `/livez` and `/readyz` returned `200`; login and Stress core reads passed. Its final precise log scan passed, SIGTERM exit was `0`, and no OOM occurred.
- Forward-only migration recovery used external ignored V11/V12 rehearsal files. V11 introduced an invalid state, V12 advanced it to `ready` and added the required constraint, then Flyway `validate` and no-op passed. Repository V1-V10 files and checksums were unchanged.
- Immutable image rollback: a local successor image exited `42` and made the Web liveness proxy return `502`; Compose was then recreated from `pfh-s12r-app-verified:f3abd40`. The restored App was healthy, readiness returned `200`, and data counts, login, and core reads all passed.

## 8. Phase Decision And Ownership

The macOS/Colima target-environment matrix required by S12 is complete with no remaining target-side blocker. Phase 2 itself remains `In Progress`: Windows must review the five signed target fixes and this final evidence, perform the Phase signature and documentation consolidation, and decide the merge into `main`.

No API key, Token, Cookie, database credential or connection string, dump, raw log, benchmark JSON, query plan, screenshot batch, image, binary, archive, or private machine path is committed. The target side returns ownership without merging `main` or marking Phase 2 complete.
