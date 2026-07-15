# Phase 1 S12 测试收尾与文档回写 - 交付记录

**更新日期**: 2026-07-16
**阶段**: P1-S12 Phase 1 测试收尾与文档回写
**当前状态**: S12-01 至 S12-07 全部完成；Phase 1 已签署并具备合并条件

---

## 1. 范围与测试对象

- 分支：`feature/phase1-foundation`。
- Windows S12-01 测试对象：`6cd41bc2c60af1298544d975c58819cc8c0600a9`。
- macOS 接收基线：`297fe636c0b441aaf0807d0487cf9320b41c780e`。
- macOS production ABI 修复：`ed0b10f4567232d5558914464092a24213958941`（`fix: support Drogon 1.8 shutdown lifecycle`）。
- macOS 原最终测试对象：`d2549af142c92fa08bc15e2027e1163053e355ca`，包含 PostgreSQL fixture、真实 Drogon smoke、Docker 编排与 Content-Type 修复。
- Provider 初始实现：`16e169bd9f67d93898f96d971443a67044afb1e9`。
- Provider TLS 修复与 macOS 测试对象：`ef66d995f0f9f51e7936f43af9ddc9d524fc6e56`（`fix: use TLS-capable exchange rate transport`）。
- 所有测试数据库、容器、网络和凭据均为一次性本地资源；未保存认证响应、Token、数据库转储或原始日志。

原轮次完成 P1-S12-02 至 S12-06；corrective round 只替换外部汇率 Provider 并复测受影响门禁。Windows 已在返回提交上完成 S12-07 最终回归、全项目一致性 review、文档定稿与 Phase 1 签署；`main` 合并和 Phase 2 开发仍不在本记录范围。

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
| libcurl | 离线 compile gate | 8.5.0-2ubuntu10.11，OpenSSL 3.0.13 backend |
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

Windows `PFH_BUILD_POSTGRESQL=ON` 真实依赖配置为 `BLOCKED`：本机未安装可供 CMake 解析的 Drogon package。该环境限制未被写成产品失败，也没有用 OFF 模式 stub 结果冒充真实 Drogon 编译；对应 production ON 结果见下方 macOS/Colima 复测。

### 3.4 macOS Provider 修正回归

| 配置 | 结果 |
| ---- | ---- |
| Debug / PostgreSQL ON | configure/build `PASS`，CTest 351/351 |
| Release / PostgreSQL ON | configure/build `PASS`，CTest 351/351 |
| PostgreSQL OFF | configure/build `PASS`，CTest 349/349 |

两个 production ON 配置均实际执行并通过 `postgresql_integration` 与 `drogon_runtime_integration`，没有 skip。Provider 定向 unit tests 为 13/13；临时诊断材料与 key/query 模式扫描为 absent。

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

原 `d2549af` 基线的 OpenExchangeRates 真实 API blocker 已被后续 Provider 替换取代，不再是当前决策项。`ef66d99` 已在 macOS/Colima 使用仓库外 key 完成真实 libcurl HTTPS、Scheduler、PostgreSQL 与 Docker runtime 复测；具体主源、整批备用和双源失败结果见第 10.3 节。

---

## 7. Docker 门禁

- 新增 Ubuntu 24.04 多阶段 `Dockerfile`、`.dockerignore`、双角色 SQL 与 Compose application service。
- 冷构建 `PASS`：base digest `sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90`。
- 最终 arm64 镜像：`sha256:b2e161b3a551b06c50d8a31760397e2e15f49e70e8049e391692f4b6a5af9217`，36,763,570 bytes。
- runtime 用户为 non-root `pfh`，包含 `tzdata`、CA 和所需共享库，内置 healthcheck 进入 `healthy`。
- request role 为 non-superuser/non-BYPASSRLS；background role 为 non-superuser/BYPASSRLS/default-read-only，表权限仅为 `accounts`、`users` SELECT。
- 8 张租户表保持 FORCE RLS；健康、响应头、Outbox/Scheduler 和 20 秒窗口优雅停止通过，退出码 0、无 OOM。

当前机器没有 Docker Compose 插件，因此本轮使用与 Compose 定义等价的隔离 `docker run`/network 编排实跑；`docker-compose.yml` 保留可复现的 postgres -> flyway -> role-init -> app 顺序。

