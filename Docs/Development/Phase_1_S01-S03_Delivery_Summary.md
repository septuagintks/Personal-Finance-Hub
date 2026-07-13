# Phase 1 S01-S03 工程骨架与本地开发 - 交付记录

**阶段**: P1-S01 目录结构与工程骨架 / P1-S02 CMake、依赖与编译选项 / P1-S03 测试入口与质量命令
**状态**: 已完成

---

## 1. 概述

根据 `Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md`，本阶段建立后端工程骨架、
构建系统与测试入口，为后续金融原语和领域实现提供可构建、可回归的基础。

- **P1-S01**: 目录结构与工程骨架
- **P1-S02**: CMake、依赖与编译选项
- **P1-S03**: 测试入口与质量命令

---

## 2. 目录结构

按 Clean Architecture 物理边界创建后端目录，头文件位于 `include/pfh/`，实现位于 `src/`：

```text
C++/PFH/
├── CMakeLists.txt              # 根构建配置
├── cmake/                      # CMake 模块（Dependencies.cmake 等）
├── config/
│   ├── config.example.json     # 配置模板
│   └── README.md               # 配置说明
├── include/pfh/                # 公共头文件（按层组织）
│   ├── domain/
│   ├── application/
│   ├── infrastructure/
│   └── presentation/
├── src/                        # 实现文件（按层组织）
│   ├── domain/
│   ├── application/
│   ├── infrastructure/
│   ├── presentation/
│   └── bootstrap/
│       └── main.cpp            # 应用启动入口
├── tests/                      # 三层测试结构
│   ├── unit/                   # 单元测试（无数据库/网络）
│   ├── integration/            # 集成测试（含 PostgreSQL）
│   ├── api/                    # API 测试
│   ├── support/                # 测试夹具与公共断言
│   └── TEST_NAMING_CONVENTION.md
├── migrations/                 # 数据库迁移脚本
├── quality_check.ps1           # 本地质量检查脚本
├── vcpkg.json                  # 依赖清单（可选）
├── .gitignore
└── README.md
```

约定：

- Domain 目录不包含 Drogon、PostgreSQL 或 JSON 相关依赖。
- 头文件与实现分离，便于管理 `#include` 路径。
- `main.cpp` 仅作应用启动入口占位，业务逻辑随后续阶段接入。

---

## 3. CMake 构建系统

**文件**: `CMakeLists.txt`

- C++23 标准（`CMAKE_CXX_STANDARD 23`，禁用编译器扩展）。
- 编译警告级别：GCC/Clang `-Wall -Wextra -Werror -pedantic -Wconversion -Wsign-conversion`；
  MSVC `/W4 /WX /permissive- /utf-8`。
- Debug/Release 构建类型，默认 Debug。
- 分层库目标：`pfh_domain`（已建立，无框架依赖）、`pfh_application`/`pfh_infrastructure`/
  `pfh_presentation`（随实现推进逐步启用）。
- `CMAKE_EXPORT_COMPILE_COMMANDS ON`，输出 `compile_commands.json`。

依赖解析见 `cmake/Dependencies.cmake`：优先 `find_package`（vcpkg/系统包），未找到时用
FetchContent 自动拉取 spdlog、nlohmann_json、GoogleTest。无需手动安装 vcpkg 即可构建。
可用 `-DPFH_FORCE_FETCHCONTENT=ON` 强制走 FetchContent（用于可复现 CI）。

---

## 4. 测试基础设施

**测试目录**：`tests/{unit,integration,api,support}` 三层分离，分别对应无依赖单元测试、
数据库集成测试和 API 测试。

**命名规范**（见 `tests/TEST_NAMING_CONVENTION.md`）：

```text
<ClassName>_When<Condition>_<ExpectedBehavior>

示例：
- Money_WhenAddingSameCurrency_ReturnsCorrectSum
- TransferAggregate_WhenOutgoingAndRateProvided_CalculatesIncoming
- CreateTransfer_WhenUnauthorized_Returns401
```

每个高风险组件应覆盖正常路径、边界路径和错误路径三类用例。

**覆盖率目标**：Domain ≥ 90%、Application ≥ 80%、Infrastructure ≥ 60%、Presentation ≥ 60%。
覆盖率不是唯一目标，金融核心规则、错误路径、事务回滚路径优先。

