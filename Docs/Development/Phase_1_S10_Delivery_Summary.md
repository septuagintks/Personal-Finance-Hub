# Phase 1 S10 REST API 与认证基础 - 交付记录

**更新日期**: 2026-07-15
**阶段**: P1-S10 REST API 与认证基础
**当前状态**: FOUNDATION PREFLIGHT PASS - 本地实现、全量 review、真实依赖构建、V1-V5、双角色启动与核心 API smoke 已通过；完整 Repository fixture 与最终环境签署保留到 P1-S12

---

## 1. 概述

本文是 P1-S10 的累计交付记录。已验收或正在验收以下内容：

- PostgreSQL 16.14 + Flyway 10.22.0 外部环境中的 V1-V3 空库迁移复测。
- P1-S10-01 REST 契约、金额符号、流水并发策略和转账删除边界收口。
- 转账手续费从 `CreateTransferCommand` 到 `TransferAggregate`、Repository 和报表语义的完整接线。
- P1-S10-02 Drogon/PostgreSQL 依赖接入与分层 CMake 目标。
- P1-S10-03 PostgreSQL Repository 与 `DrogonUnitOfWork` 适配器实现（本地静态门禁通过，外部连库验证留到 P1-S12）。
- P1-S10-04 production composition root、双角色 DbClient、注册 bootstrap tenant 绑定和启动安全校验。
- P1-S10-05 framework-neutral HTTP parser/mapper、RFC 3339、统一错误响应、TraceId 与 Drogon 全局异常边界。
- P1-S10-06 register/login/refresh/logout、Argon2id、HS256 JWT、refresh rotation/reuse detection、同步审计和 V4 session-family 撤销。
- P1-S10-07 至 S10-10 基础资源、流水、转账和报表 Application/Controller/API，以及 Tag PostgreSQL adapter、request scope 和 V5 汇率精度迁移。
- P1-S10-11 framework-neutral 全 API 回归、OpenAPI 3.1/路由契约门禁、币种目录一致性门禁和项目全量 review。
- 提交 `db07d64` 在 macOS/Colima Ubuntu ARM64 上完成真实 Drogon/PostgreSQL/OpenSSL/Argon2 构建、V1-V5 迁移、双角色启动和 S10 最小 API 闭环。

本文可以作为 P1-S10 实现、离线门禁与基础 production smoke 的验收记录，但不能替代完整 P1-S12。Outbox Publisher 与 Scheduler 属于 P1-S11；PostgreSQL 同批 Repository fixture、连接池复用、并发/失败注入、NUMERIC 边界矩阵、应用镜像和最终 Linux/Docker 门禁仍须在 P1-S12 签署。

---

## 2. PostgreSQL 迁移外部复测

详细证据见 [Phase_1_S10_PostgreSQL_Persistence_Validation_Report.md](Phase_1_S10_PostgreSQL_Persistence_Validation_Report.md)。

### 2.1 缺陷与修复

首轮空库迁移在 V3 失败，SQL State 为 `42804`。根因是 7 个二级分类 `INSERT ... SELECT ... UNION ALL` 区块将 `default_board` 字面量推断为 `text`。

修复提交 `1833789` 为 28 个二级分类 board 字面量增加显式 `::category_board`，并新增离线 `migration_enum_casts` CTest 门禁。

### 2.2 最终结果

外部复测提交：`4621f69ef940868e10591f5449ac8be1dd9c95e3`。

| 验证项 | 结果 |
| ------ | ---- |
| PostgreSQL 16.14 全新空库 V1-V3 | PASS |
| Flyway `migrate` / `info` / `validate` | PASS |
| 第二次 `migrate` no-op | PASS |
| 33 条币种种子 | PASS |
| 55 条分类模板，27 root + 28 child | PASS |
| board 分布：40 expense + 15 income | PASS |
| 孤儿、父子 board 不一致、非 `zh-CN` | 均为 0 |
| 外部 Linux ARM64 CTest | 254/254 PASS |

该结果关闭 `tasks.md` #58，但不关闭 #46。外部复测时尚无 PostgreSQL Repository、`DrogonUnitOfWork` 或真实 fixture，不能据此验收事务、RLS、行锁、连接池租户上下文隔离或 NUMERIC round-trip。

---

## 3. P1-S10-01 交付

### 3.1 转账命令契约

`CreateTransferCommand` 当前固定为：

