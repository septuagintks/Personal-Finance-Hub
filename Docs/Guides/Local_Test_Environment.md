# Personal Finance Hub - Local Test Environment

**Version**: 1.2
**Last Updated**: 2026-07-15
**Scope**: Historical validation, current S10 production preflight, and pending final S12 sign-off

---

## 1. Purpose

This document records the local test environment used to validate the PFH backend on Linux.

The project targets Linux deployment, so local feature work should periodically be checked with a Linux toolchain even when the primary development machine is macOS or Windows.

The GCC 13 / 142-test result in Sections 2-7 is historical. Section 8 records the current S10 production preflight at commit `db07d64`. That preflight validates the real dependency ABI, V1-V5 and the core API runtime, but it is not the final S12 sign-off because exhaustive PostgreSQL fixtures, S11 background jobs and the application image remain pending.

---

## 2. Host Environment

- Host OS: macOS / Darwin
- Workspace path: `/Users/septu/EMT/WorkSpace/C++/PFH`
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

This was a foundation preflight, not a full PostgreSQL integration suite. S11 has since completed its Windows implementation and 341/341 local regression, but the combined binary has not been rerun here. Repository/UoW scenario parity, connection-pool reuse, concurrent locks, failure injection, NUMERIC boundary matrices, V6/background runtime and the application Docker image remain P1-S12 work.

---

## 9. Follow-Up Notes

- The recorded GCC 13 / 142-test run predates the timezone-aware reporting
  implementation. Current HEAD requires the CMake chrono-tzdb probe to pass and
  must be revalidated on Linux with `tzdata`; this historical result is not a
  current-head Linux sign-off.
- A later external PostgreSQL 16.14/Flyway 10.22.0 run validated V1-V3 and the
  then-current 254-test baseline; see
  `Docs/Development/Phase_1_S10_PostgreSQL_Persistence_Validation_Report.md`.
  The newer S10 preflight above supersedes that build/runtime baseline, while final P1-S12 still requires the omitted fixture and release gates.
- Keep `build/` and `config/config.local.json` local.
- Run Linux validation before phase delivery or before merging phase work back to `main`.
- Re-run the completed S10 PostgreSQL/Drogon composition root and shared repository scenarios as a full PostgreSQL fixture in P1-S12.
- Re-run the real Drogon API smoke after S11 integration, including V6, OpenExchangeRates, Outbox claim/recovery, scheduled leases, token cleanup and graceful shutdown; the S10 preflight is not evidence for the later combined binary.
- Record the current commit hash, environment versions, Debug/Release commands, Docker startup/health result, migration result, PostgreSQL tests, API tests and Outbox/Scheduler tests before Phase 1 sign-off.
