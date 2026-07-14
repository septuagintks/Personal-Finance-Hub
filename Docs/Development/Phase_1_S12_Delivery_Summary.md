# Phase 1 S12 测试收尾与文档回写 - 交付记录

**更新日期**: 2026-07-15
**阶段**: P1-S12 Phase 1 测试收尾与文档回写
**当前状态**: S12-01 至 S12-06 已完成；真实 OpenExchangeRates API 因未提供外部 API key 保持 `BLOCKED`；等待 Windows 执行 S12-07 最终签署

---

## 1. 范围与测试对象

- 分支：`feature/phase1-foundation`。
- Windows S12-01 测试对象：`6cd41bc2c60af1298544d975c58819cc8c0600a9`。
- macOS 接收基线：`297fe636c0b441aaf0807d0487cf9320b41c780e`。
- macOS production ABI 修复：`ed0b10f4567232d5558914464092a24213958941`（`fix: support Drogon 1.8 shutdown lifecycle`）。
- macOS 最终测试对象：`ed0b10f` 加本交付提交中的 PostgreSQL fixture、真实 Drogon smoke、Docker 编排与 Content-Type 修复；最终提交哈希由 `.codex/HandOff.md` 在推送后记录。
- 所有测试数据库、容器、网络和凭据均为一次性本地资源；未保存认证响应、Token、数据库转储或原始日志。

本轮只完成 P1-S12-02 至 S12-06，不执行 S12-07，不合并 `main`，也不开始 Phase 2。

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

真实 OpenExchangeRates HTTPS/TLS/API 响应验证为 `BLOCKED`：维护者未提供外部 API key。mock transport、dummy key 错误路径和容器内 Scheduler 启动结果均未被当作真实 API `PASS`。

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

P1-S12-02 至 S12-06 除需要外部 key 的真实 Provider 调用外均已形成可追溯结果。Windows S12-07 应：

1. fast-forward 并验证 macOS 主项目提交的默认 GPG 签名。
2. 重跑 Windows PostgreSQL OFF 本地门禁并 review 本轮 fixture、Docker 与 adapter 差异。
3. 决定真实 OpenExchangeRates API blocker 是在签署前补测，还是以明确限制延期。
4. 定稿 Tasks 与交付文档，完成 Phase 1 最终签署和合并决策。

在 S12-07 完成前，Phase 1 仍不得宣告最终完成或合并到 `main`。