- 基础字段：`source_account_id`、`target_account_id`、`mode`、`outgoing_amount`、`incoming_amount`、`rate`、`description`、`occurred_at`。
- 手续费字段：可选 `fee_amount`、`fee_source`、`fee_account_id`。
- 所有金额和汇率均由十进制字符串解析，不接受浮点数边界。
- 三种 mode 只接受各自的两个输入字段；派生字段必须为空，避免同一请求携带相互冲突的事实。

手续费字段采用严格组合：

| `feeSource` | `feeAmount` | `feeAccountId` | 规则 |
| ----------- | ----------- | -------------- | ---- |
| 不提供 | 不提供 | 不提供 | 无手续费 |
| `SourceAccount` | 必填正数 | 禁止 | 使用源账户币种 |
| `TargetAccount` | 必填正数 | 禁止 | 使用目标账户币种 |
| `ThirdParty` | 必填正数 | 必填 | 同用户、未归档、与两端账户不同；使用该账户币种 |

### 3.2 领域与金额语义

- `TransferFee` 是 Application 完成账户解析后传给纯领域服务的输入，不引入 Repository 依赖。
- `fee_amount` 是正数 magnitude；聚合内手续费固定构造为负数 `TransactionType::Adjustment`。
- 手续费不并入 outgoing/incoming 主金额，三条流水共享同一个 `transfer_group_id` 和业务时间。
- Transfer 双边流水继续排除在 income/expense 之外；负 Adjustment 计入 expense 并减少对应账户余额。
- Adjustment 保持 signed 语义，未来返利或 FX gain 可以为正，手续费或 FX loss 为负。
- 本阶段不自动计算汇兑损益。现有命令只有成交汇率，没有市场基准汇率；伪造差额会制造不可审计的财务事实。

### 3.3 事务与持久化语义

- 基础字段和手续费字段组合在打开事务前校验，非法请求不产生业务写入或 outbox。
- 源、目标和可选第三方手续费账户在同一事务内按 ID 升序锁定，降低并发死锁风险。
- 三个账户均执行用户归属与归档校验，第三方账户不允许跨用户复用。
- In-Memory Repository 将数据库分配的 group ID 同时写入两端流水和全部 Adjustment。
- 转账聚合保存失败时，transfer group、双边流水、手续费和 outbox 一起回滚。
- 危险账户删除按 group 删除两端流水及全部 Adjustment，不遗留孤立手续费。
- Repository 对所有流水补充账户币种一致性检查，不再只校验转账双边流水。

### 3.4 已固定的其余边界

- Income/Expense 请求使用正数 magnitude；Adjustment 请求使用 signed amount。
- 对外响应使用业务金额，数据库 signed amount 不直接泄漏；Presentation mapper 已在 P1-S10-08/S10-09 完成并由 API 测试覆盖。
- Phase 1 不注册转账删除路由；完整 `DeleteTransferUseCase` 落地前只支持创建与查询。
- 流水采用追加 + 软删除，不提供普通更新，也不增加应用层行级乐观锁；账户并发继续使用 `Account.version`。

---

## 4. 主要改动

| 文件 | 内容 |
| ---- | ---- |
| `include/pfh/domain/transfer_aggregate.h` | 新增已解析的 `TransferFee` |
| `include/pfh/domain/transfer_domain_service.h` | 三种构造模式增加可选手续费输入 |
| `src/domain/transfer_domain_service.cpp` | 手续费路由、负 Adjustment 构造与聚合校验 |
| `include/pfh/application/dto.h` | `CreateTransferCommand`/`TransferResultDto` 手续费字段 |
| `include/pfh/application/use_cases/create_transfer_use_case.h` | 严格命令校验、三账户有序锁定与手续费接线 |
| `include/pfh/infrastructure/persistence/in_memory_transaction_repository.h` | grouped Adjustment 保存、币种校验和聚合级联删除 |
| `tests/unit/transfer_domain_service_test.cpp` | 领域手续费规则测试 |
| `tests/unit/use_case_test.cpp` | 用例、余额、cash flow、权限与错误路径测试 |
| `tests/integration/repository_integration_test.cpp` | 手续费聚合原子保存、回滚与级联删除测试 |

---

## 5. 验证结果

本次 P1-S10-01 本地 Windows GCC 16 Debug 门禁：

```text
build: PASS
unit/use-case: 255
In-Memory integration: 16
migration enum-cast gate: 1
CTest: 271/271 PASS
failed: 0
```

