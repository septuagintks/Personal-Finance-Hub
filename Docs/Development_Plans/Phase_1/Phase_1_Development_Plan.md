# Personal Finance Hub Phase 1 开发计划

Version: 2.0
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD
Status: Complete

---

## 1. 阶段目标

Phase 1 建立一个可部署、可测试、金融语义明确的 PFH 后端最小闭环。阶段结果包括本地记账、多币种折算、账户与转账、认证、报表、真实 PostgreSQL 持久化、可靠事件投递和后台任务。

### 1.1 范围

- CMake + C++23 工程和测试基础。
- 金融原语与核心领域模型。
- PostgreSQL 16+、Flyway、Repository、Unit of Work 和 RLS。
- 注册、登录、账户、流水、转账、资源与报表 API。
- Transactional Outbox、Scheduler、汇率刷新与认证清理。
- Linux、Docker 和真实数据库交付门禁。

### 1.2 不在范围

- 完整 Vue 3 产品界面。
- 外部账单导入和支付平台接入。
- 转账聚合删除 API。
- 多节点高可用调度与额外消息中间件。
- 完整加密货币实时定价。

---

## 2. 架构边界

```text
Presentation -> Application -> Domain <- Infrastructure
                                  ^
Bootstrap ------------------------+
```

- Domain 只包含纯业务类型和规则。
- Domain Service 不访问 Repository、不打开事务、不发布事件。
- Application 负责权限、事务、Repository 编排和错误映射。
- Infrastructure 实现 PostgreSQL、安全、Provider、Outbox 和调度。
- Presentation 处理 HTTP、DTO、认证上下文和稳定响应。
- Bootstrap 是唯一生产装配入口。

---

## 3. 交付路线

### 3.1 S01-S04 工程基础

| Step | 结果 |
| ---- | ---- |
| S01 | 建立分层目录和应用入口 |
| S02 | 建立 CMake、C++23、依赖与警告门禁 |
| S03 | 建立 GoogleTest、CTest 和质量脚本 |
| S04 | 建立强类型 ID、错误模型、配置 overlay 与日志 |

验收重点：工程可构建、依赖方向清晰、配置安全失败、基础测试可重复。

### 3.2 S05-S08 金融与持久化

| Step | 结果 |
| ---- | ---- |
| S05 | `Decimal`、`Currency`、`Money`、`ExchangeRate` |
| S06 | Account、Transaction、Transfer 和纯领域服务 |
| S07 | PostgreSQL schema、币种/分类种子与 Flyway V1-V6 |
| S08 | Repository、Unit of Work、RLS、缓存和 Outbox 持久化 |

验收重点：不使用二进制浮点承载金融值；Transfer 原子；业务与 Outbox 同事务；租户隔离 fail-closed。

### 3.3 S09-S12 应用与交付

| Step | 结果 |
| ---- | ---- |
| S09 | Application Use Case、报表与事件契约 |
| S10 | REST API、认证、资源管理与 production composition root |
| S11 | Outbox Publisher、Scheduler、汇率 Provider 和认证清理 |
| S12 | Windows/Linux/PostgreSQL/Drogon/Docker 最终门禁 |

验收重点：API 契约稳定；认证材料 hash-only；后台任务不阻塞 Event Loop；真实环境与离线门禁同时通过。

---

## 4. 最终质量门禁

| 门禁 | 结果 |
| ---- | ---- |
| Windows Debug / PostgreSQL OFF | 349/349 PASS |
| Windows Release / PostgreSQL OFF | 349/349 PASS |
| Linux Debug / PostgreSQL ON | 351/351 PASS |
| Linux Release / PostgreSQL ON | 351/351 PASS |
| Linux PostgreSQL OFF | 349/349 PASS |
| PostgreSQL fixture | 12/12 scenarios PASS |
| Flyway V1-V6 | migrate/info/validate/no-op PASS |
| Drogon runtime | Auth、RLS、财务、报表和优雅停止 PASS |
| Provider runtime | 主源、整批备用、双源失败和历史降级 PASS |
| Docker | 冷构建、healthy、non-root、双角色与后台任务 PASS |

门禁还覆盖 OpenAPI、币种目录、enum cast、NUMERIC 边界、连接池复用、乐观锁、Outbox lease、dead letter、TraceId、响应脱敏和秘密扫描。

---

## 5. 最终能力边界

### 5.1 汇率

FreeCurrencyAPI 为主源，exchangerate.fun 为整批备用源。当前实时路径覆盖 20 种法币与 BTC。其余 12 种加密货币没有实时保证；系统只在完整历史快照存在时降级，否则明确返回不可用。

### 5.2 数据操作

- Transaction 采用追加与软删除，不提供普通更新。
- Transfer 不计入收入或支出统计。
- Transfer 聚合删除未开放。
- Account 危险删除要求显式确认并清理完整关联。

### 5.3 部署

- 目标平台为 Linux。
- production ON 必须使用真实 Drogon、PostgreSQL、OpenSSL、Argon2 与 libcurl。
- request role 与 background role 必须独立并满足启动权限检查。
- 运行环境必须提供 IANA tzdb 与 `tzdata`。

---

## 6. 交付入口

- [Phase 1 开发记录](../../Archive/Phase_1_Development_Record.md)
- [S01-S04 交付摘要](../../Archive/Phase_1_S01-S04_Delivery_Summary.md)
- [S05-S08 交付摘要](../../Archive/Phase_1_S05-S08_Delivery_Summary.md)
- [S09-S12 交付摘要](../../Archive/Phase_1_S09-S12_Delivery_Summary.md)
- [总体开发计划](../Overall_Development_Plan.md)
