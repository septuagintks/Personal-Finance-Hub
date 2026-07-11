# Phase 1 S07 数据库迁移与持久化基础 - 交付记录

**完成日期**: 2026-07-12  
**阶段**: P1-S07 数据库迁移与持久化基础  
**状态**: ✅ 已完成

---

## 1. 概述

根据 `Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md` 的 P1-S07 规划，本阶段建立 PostgreSQL 16+ 结构与 Flyway 迁移基础，为 S08 Repository 提供可落库 schema。

交付目标：

- 配置本地数据库连接与迁移工具
- 创建 Flyway 初始迁移与种子数据
- 覆盖 Phase 1 核心表、索引、外键与关键约束
- 提供可复现的本地开发环境

---

## 2. 交付物

### 2.1 Flyway 迁移脚本

| 版本 | 文件 | 内容 |
| ---- | ---- | ---- |
| V1 | `migrations/V1__initial_schema.sql` | 完整 Phase 1 schema（ENUM、表、索引、触发器） |
| V2 | `migrations/V2__seed_initial_currencies.sql` | 法币 + 受控加密货币白名单种子 |
| V3 | `migrations/V3__seed_system_category_templates.sql` | 系统分类模板池种子 |

**V1 核心表：**

- `currencies`
- `users` / `user_preferences`
- `accounts`
- `system_category_templates` / `categories`
- `transfer_groups` / `transactions`
- `transaction_tags` / `transaction_tag_relations`
- `account_balance_cache`
- `exchange_rates`（append-only 触发器保护）
- `audit_logs`
- `domain_events_outbox`
- `refresh_tokens` / `revoked_access_tokens`

**关键约束：**

- 金额：`NUMERIC(20,8)`
- 汇率：`NUMERIC(20,10)` / `NUMERIC(30,10)`
- 跨用户表含 `user_id` 或通过 FK 强约束用户边界
- `exchange_rates` 禁止 UPDATE/DELETE（append-only）
- `currencies.code` 使用 `VARCHAR(10)`，兼容 crypto ticker
- `category_board` 仅 `income | expense`（费用类 Adjustment 使用 expense board）

### 2.2 本地环境与配置

| 文件 | 说明 |
| ---- | ---- |
| `docker-compose.yml` | PostgreSQL 16 + Flyway 迁移服务 |
| `flyway.conf` | 本地 Flyway 配置 |
| `.env.example` | 环境变量模板（`PFH_*` 命名） |
| `Docs/Guides/Database_Migration_Guide.md` | 迁移操作手册 |

### 2.3 仓库保护

`.gitignore` 增加：

- `.env` / `.env.local` / `.env.*.local`
- `pgdata/` / `postgres_data/`
- `*.sql.tmp`

---

## 3. 设计要点

### 3.1 Append-only 汇率

```sql
CREATE TRIGGER trg_exchange_rates_no_update
    BEFORE UPDATE ON exchange_rates
    FOR EACH ROW
    EXECUTE FUNCTION forbid_exchange_rate_mutation();
```

历史汇率只能 INSERT，保证报表与转账回放可复现。

### 3.2 用户隔离索引

```sql
CREATE INDEX idx_accounts_user_id ON accounts(user_id);
CREATE INDEX idx_transactions_user_time ON transactions(user_id, transaction_time);
CREATE INDEX idx_categories_user_board ON categories(user_id, board);
```

### 3.3 Outbox / Transfer 索引

```sql
CREATE INDEX idx_outbox_pending
    ON domain_events_outbox(status, next_retry_at)
    WHERE status IN ('pending', 'failed');

CREATE INDEX idx_transactions_transfer_group
    ON transactions(transfer_group_id)
    WHERE transfer_group_id IS NOT NULL;
```

### 3.4 与 Domain / S08 对齐

- 金额采用带符号约定：income 正、expense 负、transfer 出负入正
- Domain `CategoryBoard` 仅 Income/Expense，与 DB enum 一致
- 配置环境变量优先 `PFH_*`，兼容旧无前缀名

---

## 4. 验收对照

| 验收项 | 状态 |
| ------ | ---- |
| 配置 PostgreSQL 16+ 连接与 Flyway | ✅ |
| 初始迁移覆盖核心表 | ✅ |
| 跨用户表含 user_id / FK 隔离 | ✅ |
| 汇率 append-only | ✅ |
| 金额/汇率 NUMERIC 精度 | ✅ |
| 迁移可在空库执行 | ✅ 脚本与 compose 已提供 |

对应 Tasks：

- [x] #28 配置 PostgreSQL 16+ 与 Flyway 迁移脚本
- [x] #29 编写 Phase 1 初始迁移

---

## 5. 本地验证

```powershell
cd e:\AMLY\works\C++\PFH
docker-compose up -d postgres
docker-compose up flyway
```

```sql
-- docker exec -it pfh-postgres psql -U pfh_user -d pfh_dev
\dt
SELECT code, is_crypto FROM currencies LIMIT 10;
SELECT name, default_board FROM system_category_templates WHERE parent_id IS NULL;
INSERT INTO exchange_rates (base_currency_code, target_currency_code, rate, source, fetched_at)
VALUES ('USD', 'CNY', 7.18, 'Test', NOW());
UPDATE exchange_rates SET rate = 7.20 WHERE source = 'Test'; -- 应失败
DELETE FROM exchange_rates WHERE source = 'Test';             -- 应失败
```

---

## 6. 遗留与后续

1. 本机 Docker 实际 migrate 建议再确认一次。
2. `accounts.version`：DB 为 `BIGINT`，Domain 当前为 `int32_t`；后续可统一到 `int64_t`。
3. 真实 SQL 仓储实现见 [S08 交付记录](Phase_1_S08_Delivery_Summary.md)。

---

## 7. 参考

- [Database Design](../Architecture/02_Database_Design.md)
- [Database Migration Guide](../Guides/Database_Migration_Guide.md)
- [Phase 1 Detailed Plan](../Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md)
- [S08 Delivery Summary](Phase_1_S08_Delivery_Summary.md)
- [Tasks](Tasks.md)