相较外部 V3 复测时的 254 项基线，本次新增 17 项测试：6 个 Domain、8 个 Use Case、3 个 Repository integration 场景。

S10-03 review 后本地基线增加 `postgresql_adapter_contracts`，并让 PostgreSQL 适配器在 OFF 模式下也通过 API stub 执行全源语法编译。该结果仍不包含真实 Drogon ABI 或 PostgreSQL 行为；S10-04 已接入运行期 DbClient，PostgreSQL fixture 及聚合原子性、NUMERIC、RLS、行锁和连接池上下文复跑统一留在 P1-S12。

---

## 6. P1-S10-02 与 S10-03 交付

### 6.1 S10-02：Drogon/PostgreSQL 依赖与分层 CMake

- 新增 `PFH_BUILD_POSTGRESQL` 选项（默认 OFF）。OFF 时运行 In-Memory 服务/测试，同时通过 `pfh_postgresql_adapter_compile_gate` 编译全部 PostgreSQL 翻译单元；ON 时把这些源文件加入真实 `pfh_infrastructure`。
- 当 `PFH_BUILD_POSTGRESQL=ON` 时，CMake 通过 `find_package` 强制要求 Drogon 和 PostgreSQL 库。
- 新增分层目标：`pfh_domain`、`pfh_application`、`pfh_infrastructure`、`pfh_presentation` 和 `pfh_server`；Application/Presentation 在 S10-05 出现稳定 `.cpp` 后已转为静态库。
- Domain 层保持零外部依赖，不链接 Drogon、PostgreSQL 或 spdlog。
- Application 层仅依赖 Domain；Infrastructure 实现 Application/Domain 端口；Presentation 依赖 Application，并只在 PostgreSQL/Drogon 生产模式链接 Drogon。
- 新增 `pfh_test_support` 库，供 unit/integration 测试共享 fixture 与 test helpers。
- 当前 HEAD 的 Windows GCC 16 / PostgreSQL OFF 构建、全量适配器离线编译与 CTest 均通过。外部 Linux ARM64 结果来自较早迁移复测提交，不能冒充当前 S10-03 实现验证。

### 6.2 S10-03：PostgreSQL Repository 实现

**适配器范围**：

| 适配器类 | 功能概要 |
| -------- | -------- |
| `UserRepositoryImpl` | create、find by id/username、save；提供事务感知读取，全局用户表写入额外校验“未绑定注册事务创建 / 同用户事务更新” |
| `UserPreferenceRepositoryImpl` | request-scoped find/fallback 与 upsert；事务感知重载保证 `users`/`user_preferences` read-your-writes |
| `AccountRepositoryImpl` | 创建、查询、乐观锁更新、`FOR UPDATE NOWAIT`、余额缓存 hit/miss/rebuild |
| `TransactionRepositoryImpl` | signed 单笔流水、Transfer + Adjustment 聚合保存、软删除、聚合物理删除与缓存失效 |
| `ExchangeRateRepositoryImpl` | append、latest、historical、pair history；历史查询使用 `fetched_at <= target_time` |
| `CategoryRepositoryImpl` | request-scoped 查询/保存、锁定读、父子 board 校验、root id 回溯 |
| `PostgresActiveCurrencyQuery` | 合并未归档账户币种与用户报表基准币种；要求独立后台只读连接 |
| `DrogonUnitOfWork` | 同一 Transaction 内业务写入 + outbox、异常回滚、commit callback 确认 |

**NUMERIC 映射与精度**：

- schema 金额列使用 `NUMERIC(20,8)`，所有汇率及 Transfer 快照列经 V5 统一使用 `NUMERIC(20,10)`；客户端通过字符串 round-trip 避免二进制浮点截断。
- 应用层 `Decimal` 内部 `__int128` scale 固定 10^10，与数据库列 scale 一致。
- 所有 SQL `INSERT`/`UPDATE` 使用参数化查询，传递十进制字符串；`SELECT` 结果以字符串读取后由 `Decimal::parse_numeric_20_8/20_10` 重建。
- Repository 在写入前再次执行 `fits_numeric_20_8/20_10`，拒绝依赖数据库隐式舍入或溢出报错。

**RLS 上下文注入**：

