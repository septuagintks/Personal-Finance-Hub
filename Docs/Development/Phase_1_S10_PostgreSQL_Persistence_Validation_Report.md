# Phase 1 S10 PostgreSQL Persistence - 验证报告

**验证日期**: 2026-07-13
**验证提交**: `0a1ac5f7600f3ba5573520826847c08bb783b27a`
**对应任务**: `Tasks.md` #46 / P1-S10-03 / P1-S10-04 / P1-S12-03
**验证结论**: **FAIL / BLOCKED**

---

## 1. 验证目标

本轮针对以下验收目标执行验证：

- 实现 `DrogonUnitOfWork`。
- 实现 PostgreSQL 版 `*RepositoryImpl`。
- 在 composition root 中以真实持久化替换 In-Memory 实现。
- 将现有 Repository integration scenarios 对 PostgreSQL 16+ 真实测试库复跑。
- 在空库上完成 Flyway V1-V3 迁移。

---

## 2. 验证环境

- Host: macOS / Darwin ARM64
- Linux VM: Colima / Ubuntu 24.04 / Linux 6.8 / aarch64
- Compiler: GCC / G++ 13.3.0
- CMake: 3.28.3
- Ninja: 1.11.1
- PostgreSQL: 16.14 (`postgres:16-alpine`)
- Flyway: 10.22.0 (`flyway/flyway:10`)

数据库验证使用隔离 Docker network 与 tmpfs 数据目录，验证后已删除临时容器和网络，未保留测试数据卷。

---

## 3. 验证结果摘要

| 验收项 | 结果 | 说明 |
| ------ | ---- | ---- |
| `DrogonUnitOfWork` | FAIL | 当前源码中不存在实现 |
| PostgreSQL `*RepositoryImpl` | FAIL | 当前源码中不存在实现 |
| Drogon/PostgreSQL CMake 接线 | FAIL | `find_package(Drogon)` 与 `find_package(PostgreSQL)` 仍被注释 |
| composition root 使用真实持久化 | FAIL | `main.cpp` 仍是启动占位，不装配 Repository/UoW |
| PostgreSQL integration test target | FAIL | 当前 integration target 只构造 In-Memory Repository |
| 同一批 scenarios 在真实库复跑 | BLOCKED | 无 PostgreSQL adapter 与测试 fixture，无法执行 |
| PostgreSQL 16 空库迁移 | FAIL | V3 enum 类型错误，迁移回滚 |
| Linux Debug build | PASS | 43/43 build steps completed |
| Linux Debug tests | PASS | 253/253 passed |
| Linux Release build | PASS | 43/43 build steps completed |
| Linux Release tests | PASS | 253/253 passed |
| Debug/Release bootstrap smoke | PASS | 配置读取与 logger 初始化成功 |

现有 253 个测试由 240 个 unit/use-case tests 和 13 个 In-Memory repository integration tests 构成。它们证明当前 Domain/Application/In-Memory 基线没有回归，但不能作为真实 PostgreSQL 持久化验收结果。

---

## 4. 阻断问题

### 4.1 真实持久化 adapter 尚未实现

源码检索未发现以下目标：

- `DrogonUnitOfWork`
- PostgreSQL `AccountRepositoryImpl`
- PostgreSQL `TransactionRepositoryImpl`
- PostgreSQL `UserRepositoryImpl`
- PostgreSQL `UserPreferenceRepositoryImpl`
- PostgreSQL `CategoryRepositoryImpl`
- PostgreSQL `ExchangeRateRepositoryImpl`

当前 `tests/integration/repository_integration_test.cpp` 明确使用：

- `InMemoryStore`
- `InMemoryUnitOfWork`
- `InMemoryAccountRepository`
- `InMemoryTransactionRepository`
- `InMemoryExchangeRateRepository`
- `InMemoryUserRepository`
- `InMemoryUserPreferenceRepository`

因此现有 integration scenarios 没有连接 PostgreSQL，也没有验证 SQL、连接池、事务对象、RLS、真实行锁或数据库约束。

### 4.2 V3 空库迁移失败

执行：

```bash
flyway migrate
```

环境：

```text
PostgreSQL 16.14
Flyway OSS 10.22.0
```

结果：

```text
V1__initial_schema.sql: PASS
V2__seed_initial_currencies.sql: PASS
V3__seed_system_category_templates.sql: FAIL
```

数据库错误：

```text
SQL State: 42804
ERROR: column "default_board" is of type category_board
       but expression is of type text
```

首个失败点位于 `V3__seed_system_category_templates.sql` 的二级分类 `INSERT ... SELECT ... UNION ALL` 段。`'expense'` 和 `'income'` 在该表达式中被推断为 `text`，写入 `category_board` enum 列时没有隐式转换。

