# Phase 1 S12 测试收尾与文档回写 - 交付记录

**更新日期**: 2026-07-15
**阶段**: P1-S12 Phase 1 测试收尾与文档回写
**当前状态**: S12-01 至 S12-06 原基线已完成；FreeCurrencyAPI/exchangerate.fun 修正已通过 Windows 349/349 与脱敏端点探测，等待 macOS/Colima 在新提交上复测后执行 S12-07

---

## 1. 范围与测试对象

- 分支：`feature/phase1-foundation`。
- Windows S12-01 测试对象：`6cd41bc2c60af1298544d975c58819cc8c0600a9`。
- macOS 接收基线：`297fe636c0b441aaf0807d0487cf9320b41c780e`。
- macOS production ABI 修复：`ed0b10f4567232d5558914464092a24213958941`（`fix: support Drogon 1.8 shutdown lifecycle`）。
- macOS 原最终测试对象：`d2549af142c92fa08bc15e2027e1163053e355ca`，包含 PostgreSQL fixture、真实 Drogon smoke、Docker 编排与 Content-Type 修复。
- Provider 修正测试对象：本记录所在的新签名提交；精确哈希与 macOS 返回结果由 `.codex/HandOff.md` 固定。
- 所有测试数据库、容器、网络和凭据均为一次性本地资源；未保存认证响应、Token、数据库转储或原始日志。

原轮次完成 P1-S12-02 至 S12-06；当前 corrective round 只替换外部汇率 Provider 并复测受影响门禁。S12-07、`main` 合并和 Phase 2 仍不在本提交范围。

---

## 2. 环境矩阵

| 项目 | Windows S12-01 | macOS / Colima S12-02 至 S12-06 |
| ---- | -------------- | -------------------------------- |
| Host | Windows 10 x64 | macOS 26.5.2 / Darwin 25.5.0 / arm64 |
| Linux | `NOT RUN` | Colima 0.10.3，Ubuntu 24.04.4 LTS / aarch64 |
| Compiler | GCC 16.1 | GCC/G++ 13.3.0，libstdc++6 14.2.0 |
| CMake / Ninja | 4.3.2 / 1.13.2 | 3.28.3 / 1.11.1 |
| Python | 3.14.6 | 3.12.3 |
| Drogon / Trantor | 离线 compile gate | 1.8.7 / 1.5.12 |
| PostgreSQL | `PFH_BUILD_POSTGRESQL=OFF` | client/server 16.14 |
| Flyway | `NOT RUN` | OSS 10.22.0 |
| OpenSSL / Argon2 | 离线 compile gate | 3.0.13 / 20190702 |
| tzdata | N/A | Linux VM 2026a；最终镜像 2026b |
| Docker | `NOT RUN` | client/server 29.5.2；Compose 插件不可用 |

Colima VM 配置为 2 GiB 内存且无 swap，因此 production ON 和镜像构建均使用 `--parallel 1`。Ubuntu Drogon CMake 需要显式传入系统 MySQL client 路径。

---

## 3. 构建与 CTest

### 3.1 Windows S12-01

| 配置 | 结果 |
| ---- | ---- |
| Debug / PostgreSQL OFF | configure/build `PASS`，CTest 341/341 |
| Release / PostgreSQL OFF | configure/build `PASS`，CTest 341/341 |

每个配置包含 292 个 unit/use-case、17 个 In-Memory integration、28 个 framework-neutral API 和 4 个 static gate。

### 3.2 Linux S12-06

| 配置 | 结果 |
| ---- | ---- |
| Debug / PostgreSQL ON | production configure/build `PASS`，CTest 343/343 |
| Release / PostgreSQL ON | production configure/build `PASS`，CTest 343/343 |
| Debug / PostgreSQL OFF | 全新目录 88/88 build steps，CTest 341/341 |

PostgreSQL ON 的 343 项由原有 341 项加两个强制外部 target 构成：

1. `postgresql_integration`：单进程内执行 12 个真实 PostgreSQL scenario。
2. `drogon_runtime_integration`：启动真实 `pfh_server`，执行 API smoke 并以 SIGTERM 关闭。

CTest 外层只注入 `PFH_TEST_DB_ADMIN`、`PFH_TEST_DB_REQUEST`、`PFH_TEST_DB_BACKGROUND` 三条 libpq conninfo。runtime 脚本用 `shlex` 解析 request/background conninfo，仅向子进程注入 `PFH_DB_*`，避免污染配置加载单元测试；本地 HTTP client 显式禁用代理，避免 Colima 代理截获 `127.0.0.1` 请求。Release runtime 连续 10 次复跑均通过。

### 3.3 Windows Provider 修正回归

| 配置 | 结果 |
| ---- | ---- |
| Debug / PostgreSQL OFF | configure/build `PASS`，CTest 349/349 |
| Release / PostgreSQL OFF | configure/build `PASS`，CTest 349/349 |