- 写路径由 `DrogonUnitOfWork` 在首条业务 SQL 前执行事务级 `SET LOCAL app.current_user_id`；租户读路径也创建短 Transaction，在同一连接上 SET 后查询。
- PostgreSQL 所有 user-scoped 表的 RLS policy 从 `current_setting('app.current_user_id')::BIGINT` 读取当前用户 id，拒绝跨用户访问。
- request-scoped Repository 同时校验构造时的 tenant、方法参数、实体 owner 与 Transaction context；RLS 作为数据库末端防线。禁止在普通池化 `DbClient` 上 SET 后再发下一条租户查询。
- `SET LOCAL` 随 commit/rollback 自动清除，不在已结束事务上手工 RESET；S12 仍需证明连接池复用不会串租户。

**乐观锁与行锁**：

- `Account` 更新在 SQL 层校验 `version`，不匹配时返回 0 行，adapter 映射为 `RepositoryStatus::Conflict`。
- `IAccountRepository::find_by_id_for_update` 执行 `SELECT ... FOR UPDATE NOWAIT`，被锁时立即失败而不阻塞事务。
- 转账用例在锁定三账户时按 `account_id` 升序排列，降低死锁概率。
- `balance_of` 在检查/重建缓存前锁定账户；单笔流水写入与软删除也锁定对应账户。危险删除会按 ID 尝试锁定转账两端及 grouped Adjustment 账户，无法立即取得时回滚，避免缓存重建与余额事实并发交错。

**事务边界与异常映射**：

- `DrogonUnitOfWork::execute_in_transaction` 捕获 Drogon `DrogonDbException` 和 C++ `std::exception`，失败时回滚并清空本次事件队列。
- Drogon Transaction 没有直接 `commit()` API；实现释放最后一个 Transaction owner 触发提交，并等待 commit callback 返回 `true` 后才报告成功，禁止执行字面量 `COMMIT`。
- 用户名/分类唯一约束映射为稳定 `Conflict`；其他底层异常记录完整服务端日志，对 Application 只返回不含 SQL/驱动文本的 `DatabaseError`。当前未声称已经可靠解析全部 SQLSTATE。

**余额缓存 rebuild**：

- `balance_of` 在同一租户事务读取 `account_balance_cache`，同时校验 `source_version = COALESCE(MAX(transactions.version), 0)` 与最新未删除流水 ID；不使用 In-Memory 的流水数量简化规则。
- miss/stale 时调用 `BalanceCalculationService::calculate_balance`，同事务 UPSERT 缓存并递增 `cache_version`。
- 单笔流水、Transfer/Adjustment、软删除与聚合物理删除都会删除全部受影响账户缓存，下次 `balance_of` rebuild。
- 物理删除普通流水时先在同一数据修改 CTE 中删除余额缓存，再删除 tag relations 与流水，避免 `account_balance_cache.last_transaction_id` 外键仍引用待删流水。

**转账聚合保存**：

- `save_transfer` 在一个事务内依次插入 `transfer_groups` 行、outgoing transaction、incoming transaction、以及所有 Adjustment（手续费）。
- Adjustment 入库时重建为数据库刚分配的真实 `transfer_group_id`，不会保留 Domain 的无效占位 ID。
- 任一 INSERT 失败时由外层 UoW 回滚，不产生孤立 group 或单边流水。
- `TransferPersistResult` 按当前接口返回 `transfer_group_id`、`outgoing_id` 和 `incoming_id`；接口不承诺返回 Adjustment IDs。
- 账户危险删除先物化所有关联 group，再删除 tag relations、两端与 grouped Adjustment、group 行和全部受影响缓存；第三方手续费账户单独触碰 group 时也能完整级联。

**分类 root 回溯**：

- `resolve_root_id_for_user` 从给定 `category_id` 向上遍历 `parent_id` 直到 `parent_id IS NULL`。
- 最多遍历 64 层（防止循环引用）；超出或遇到数据库行不存在时返回 `Database` 错误。
- 报表按一级分类聚合时调用此方法将子分类映射到 root。

### 6.3 本地静态门禁

本次 review 在 `PFH_BUILD_POSTGRESQL=OFF` 模式下构建并测试。生产适配器通过窄 Drogon API stub 编译，另有结构门禁检查 CMake 条件、TypedId、RLS、提交方式、余额版本和 Transfer 级联；未执行真实数据库连接：

