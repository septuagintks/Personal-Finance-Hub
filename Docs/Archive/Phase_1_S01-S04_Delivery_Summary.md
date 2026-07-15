# Phase 1 S01-S04 工程基础交付摘要

Version: 1.0
Status: Complete

---

## 1. 交付范围

P1-S01 至 P1-S04 建立 PFH 后端的工程基础、分层边界、测试入口、配置和日志能力，为金融领域与生产适配器提供可持续扩展的骨架。

| Step | 交付内容 |
| ---- | -------- |
| S01 | 工程目录和 Clean Architecture 物理边界 |
| S02 | CMake、C++23、依赖解析和编译策略 |
| S03 | GoogleTest、CTest、质量脚本和测试组织 |
| S04 | 强类型 ID、错误模型、配置 overlay 和日志 |

---

## 2. 工程结构

### 2.1 分层目录

```text
src/
  domain/
  application/
  infrastructure/
  presentation/
  bootstrap/
include/pfh/
tests/
  unit/
  integration/
  api/
config/
migrations/
```

依赖方向固定为：

```text
Presentation -> Application -> Domain <- Infrastructure
```

Domain 不包含框架、数据库、JSON 或系统 I/O 依赖。Composition root 位于 Bootstrap，只在进程启动时完成生产依赖装配。

### 2.2 构建目标

- `pfh_domain`：金融原语、实体、聚合和纯领域服务。
- `pfh_application`：用例、查询服务、事务编排和事件处理。
- `pfh_infrastructure`：配置、日志、调度和外部 Provider。
- `pfh_presentation`：HTTP parser、mapper、Controller 和认证过滤器。
- `pfh_server`：应用入口。
- compile gates：在 PostgreSQL OFF 模式下验证生产适配器的接口形状。

CMake 要求 C++23，并对警告、符号转换和不支持的核心能力执行 fail-fast。`std::chrono` IANA tzdb 能力通过配置期探针确认。

---

## 3. 基础类型与错误模型

### 3.1 强类型 ID

User、Account、Transaction、TransferGroup、Category、Tag、ExchangeRate、Outbox 等标识使用不同的强类型，避免跨实体误传。默认或非正 ID 被视为无效，边界层在进入业务用例前完成解析与校验。

### 3.2 错误分层

- Domain Error：只表达金融和领域规则，不包含 HTTP 或 SQL。
- Repository Error：表达数据访问的 NotFound、Conflict、Validation 和 Database Error。
- Application Error：统一权限、验证、冲突、领域违规和基础设施故障。
- Presentation：将 Application Error 稳定映射为 HTTP 状态码、`error_code`、`message` 与 `trace_id`。

所有预期失败通过 `std::expected` 风格结果返回；异常只用于不可恢复或非预期路径，并由全局边界脱敏。

---

## 4. 配置与日志

### 4.1 配置来源

配置读取优先级为：

1. 环境变量。
2. JSON 配置文件。
3. 内置默认值。

首选环境变量使用 `PFH_*` 命名，覆盖运行环境、JWT、password pepper、request/background 数据库连接和 FreeCurrencyAPI key。完整映射以 [`config/README.md`](../../config/README.md) 为准。

### 4.2 安全约束

- 本地配置保存在 `config/config.local.json`，该文件被 Git 忽略。
- placeholder secret、弱 JWT secret、非法端口和生产角色冲突会导致配置加载失败。
- Password pepper 可为空；非空时必须是实际 secret。
- API key、数据库密码、JWT secret 和 Token 不进入日志、文档或 Git。
- 环境变量中的非法值不会静默回退到 JSON。

### 4.3 日志上下文

日志基础设施支持 level、输出目标和 TraceId。业务日志使用稳定上下文字段关联用户、请求和后台任务，同时禁止记录认证材料、SQL 明文参数和 Provider 响应正文。

---

## 5. 测试与质量入口

### 5.1 测试组织

- Unit：Domain、Application、配置、调度和 Provider 的快速测试。
- Integration：In-Memory 语义场景与 production ON 下的 PostgreSQL fixture。
- API：framework-neutral HTTP 契约与真实 Drogon runtime。
- Static gates：迁移、OpenAPI、币种目录和 PostgreSQL 适配器契约。

测试命名强调“条件与预期行为”，覆盖正常路径、边界、权限、失败和回滚。

### 5.2 本地质量命令

```powershell
./quality_check.ps1
```

该命令执行空白符检查、CMake configure、构建、CTest 和 Markdown 检查。最终 Windows Debug / PostgreSQL OFF 基线为 349/349。

---

## 6. 验收结论

- 工程可在 Windows GCC 16 与 Linux GCC 13 配置、编译和测试。
- 各层 CMake 目标和物理目录与 Clean Architecture 一致。
- 强类型 ID 与错误模型进入后续全部 Use Case。
- 配置 overlay、placeholder 拒绝、端口校验和 secret 优先级有自动化测试。
- 日志、TraceId 和全局错误边界满足生产脱敏要求。
- 本阶段没有遗留的实现任务。

后续金融与持久化结果见 [S05-S08 交付摘要](Phase_1_S05-S08_Delivery_Summary.md)。