349 项由 300 个 unit/use-case、17 个 In-Memory integration、28 个 framework-neutral API 和 4 个 static gate 构成；PostgreSQL adapter、production bootstrap 与 production security compile gate 均通过。新增覆盖包括两个响应 schema、外部 rate Half-Even 归一、完整集合校验、整批主备切换、双源失败脱敏、串行 timeout 约束和 key 环境变量优先级。

Windows `PFH_BUILD_POSTGRESQL=ON` 真实依赖配置为 `BLOCKED`：本机未安装可供 CMake 解析的 Drogon package。该环境限制未被写成产品失败，也没有用 OFF 模式 stub 结果冒充真实 Drogon 编译；production ON 结果必须由 macOS/Colima 复测提供。

---

## 4. V1-V6 迁移矩阵

| 检查 | 结果 |
| ---- | ---- |
| 空库 V1-V6 `migrate` | `PASS` |
| `info` / `validate` | `PASS`，6 个 versioned migration 均为 Success |
| 第二次 `migrate` | `PASS`，明确 no-op |
| V1-V5 seed/schema 回归 | `PASS`，33 currencies、55 templates、认证索引/枚举、Transfer precision/constraint |
| RLS | `PASS`，8 张租户表同时 ENABLE/FORCE |
| V6 schema | `PASS`，Outbox lease/failure、handler receipt、System audit actor、scheduled job lease |
| V1-V5 legacy `processing` 升级 | `PASS`，可重试行转 `failed`，耗尽行转 `dead_letter`，lease 字段清空 |

迁移使用隔离 Docker network 和 tmpfs PostgreSQL 16.14 容器，没有操作开发数据库或持久 volume。

---

## 5. PostgreSQL Fixture

真实 fixture 为 `PFH_BUILD_POSTGRESQL=ON` 下的强制 CTest target；环境缺失时明确失败，不自动 skip。12/12 scenario 通过，覆盖：

- UoW commit、action error/exception/outbox error rollback、read-your-writes 和 outbox 原子性。
- FORCE RLS fail-closed、两用户隔离、连接池 tenant context 清理和 request/background 权限边界。
- User、Preference、Account、Category、Tag、Transaction、Transfer、ExchangeRate、Audit、Auth 主要写读路径。
- Account optimistic lock、余额缓存 `MAX(version)` + latest transaction id、三账户有序锁冲突回滚。
- Transfer 双腿与三种 fee source、聚合回滚、signed amount 和危险删除边界。
- `NUMERIC(20,8/10)` 最小/最大/scale/round-trip、append-only exchange rate 和 latest-at-or-before。
- 两 worker `SKIP LOCKED` claim、旧 token 拒绝、crashed worker lease 恢复、1m/5m/15m/1h/6h 退避与 dead letter。
- handler receipt + supplemental Audit 同事务、数据库时钟 job lease 接管、过期认证数据清理。

---

## 6. Drogon 与后台 Runtime

真实 production composition root + 双数据库角色 API smoke `PASS`：

- currencies 200、33 项、ETag、X-Trace-Id，且只有一个 `application/json; charset=utf-8` Content-Type。
- register、login、错误密码、refresh rotation、旧 token 复用撤销 session、logout 后 access 拒绝。
- 两用户 RLS 隔离，Account、Category、Tag、Preference、Transaction、Transfer、Balance、Net Worth、Cash Flow、Dashboard 闭环。
- 金额 JSON number 拒绝、金额 string round-trip、RFC 3339 offset、失败 TraceId 与实现细节脱敏。
- 真实进程反复启动/停止与 SIGTERM 退出码 0。

Outbox/Scheduler runtime `PASS`：并发 claim、lease recovery、dead letter、receipt 幂等和数据库时钟由 fixture 验证；最终容器发布 11/11 Outbox，`exchange-rate-refresh` 与 `session-cleanup` 两个 lease 均创建并释放，三个 timer 在 SIGTERM 后停止。

原 `d2549af` 基线的 OpenExchangeRates 真实 API blocker 已被后续 Provider 替换取代，不再是当前决策项。Windows 已用仓库外 key 完成脱敏 HTTPS 契约探测，但该探测不等于真实 Drogon transport、Scheduler 或 Linux runtime `PASS`，这些必须在新提交上由 macOS/Colima 复测。

---

## 7. Docker 门禁

- 新增 Ubuntu 24.04 多阶段 `Dockerfile`、`.dockerignore`、双角色 SQL 与 Compose application service。
- 冷构建 `PASS`：base digest `sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90`。
- 最终 arm64 镜像：`sha256:b2e161b3a551b06c50d8a31760397e2e15f49e70e8049e391692f4b6a5af9217`，36,763,570 bytes。
- runtime 用户为 non-root `pfh`，包含 `tzdata`、CA 和所需共享库，内置 healthcheck 进入 `healthy`。
- request role 为 non-superuser/non-BYPASSRLS；background role 为 non-superuser/BYPASSRLS/default-read-only，表权限仅为 `accounts`、`users` SELECT。
- 8 张租户表保持 FORCE RLS；健康、响应头、Outbox/Scheduler 和 20 秒窗口优雅停止通过，退出码 0、无 OOM。