```text
build (PostgreSQL OFF): PASS
unit/use-case: 254
In-Memory integration: 16
migration enum-cast gate: 1
PostgreSQL adapter contract gate: 1
PostgreSQL adapter compile gate: PASS (build target)
CTest: 273/273 PASS
failed: 0
```

该 stub 只验证项目源码语法和所用 API 形状，不替代真实 Drogon 头文件/ABI。P1-S10-04 已完成生产接线；真实 Repository/UoW fixture、连库验证与结果签署统一在 S12 目标环境执行。

### 6.4 主要新增文件

| 文件 | 功能 |
| ---- | ---- |
| `include/pfh/infrastructure/persistence/*_repository_impl.h` | 6 个核心 PostgreSQL Repository 实现声明 |
| `src/infrastructure/persistence/*_repository_impl.cpp` | User/Preference/Account/Transaction/Category/ExchangeRate SQL 实现 |
| `include/pfh/application/ports/i_active_currency_query.h` | 从租户 AccountRepository 拆出的系统级调度查询端口 |
| `src/infrastructure/persistence/postgres_active_currency_query.cpp` | 独立后台只读连接的跨租户活跃币种查询 |
| `include/pfh/infrastructure/persistence/postgres_repository_support.h` | 固定事务读、commit callback、context 校验与统一异常边界 |
| `src/infrastructure/persistence/postgres_repository_support.cpp` | 回滚、日志与稳定 RepositoryError 映射 |
| `include/pfh/infrastructure/persistence/drogon_unit_of_work.h` | request-scoped/全局任务可选租户 UoW |
| `src/infrastructure/persistence/drogon_unit_of_work.cpp` | Transaction + RLS + outbox co-commit |
| `tests/support/drogon_stub/` | OFF 模式编译专用最小 Drogon API stub |
| `tests/sql/validate_postgresql_adapter_contracts.py` | PostgreSQL adapter 离线结构回归门禁 |

---

## 7. P1-S10-04 至 S10-06 交付与专项 Review

### 7.1 S10-04：Composition Root、DbClient 与 RLS

- `ProductionCompositionRoot` 是生产依赖的唯一装配入口；`pfh_server` 在 `PFH_BUILD_POSTGRESQL=OFF` 时明确拒绝作为生产服务启动，不回退到 In-Memory Repository。
- request DbClient 与 background DbClient 使用两份配置和两个对象。启动时拒绝相同角色或相同 client；request 角色必须为非 superuser 且无 `BYPASSRLS`，后台角色必须为非 superuser、具备 `BYPASSRLS` 且默认事务只读。
- 后台 client 只注入 `PostgresActiveCurrencyQuery`，不注入 `AuthService`、`JwtFilter`、Controller 或 request-scoped Repository。
- `DrogonUnitOfWorkFactory` 为认证用户创建预绑定 tenant 的 request UoW；注册使用无 tenant 的 bootstrap UoW。
- `DrogonTransactionContext::bind_tenant_once` 在创建 User 后执行一次事务级 `SET LOCAL app.current_user_id`；重复绑定、非法 ID 和未绑定租户访问均 fail closed。
- request/后台数据库密码、JWT secret、password pepper 在 composition root 生命周期结束时清零；启动错误不输出连接串或密钥。

### 7.2 S10-05：HTTP 边界

- Application 和 Presentation 从 `INTERFACE` 目标转为真实静态库；HTTP 核心不依赖 Drogon，可由生产 adapter 和本地 API test 共用。
- `JsonRequestParser` 强制 JSON object、字段白名单、精确 string/integer/null 类型和正数 TypedId；金额 JSON number 在进入 Use Case 前即返回 400。
- `TimeCodec` 严格解析带 `Z`/offset 的 RFC 3339，拒绝非法日历日期、缺失时区、超长小数秒及平台 `system_clock` 不可表示的时间；响应统一输出 UTC `Z` 并保留非零小数秒。
- `HttpResponseMapper` 固定 400/401/403/404/409/422/500/502 映射；500/502 和 401 响应不返回底层 `details`、SQL、路径或 token 解析原因。
- `ApiApplication` 为每个成功/失败响应写 `X-Trace-Id`；错误 body 保持 `error_code/message/trace_id`。
- Drogon adapter 只做请求/响应转换和路由注册；全局 exception handler 记录服务端上下文并返回脱敏 500。

### 7.3 S10-06：认证生命周期

