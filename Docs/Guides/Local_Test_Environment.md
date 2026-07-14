# Personal Finance Hub - Local Test Environment

**Version**: 1.3
**Last Updated**: 2026-07-15
**Scope**: Historical validation, S10 production preflight, and current S12 Linux/Docker gate

---

## 1. Purpose

This document records the local test environment used to validate the PFH backend on Linux.

The project targets Linux deployment, so local feature work should periodically be checked with a Linux toolchain even when the primary development machine is macOS or Windows.

The GCC 13 / 142-test result in Sections 2-7 is historical. Section 8 records the S10 production preflight at commit `db07d64`. Section 9 records the current combined S12 Linux/PostgreSQL/Docker result. Windows still owns the final S12-07 review and Phase 1 sign-off.

---

## 2. Host Environment

- Host OS: macOS / Darwin
- Workspace path: project root mounted into the Colima VM
- Git branch verified: `feature/phase1-foundation`
- Linux provider: Colima VM
- Container runtime: Docker runtime inside Colima

The Colima VM was stopped after the validation run.

---

## 3. Linux Test Environment

The Linux validation was executed inside the Colima VM.

- Distribution: Ubuntu 24.04.4 LTS
- Kernel: Linux 6.8.0-117-generic
- Architecture: aarch64
- CPU allocation: 2 CPUs
- Memory allocation: approximately 2 GiB

This environment matches the recommended Ubuntu 24.04 workflow described in [Linux_Development_Workflow.md](./Linux_Development_Workflow.md).

---

## 4. Toolchain

The following toolchain versions were installed and verified inside the Linux VM:

- GCC: `gcc-13 (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`
- G++: `g++-13 (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`
- CMake: `3.28.3`
- Ninja: `1.11.1`
- Git: Ubuntu package version available in the Colima VM

The Debug build was configured with:

```bash
export CC=gcc-13
export CXX=g++-13
```

---

## 5. Local Artifacts

The Linux validation creates local files that should not be committed.

### 5.1 Build Directory

```text
build/linux-gcc-debug
```

This directory is ignored by the existing `build/` rule in `.gitignore`.

### 5.2 Local Configuration

```text
config/config.local.json
```

This file is ignored by:

```gitignore
config/*.local.*
```

For the service startup check, the file was copied from `config/config.example.json` and given a local test JWT secret:

```json
"secret": "local-linux-test-secret"
```

Real secrets, database passwords, JWT secrets, API keys, and environment-specific credentials must stay out of Git.

---

## 6. Validation Commands

The Linux validation followed the standard workflow:

```bash
git diff --check

cmake -S . -B build/linux-gcc-debug \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build/linux-gcc-debug

ctest --test-dir build/linux-gcc-debug --output-on-failure
```

The service startup check used:

```bash
cp config/config.example.json config/config.local.json
./build/linux-gcc-debug/pfh_server
```

The example configuration intentionally uses placeholder secrets. Starting the service directly with the example JWT secret fails as expected:

```text
Failed to load configuration: JWT secret still holds a placeholder value; set a real secret
```

After replacing the local JWT secret in `config/config.local.json`, the startup check completed successfully.

---

## 7. Validation Results

### 7.1 Git Whitespace Check

```text
git diff --check: PASS
```

### 7.2 CMake Configure

```text
CMake configure: PASS
Generator: Ninja
Build Type: Debug
Compiler: GNU 13.3.0
```

The configure step resolved the following dependencies through FetchContent:

- GoogleTest
- spdlog 1.14.1
- nlohmann_json 3.11.3

### 7.3 Build

```text
cmake --build build/linux-gcc-debug: PASS
35/35 build steps completed
```

Main build outputs:

- `build/linux-gcc-debug/pfh_server`
- `build/linux-gcc-debug/tests/unit/pfh_unit_tests`

### 7.4 Unit Tests

```text
ctest --test-dir build/linux-gcc-debug --output-on-failure: PASS
142/142 tests passed
0 tests failed
Total Test time: 0.41 sec
```

### 7.5 Service Startup

```text
./build/linux-gcc-debug/pfh_server: PASS
Configuration loaded successfully
Logger initialized
Initialization complete (application logic pending)
```

---

## 8. Current S10 Production Preflight

The S10 preflight completed on 2026-07-15 using macOS 26.5.2 ARM64 with Colima 0.10.3 and Ubuntu 24.04.4 ARM64.

| Item | Result |
| ---- | ------ |
| Commit | `db07d64dbc0d70e9cc50709c4bfb8247fc4b52da` |
| Linux toolchain | GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, tzdata 2026a |
| Runtime dependencies | Drogon 1.8.7, PostgreSQL 16.14, OpenSSL 3.0.13, Argon2 20190702 |
| Debug production ON | Build PASS, CTest 321/321 |
| Release production ON | Build PASS, CTest 321/321 |
| Flyway | V1-V5 migrate/info/validate PASS; second migrate no-op |
| Production startup | Distinct request/background roles PASS |
| API smoke | Auth, two-user isolation, accounts, transactions, transfer and reports PASS |