当前机器没有 Docker Compose 插件，因此本轮使用与 Compose 定义等价的隔离 `docker run`/network 编排实跑；`docker-compose.yml` 保留可复现的 postgres -> flyway -> role-init -> app 顺序。

---

## 8. 缺陷与修复

1. Drogon 1.8.7 不存在 S11 stub 声明的 `registerEndingAdvice`。`ed0b10f` 删除虚构 ABI，由 composition root 析构保持 JobManager -> worker pool 的停止顺序；签名提交已推送。
2. Ubuntu `libdrogon-dev` 未递归安装其导出 CMake 配置探测的全部开发包。Docker builder 显式安装 Jsoncpp、UUID、Zlib、SQLite、Brotli、Hiredis、YAML 等依赖后冷构建通过。
3. `to_drogon()` 对 Content-Type 使用 `addHeader`，与 Drogon 默认值形成重复单值头。改用 `setContentTypeString` 后真实响应只保留唯一 JSON Content-Type，并由 runtime smoke 回归。
4. CTest 首轮把 `PFH_DB_*` 注入全部 343 项，污染 4 个配置测试；runtime 改为从 `PFH_TEST_DB_*` 为子进程构造环境后 Debug/Release 343/343。
5. Colima 的 urllib 代理不绕过 localhost，导致快速 runtime 复跑间歇性空体 502/超时；测试 client 固定无代理 opener 后连续 10/10 通过。

---

## 9. 返回条件

P1-S12-02 至 S12-06 原基线已经形成可追溯结果。Provider corrective round 返回后，Windows S12-07 应：

1. fast-forward 并验证 macOS 返回提交的 GPG 签名与固定测试对象。
2. 确认新提交上的 Linux production ON Debug/Release、真实主备 HTTPS、Scheduler 入库 source 和 Docker 均通过。
3. 重跑 Windows PostgreSQL OFF 门禁并完成 Provider、配置、文档和全项目一致性 review。
4. 定稿 Tasks 与交付文档，完成 Phase 1 最终签署和合并决策。

在 S12-07 完成前，Phase 1 仍不得宣告最终完成或合并到 `main`。

---

## 10. Provider Corrective Round

### 10.1 实现结论

- 主源：FreeCurrencyAPI，base URL `https://api.freecurrencyapi.com`，key 只通过 `PFH_FREECURRENCYAPI_API_KEY` 注入；旧环境变量保留为兼容别名。
- 备用源：exchangerate.fun，base URL `https://api.exchangerate.fun`，不需要 key。
- `FailoverExchangeRateProvider` 先请求完整主源批次；任一 transport、timeout、HTTP 或响应校验失败后，完整原批次切换备用源，同一成功批次不混源。
- 两个 adapter 通过 SAX 保留 JSON numeric token，外部 rate 显式 Half-Even 归一到 10 位后校验 `NUMERIC(20,10)`；用户输入 rate 的严格 scale 拒绝规则不变。
- 成功快照保存实际 source `FreeCurrencyAPI` 或 `exchangerate.fun`；双源失败事件 identity 为 `FreeCurrencyAPI/exchangerate.fun`，错误不含 key、URL、响应 body 或底层异常。

### 10.2 脱敏真实端点结果

| 场景 | 结果 |
| ---- | ---- |
| FreeCurrencyAPI 支持币种批次 | `PASS`，HTTP 200，返回集合与请求集合精确相等 |
| FreeCurrencyAPI 请求含 TWD | 预期 HTTP 422，触发整批备用路径 |
| exchangerate.fun 请求 CNY + TWD | `PASS`，HTTP 200，USD base、整数 timestamp、两目标完整；额外 superset 被忽略 |
| 精度 | 备用源 BTC 可返回超过 10 位小数；显式 Half-Even 归一测试已覆盖并通过 |

能力快照只用于暴露当前限制，不是永久供应商承诺：FreeCurrencyAPI 当前覆盖 PFH 的 USD + 18 法币；exchangerate.fun 覆盖 20 法币 + BTC。其余 12 种加密货币需要后续专用定价源；当前整批失败后只能使用完整历史快照降级。

### 10.3 待 macOS/Colima 复测

- production ON Debug/Release 预计各 351 项（Windows 349 基线 + 两个真实 PostgreSQL/runtime target），数量变化必须解释。
- 使用仓库外 key 验证真实 Drogon FreeCurrencyAPI 成功路径；不得记录 key、完整 URL、响应 body 或原始日志。
- 通过受控目标币种或等价故障注入让主源失败，验证 exchangerate.fun 整批成功、实际 source 入库和事件 payload。
- 验证双源失败时只进入历史快照降级，并复跑 Scheduler、Docker 冷构建/健康、Outbox、lease 和 SIGTERM 停止。