- `AuthService` 实现 register/login/refresh/logout；用户名先 trim + ASCII 小写规范化，密码注册长度限制为 12-128。
- 注册先在无 tenant 事务创建 User，再一次性绑定新 `UserId`，随后初始化 Preference、分类模板、`categories_initialized`、refresh hash、同步审计和 `UserRegistered` outbox；中途失败全部回滚。
- 生产密码实现使用带随机 salt 的 Argon2id PHC 字符串；不存在用户名也执行一次等成本哈希，降低用户名时序枚举。
- Access Token 使用 OpenSSL HS256，验证 `alg/typ/iss/aud/sub/sid/jti/roles/iat/nbf/exp`、签名常量时间比较和时钟偏差；HMAC secret 至少 32 字节。
- Refresh Token 使用 32 字节 CSPRNG 不透明值，数据库只保存 SHA-256 hash。刷新在锁定旧 token 后原子撤销并 rotation；已撤销 token 复用会提交整 `sid` 撤销、安全审计和 outbox，再返回 401。
- logout 在同一 tenant UoW 撤销 refresh token、写入当前 `iss+jti` 黑名单、同步审计和 `UserLoggedOut` outbox。
- V4 新增 `revoked_sessions`、认证审计动作和 active-session 索引；V4 尚未在外部 PostgreSQL 执行，保留到 S12。

### 7.4 专项 Review 与门禁

专项 review 修复了未知用户名廉价返回、密钥对象未清零、refresh 事务内 session 检查使用池化连接、空分类模板仍标记初始化、成功响应缺 TraceId header 等问题。

```text
build (PostgreSQL OFF): PASS
unit/use-case: 265
In-Memory integration: 16
framework-neutral API: 11
migration enum-cast gate: 1
PostgreSQL adapter contract gate: 1
CTest: 294/294 PASS
PostgreSQL/production bootstrap/security compile gates: PASS
```

窄 API stub 只验证源码语法和 API 形状，不证明真实 Drogon/OpenSSL/Argon2 ABI、PostgreSQL SQL、角色权限、连接池复用或事务提交行为。上述项目继续由 #46/#57 和 P1-S12 阻断。

---

## 8. P1-S10-07 至 S10-11 交付与全量 Review

### 8.1 Application 与 request scope

- 新增 `IRequestScope` / `IRequestScopeFactory`，PostgreSQL 与 In-Memory 实现均在每个认证操作中提供同一 tenant 的 Account、Transaction、Category、Tag、Preference、ExchangeRate、AuditLog Repository 与 Unit of Work。
- `FinanceApplicationService` 是资源 Controller 的 Application 入口；Presentation 只解析 HTTP 并调用 facade，不读取 Repository、打开事务或构造 Domain 金融对象。
- Account、Category、Tag 和 Preference 写路径在同一事务保存业务事实与同步审计；账户归档/危险删除、分类创建/删除与偏好更新登记强类型领域事件。
- 全量 review 补齐可独立调用 Use Case 的 ID 和枚举校验，防止其他 adapter 绕过 HTTP parser 注入非法 TypedId 或枚举值。

### 8.2 资源、流水与转账 API

- 完成 Account 列表/创建/余额/归档/危险删除，Category 树/创建/软删除，Tag 列表/创建/软删除/流水关系替换，Preference 读写和公开 Currency catalog。
- Category Repository 的创建与更新路径均执行最大 64 层祖先遍历；创建父节点采用 `FOR UPDATE NOWAIT`，删除与并发创建不能绕过 active-child 规则。
- 完成 Transaction 创建与软删除。Income/Expense 接受正数 magnitude，Adjustment 接受非零 signed amount；Application 与 Presentation 均限制说明 4096 字节和普通十进制输入 128 字节；普通删除接口拒绝 Transfer 双边及同组 Adjustment。
- 完成 Transfer 创建与查询。三种输入模式、三类手续费来源、聚合原子性和 magnitude 响应均有测试；Phase 1 继续不注册 Transfer 删除路由。
- 新增 `TagRepositoryImpl`，所有标签和关系 SQL 显式绑定 `user_id`；V5 将 `transfer_groups.exchange_rate` 从旧快照精度统一为 `NUMERIC(20,10)`。

### 8.3 报表与 API 契约