同一模式在多个二级分类插入段重复出现，不能只修复首个 `food_parent` 段。应对所有相关 SELECT 列使用显式 enum 类型，例如：

```sql
'expense'::category_board
'income'::category_board
```

V3 失败后的数据库状态符合事务回滚预期：

```text
flyway_schema_history: V1=true, V2=true
currencies: 33
system_category_templates: 0
```

这说明 V3 自身完整回滚，但当前空库无法迁移到最新版本。

---

## 5. 尚未被真实数据库验证的关键语义

在 PostgreSQL adapter 和共享测试 fixture 落地前，以下行为仍无真实数据库证据：

- 业务写入与 outbox 在同一数据库事务内提交或回滚。
- `SELECT ... FOR UPDATE` 锁定读取及并发行为。
- `Account.version` 乐观锁冲突。
- 余额缓存 `source_version` 与 schema `version` 语义对齐。
- `NUMERIC(20,8)` / `NUMERIC(20,10)` 边界和数据库 round-trip。
- `SET app.current_user_id` 下的 RLS 用户隔离和 fail-closed 行为。
- 连接归还连接池前执行 `RESET app.current_user_id`。
- transfer group、双边流水和 adjustment 的原子写入。
- append-only exchange rate 与历史时间点查询。
- 数据库唯一约束、复合外键和跨用户写入拒绝。

---

## 6. 修复与复测要求

1. 修复 V3 中所有 `default_board` 的 enum 类型表达式。
2. 在 PostgreSQL 16+ 空库重新执行 `flyway migrate`、`flyway validate` 和 `flyway info`。
3. 实现 `DrogonUnitOfWork` 和 PostgreSQL Repository adapters，并在 CMake 中完成 Drogon/PostgreSQL 依赖与 target 接线。
4. 将现有 13 个 repository scenarios 抽为可复用测试契约，分别运行 In-Memory fixture 与 PostgreSQL fixture。
5. 增加 RLS、连接池 session reset、真实并发、行锁、乐观锁和 NUMERIC round-trip 场景。
6. 在 Debug 和 Release 下重新执行完整 CTest，并记录 PostgreSQL/Flyway 版本、commit hash 和测试结果。

在上述项目完成前，`Tasks.md` #46 不满足完成条件，不应标记为 `[x]`。

---

## 7. 本轮执行说明

- 本轮只执行验证和记录报告，未修改生产源码、迁移脚本或任务状态。
- 构建目录位于 ignored 的 `build/` 下。
- 临时 PostgreSQL 容器、Docker network 已清理。
- Colima 已在验证结束后关闭。

---

## 8. 第二次测试说明：V3 修复复测

### 8.1 复测定位

本轮只验证上轮发现的 V3 空库迁移缺陷是否已修复，不提前验收尚未实现的 P1-S10-02 至 P1-S10-04：

- `DrogonUnitOfWork`、PostgreSQL Repository、CMake 生产接线和 composition root 仍属于后续开发内容。
- RLS、真实事务、行锁、NUMERIC round-trip 和连接池 session reset 仍因 PostgreSQL adapter 未实现而保持 `BLOCKED`，不纳入本次 V3 修复结论。
- 本次通过后只关闭 `Tasks.md` #58；#46、#57 以及 #28 中的运行期 DbClient 接线仍不得标记完成。

上轮 V3 失败属于真实迁移缺陷，而非 S10 生产适配器缺失造成的预期阻断。原始错误和回滚状态保留在本文 4.2 节，作为修复前基线。

### 8.2 待测修复

修复提交：`183378981e33a8aa9853e19cdd4a19c75d5a6e77`

修复内容：

1. V3 的 7 个二级分类 `INSERT ... SELECT ... UNION ALL` 区块中，28 个 `default_board` 字面量全部增加显式转换：
   - `'expense'::category_board`：19 处。
   - `'income'::category_board`：9 处。
2. 顶级分类使用 `INSERT ... VALUES`，目标列直接提供 enum 类型上下文，因此不要求额外转换。
3. `group_name` 的 `expense/income` 写入 `TEXT` 列，保持普通字符串是正确行为，不应转换为 `category_board`。
4. 新增离线 CTest `migration_enum_casts`，用于阻止同一类二级分类裸 enum 字面量回归。

复测前必须记录实际待测提交，且该提交必须包含上述修复：

```bash
git rev-parse HEAD
git merge-base --is-ancestor 183378981e33a8aa9853e19cdd4a19c75d5a6e77 HEAD
```

第二条命令必须返回退出码 0。最终报告以 `git rev-parse HEAD` 的输出为准，不只记录分支名。

### 8.3 纸面检查结果

本机在不连接 PostgreSQL 的前提下完成以下静态核对：