The run found and fixed four real-environment defects in signed commit `db07d64`: a libstdc++ 13 `std::quoted` overload collision, missing real Drogon Row/Field includes, unsupported `Field::as<trantor::Date>()` conversion, and a four-byte integer bound to PostgreSQL `SMALLINT`. The disposable service, database container and network were removed after validation; no credentials, tokens or database dump were retained.

This was a foundation preflight, not a full PostgreSQL integration suite. The combined S12 validation in the next section supersedes its pending-work statement.

---

## 9. Current S12 Linux and Docker Gate

The P1-S12-02 through S12-06 run used the same Colima Ubuntu 24.04 ARM64 environment after the S11 implementation was integrated.

| Item | Result |
| ---- | ------ |
| Production baseline | `ed0b10f4567232d5558914464092a24213958941` plus the S12 delivery changes |
| Linux dependencies | GCC 13.3.0, Drogon 1.8.7, Trantor 1.5.12, PostgreSQL 16.14, OpenSSL 3.0.13, Argon2 20190702 |
| Debug production ON | Configure/build PASS, CTest 343/343 |
| Release production ON | Configure/build PASS, CTest 343/343 |
| Debug PostgreSQL OFF | Fresh 88/88-step build, CTest 341/341 |
| Flyway | V1-V6 migrate/info/validate/no-op and V1-V5 legacy upgrade PASS |
| PostgreSQL fixture | 12/12 real scenarios PASS; CTest target is mandatory when production is ON |
| Drogon runtime | Auth, RLS, finance/report API, headers and SIGTERM PASS |
| Docker runtime | Healthy, non-root, dual role, Outbox/Scheduler and exit 0 PASS |

Production ON adds two CTest entries to the 341-test base: `postgresql_integration` runs 12 real database scenarios, and `drogon_runtime_integration` starts the production server against the same disposable database. The outer CTest environment should expose only these connection strings:

```bash
export PFH_TEST_DB_ADMIN='host=127.0.0.1 port=<port> dbname=<db> user=<admin> password=<temporary-password>'
export PFH_TEST_DB_REQUEST='host=127.0.0.1 port=<port> dbname=<db> user=<request-role> password=<temporary-password>'
export PFH_TEST_DB_BACKGROUND='host=127.0.0.1 port=<port> dbname=<db> user=<background-role> password=<temporary-password>'

ctest --test-dir build/s12-linux-debug --output-on-failure
ctest --test-dir build/s12-linux-release --output-on-failure
```

Do not export `PFH_DB_*` to the full CTest process. The runtime script parses the request/background libpq conninfo and injects application variables only into its `pfh_server` child. It also disables HTTP proxies for loopback requests because this Colima profile does not bypass `127.0.0.1` automatically.

The Docker gate cold-built the repository `Dockerfile` from Ubuntu 24.04 base digest `sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90`. The final ARM64 image was `sha256:b2e161b3a551b06c50d8a31760397e2e15f49e70e8049e391692f4b6a5af9217`, 36,763,570 bytes, with tzdata 2026b. It ran as user `pfh`, reached `healthy`, published 11/11 Outbox rows, released both scheduled job leases, preserved FORCE RLS on all eight tenant tables, and stopped with exit code 0 without OOM.

The real OpenExchangeRates HTTPS/API response remains `BLOCKED` because no external API key was supplied. Mock transport tests and a dummy-key Scheduler run are not evidence that the real provider passed.

All disposable S12 application/database containers and the dedicated network were removed after validation, and Colima was stopped. No test password, JWT, API key, token, response body, raw log or database dump was retained.

---

## 10. Follow-Up Notes

- The recorded GCC 13 / 142-test run predates the timezone-aware reporting
  implementation. Current HEAD requires the CMake chrono-tzdb probe to pass and
  must be revalidated on Linux with `tzdata`; this historical result is not a
  current-head Linux sign-off.
- A later external PostgreSQL 16.14/Flyway 10.22.0 run validated V1-V3 and the
  then-current 254-test baseline; see
  `Docs/Development/Phase_1_S10_PostgreSQL_Persistence_Validation_Report.md`.
  The S10 preflight superseded that build/runtime baseline; the S12 run above now supersedes both for current Linux behavior.
- Keep `build/` and `config/config.local.json` local.
- Run Linux validation before phase delivery or before merging phase work back to `main`.
- Windows S12-07 must verify the returned signed commit, review the new fixture/Docker changes, rerun its PostgreSQL OFF gate and decide how to handle the external Provider blocker.
- Record the final returned commit hash and blocker decision before Phase 1 sign-off.