- 完成 net worth、最长 120 个月 cash flow trend 和当前月 dashboard summary API；Dashboard 不接受自定义日期范围，cash flow 月份在时区转换后缩窄为 `system_clock::time_point` 前执行范围检查。
- 用户时区转换使用 IANA tzdb 和半开月窗；cash flow 排除 Transfer、按发生时间使用历史汇率，signed Adjustment 正数计收入、负数计支出。
- top expense category 回溯到 root 聚合，并在分类软删除后继续读取历史名称；缺失分类行保留为未分类，数据库错误不被吞掉。
- 同额 top expense category 使用稳定排序，保留首次出现顺序；事件负时间戳按 floor 取 epoch 秒，避免 1970 年前小数秒向零截断。
- 跨币种零余额直接折算为基准币种零值，不再要求无财务影响的空账户必须存在历史汇率；非零金额仍严格执行 point-in-time 汇率错误语义。
- 新增 OpenAPI 3.1 契约，`openapi_contract` 对照完整路由/HTTP 方法、金额字符串 schema、nullable 字段和 Drogon route table；`currency_catalog_parity` 逐项比较 Domain 33 币种与 V2 种子。
- 最终 review 修正 `RegisterResponse` / `CategoryTree` 的 closed-object `allOf` 冲突，并为 locale 建立注册、偏好更新与 OpenAPI 共用规则。
- 币种目录 ETag 改由完整响应内容稳定计算，catalog 变化后不会继续用固定 `v1` 错误返回 304；REST 时间响应统一记录为 UTC `Z`。
- `DeleteTransactionUseCase` 现将软删除、同步 AuditLog 与 `TransactionDeleted` outbox 放在同一事务；事件设计明确禁止 S11 handler 重复写同一业务审计。
- In-Memory 与 PostgreSQL Transaction Repository 统一 create-only、active、non-grouped、non-zero、Income/Expense magnitude 和 Transfer group-id 前置条件。
- 配置安全收尾拒绝 `REPLACE_WITH_` 开头的 JWT/可选 password pepper 占位值；pepper 留空仍合法，JSON loader 与 production composition root 均独立 fail fast。
- 最终文档复核将金额/汇率设计中的现行数据库汇率口径统一为 `NUMERIC(20,10)`；V1 的 `NUMERIC(30,10)` 仅作为 V5 迁移前历史结构保留。

### 8.4 最终本地门禁

```text
configure/build (Windows GCC 16, PostgreSQL OFF): PASS
unit/use-case: 272
In-Memory integration: 17
framework-neutral API: 28
static gates: 4
CTest: 321/321 PASS
PostgreSQL/production bootstrap/security compile gates: PASS
```

本地 stub/静态门禁本身不证明真实 ABI 或数据库行为；下节记录了独立目标环境基础预检。未在该预检中执行的完整 fixture、连接池复用、并发与数值边界继续由 #46/#57/P1-S12 阻断。

### 8.5 macOS / Colima S10 基础预检

原始测试基线为 `c4fe603`；真实环境发现并修复 4 个兼容性缺陷后，最终验证提交为 `db07d64dbc0d70e9cc50709c4bfb8247fc4b52da`。

| 项目 | 结果 |
| ---- | ---- |
| 环境 | macOS 26.5.2 ARM64 + Colima 0.10.3 / Ubuntu 24.04.4 ARM64 |
| 工具链 | GCC 13.3、CMake 3.28.3、Ninja 1.11.1、tzdata 2026a |
| 真实依赖 | Drogon 1.8.7、PostgreSQL client/server 16.14、OpenSSL 3.0.13、Argon2 20190702 |
| Debug production ON build / CTest | PASS，321/321 |
| Release production ON build / CTest | PASS，321/321 |
| V1-V5 空库 migrate/info/validate/第二次 no-op | PASS |
| 双角色 production startup | PASS；request 非 superuser/BYPASSRLS，background BYPASSRLS + 默认只读 |
| 认证生命周期 | PASS；register/login/refresh reuse/logout 及 hash-only 存储均通过 |
| 两用户 RLS smoke | PASS；跨用户列表隔离和按 ID 查询 404 |
| 财务与报表闭环 | PASS；账户、流水、转账、net worth、dashboard、cash flow |

真实依赖依次暴露并由 `db07d64` 修复：