Provider corrective round 再次从当前 `Dockerfile` 执行 `--no-cache --pull` 冷构建并通过。Ubuntu 24.04 base digest 仍为 `sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90`；最终 ARM64 image 为 `sha256:86d3ef5d0c29a26fc4a4d13548ba1969bf4302d0509aad27ae66ddf64c7fed1e`，36,770,560 bytes，包含 tzdata `2026b-0ubuntu0.24.04.1`、curl/libcurl `8.5.0-2ubuntu10.11` 和 OpenSSL backend。容器以 `pfh`/UID 1001 运行并进入 `healthy`，真实主源生成 1 条 CNY 快照与 1 条已发布事件，全部 11 条 Outbox 为 published，lease 为 0；双角色、8/8 FORCE RLS、唯一 JSON Content-Type、ETag、TraceId、日志脱敏、SIGTERM exit 0 和无 OOM 均通过。

---

## 8. 缺陷与修复

1. Drogon 1.8.7 不存在 S11 stub 声明的 `registerEndingAdvice`。`ed0b10f` 删除虚构 ABI，由 composition root 析构保持 JobManager -> worker pool 的停止顺序；签名提交已推送。
2. Ubuntu `libdrogon-dev` 未递归安装其导出 CMake 配置探测的全部开发包。Docker builder 显式安装 Jsoncpp、UUID、Zlib、SQLite、Brotli、Hiredis、YAML 等依赖后冷构建通过。
3. `to_drogon()` 对 Content-Type 使用 `addHeader`，与 Drogon 默认值形成重复单值头。改用 `setContentTypeString` 后真实响应只保留唯一 JSON Content-Type，并由 runtime smoke 回归。
4. CTest 首轮把 `PFH_DB_*` 注入全部 343 项，污染 4 个配置测试；runtime 改为从 `PFH_TEST_DB_*` 为子进程构造环境后 Debug/Release 343/343。
5. Colima 的 urllib 代理不绕过 localhost，导致快速 runtime 复跑间歇性空体 502/超时；测试 client 固定无代理 opener 后连续 10/10 通过。
6. Ubuntu 24.04 的系统 Trantor 1.5.12 报告 TLS backend `None`，Drogon `supportsTls()` 为 false，原 transport 因而把明文 HTTP 发到 HTTPS 443 并稳定收到 Cloudflare 400。`ef66d99` 改用证书校验开启、只允许 HTTPS、无 redirect、带硬超时与响应大小上限的 libcurl transport；Drogon 继续只承载入站 runtime。修复后两端真实 HTTPS 与全部回归门禁均通过。

---

## 9. 最终签署

P1-S12-02 至 S12-06 原基线和 Provider corrective round 均已形成可追溯结果。Windows S12-07 已完成：

1. 主仓库 fast-forward 到 `9c470dd1d7c75ffc6848a741c1b8ff186620aa18`，父链包含 `ef66d995f0f9f51e7936f43af9ddc9d524fc6e56`。两次提交均携带预期 macOS key id；macOS 已记录 `Good signature`，Windows 因缺少对应公钥只验证了提交对象、父链、key id 与远端一致。
2. Linux production ON Debug/Release、真实主备 HTTPS、Scheduler 入库 source、双源失败历史降级和 Docker 结果全部满足返回条件。
3. Windows Debug / PostgreSQL OFF 通过 `quality_check.ps1` 与 349/349 CTest；独立 Release 配置、编译和 349/349 CTest 通过。
4. Provider、配置、安全边界、架构依赖、OpenAPI、迁移、Scheduler/Outbox、Docker 与文档完成全项目一致性 review，未发现新的产品缺陷或设计偏离。
5. Tasks 与交付文档已定稿，Phase 1 阶段记录已归档；测试 API key 已轮换。

结论：Phase 1 已完成并满足合并到 `main` 的门槛。本结论不表示 feature 分支已经实际合并；合并仍由维护者单独确认。

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

### 10.3 macOS/Colima 复测结果

四个真实 Scheduler 场景均在隔离 PostgreSQL 16.14 fixture 上通过：

1. CNY + EUR 主源：写入 2 条新快照，source 全为 `FreeCurrencyAPI`，2 条匹配 Outbox 事件 published，2 条 supplemental audit，活跃 lease 为 0。
2. CNY + TWD 整批备用：写入 2 条新快照，source 全为 `exchangerate.fun`，主源写入为 0，2 条匹配 Outbox 事件 published，活跃 lease 为 0。
3. EUR + ETH 双源失败且历史完整：无新快照，保留 2 条预置历史；失败事件 provider 为 `FreeCurrencyAPI/exchangerate.fun`、`historicalAvailable=true`，事件 published，活跃 lease 为 0。
4. EUR + ETH 双源失败且历史不完整：无新快照，仅保留 1 条预置 EUR 历史；失败事件 provider 相同、`historicalAvailable=false`，事件 published，活跃 lease 为 0。

所有场景与最终 Docker runtime 的日志扫描均未发现 key、Authorization、完整 query URL、response body 或临时诊断标记。API key 轮换状态：已轮换；仓库中未记录任何可关联材料。
