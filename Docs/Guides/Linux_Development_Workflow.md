# Personal Finance Hub Linux 开发与交付工作流

Version: 2.0
Status: Active

---

## 1. 目标

PFH 的生产部署目标是 Linux。Windows/PostgreSQL OFF 用于快速回归，阶段交付必须在 Linux production ON、真实 PostgreSQL 和 Docker 环境验证。

推荐环境：

- Ubuntu 24.04 LTS。
- GCC 13+ 或兼容 Clang/libstdc++。
- CMake 3.20+、Ninja。
- PostgreSQL 16+。
- Drogon、OpenSSL、Argon2、libcurl。
- `tzdata`。
- Docker。

---

## 2. 环境准备

### 2.1 Ubuntu Packages

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build git pkg-config tzdata \
  libdrogon-dev libpq-dev libssl-dev libargon2-dev \
  libcurl4-openssl-dev
```

使用 vcpkg 时，以项目 `vcpkg.json` 为依赖清单，并把 toolchain 传给 CMake。

### 2.2 工具检查

```bash
uname -a
cmake --version
ninja --version
g++ --version
git --version
docker version
```

CMake configure 会探测 `std::chrono` IANA tzdb；探针失败时不得用 UTC fallback 绕过。

---

## 3. 快速 PostgreSQL OFF 回归

```bash
cmake -S . -B build/linux-off -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPFH_BUILD_POSTGRESQL=OFF
cmake --build build/linux-off
ctest --test-dir build/linux-off --output-on-failure
```

OFF 模式运行 Domain、Application、In-Memory integration、framework-neutral API 和 static gates，并编译 production adapter stubs。它不能替代真实 ABI、SQL、RLS 或运行时测试。

---

## 4. Production ON 构建

### 4.1 Debug

```bash
cmake -S . -B build/linux-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPFH_BUILD_POSTGRESQL=ON
cmake --build build/linux-debug --parallel
```

### 4.2 Release

```bash
cmake -S . -B build/linux-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPFH_BUILD_POSTGRESQL=ON
cmake --build build/linux-release --parallel
```

Production ON 必须真实解析并链接 Drogon、PostgreSQL、OpenSSL、Argon2 和 libcurl。缺依赖应明确失败，不得用 OFF stub 结果替代。

---

## 5. 数据库与迁移

### 5.1 一次性测试环境

```bash
docker compose up -d postgres
docker compose run --rm flyway migrate
docker compose run --rm flyway info
docker compose run --rm flyway validate
docker compose run --rm flyway migrate
```

第二次 migrate 应为 no-op。只对可删除的测试数据库或 volume 执行初始化，不操作个人数据。

### 5.2 双数据库角色

Production runtime 要求：

- request role：non-superuser、non-BYPASSRLS。
- background role：独立、non-superuser、BYPASSRLS、`default_transaction_read_only=on`。
- background role 仅具有 `accounts(currency_code, is_archived)` 与 `users(base_currency_code)` 的列级 `SELECT` 权限；`users.password_hash` 等其他字段必须不可读。
- 租户表保持 ENABLE/FORCE RLS。
- USER/OPERATOR 是 `users.role` 中的应用授权事实，不是数据库角色；Operator HTTP 请求仍使用 request role。
- V10 幂等清理函数只向 request role 授予 EXECUTE，不授予 `request_idempotency` 的跨租户直接读写。

凭据通过环境变量、忽略的本地配置或 secret store 注入。

### 5.3 CTest Connection Strings

```bash
export PFH_TEST_DB_ADMIN='host=127.0.0.1 port=<port> dbname=<db> user=<admin> password=<secret>'
export PFH_TEST_DB_REQUEST='host=127.0.0.1 port=<port> dbname=<db> user=<request> password=<secret>'
export PFH_TEST_DB_BACKGROUND='host=127.0.0.1 port=<port> dbname=<db> user=<background> password=<secret>'
```

不要把 `PFH_DB_*` 注入整个 CTest 进程。Runtime integration 会只向子 `pfh_server` 传入应用配置，避免污染配置单元测试。

---

## 6. 全量测试

```bash
ctest --test-dir build/linux-debug --output-on-failure
ctest --test-dir build/linux-release --output-on-failure
```

Production ON 必须实际执行：

- `postgresql_integration`。
- `drogon_runtime_integration`。

验收范围：

- V1-V10、legacy upgrade、种子数据和 V10 函数授权。
- Unit of Work commit/rollback 与 Outbox 原子性。
- FORCE RLS、两用户隔离和连接池复用。
- Repository、并发锁、缓存和 NUMERIC 边界。
- 认证、核心财务 API、报表、响应头和脱敏。
- 用户维护、USER/OPERATOR 授权、探针、Metrics 和 dead-letter 重试脱敏。
- Outbox claim/retry/dead letter、四个 Scheduler Job、lease 和优雅停止。

### 6.1 Web Release Matrix

```bash
corepack pnpm install --frozen-lockfile
corepack pnpm --dir frontend quality
corepack pnpm --dir frontend exec playwright install chromium firefox webkit
PLAYWRIGHT_FULL_BROWSERS=1 corepack pnpm --dir frontend e2e
corepack pnpm audit --prod --audit-level high
```

`PLAYWRIGHT_EXECUTABLE_PATH` 只用于本机单浏览器验证，不得与 `PLAYWRIGHT_FULL_BROWSERS=1` 同时设置。

### 6.2 Fixed Performance Profiles

只在可删除的 PostgreSQL 测试库和专用测试用户上执行。先通过 API 创建用户并取得短期 access token；数据库 URL 使用具备夹具写入权限的测试角色，任何凭据和结果原文都不得提交。

```bash
export PFH_PERF_DATABASE_URL='<test-database-uri>'
export PFH_PERF_BASE_URL='http://127.0.0.1:8081'
export PFH_PERF_ACCESS_TOKEN='<ephemeral-access-token>'