| 检查项 | 结果 | 说明 |
| ------ | ---- | ---- |
| `category_board` enum 定义 | PASS | V1 仅定义 `income`、`expense` |
| 二级分类区块数量 | PASS | 共 7 个 parent CTE / UNION 区块 |
| 二级分类写入数量 | PASS | 共 28 条，19 expense + 9 income |
| 显式 `::category_board` 数量 | PASS | 28 处 |
| 二级分类裸 board 字面量 | PASS | 0 处 |
| 父分类查找范围 | PASS | 均限制 `locale='zh-CN'`、`group_name` 和顶级 `parent_id IS NULL` |
| 离线回归门禁 | PASS | `python tests/sql/validate_enum_casts.py` 返回 0 |

离线门禁是针对本次 SQL 形态的正则扫描器，不是 PostgreSQL parser。它不能证明 CHECK、FK、RLS、trigger、事务、编码、Flyway checksum 或 PostgreSQL/Flyway 版本兼容性；本轮真实空库复测仍是必要阻断项。

### 8.4 推荐复测环境

为便于与上轮结果直接比较，优先复用以下版本：

- Host：macOS / Darwin ARM64。
- Linux VM：Colima / Ubuntu 24.04 / aarch64。
- PostgreSQL：16.14，镜像 `postgres:16-alpine`。
- Flyway：10.22.0，镜像 `flyway/flyway:10.22.0`。
- Python 3：用于运行离线 `migration_enum_casts` CTest。
- 数据库：全新空库，使用临时容器、隔离 network 和 tmpfs 数据目录。

可以使用更新的 PostgreSQL 16.x 或 Flyway 10.22+ 补充验证，但至少保留一组与上轮相同版本的对照结果。不得在上轮残留 schema 上直接执行，以免跳过 V1-V3 的真实空库路径。

### 8.5 Docker 复测准备

以下命令以 Bash、仓库根目录和一次性测试凭据为例：

```bash
export PFH_TEST_NETWORK=pfh-s10-v3-test
export PFH_TEST_CONTAINER=pfh-s10-v3-postgres
export PFH_TEST_DB=pfh_validation
export PFH_TEST_USER=pfh
export PFH_TEST_PASSWORD=pfh_validation_password

docker network create "$PFH_TEST_NETWORK"

docker run -d \
  --name "$PFH_TEST_CONTAINER" \
  --network "$PFH_TEST_NETWORK" \
  --tmpfs /var/lib/postgresql/data \
  -e POSTGRES_DB="$PFH_TEST_DB" \
  -e POSTGRES_USER="$PFH_TEST_USER" \
  -e POSTGRES_PASSWORD="$PFH_TEST_PASSWORD" \
  postgres:16-alpine

until docker exec "$PFH_TEST_CONTAINER" \
  pg_isready -U "$PFH_TEST_USER" -d "$PFH_TEST_DB"; do
  sleep 1
done
```

准备完成后先确认数据库为空：

```bash
docker exec -i "$PFH_TEST_CONTAINER" \
  psql -U "$PFH_TEST_USER" -d "$PFH_TEST_DB" -Atc \
  "SELECT COUNT(*) FROM pg_tables WHERE schemaname = 'public';"
```

首次执行应返回 `0`。

### 8.6 Flyway 必跑命令

定义公共参数：

```bash
FLYWAY_IMAGE=flyway/flyway:10.22.0
FLYWAY_URL="jdbc:postgresql://${PFH_TEST_CONTAINER}:5432/${PFH_TEST_DB}"
```

#### 8.6.1 空库迁移

```bash
docker run --rm \
  --network "$PFH_TEST_NETWORK" \
  -v "$PWD/migrations:/flyway/sql:ro" \
  "$FLYWAY_IMAGE" \
  -url="$FLYWAY_URL" \
  -user="$PFH_TEST_USER" \
  -password="$PFH_TEST_PASSWORD" \
  -connectRetries=60 \
  migrate
```

期望结果：

- V1、V2、V3 均显示 `Success`。
- 不再出现 SQL State `42804`。
- schema version 到达 `3`。

#### 8.6.2 迁移信息

```bash
docker run --rm \
  --network "$PFH_TEST_NETWORK" \
  -v "$PWD/migrations:/flyway/sql:ro" \
  "$FLYWAY_IMAGE" \
  -url="$FLYWAY_URL" \
  -user="$PFH_TEST_USER" \
  -password="$PFH_TEST_PASSWORD" \
  info
```

期望结果：V1-V3 的状态均为 `Success`，不存在 `Failed`、`Pending` 或 `Missing`。

#### 8.6.3 Checksum 验证

```bash
docker run --rm \
  --network "$PFH_TEST_NETWORK" \
  -v "$PWD/migrations:/flyway/sql:ro" \
  "$FLYWAY_IMAGE" \
  -url="$FLYWAY_URL" \
  -user="$PFH_TEST_USER" \
  -password="$PFH_TEST_PASSWORD" \
  validate
```

