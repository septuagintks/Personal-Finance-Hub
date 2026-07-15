# Personal Finance Hub 开发者快速参考

Version: 1.0
Status: Active

---

## 1. 本地构建

### 1.1 Windows 快速回归

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DPFH_BUILD_POSTGRESQL=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

综合检查：

```powershell
./quality_check.ps1
```

### 1.2 Linux Production Build

```bash
cmake -S . -B build/linux-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPFH_BUILD_POSTGRESQL=ON
cmake --build build/linux-release
ctest --test-dir build/linux-release --output-on-failure
```

Production ON 需要 Drogon、PostgreSQL client、OpenSSL、Argon2、libcurl 和 `tzdata`。完整流程见 [Linux Development Workflow](Linux_Development_Workflow.md)。

---

## 2. 配置

创建本地配置：

```powershell
Copy-Item config/config.example.json config/config.local.json
```

环境变量优先于 JSON。主要变量：

- `PFH_ENVIRONMENT`
- `PFH_JWT_SECRET`
- `PFH_PASSWORD_PEPPER`
- `PFH_DB_*`
- `PFH_BACKGROUND_DB_*`
- `PFH_FREECURRENCYAPI_API_KEY`

真实 secret 只放在本地配置、环境变量或 secret store。完整说明见 [`config/README.md`](../../config/README.md)。

---

## 3. 数据库

```powershell
docker compose up -d postgres
docker compose run --rm flyway migrate
docker compose run --rm flyway validate
docker compose run --rm flyway info
```

迁移文件 append-only。已发布 migration 不改写 checksum；修正使用新版本。详细规则见 [Database Migration Guide](Database_Migration_Guide.md)。

---

## 4. 测试入口

| 类型 | 位置 | 目的 |
| ---- | ---- | ---- |
| Unit | `tests/unit/` | Domain、Application、配置、调度和 Provider |
| Integration | `tests/integration/` | In-Memory 语义与 PostgreSQL scenarios |
| API | `tests/api/` | HTTP 契约与资源流程 |
| Static | `tests/sql/` 等 | migration、OpenAPI、币种和 adapter 契约 |

测试命名遵循 `<Class>_When<Condition>_<Expected>`，详见 [`tests/TEST_NAMING_CONVENTION.md`](../../tests/TEST_NAMING_CONVENTION.md)。

最终 Phase 1 基线：Windows PostgreSQL OFF 349/349，Linux production ON 351/351，PostgreSQL fixture 12/12 scenarios。

---

## 5. Git 工作流

```powershell
git status --short --branch
git diff --check
./quality_check.ps1
```

- 每个 Phase 使用独立分支。
- 代码、测试、文档和交付摘要完成后再合并 `main`。
- Codex 创建提交时必须遵循项目 `git-signing` Skill。
- 不使用 force-push、reset 或 clean 处理未知改动。

---

## 6. 架构速记

```text
Presentation -> Application -> Domain <- Infrastructure
```

- 金额与汇率不使用二进制浮点。
- Domain Service 无 Repository/事务/事件副作用。
- Application 管理事务和权限。
- 业务事实与 Outbox 同事务。
- 租户访问受 `user_id` 与 FORCE RLS 约束。
- API 金额使用字符串，错误响应包含 TraceId 且不泄露内部细节。

---

## 7. 文档入口

- [文档中心](../README.md)
- [总体开发计划](../Development_Plans/Overall_Development_Plan.md)
- [Phase 1 开发计划](../Development_Plans/Phase_1/Phase_1_Development_Plan.md)
- [Phase 1 开发记录](../Archive/Phase_1_Development_Record.md)
- [技术架构](../Architecture/01_Technical_Architecture.md)
- [REST API OpenAPI](../Architecture/10_REST_API_OpenAPI.json)
