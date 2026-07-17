# Personal Finance Hub 数据库迁移指南

Version: 2.0
Database: PostgreSQL 16+
Migration Tool: Flyway 10.22.0
Status: Active

---

## 1. 运行边界

PFH 只使用 `migrations/` 下的 Flyway versioned SQL 演进 schema。迁移由部署流程、Flyway CLI 或独立容器在应用启动前执行，`pfh_server` 不调用 shell 或嵌入 Flyway。

规则：

- 文件名为 `V<version>__<description>.sql`。
- 已发布 migration append-only，禁止修改文件内容或 checksum。
- Schema 修正使用下一个版本，不用手工 SQL 绕过 `flyway_schema_history`。
- 凭据从环境变量或 secret store 注入，不写入命令历史、文档或 Git。
- `clean` 只允许用于明确可删除的测试数据库，项目配置默认禁用。

---

## 2. 当前迁移

| 版本 | 文件 | 结果 |
| ---- | ---- | ---- |
| V1 | `V1__initial_schema.sql` | 核心 schema、约束、索引与 FORCE RLS |
| V2 | `V2__seed_initial_currencies.sql` | 20 种法币与 13 种加密货币 |
| V3 | `V3__seed_system_category_templates.sql` | 55 个 `zh-CN` 分类模板 |
| V4 | `V4__authentication_session_security.sql` | revoked session 与认证审计 |
| V5 | `V5__align_transfer_rate_precision.sql` | Transfer 汇率对齐 `NUMERIC(20,10)` |
| V6 | `V6__outbox_scheduler_foundation.sql` | Outbox lease、Handler receipt、system audit 与 Job lease |
| V7 | `V7__request_idempotency.sql` | 金融写请求幂等记录与 FORCE RLS |
| V8 | `V8__transaction_corrections.sql` | 追加式流水更正关系与 FORCE RLS |
| V9 | `V9__transfer_corrections.sql` | 追加式 Transfer 聚合更正关系与 FORCE RLS |

V1-V9 的空库迁移、重复执行、角色授权和结构门禁按 Phase 2 交付矩阵执行；V7-V9 新表由 `role-init` 纳入运行角色权限和 11 张 FORCE RLS 租户表断言。

---

## 3. Docker 工作流

### 3.1 配置 Secret

使用本地 `.env`、shell 环境或 secret store 设置：

- `PFH_POSTGRES_ADMIN_PASSWORD`。
- `PFH_DB_PASSWORD`。
- `PFH_BACKGROUND_DB_PASSWORD`。

不要使用文档中的占位值作为真实凭据。

### 3.2 空库迁移

```bash
docker compose up -d postgres
docker compose run --rm flyway migrate
docker compose run --rm flyway info
docker compose run --rm flyway validate
docker compose run --rm flyway migrate
```

第二次 `migrate` 必须为 no-op。随后初始化运行角色：

```bash
docker compose run --rm role-init
```

完整应用启动可使用：

```bash
docker compose up -d
docker compose ps
```

`app` 只有在 Flyway 与 role-init 成功后才启动。

---

## 4. 本地 Flyway CLI

仓库根目录的 `flyway.conf` 读取 `FLYWAY_PASSWORD`：

```bash
export FLYWAY_PASSWORD='<secret>'
flyway migrate
flyway info
flyway validate
flyway migrate
```

连接 URL、用户和 location 可通过 Flyway 环境变量覆盖。不要在共享配置文件中写真实密码。

---

## 5. 新增 Migration

1. 从当前最大版本递增，例如 V6 之后创建 `V7__<description>.sql`。
2. 设计向后兼容的 schema 变化。
3. 在文件头说明目的、数据影响和运行前提。
4. 在空库执行全部 migration。
5. 从上一发布 schema 执行 upgrade。
6. 运行 `info`、`validate` 和第二次 no-op。
7. 更新数据库设计、静态契约测试和交付摘要。

示例：

```sql
-- Version: 7
-- Description: Add an optional account display field
ALTER TABLE accounts
    ADD COLUMN display_note VARCHAR(128);
```

不得在已发布 migration 中追加“顺手修正”。

---

## 6. 兼容性规则

- 新增列应允许 NULL 或提供安全默认值。
- 删除列采用 expand/contract：先停止读取，再在后续版本删除。
- 重命名采用新增、回填、双读/双写切换、删除旧列的多版本流程。
- 新约束上线前先检查并修复现有数据。
- 大表索引、回填和锁表操作必须在接近生产规模的数据副本上评估。
- Migration 不调用外部网络，不依赖应用进程或用户交互。
- 每个脚本应保持 PostgreSQL 事务语义；无法事务化的操作必须单独评审。

---

## 7. 失败与恢复

### 7.1 Migration 执行失败

PostgreSQL 事务内失败会回滚该 migration。先修复尚未发布的脚本，再在可删除测试库重跑完整链路。不要通过手工修改 `flyway_schema_history` 掩盖失败。

### 7.2 已发布变更有缺陷

创建新的向前修复 migration。应用回滚要求 schema 对上一应用版本仍兼容；不要依赖 Flyway Undo 或删除已应用 migration。

### 7.3 Checksum mismatch

先确认 migration 是否被意外修改：

```bash
git diff -- migrations/
flyway validate
```

恢复已发布文件的原始内容，并用新版本表达修正。`flyway repair` 只用于已经人工确认 schema 与仓库完全一致的本地/测试元数据修复，不能作为生产常规流程。

---

## 8. 验收清单

每次数据库变更至少验证：

1. PostgreSQL 16+ 空库 `migrate`。
2. 从上一发布版本 upgrade。
3. `info`、`validate` 与第二次 no-op。
4. 种子数量、父子关系、enum cast、CHECK、FK 和索引。
5. FORCE RLS、两用户隔离与未绑定 tenant fail closed。
6. request/background 角色权限。
7. `NUMERIC`、事务、Repository 和 Outbox fixture。
8. Docker cold start 与应用 runtime smoke。

完整 Linux/PostgreSQL 流程见 [Linux Development Workflow](Linux_Development_Workflow.md)，schema 契约见 [Database Design](../Architecture/02_Database_Design.md)，测试规则见 [Testing Strategy](../Architecture/16_Testing_Strategy.md)。