期望结果：`Successfully validated 3 migrations` 或同版本等价成功信息。

#### 8.6.4 第二次 migrate

使用 8.6.1 的同一命令再次执行 `migrate`。

期望结果：schema 已是最新版本，不重复执行 V1-V3，不新增或修改种子数据。

### 8.7 数据完整性断言

迁移成功后执行：

```bash
docker exec -i "$PFH_TEST_CONTAINER" \
  psql -v ON_ERROR_STOP=1 -U "$PFH_TEST_USER" -d "$PFH_TEST_DB" <<'SQL'
SELECT version, description, success
FROM flyway_schema_history
ORDER BY installed_rank;

SELECT COUNT(*) AS currency_count
FROM currencies;

SELECT COUNT(*) AS template_count,
       COUNT(*) FILTER (WHERE parent_id IS NULL) AS root_count,
       COUNT(*) FILTER (WHERE parent_id IS NOT NULL) AS child_count
FROM system_category_templates;

SELECT default_board, COUNT(*)
FROM system_category_templates
GROUP BY default_board
ORDER BY default_board;

SELECT COUNT(*) AS orphan_count
FROM system_category_templates child
LEFT JOIN system_category_templates parent ON parent.id = child.parent_id
WHERE child.parent_id IS NOT NULL AND parent.id IS NULL;

SELECT COUNT(*) AS parent_board_mismatch_count
FROM system_category_templates child
JOIN system_category_templates parent ON parent.id = child.parent_id
WHERE child.default_board IS DISTINCT FROM parent.default_board;

SELECT COUNT(*) AS non_zh_cn_count
FROM system_category_templates
WHERE locale <> 'zh-CN';
SQL
```

期望值：

| 断言 | 期望值 |
| ---- | ------ |
| `flyway_schema_history` | V1/V2/V3 均为 `success=true` |
| `currency_count` | 33 |
| `template_count` | 55 |
| `root_count` | 27 |
| `child_count` | 28 |
| `default_board=expense` | 40 |
| `default_board=income` | 15 |
| `orphan_count` | 0 |
| `parent_board_mismatch_count` | 0 |
| `non_zh_cn_count` | 0 |

第二次 `migrate` 后应再次执行数量断言，结果必须保持不变。

### 8.8 CTest 回归

在待测提交上重新配置后执行离线迁移门禁和完整测试：

```bash
cmake -S . -B build/linux-gcc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-gcc-debug
ctest --test-dir build/linux-gcc-debug -R migration_enum_casts --output-on-failure
ctest --test-dir build/linux-gcc-debug --output-on-failure
```

预期：

- `migration_enum_casts` 单测通过。
- 当前基线共 254 个测试：240 个 unit/use-case、13 个 In-Memory integration、1 个 migration gate。
- 本轮仍没有 PostgreSQL Repository integration target，因此不得把 254/254 描述为真实持久化测试通过。

### 8.9 结果回填模板

复测完成后在本文后追加“第二次测试结果”，至少填写：

```text
测试日期：
测试提交：
Host / VM：
PostgreSQL：
Flyway：

空库确认：PASS / FAIL
flyway migrate：PASS / FAIL
flyway info：PASS / FAIL
flyway validate：PASS / FAIL
第二次 migrate：PASS / FAIL
数据完整性断言：PASS / FAIL
migration_enum_casts：PASS / FAIL
完整 CTest：通过数 / 总数

异常 SQL State：无 / 具体值
实际分类模板计数：
实际 board 分布：
结论：PASS / FAIL / BLOCKED
```

如果失败，必须保留 Flyway 完整错误、SQL State、失败 migration、`flyway info`、`flyway_schema_history` 和分类模板计数；不得只记录最后一行错误。

### 8.10 本轮通过标准

只有同时满足以下条件，V3 修复复测才可记为 `PASS`：

1. 全新 PostgreSQL 16+ 空库可以从 V1 连续迁移到 V3。
2. `info` 与 `validate` 均成功，V1-V3 checksum 一致。
3. 33 条币种、55 条分类模板、根/子分类和 board 分布全部符合预期。
4. 不存在孤儿分类、父子 board 不一致或非 `zh-CN` 种子。
5. 第二次 `migrate` 为 no-op，数据计数不变。
6. 离线 `migration_enum_casts` 和当前完整 CTest 通过。

本轮通过后可将 `Tasks.md` #58 标记为完成，并补充真实环境、commit hash 和结果；它不会自动完成 #46、#57，也不会证明 PostgreSQL Repository/UoW/RLS 语义。

### 8.11 环境清理

结果记录完成后清理一次性资源：

```bash
docker rm -f "$PFH_TEST_CONTAINER"
docker network rm "$PFH_TEST_NETWORK"
```