python3 tools/phase2_performance.py seed \
  --profile daily --user-id <test-user-id> --confirm-test-database
python3 tools/phase2_performance.py benchmark \
  --profile daily --base-url "$PFH_PERF_BASE_URL" --enforce \
  --output /tmp/pfh-phase2-daily.json
python3 tools/phase2_performance.py explain \
  --profile daily --user-id <test-user-id> --confirm-test-database --enforce \
  --output /tmp/pfh-phase2-daily-plans.json

python3 tools/phase2_performance.py seed \
  --profile stress --user-id <test-user-id> --confirm-test-database
python3 tools/phase2_performance.py benchmark \
  --profile stress --base-url "$PFH_PERF_BASE_URL" --enforce \
  --output /tmp/pfh-phase2-stress.json
python3 tools/phase2_performance.py explain \
  --profile stress --user-id <test-user-id> --confirm-test-database --enforce \
  --output /tmp/pfh-phase2-stress-plans.json
```

每次 seed 会先清理该用户的 `PFH-PERF-*` 夹具，使 Daily 与 Stress 互斥。Daily 标签报表覆盖 120 个月和全部 10,000 条夹具；Stress 标签报表固定使用上一个完整历史月，使均匀分布的夹具保持在 10,000 条 detailed-report 安全上限内，同时仍以接近上限的数据量验证 100,000 条总流水下的查询路径。记录 p50/p95、响应大小、数据库/应用资源、页面指标和关键查询的 `EXPLAIN (ANALYZE, BUFFERS)` 脱敏摘要；不要提交完整数据库 URL、Token、原始日志或大体积结果。

### 6.3 Sanitizer、压力与浏览器 Heap

Sanitizer 使用独立 build tree，并分别运行 ASan 和 UBSan：

```bash
PFH_SANITIZER_POSTGRESQL=OFF tools/run_sanitizers.sh
PFH_SANITIZER_BUILD_ROOT=build/sanitizers-postgresql \
  PFH_SANITIZER_POSTGRESQL=ON tools/run_sanitizers.sh
```

PostgreSQL ON 前先设置第 5.3 节三个测试连接串。切换 GCC/Clang 时使用不同的 `PFH_SANITIZER_BUILD_ROOT`，避免 CMake cache 复用编译器；任何 sanitizer 告警均阻断交付。

压力测试直接连接 Backend 的 HTTP 测试端口，并使用短期测试 Token。`PFH_PRESSURE_SERVER_PID` 必须是该次 `pfh_server` 进程，Operator Token 只用于采集受控拒绝计数：

```bash
export PFH_PRESSURE_BASE_URL='http://127.0.0.1:8081'
export PFH_PRESSURE_ACCESS_TOKEN='<ephemeral-access-token>'
export PFH_PRESSURE_OPERATOR_TOKEN='<ephemeral-operator-token>'
export PFH_PRESSURE_SERVER_PID='<pfh-server-pid>'

