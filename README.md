# Personal Finance Hub (PFH)

Version: 0.1.0-alpha
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD

Personal Finance Hub 是一个高精度、可审计的个人财务后端。Phase 1 已完成账户、流水、转账、多币种汇率、报表、认证、PostgreSQL 持久化、Transactional Outbox、后台调度和 Docker 运行闭环。

---

## 1. 当前能力

- 多账户、分类、标签和用户偏好。
- Income、Expense、Adjustment 与 Transfer。
- 三种跨币种转账输入模式和三种手续费来源。
- Balance、Net Worth、Cash Flow 与 Dashboard Summary。
- JWT、Refresh Token rotation/reuse detection 与 session 撤销。
- PostgreSQL 16+、Flyway V1-V6、FORCE RLS 与双数据库角色。
- Transactional Outbox、重试、dead letter、幂等 Handler 和 Scheduler。
- FreeCurrencyAPI 主源、exchangerate.fun 整批备用与历史降级。
- Ubuntu 24.04 Docker 镜像、non-root、healthcheck 和优雅停止。

金额与汇率不使用二进制浮点：金额使用 `NUMERIC(20,8)`，汇率使用 `NUMERIC(20,10)`，JSON 金额使用十进制字符串。

---

## 2. 架构

```text
Presentation -> Application -> Domain <- Infrastructure
                                  ^
Bootstrap ------------------------+
```

- Domain：金融原语、实体、聚合和纯领域服务。
- Application：权限、事务、Repository 编排、查询和事件处理。
- Infrastructure：PostgreSQL、安全、Provider、Outbox 和调度。
- Presentation：Drogon HTTP、DTO、认证上下文和错误映射。
- Bootstrap：生产依赖装配与进程生命周期。

业务写入和 Outbox 写入共享同一个 Unit of Work；事件只在提交后发布。租户隔离由用户约束、复合外键和 FORCE RLS 共同保证。

---

## 3. 快速开始

### 3.1 依赖

- GCC/Clang with C++23 and `__int128`。
- CMake 3.20+。
- Ninja。
- Linux production ON：Drogon、PostgreSQL client、OpenSSL、Argon2、libcurl、`tzdata`。

### 3.2 Windows 快速回归

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DPFH_BUILD_POSTGRESQL=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

或运行：

```powershell
./quality_check.ps1
```

### 3.3 本地配置

```powershell
Copy-Item config/config.example.json config/config.local.json
```

编辑本地数据库凭据、JWT secret 和 Provider key。环境变量优先于 JSON；完整映射见 [config/README.md](config/README.md)。真实 secret 不得提交。

### 3.4 Production ON

```bash
cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPFH_BUILD_POSTGRESQL=ON
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

---

## 4. 项目结构

```text
include/pfh/          Public headers by architecture layer
src/                  Domain, Application, Infrastructure, Presentation, Bootstrap
tests/unit/           Unit and use-case tests
tests/integration/    In-Memory and PostgreSQL scenarios
tests/api/            HTTP contract and resource tests
tests/sql/            Migration and adapter gates
config/               Runtime configuration templates
migrations/           Flyway V1-V6
Docs/                 Architecture, plans, guides, standards and archive
```

---

## 5. 验收基线

| 门禁 | 结果 |
| ---- | ---- |
| Windows Debug / PostgreSQL OFF | 349/349 PASS |
| Windows Release / PostgreSQL OFF | 349/349 PASS |
| Linux Debug / PostgreSQL ON | 351/351 PASS |
| Linux Release / PostgreSQL ON | 351/351 PASS |
| PostgreSQL fixture | 12/12 scenarios PASS |
| Flyway V1-V6 | migrate/info/validate/no-op PASS |
| Docker | cold build、healthy、non-root、RLS、Outbox/Scheduler PASS |

---

## 6. 能力边界

- 当前实时汇率路径覆盖 20 种法币与 BTC。
- 其他 12 种加密货币没有实时保证，只能使用完整历史快照降级或返回不可用。
- 完整加密货币实时定价不在当前计划内。
- Phase 1 不开放转账聚合删除。
- 完整前端属于 Phase 2。
- 账单导入、银行和支付平台接入保留到 Phase 3。

---

## 7. 文档入口

- [文档中心](Docs/README.md)
- [总体开发计划](Docs/Development_Plans/Overall_Development_Plan.md)
- [Phase 1 开发记录](Docs/Archive/Phase_1_Development_Record.md)
- [技术架构](Docs/Architecture/01_Technical_Architecture.md)
- [OpenAPI 3.1](Docs/Architecture/10_REST_API_OpenAPI.json)
- [开发者快速参考](Docs/Guides/Quick_Reference.md)
- [Linux 工作流](Docs/Guides/Linux_Development_Workflow.md)
