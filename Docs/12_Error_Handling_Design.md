# Personal Finance Hub - Error Handling Design

Version: 1.0  
Backend: C++23  
Architecture: Clean Architecture + std::expected

---

## 1. 目标

错误处理必须让业务失败、权限失败和系统故障可区分、可测试、可映射到 HTTP。

目标：

- Domain 不使用异常表达可预期业务失败
- Application 使用 `std::expected` 编排错误
- Infrastructure 捕获异常并转换为强类型错误
- Presentation 统一映射 HTTP 状态码和 JSON 错误响应
- 日志记录足够排查问题，但不向前端泄露敏感信息

---

## 2. Error Taxonomy

统一错误分类：

```cpp
enum class ErrorKind
{
    ValidationError,
    DomainRuleViolation,
    NotFound,
    AuthenticationError,
    AuthorizationError,
    Conflict,
    ExternalServiceError,
    DatabaseError,
    InternalError
};
```

语义：

| ErrorKind | 含义 | 示例 |
| --- | --- | --- |
| ValidationError | 请求格式或字段基础校验失败 | amount 不是字符串、缺少 accountId |
| DomainRuleViolation | 字段格式正确，但违反业务规则 | 转账金额不平衡、账户已归档 |
| NotFound | 资源不存在或不属于当前用户 | accountId 不存在 |
| AuthenticationError | 未登录或 token 失效 | JWT 过期 |
| AuthorizationError | 已登录但无权限 | 访问其他用户账户 |
| Conflict | 资源版本冲突或唯一约束冲突 | 重复分类名、乐观锁版本不一致 |
| ExternalServiceError | 外部服务不可用或返回非法数据 | 汇率 API 超时 |
| DatabaseError | 数据库连接、事务、约束等故障 | deadlock、connection lost |
| InternalError | 未分类的系统内部错误 | 未预期异常 |

---

## 3. HTTP Mapping

Presentation 层统一映射：

| ErrorKind | HTTP Status |
| --- | --- |
| ValidationError | 400 Bad Request |
| AuthenticationError | 401 Unauthorized |
| AuthorizationError | 403 Forbidden |
| NotFound | 404 Not Found |
| Conflict | 409 Conflict |
| DomainRuleViolation | 422 Unprocessable Entity |
| ExternalServiceError | 502 Bad Gateway |
| DatabaseError | 500 Internal Server Error |
| InternalError | 500 Internal Server Error |

错误响应格式：

```json
{
  "error_code": "DOMAIN_RULE_VIOLATION",
  "message": "Transfer amount is not balanced.",
  "trace_id": "req-20260623-0001"
}
```

规则：

1. `error_code` 使用稳定枚举字符串
2. `message` 面向用户或前端开发者，可读但不泄露 SQL、token、密钥
3. `trace_id` 用于关联服务端日志
4. 生产环境不得把异常堆栈返回给前端

---

## 4. Layer Rules

### Domain Layer

Domain 层不用异常表达业务控制流。

```cpp
std::expected<TransferAggregate, DomainError> buildTransfer(...);
```

DomainError 只描述纯业务错误：

- CurrencyMismatch
- InvalidExchangeRate
- TransferImbalance
- ArchivedAccount
- InvalidCategoryBoard
- NegativeAmountNotAllowedForThisOperation

Domain 不记录日志、不访问数据库、不知道 HTTP。

### Application Layer

Application 层负责把 DomainError、RepositoryError、ProviderError 映射为 UseCaseError。

```cpp
std::expected<OutputDTO, UseCaseError> execute(InputDTO dto);
```

Application 可以：

- 校验权限
- 开启事务
- 调用 Repository
- 调用 Domain Service
- 注册 Domain Event
- 写 AuditLog

### Infrastructure Layer

Infrastructure 层可以遇到底层异常，但必须捕获并转换。

```cpp
try {
    auto rows = dbClient_->execSqlSync(sql, args...);
} catch (const drogon::orm::DrogonDbException& e) {
    return std::unexpected(RepositoryError{
        RepositoryStatus::DatabaseError,
        "database operation failed"
    });
}
```

规则：

1. 不允许把 Drogon、PostgreSQL、HTTP Client 异常向 Domain/Application 泄漏
2. 外部服务错误映射为 `ExternalServiceError`
3. 数据库错误映射为 `DatabaseError` 或更具体的 `Conflict`
4. 日志中可记录底层异常细节，但前端响应只返回安全摘要

---

## 5. Repository Error Model

```cpp
enum class RepositoryStatus
{
    NotFound,
    ValidationError,
    Conflict,
    DatabaseError
};

struct RepositoryError
{
    RepositoryStatus status;
    std::string message;
};
```

映射规则：

| RepositoryStatus | UseCaseError / ErrorKind |
| --- | --- |
| NotFound | NotFound |
| ValidationError | ValidationError |
| Conflict | Conflict |
| DatabaseError | DatabaseError |

---

## 6. Audit And Logging

必须写 AuditLog：

- Dangerous Delete
- 账户归档
- 用户偏好修改
- 同步导入
- 汇率刷新
- 关键资源更新失败后的补偿动作

服务日志必须包含：

- trace_id
- user_id
- use_case
- resource_type
- resource_id
- error_kind
- safe_message

日志不得包含：

- password_hash
- JWT
- API key
- 银行授权 token
- 完整请求头

---

## 7. Final Rules

1. 可预期业务失败用 `std::expected`
2. Domain 不抛业务异常
3. Infrastructure 捕获异常并转换为错误值
4. Presentation 唯一负责 HTTP 状态码映射
5. 错误响应格式稳定
6. 关键操作失败和危险操作必须可审计
7. 测试必须覆盖错误映射和事务回滚路径
