# Personal Finance Hub (PFH)

Version: 0.1.0-alpha
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD

Personal Finance Hub（PFH）是一个面向个人财务管理场景的聚合平台，目标是把账户、流水、转账、预算、报表、汇率和外部账单同步整合到一个高精度、可审计、可扩展的后端系统中。项目当前处于 **Phase 1 收尾阶段**：S12 的 Linux/PostgreSQL/Drogon/Docker 基线已经通过；汇率 Provider 随后改为 FreeCurrencyAPI 主源与 exchangerate.fun 整批备用源，Windows Debug/Release 349/349 和脱敏 HTTPS 契约探测已通过。当前等待 macOS/Colima 在新提交上复测真实主备切换、Linux 生产构建、Scheduler 与 Docker，返回后执行 S12-07 最终 review 和分支签署。详细开发规范见 [Docs/README.md](Docs/README.md)。

## 主要功能

- **多账户管理**：支持储蓄账户、信用账户、现金、投资账户、加密资产账户等账户类型，并允许按用户自定义子类型分组展示。
- **交易与转账记录**：支持收入、支出、调整和跨账户转账；转账场景覆盖跨币种金额推导、手续费、汇兑差异和转账聚合。
- **多币种与汇率管理**：以 USD 作为固定枢纽货币保存汇率快照，通过领域服务在内存中完成直接汇率、反向汇率和三角折算。
- **报表与净值分析**：按用户基准货币聚合账户余额、收支趋势、分类统计和 Dashboard 指标；报表读路径采用轻量级 CQRS 思路优化查询效率。
- **分类、标签与用户偏好**：支持收入/支出分类树、交易标签、默认基准货币、语言、时区、主题和默认报表周期等偏好配置。
- **外部账单同步与对账**：预留银行、支付平台、CSV/JSON 等 Provider 接入能力，通过幂等指纹避免重复导入，并支持同步后余额对账。
- **安全与审计**：REST API 使用 JWT 鉴权；危险操作、同步结果、汇率刷新和关键业务事件会进入审计与事件处理流程。

## 实现方式和架构设计概述

PFH 后端基于 **C++23 + Drogon + PostgreSQL 16+ + CMake** 构建，并采用 **Clean Architecture + 轻量级 DDD** 组织代码。系统以领域模型表达核心财务规则，让外部框架、数据库和第三方服务通过接口适配进入系统边界。

整体分层如下：

```text
Presentation  ->  Application  ->  Domain  <-  Infrastructure
REST API          Use Cases         Entities     PostgreSQL / Providers
JWT Filters       Query Services    Value Obj.   Repositories / UoW
DTO Mapping       Transactions      Services     Scheduler / Outbox
```

- **Domain 层**：承载 `Money`、`Decimal`、`Account`、`Transaction`、`TransferAggregate`、`ExchangeRate` 等核心模型，并集中处理金额精度、币种匹配、转账平衡、历史汇率选择等金融规则。
- **Application 层**：通过 `CreateTransactionUseCase`、`CreateTransferUseCase`、`RefreshExchangeRatesUseCase`、`RunSyncJobUseCase` 等用例编排仓储、事务和领域服务。
- **Infrastructure 层**：负责 PostgreSQL 持久化、Repository 实现、Unit of Work、Flyway 迁移、外部汇率 Provider、同步 Provider、后台调度任务和 Outbox 事件发布。
- **Presentation 层**：通过 Drogon 暴露 REST API，处理 JWT 鉴权、请求校验、DTO 映射、错误码转换和全局异常边界。

金额计算坚持不使用浮点数：金额使用 `NUMERIC(20,8)` 与强类型 `Money`，汇率统一使用 `NUMERIC(20,10)` 快照并保留历史记录。写路径通过领域实体、Repository 和 Unit of Work 保证一致性；Phase 1 报表通过 request-scoped Repository 读取并在 Application 层按用户基准货币完成折算，SQL 聚合端口作为后续性能优化。领域事件采用事务提交后的处理边界，并可结合 Outbox 方案保证副作用可重试、可审计。

## 快速开始

### 环境要求

- **C++23 兼容编译器**（Phase 1 依赖 `__int128` 扩展实现高精度 Decimal，当前仅支持 GCC/Clang）:
  - GCC 14+ 与配套新版 libstdc++（推荐，最终以 CMake tzdb 能力探测为准）
  - Clang 18+ 与能够通过同一 tzdb 探测的标准库
  - MSVC 支持计划在后续阶段（需切换至 Boost.Multiprecision 或自实现 128 位定点数）
