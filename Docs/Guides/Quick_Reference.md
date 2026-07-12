# Personal Finance Hub - 开发者快速参考

**版本**: 0.2.0-alpha
**更新日期**: 2026-07-13

---

## 快速开始

### 构建项目

```powershell
# 配置
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build . --config Debug

# 运行
./pfh_server.exe
```

### 运行质量检查

```powershell
# 在项目根目录执行
./quality_check.ps1
```

此脚本会自动执行：

- Git 空白符检查
- CMake 配置验证
- 完整构建
- 单元测试与 In-Memory 集成测试
- Markdown 文件检查

---

## 项目结构速览

```
C++/PFH/
├── src/                    # 实现文件
│   ├── domain/             # 纯业务逻辑（无框架依赖）
│   ├── application/        # 用例编排
│   ├── infrastructure/     # 持久化与外部服务
│   ├── presentation/       # REST API 控制器
│   └── bootstrap/          # 应用入口 (main.cpp)
├── include/pfh/            # 公共头文件（对应 src/ 结构）
├── tests/
│   ├── unit/               # 单元测试（无数据库）
│   ├── integration/        # In-Memory 语义测试与 PostgreSQL 集成场景
│   └── api/                # API 测试
├── config/                 # 配置文件
└── migrations/             # 数据库迁移脚本
```

---

## 配置管理

### 创建本地配置

```powershell
Copy-Item config/config.example.json config/config.local.json
```

### 配置文件优先级

1. `config.local.json` - 本地开发（不提交到 Git）
2. `config.test.json` - 测试环境
3. `config.example.json` - 配置模板

⚠️ **绝不提交包含真实密码、JWT 密钥或 API 密钥的文件**

---

## 测试命名规范

遵循模式：`<ClassName>_When<Condition>_<ExpectedBehavior>`

### 示例

```cpp
// 单元测试
Money_WhenAddingSameCurrency_ReturnsCorrectSum
Money_WhenAddingDifferentCurrency_ThrowsError

// 集成测试
AccountRepository_WhenSavingAccount_PersistsCorrectly
TransactionRepository_WhenQueryingByUser_ReturnsOnlyUserTransactions

// API 测试
CreateTransfer_WhenValidInput_Returns201
CreateTransfer_WhenUnauthorized_Returns401
```

详见：[tests/TEST_NAMING_CONVENTION.md](../../tests/TEST_NAMING_CONVENTION.md)

---

## 编译选项

### 当前启用的警告

- `-Wall` - 大部分警告
- `-Wextra` - 额外警告
- `-Werror` - 警告视为错误
- `-pedantic` - 严格 ISO C++ 遵从性
- `-Wconversion` - 隐式类型转换警告
- `-Wsign-conversion` - 符号转换警告

### 构建类型

- **Debug**: 包含调试信息，禁用优化 (`-g -O0`)
- **Release**: 启用优化 (`-O3`)

---

## 依赖项安装

### 使用 vcpkg（推荐）

```powershell
# 安装依赖
vcpkg install drogon
vcpkg install libpq
vcpkg install spdlog
vcpkg install gtest

# 配置 CMake 使用 vcpkg
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake
```

---

## Git 工作流

### 提交前检查

```powershell
# 运行质量检查
./quality_check.ps1

# 检查空白符错误
git diff --check

# 查看变更
git status
git diff
```

### 分支策略

- `main` - 稳定分支，只接收已完成完整验证的 Phase 交付
- `feature/phase1-foundation` - 当前 Phase 1 长期开发分支
- 后续 Phase 分支 - 从 `main` 单独创建，并在计划中记录实际名称

Phase 内的代码、文档、测试和交付总结应先在对应 Phase 分支完成，再统一合并回 `main`。

---

## 当前开发状态

### 已完成

- 项目骨架和目录结构
- CMake 构建系统
- C++23 编译配置
- 测试框架准备
- 质量检查脚本
- 配置管理机制
- P1-S05 金融原语
- P1-S06 领域模型与领域服务
- P1-S07 数据库迁移脚本
- P1-S08 Repository/UoW 接口与 In-Memory 语义实现
- P1-S09 Application Use Case
- Windows GCC 16：240 单元 + 13 In-Memory 集成测试，253/253 通过

### 进行中

- P1-S10: PostgreSQL/Drogon 生产适配器、REST API 与认证基础

### 待办

- P1-S11: Outbox、Scheduler、汇率 HTTP Provider 与后台任务
- P1-S12: Phase 1 全量回归、文档定稿与分支交付
- 另一台机器执行 Linux、Docker、PostgreSQL 16+、Debug/Release 和真实运行时阻断门禁

详见：[Tasks.md](../Development/Tasks.md)

---

## 架构原则

### Clean Architecture 依赖方向

```
Presentation → Application → Domain ← Infrastructure
```

**关键规则**：

- Domain 层不依赖任何框架
- 所有依赖指向内层（Domain）
- Infrastructure 实现 Domain 定义的接口

### 金融正确性原则

- ❌ 禁止使用 `float` 或 `double` 表示金额和汇率
- ✅ 使用定点数 `Decimal` 类型
- ✅ 跨币种操作必须显式提供汇率
- ✅ Transfer 不计入收入/支出统计

---

## 常用文档

- [技术架构](../Architecture/01_Technical_Architecture.md)
- [数据库设计](../Architecture/02_Database_Design.md)
- [领域模型设计](../Architecture/03_Domain_Model_Design.md)
- [测试策略](../Architecture/16_Testing_Strategy.md)
- [Phase 1 开发计划](../Development_Plans/Phase_1_Development_Plan.md)
- [Phase 1 详细计划](../Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md)
- [Linux 开发工作流](Linux_Development_Workflow.md)

---

## 问题排查

### 构建失败

1. 检查编译器版本是否支持 C++23
2. 确认 CMake 版本 >= 3.20
3. 查看构建输出中的具体错误信息

### 测试失败

1. 确认 GoogleTest 已正确安装
2. PostgreSQL 测试时检查测试数据库、迁移和 RLS 用户上下文；In-Memory 回归不需要数据库
3. 查看测试输出定位失败的具体测试

### 配置问题

1. 确认 `config.local.json` 已创建
2. 检查 JSON 格式是否正确
3. 验证数据库连接参数

---

## 联系与支持

- 项目文档：`Docs/`
- 任务跟踪：`Docs/Development/Tasks.md`
- 阶段交付记录：`Docs/Development/Phase_1_S01-S03_Delivery_Summary.md`
