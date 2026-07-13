# Personal Finance Hub - Linux Development Workflow

Version: 1.1
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD
Status: Active

---

## 1. 目标

本指南用于约定 PFH 后端在 Linux 环境下的标准开发、构建、运行与测试工作流。

由于最终部署目标是 Linux，本项目在 PostgreSQL、Drogon、Repository 和 API 开发期间，应持续保证以下事实：

- 代码可以在 Linux 工具链下完成配置与编译。
- 单元测试和后续集成测试优先在 Linux 环境回归。
- Phase 交付前，Linux 环境下的关键质量命令必须跑通。

---

## 2. 推荐环境

### 2.1 推荐发行版

- Ubuntu 24.04 LTS
- Ubuntu 22.04 LTS

其他 Debian 系发行版通常也可使用，但默认以 Ubuntu 工作流为准。

### 2.2 推荐开发方式

按优先顺序推荐：

1. **原生 Linux 开发机**
2. **Windows + WSL2 (Ubuntu)**
3. **Linux 虚拟机**

如果主要开发机仍是 Windows，建议至少保留一套 WSL2 Linux 环境，用于：

- Linux 工具链构建
- Linux 单元测试回归
- 后续 PostgreSQL / Drogon 集成验证

### 2.3 基础工具

- 支持 `std::chrono` IANA tzdb 的 C++23 工具链（以 GCC 14+ / 新版 libstdc++ 为基线，最终以 CMake 能力探测结果为准）
- CMake 3.20+
- Ninja
- Git
- pkg-config
- curl / unzip / tar
- PostgreSQL 16+（P1-S07 之后需要）

---

## 3. Phase 分支工作流

PFH 的阶段开发默认采用 **每个 Phase 一个长期分支** 的方式推进。

当前 Phase 1 分支为 `feature/phase1-foundation`。后续 Phase 继续遵循“一阶段一长期分支”，实际名称在对应阶段计划中记录。

标准流程：

1. 从 `main` 切出对应 Phase 分支。
2. 整个 Phase 的开发、测试、文档回写和交付记录都在该 Phase 分支内完成。
3. 在该 Phase 分支上完成完整回归和交付验收后，再合并回 `main`。
4. 未完成完整测试和交付总结前，不直接把该 Phase 的半成品合并到 `main`。

示例：

```bash
git switch main
git pull --ff-only origin main
git switch -c feature/phase1-foundation
```

如果分支已存在：

```bash
git switch feature/phase1-foundation
git pull --ff-only origin main
```

---

## 4. Linux 环境初始化

### 4.1 安装工具链

以 Ubuntu 为例：

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  gcc-14 g++-14 \
  clang-16 \
  cmake \
  ninja-build \
  pkg-config \
  git \
  curl \
  unzip \
  tar \
  tzdata