1. `quoted` helper 与 libstdc++ 13 `std::quoted` 发生未限定名重载冲突，重命名为 `json_quoted`。
2. 真实 Drogon 需要显式包含 `drogon/orm/Row.h` 与 `Field.h`；compile stub 同步补 wrapper，避免再次掩盖 include surface。
3. Drogon 1.8.7 不支持 `Field::as<trantor::Date>()`；改为严格解析 PostgreSQL `TIMESTAMPTZ` 文本及数值 UTC offset，实测 `+08` 和六位微秒正确归一化。
4. `transfer_mode SMALLINT` 不能绑定 4 字节 `int`；改为 `std::int16_t` 后真实转账写入和回滚通过。

返回 Windows 后，GCC 16 PostgreSQL OFF build 与 321/321 CTest 再次通过。此次预检没有执行完整 PostgreSQL Repository fixture、连接池复用压力、并发/故障注入、NUMERIC 边界矩阵、S11 后台 runtime 或应用 Docker 镜像，因此这些项目仍为 `NOT RUN`，不得由本节结果替代。

---

## 9. 任务状态

| 任务 | 状态 | 说明 |
| ---- | ---- | ---- |
| P1-S10-01 | 完成 | 契约、手续费、符号、并发与删除边界已固定 |
| P1-S10-02 | 完成 | Drogon/PostgreSQL 依赖接入与分层 CMake 目标 |
| P1-S10-03 | 完成 | PostgreSQL Repository 与 `DrogonUnitOfWork` 实现；本地静态门禁通过 |
| P1-S10-04 | 基础预检通过 | production composition root、双 DbClient、真实双角色启动与 bootstrap tenant 绑定通过；连接池复用矩阵待 S12 |
| P1-S10-05 | 完成 | HTTP parser/mapper、TraceId、异常脱敏与全部资源 DTO 已接入 |
| P1-S10-06 | 完成 | 认证生命周期、本地 API 回归及真实 Drogon/OpenSSL/Argon2/PostgreSQL smoke 通过 |
| P1-S10-07 | 完成 | 基础资源 Use Case、request scope、Controller、Tag adapter 与同步审计 |
| P1-S10-08 | 完成 | Transaction 创建/软删除、严格金额与分类边界 |
| P1-S10-09 | 完成 | Transfer 创建/查询、三模式与手续费 API |
| P1-S10-10 | 完成 | Net worth、cash flow 与当前月 dashboard API |
| P1-S10-11 | 基础预检通过 | Windows 321/321、Linux Debug/Release 321/321、真实 runtime 核心 smoke 通过；最终 S12 仍保留 |
| #48 | 完成 | 手续费 Application/Domain/Repository 路径已接通 |
| #50 | 完成 | 流水并发策略已固定 |
| #52 | 完成 | Phase 1 转账删除边界已固定 |
| #55 | 完成 | DTO 金额符号说明已固定 |
| #58 | 完成 | V3 PostgreSQL 16.14 空库复测通过 |
| #28 | 完成 | V1-V5、双 DbClient、真实双角色连接和启动权限校验通过 |
| #40 | 完成 | 认证实现、本地回归和真实 Drogon/安全库/PostgreSQL lifecycle smoke 通过 |
| #14/#44 | 完成 | framework-neutral API 回归、完整 DTO/parser/mapper 与 OpenAPI 契约已交付 |
| #45 | 完成 | Drogon exception adapter、TraceId 与真实 runtime 脱敏行为通过 |
| #46 | 部分完成 | 核心生产写读 smoke 已通过；同批 Repository/UoW fixture、并发与故障注入待 S12 |
| #49 | 后续已完成 | S10 完成 Tag/同步审计；S11 已补系统事件与 dead-letter 幂等审计处理器 |
| #51 | 部分完成 | `MAX(version)` + 最新流水 ID 与全写路径缓存失效已实现；真实 DB 复核待 S12 |
| #53 | 完成 | Application/Infrastructure/Presentation 分层 CMake 目标已落地 |
| #57 | 部分完成 | S10 基础 production preflight 已通过；S11 runtime、完整 fixture、应用镜像与最终 P1-S12 门禁仍保留 |

---

## 10. 后续顺序

1. P1-S11：完成 Outbox publisher、重试/死信、审计事件处理、真实汇率 Provider 与 Scheduler。
2. P1-S12：在目标 Linux/Docker/PostgreSQL 环境执行 V4/V5、真实 Repository/UoW/RLS、双角色连接池、Drogon/安全库 ABI、API smoke 和后台任务 runtime。
3. P1-S12 全部阻断门禁取得可追溯结果后，才允许签署 Phase 1 并合并到 `main`。
