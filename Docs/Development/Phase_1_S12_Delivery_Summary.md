# Phase 1 S12 测试收尾与文档回写 - 交付记录

**更新日期**: 2026-07-15
**阶段**: P1-S12 Phase 1 测试收尾与文档回写
**当前状态**: IN PROGRESS - S12-01 Windows 本地回归已通过；S12-02 至 S12-06 待 macOS/Colima Linux 目标环境执行；S12-07 与 Phase 1 最终签署待结果返回后完成

---

## 1. 本轮范围与测试基线

Windows 侧本轮只完成 P1-S12-01 和外部交接准备，不把本地 compile gate、In-Memory 测试或 framework-neutral API 测试解释为真实 PostgreSQL/Drogon runtime 验收。

- 测试对象：`6cd41bc2c60af1298544d975c58819cc8c0600a9`（`feat: complete Phase 1 S11 background runtime`）。
- 分支：`feature/phase1-foundation`。
- 本轮代码变化：无；Debug/Release 独立构建与全量测试未发现需要修改的产品缺陷。
- 本轮文档变化：记录 Windows S12-01 结果、外部阻断项、测试基础设施现状和交接返回条件。
- 最终状态边界：取得 S12-02 至 S12-06 的可追溯结果前，本文档保持 `IN PROGRESS`，Phase 1 不得签署完成或合并到 `main`。

---

## 2. Windows 测试环境

| 项目 | 版本或取值 |
| ---- | ---------- |
| OS | Microsoft Windows `10.0.22631.6060`，x64 |
| PowerShell | 7.6.3 |
| Compiler | GCC/G++ 16.1.0，C++23 |
| CMake | 4.3.2 |
| Ninja | 1.13.2 |
| Python | 3.14.6 |
| Build mode | 独立 Debug 与 Release |
| PostgreSQL adapter | `PFH_BUILD_POSTGRESQL=OFF`；生产源码由三类离线 compile gate 覆盖 |

两个配置均通过 `std::chrono::locate_zone` 能力探测。依赖源码复用已有 `build/_deps/*-src`，项目本身在两个全新构建目录中重新配置和编译，没有复用旧项目目标。

---

## 3. 可复现命令

以下 PowerShell 命令等价于本轮实际执行方式，同时避免写入机器绝对路径：

```powershell
$deps = (Resolve-Path build/_deps).Path.Replace('\', '/')
$common = @(
    '-G', 'Ninja',
    '-DPFH_BUILD_POSTGRESQL=OFF',
    "-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$deps/googletest-src",
    "-DFETCHCONTENT_SOURCE_DIR_SPDLOG=$deps/spdlog-src",
    "-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$deps/nlohmann_json-src"
)

cmake -S . -B build/s12-dbg @common -DCMAKE_BUILD_TYPE=Debug
cmake --build build/s12-dbg --parallel 4
ctest --test-dir build/s12-dbg --output-on-failure -C Debug

cmake -S . -B build/s12-rel @common -DCMAKE_BUILD_TYPE=Release
cmake --build build/s12-rel --parallel 4
ctest --test-dir build/s12-rel --output-on-failure -C Release

git diff --check
```

---

## 4. P1-S12-01 验证结果

| 检查 | Debug | Release |
| ---- | ----- | ------- |
| 独立 CMake configure | `PASS` | `PASS` |
| IANA tzdb capability probe | `PASS` | `PASS` |
| 全新项目 build | `PASS`，104/104 steps | `PASS`，104/104 steps |
| CTest | `PASS`，341/341 | `PASS`，341/341 |
| 失败测试 | 0 | 0 |
| PostgreSQL adapter compile/contract gate | `PASS` | `PASS` |
| Production bootstrap compile gate | `PASS` | `PASS` |
| Production security compile gate | `PASS` | `PASS` |

每个配置的 341 项测试构成一致：

| 测试层级 | 数量 |
| -------- | ---: |
| Unit / Use Case | 292 |
| In-Memory Integration | 17 |
| Framework-neutral API | 28 |
| Static contract gates | 4 |
| 合计 | 341 |

提交前补充门禁：`git diff --check` 通过，项目 Markdown 基础检查通过，未发现真实凭据或 `.codex/` 主仓库暂存项。