python3 tools/phase2_pressure.py --scenario read \
  --requests 500 --concurrency 32 --enforce --output /tmp/pfh-pressure-read.json
python3 tools/phase2_pressure.py --scenario csv \
  --requests 20 --concurrency 4 --enforce --output /tmp/pfh-pressure-csv.json
python3 tools/phase2_pressure.py --scenario auth \
  --requests 100 --concurrency 8 --enforce --output /tmp/pfh-pressure-auth.json
python3 tools/phase2_pressure.py --scenario queue \
  --requests 500 --concurrency 384 --enforce \
  --output /tmp/pfh-pressure-queue.json
python3 tools/phase2_pressure.py --scenario disconnect \
  --requests 100 --concurrency 16 --enforce --output /tmp/pfh-pressure-disconnect.json
```

每个场景都必须通过状态、恢复时间、峰值 RSS 与冷却后 RSS 预算；auth 与 queue 还必须在 Operator Metrics 中观察到对应 admission rejection 增长，否则不构成饱和测试。queue 使用无副作用的 Dashboard 慢读和高于默认 256 task queue 的并发，避免依赖 HTTP body 到达时序触发 admission；断连场景使用 Backend 直连 HTTP，避免把反向代理 TLS 行为误记为应用进程结果。

Chromium heap 门禁与完整三浏览器矩阵分开执行：

```bash
corepack pnpm --dir frontend exec playwright test \
  e2e/long-session-memory.spec.ts --project=local-chromium
PLAYWRIGHT_FULL_BROWSERS=1 corepack pnpm --dir frontend e2e
```

heap 用例通过 CDP 强制 GC；Firefox/WebKit 项目会跳过该单项，但必须通过其余完整 E2E。

---

## 7. Provider 验证

真实 Provider 测试从仓库外设置：

```bash
export PFH_FREECURRENCYAPI_API_KEY='<secret>'
```

至少验证：

1. FreeCurrencyAPI 支持批次写入实际主源 source。
2. 主源失败后 exchangerate.fun 对原批次整体成功。
3. 两个 Provider 失败时无新快照。
4. 完整与不完整历史正确设置 `historicalAvailable`。
5. Event、Outbox、lease 和日志脱敏正确。

不记录 key、完整 URL query、响应正文或原始日志。

---

## 8. Docker 门禁

```bash
docker build --pull --no-cache -t pfh:validation .
docker compose up -d
docker compose ps
```

验收：

- 容器以 non-root 用户运行并进入 healthy。
- `tzdata`、CA 和生产共享库完整。
- request/background 角色与 FORCE RLS 正确。
- 公共 currencies、认证和核心财务 API 可用。
- `/livez` 与 `/readyz` 语义正确，普通 USER 无法访问 operations API，Operator smoke 不泄露 payload 或内部错误。
- Outbox/Scheduler 运行并释放 lease。
- JSON Content-Type 唯一，ETag 与 TraceId 存在。
- SIGTERM 后优雅停止并 exit 0，无 OOM。

测试后只删除本轮创建的容器、network、volume 和本地 secret 文件。

---

## 9. 阶段交付检查

合并 Phase 分支前至少确认：

- `git diff --check` 通过。
- Linux Debug/Release production ON 构建通过。
- 两个 production CTest target 实际执行。
- Flyway migrate/info/validate/no-op 通过。
- PostgreSQL、Drogon runtime 和 Docker 门禁通过。
- 文档、OpenAPI、migration 与代码一致。
- Secret scan 无真实凭据。
- 交付摘要记录固定 commit、环境、测试数量和能力边界。

结果写入对应 Phase 的 `Docs/Archive/Phase_N_Sxx-Syy_Delivery_Summary.md`，不另建逐轮环境日志。

---

## 10. 相关文档

- [Dependency Installation Guide](Dependency_Installation_Guide.md)
- [Ubuntu Server Deployment Guide](Ubuntu_Server_Deployment_Guide.md)
- [Database Migration Guide](Database_Migration_Guide.md)
- [Quick Reference](Quick_Reference.md)
- [Testing Strategy](../Architecture/16_Testing_Strategy.md)