```

如需 PostgreSQL 本地测试环境：

```bash
sudo apt install -y postgresql postgresql-client
```

### 4.2 选择编译器

当前 Phase 1 的 `Decimal` 实现依赖 GCC/Clang 的原生 `__int128` 扩展，因此 Linux 环境推荐优先使用 GCC。

```bash
export CC=gcc-14
export CXX=g++-14
```

如需改用 Clang：

```bash
export CC=clang-16
export CXX=clang++-16
```

Clang 在 Linux 上通常仍使用系统 `libstdc++`；仅升级 Clang 本身不代表具备
`std::chrono::locate_zone`。项目在 CMake configure 阶段会编译并链接一个 tzdb
探针，不满足时直接报错。运行环境还必须安装 `tzdata`，否则 IANA 时区无法加载。

---

## 5. 获取代码与目录准备

```bash
git clone <repo-url>
cd PFH
git switch feature/phase1-foundation
```

建议为 Linux 构建单独使用专用目录，避免和 Windows 构建目录混用：

```bash
mkdir -p build/linux-gcc-debug
```

---

## 6. 配置与编译

### 6.1 配置

```bash
cmake -S . -B build/linux-gcc-debug \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
```

### 6.2 编译

```bash
cmake --build build/linux-gcc-debug
```

### 6.3 Release 构建

```bash
cmake -S . -B build/linux-gcc-release \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/linux-gcc-release
```

---

## 7. 配置文件工作流

首次运行前复制本地配置：

```bash
cp config/config.example.json config/config.local.json
```

当前阶段：

- `config.example.json` 用于提供结构模板。
- `config.local.json` 用于本地 Linux 开发。
- 敏感信息不应提交到 Git。

`JsonConfigLoader` 已支持环境变量 overlay，应优先使用环境变量覆盖敏感项，例如：

- `PFH_JWT_SECRET`
- `PFH_DB_PASSWORD`
- `PFH_DB_HOST`

---

## 8. 运行测试

### 8.1 单元测试

```bash
ctest --test-dir build/linux-gcc-debug --output-on-failure
```

### 8.2 指定测试

```bash
ctest --test-dir build/linux-gcc-debug -R Decimal --output-on-failure
```

### 8.3 当前阶段测试约定

- P1-S01 至 P1-S09：单元测试与 In-Memory integration scenarios 已建立快速回归基线。
- P1-S10：加入 Drogon API 测试和 PostgreSQL 适配器测试场景。
- P1-S11：加入 Outbox、Scheduler、汇率 HTTP Provider 和清理任务测试。
- P1-S12：在另一台机器执行 Linux、Docker、真实 PostgreSQL 和 Debug/Release 阻断门禁。

---

## 9. Linux 下的质量检查工作流

当前仓库已有 `quality_check.ps1`，更适合 PowerShell 环境。

Linux 下建议先执行等价命令链：

```bash
git diff --check
cmake -S . -B build/linux-gcc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-gcc-debug
ctest --test-dir build/linux-gcc-debug --output-on-failure
```

如果本机已安装 PowerShell，也可以直接运行：

```bash
pwsh ./quality_check.ps1
```

---

## 10. 运行服务

```bash
./build/linux-gcc-debug/pfh_server
```

当前 `main.cpp` 仍处于启动占位阶段，主要用于验证：

- 配置文件可读取
- 日志可初始化
- Linux 可执行文件可正常启动

---

## 11. PostgreSQL 与 Docker 外部验收

P1-S12 的 Linux、Docker 和真实 PostgreSQL 验收在另一台具备对应环境的机器执行。该机器必须验证：

1. PostgreSQL 本地实例可连接
2. 迁移脚本可在空库执行
3. Repository 集成测试可重复运行
4. 事务回滚与 outbox 路径在 Linux 下行为一致
5. RLS fail-closed 与连接池用户上下文复用不串租户
6. `NUMERIC(20,8/10)` 边界、舍入和超界拒绝与 Domain 一致
7. Docker 服务可启动并通过健康检查与关键 API smoke test

建议为本地测试数据库单独创建用户和库，避免污染日常开发数据库。

结果必须回写 `Docs/Guides/Local_Test_Environment.md` 或 P1-S12 交付总结，至少记录 commit hash、Linux/编译器/Drogon/PostgreSQL/Docker 版本、迁移命令、测试命令、通过数量和失败详情。未取得这些记录前，不得将历史 Linux 测试或 Windows/In-Memory 结果当作当前 HEAD 的目标环境签署。

---

## 12. Phase 交付前检查清单

在对应 Phase 分支准备合并回 `main` 之前，至少完成以下检查：

- `git diff --check` 通过
- Linux Debug 与 Release 构建均通过
- Linux Debug 与 Release 对应测试集均通过
- PostgreSQL 空库迁移、真实 Repository/UoW/RLS 与 API 测试通过
- Docker 服务启动、健康检查和关键 API smoke test 通过
- 该 Phase 的交付总结文档已回写
- `Docs/Development/tasks.md` 已同步任务状态
- 与设计不一致的地方已先回写架构文档

---

## 13. 常用命令速查

```bash
# 进入 Phase 分支
git switch feature/phase1-foundation

# Debug 配置
cmake -S . -B build/linux-gcc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build build/linux-gcc-debug

# 测试
ctest --test-dir build/linux-gcc-debug --output-on-failure

# 运行
./build/linux-gcc-debug/pfh_server
```