---

## 5. 本地 review 结论

1. Debug 与 Release 均从空项目构建目录完成，未发现仅在某个配置下出现的编译、链接、告警或测试差异。
2. `-Werror` 下 S11 新增 Outbox、Provider、Scheduler、production composition root 和测试目标均可完整编译。
3. PostgreSQL、Drogon、OpenSSL 与 Argon2 的 OFF 模式 stub/compile gate 只能证明接口和翻译单元形状，不能证明真实 ABI、SQL、权限或 runtime 行为。
4. 当前 `tests/integration/repository_integration_test.cpp` 仍只驱动 In-Memory adapter；不存在可直接运行的 PostgreSQL-backed fixture。
5. 当前 `docker-compose.yml` 只包含 PostgreSQL 与 Flyway；尚无应用 Dockerfile、应用 service、双角色初始化和容器内 API/runtime smoke 入口。

因此，外部阶段不是单纯重复现有 CTest。macOS 侧需要在真实环境中补齐并验证 PostgreSQL fixture、runtime 测试入口和应用镜像；这些测试/部署基础设施属于 P1-S12 允许范围。

---

## 6. macOS/Linux 外部阶段范围

### 6.1 P1-S12-02 环境固定

- 记录 macOS、Colima/Linux、CPU 架构、GCC/libstdc++、CMake、Ninja、Drogon、PostgreSQL client/server、Flyway、OpenSSL、Argon2、Docker 与 `tzdata` 版本。
- 从交接文档指定的完整 commit 开始；两个仓库必须干净且与远端一致。
- 使用临时凭据和可删除数据库，不在主项目或交接仓库保存密码、Token、完整响应和原始日志。

### 6.2 P1-S12-03 至 S12-05 真实行为

- V1-V6 空库迁移、`info`、`validate`、第二次 no-op，以及含 legacy `processing` outbox 行的 V1-V5 到 V6 升级。
- PostgreSQL Repository、`DrogonUnitOfWork`、RLS、连接池复用、事务回滚、并发锁、乐观锁、余额缓存、历史汇率和 `NUMERIC(20,8/10)` 边界。
- 真实 Drogon API 与认证 smoke、两用户隔离、转账原子性、错误脱敏、TraceId 和报表时区月边界。
- Outbox 多连接 claim、claim token、重试/dead letter、handler receipt 原子性、进程重启恢复和数据库时钟语义。
- Scheduler 多实例 lease、任务超过 lease 的接管/重复执行、真实 timer lifecycle、token 清理、优雅停止和 OpenExchangeRates HTTPS/TLS/timeout。

### 6.3 P1-S12-06 Linux 与 Docker

- `PFH_BUILD_POSTGRESQL=ON` 下独立 Debug/Release configure、build 和全量测试。
- 创建并验证应用 Docker 镜像及必要的测试 Compose/脚本；不得使用 superuser request role、同一 request/background role 或关闭 FORCE RLS 绕过启动校验。
- 容器启动后执行健康判定和关键 API/runtime smoke，并确认 `tzdata`、迁移、配置注入和优雅退出完整。

---

## 7. 缺陷处理与返回条件

- 环境问题与产品缺陷分开记录；首个稳定失败应包含命令、环境版本、脱敏错误和最小复现。
- macOS 侧可直接修复能够稳定复现的实现、测试或部署阻断项，但主项目提交必须遵守 `git-signing`；不得降低断言、削弱 RLS、改写历史迁移或关闭安全校验。
- 涉及架构边界、数据语义或安全降级的选择不得静默决定，应先在 `HandOff.md` 记录问题与影响。
- 每次修复后重跑直接相关测试，并在最终提交上重跑完整 Debug/Release、PostgreSQL、API、Outbox/Scheduler 和 Docker 门禁。
- 返回时提供最终主项目 commit、签名状态、环境矩阵、命令与数量摘要、未执行项、残余风险，以及两个仓库的 clean/sync 状态。
- Windows 接收后完成 P1-S12-07：复核签名与差异、重跑本地门禁、执行 Phase 1 最终全量 review、关闭或延期 Tasks、定稿并归档交付文档，再决定是否合并 `main`。