GoogleTest 通过 FetchContent 自动拉取（v1.15.2）并已实际运行；单元测试通过 `ctest` 统一执行。

---

## 5. 质量门禁

**文件**: `quality_check.ps1`

覆盖以下检查：

1. Git 空白符检查（`git diff --check`）。
2. CMake 配置验证。
3. 完整构建验证。
4. 单元测试执行（`ctest`）。
5. Markdown 文件检查。

---

## 6. 配置管理

- `config/config.example.json`：配置模板，含服务器、数据库、JWT、日志、调度器、汇率提供者配置项。
- `config/README.md`：配置说明与安全注意事项。
- `.gitignore` 保护敏感配置文件（`config/*.local.*`、`.env` 等），示例模板使用占位符。

---

## 7. 验收标准对照

### P1-S01 目录结构与工程骨架

- [x] 建立项目后端代码目录，固定 Clean Architecture 物理边界。
- [x] 目录结构与计划文档一致。
- [x] Domain 目录不包含框架依赖。
- [x] 空工程可以进入 CMake 配置阶段。
- [x] 创建最小 `main.cpp` 入口点。
- [x] 建立 `tests/support/` 测试工具目录。

### P1-S02 CMake、依赖与编译选项

- [x] 根 `CMakeLists.txt` 设置 C++23。
- [x] 拆分核心库目标（domain/application/infrastructure/presentation）。
- [x] 测试目标单独放在 `tests/`。
- [x] 引入 spdlog、nlohmann_json、GoogleTest（FetchContent 回退）。
- [x] 设置 Debug/Release 构建类型。
- [x] 可以完成 CMake configure。
- [x] Domain 目标可以独立编译，不链接框架。
- [x] 测试目标可以被 CTest 发现。

### P1-S03 测试入口与质量命令

- [x] 接入 GoogleTest。
- [x] 创建 smoke unit test。
- [x] 创建测试命名规范。
- [x] 创建测试数据目录。
- [x] 建立本地质量命令（构建、单元测试、文档检查）。
- [x] 测试失败时能定位到具体测试名。
- [x] `git diff --check` 可作为提交前检查。

---

## 8. 验证结果

```text
cmake configure: 通过（GNU 16.1.0, C++23, Debug）
cmake build:     通过（-Wall -Wextra -Werror -pedantic）
ctest:           71 个单元测试全部通过
Domain 库:       pfh_domain 独立编译，不链接 spdlog/框架
```

> 注：本文件初版编写时依赖尚未接入、测试未运行；随 P1-S04/S05 接入 FetchContent
> 依赖并实现 Decimal 后，构建与全部单元测试已实际通过，验证结果以此处为准。

---

## 9. 关键设计决策

### 9.1 编译器警告策略

启用 `-Werror`（警告视为错误）。在项目早期建立严格代码质量标准，避免警告积累；金融系统对
正确性要求高。影响：所有代码须通过严格警告检查，未使用参数标记 `[[maybe_unused]]`。

### 9.2 头文件与实现分离

头文件在 `include/pfh/`，实现在 `src/`。清晰区分公共接口和私有实现，便于管理 `#include` 路径。

### 9.3 测试三层结构

unit/integration/api 三层分离。不同测试类型依赖和速度不同：单元测试快速频繁运行，集成测试
覆盖数据库交互，API 测试验证端到端行为。

### 9.4 配置文件格式

使用 JSON 而非 YAML。Drogon 原生支持 JSON，C++ JSON 库更成熟，配置结构简单无需 YAML 高级特性。

### 9.5 依赖解析：FetchContent 回退

`find_package` 优先，未命中时 FetchContent 自动拉取。使项目在无 vcpkg 环境下可自举构建，
同时保留 vcpkg/系统包的优先级。

---

## 10. 参考文档

- [Phase 1 详细开发计划](../Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md)
- [技术架构](../Architecture/01_Technical_Architecture.md)
- [测试策略](../Architecture/16_Testing_Strategy.md)
- [任务跟踪](tasks.md)
- [依赖安装指南](../Guides/Dependency_Installation_Guide.md)
- [开发者快速参考](../Guides/Quick_Reference.md)