- **CMake** 3.20 或更高版本
- **PostgreSQL** 16 或更高版本
- **tzdata**（Linux 编译、测试和运行环境必需）
- **vcpkg**（推荐用于依赖管理）

### 构建步骤

1. **配置项目**:

   ```powershell
   mkdir build
   cd build
   cmake .. -DCMAKE_BUILD_TYPE=Debug
   ```

2. **编译**:

   ```powershell
   cmake --build . --config Debug
   ```

3. **运行** (实现后):
   ```powershell
   ./pfh_server
   ```

### 配置文件

1. 复制示例配置:

   ```powershell
   Copy-Item config/config.example.json config/config.local.json
   ```

2. 编辑 `config/config.local.json` 设置数据库凭据、JWT 密钥等

详见 [config/README.md](config/README.md)

### 质量检查

运行综合质量检查脚本:

```powershell
./quality_check.ps1
```

此脚本会执行：

- Git 空白符检查
- CMake 配置
- 完整构建
- 单元测试（实现后）
- Markdown 验证

## 项目结构

```
C++/PFH/
├── CMakeLists.txt           # 根构建配置
├── include/pfh/             # 公共头文件
│   ├── domain/              # 领域实体、值对象、服务
│   ├── application/         # 用例和 DTO
│   ├── infrastructure/      # Repository 实现
│   └── presentation/        # 控制器和中间件
├── src/                     # 实现文件
│   ├── domain/
│   ├── application/
│   ├── infrastructure/
│   ├── presentation/
│   └── bootstrap/           # 应用入口 (main.cpp)
├── tests/                   # 测试套件
│   ├── unit/                # 单元测试（无数据库/网络）
│   ├── integration/         # 集成测试（含数据库）
│   ├── api/                 # API 测试
│   └── support/             # 测试工具和夹具
├── config/                  # 配置文件
├── migrations/              # 数据库迁移脚本
├── cmake/                   # CMake 模块
└── Docs/                    # 架构和设计文档
```

## 开发阶段

### Phase 1: 核心后端（进行中）

- [x] 项目骨架、构建系统、金融原语和领域模型
- [x] Repository/UoW 接口、In-Memory 语义实现与 PostgreSQL adapter
- [x] REST API、认证、基础资源、流水、转账与报表的本地实现
- [x] OpenAPI、framework-neutral API 回归与离线生产源码门禁
- [x] P1-S11 Outbox、Scheduler、汇率 Provider 与后台任务
- [ ] P1-S12 新 Provider 外部复测、最终 review 与 Phase 分支交付

### Phase 2: 增强功能（计划中）

- 高级报表和分析
- 外部平台同步框架
- 性能优化
- 增强安全性

### Phase 3: 生产就绪（计划中）

- 全面监控
- 生产部署
- 文档完善

详见 [Docs/Development_Plans/Phase_1_Development_Plan.md](Docs/Development_Plans/Phase_1_Development_Plan.md)

## 测试策略

测试遵循命名约定:

```
<ClassName>_When<Condition>_<ExpectedBehavior>
```

示例:

```cpp
Money_WhenAddingSameCurrency_ReturnsCorrectSum
TransferAggregate_WhenOutgoingAndRateProvided_CalculatesIncoming
```

详见 [tests/TEST_NAMING_CONVENTION.md](tests/TEST_NAMING_CONVENTION.md)

**覆盖率目标:**

- Domain Layer: ≥ 90%
- Application Layer: ≥ 80%
- Infrastructure Layer: ≥ 60%
- Presentation Layer: ≥ 60%

## 文档入口

- [开发者文档中心](Docs/README.md)
- [技术架构总览](Docs/Architecture/01_Technical_Architecture.md)
- [数据库设计](Docs/Architecture/02_Database_Design.md)
- [领域模型设计](Docs/Architecture/03_Domain_Model_Design.md)
- [REST API 设计](Docs/Architecture/10_REST_API_Design.md)
- [OpenAPI 3.1 契约](Docs/Architecture/10_REST_API_OpenAPI.json)
- [测试策略](Docs/Architecture/16_Testing_Strategy.md)
