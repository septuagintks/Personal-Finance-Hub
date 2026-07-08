# Personal Finance Hub - Local Test Environment

**Version**: 1.0  
**Last Updated**: 2026-07-09  
**Scope**: Local Linux validation, Colima-based development checks, ignored local artifacts

---

## 1. Purpose

This document records the local test environment used to validate the PFH backend on Linux.

The project targets Linux deployment, so local feature work should periodically be checked with a Linux toolchain even when the primary development machine is macOS or Windows.

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

## 8. Follow-Up Notes

- Keep `build/` and `config/config.local.json` local.
- Run Linux validation before phase delivery or before merging phase work back to `main`.
- Re-run PostgreSQL integration checks after P1-S07 / P1-S08 introduces repository and database integration work.
- Re-run API smoke tests after P1-S10 introduces the Drogon API surface.
