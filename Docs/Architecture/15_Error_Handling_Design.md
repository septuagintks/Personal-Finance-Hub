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

| ErrorKind            | 含义                         | 示例                              |
| -------------------- | ---------------------------- | --------------------------------- |
| ValidationError      | 请求格式或字段基础校验失败   | amount 不是字符串、缺少 accountId |
| DomainRuleViolation  | 字段格式正确，但违反业务规则 | 转账金额不平衡、账户已归档        |
| NotFound             | 资源不存在或不属于当前用户   | accountId 不存在                  |
| AuthenticationError  | 未登录或 token 失效          | JWT 过期                          |
| AuthorizationError   | 已登录但无权限               | 访问其他用户账户                  |
| Conflict             | 资源版本冲突或唯一约束冲突   | 重复分类名、乐观锁版本不一致      |
| ExternalServiceError | 外部服务不可用或返回非法数据 | 汇率 API 超时                     |
| DatabaseError        | 数据库连接、事务、约束等故障 | deadlock、connection lost         |
| InternalError        | 未分类的系统内部错误         | 未预期异常                        |

---

## 3. HTTP Mapping

Presentation 层统一映射：

| ErrorKind            | HTTP Status               |
| -------------------- | ------------------------- |
| ValidationError      | 400 Bad Request           |
| AuthenticationError  | 401 Unauthorized          |
| AuthorizationError   | 403 Forbidden             |
| NotFound             | 404 Not Found             |
| Conflict             | 409 Conflict              |
| DomainRuleViolation  | 422 Unprocessable Entity  |
| ExternalServiceError | 502 Bad Gateway           |
| DatabaseError        | 500 Internal Server Error |
| InternalError        | 500 Internal Server Error |

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
| ---------------- | ------------------------ |
| NotFound         | NotFound                 |
| ValidationError  | ValidationError          |
| Conflict         | Conflict                 |
| DatabaseError    | DatabaseError            |

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

## 7. 全局异常拦截器 (Global Exception Handler)

在 C++23 + Drogon 环境中，除了预期的 `std::expected` 错误流程外，第三方库或基础设施（如 JsonCpp 解析错误、PostgreSQL 驱动崩溃、内存分配失败等）仍可能抛出非预期异常。

### 7.1 设计目标

1. **统一兜底**：捕获所有未被 Use Case 或 Controller 显式处理的异常
2. **安全响应**：避免泄露 SQL 片段、内存地址、堆栈跟踪等敏感信息
3. **可追溯**：生成唯一 `trace_id`，关联服务端日志便于排查
4. **标准格式**：统一返回 500 状态码和结构化 JSON

### 7.2 Drogon 全局异常处理器配置

Drogon 支持通过 `setExceptionHandler` 注册全局异常处理器：

```cpp
// main.cpp 或 Application 初始化代码
#include <drogon/drogon.h>
#include <uuid/uuid.h> // 或使用 Boost.UUID

void setupGlobalExceptionHandler() {
    drogon::app().setExceptionHandler([](const std::exception& e,
                                          const drogon::HttpRequestPtr& req,
                                          std::function<void(drogon::HttpResponsePtr)>&& callback) {
        // 1. 生成唯一追踪 ID
        std::string traceId = generateTraceId();

        // 2. 记录服务端日志（包含完整异常信息）
        LOG_ERROR << "[trace_id=" << traceId << "] "
                  << "Unhandled exception: " << e.what()
                  << " | path=" << req->path()
                  << " | method=" << req->methodString();

        // 可选：记录堆栈跟踪（生产环境推荐）
        // LOG_ERROR << "Stack trace: " << getStackTrace();

        // 3. 构造安全的前端响应（不泄露敏感信息）
        Json::Value errorBody;
        errorBody["error_code"] = "INTERNAL_SERVER_ERROR";
        errorBody["message"] = "An unexpected error occurred. Please contact support if the problem persists.";
        errorBody["trace_id"] = traceId;
        errorBody["path"] = req->path();
        errorBody["timestamp"] = getCurrentTimestampISO8601();

        auto response = drogon::HttpResponse::newHttpJsonResponse(errorBody);
        response->setStatusCode(drogon::k500InternalServerError);
        response->setContentTypeCode(drogon::CT_APPLICATION_JSON);

        callback(response);
    });
}

// 应用启动时调用
int main() {
    setupGlobalExceptionHandler();

    drogon::app().addListener("0.0.0.0", 8080);
    drogon::app().run();
    return 0;
}
```

### 7.3 TraceId 生成策略

TraceId 用于关联前端错误和服务端日志，推荐格式：

```cpp
std::string generateTraceId() {
    // 方案 1: UUID v4
    uuid_t uuid;
    uuid_generate_random(uuid);
    char uuid_str[37];
    uuid_unparse(uuid, uuid_str);
    return std::string(uuid_str);

    // 方案 2: 时间戳 + 随机数（更轻量）
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    return "trace-" + std::to_string(timestamp) + "-" + std::to_string(dis(gen));
}
```

### 7.4 分层异常处理策略

**Infrastructure 层**

捕获所有底层异常，转换为 `std::expected` 错误：

```cpp
// Repository 实现中
std::expected<Account, RepositoryError> AccountRepositoryImpl::findById(int64_t id) {
    try {
        auto result = dbClient_->execSqlSync(
            "SELECT * FROM accounts WHERE id = $1", id
        );

        if (result.empty()) {
            return std::unexpected(RepositoryError{
                RepositoryStatus::NotFound,
                "Account not found"
            });
        }

        return mapToAccount(result[0]);

    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "Database exception in findById: " << e.base().what();
        return std::unexpected(RepositoryError{
            RepositoryStatus::DatabaseError,
            "Database query failed"
        });
    } catch (const std::exception& e) {
        LOG_ERROR << "Unexpected exception in findById: " << e.what();
        return std::unexpected(RepositoryError{
            RepositoryStatus::DatabaseError,
            "Unexpected database error"
        });
    }
}
```

**Application 层**

Use Case 不应抛出异常，所有错误通过 `std::expected` 返回：

```cpp
std::expected<void, UseCaseError> CreateTransferUseCase::execute(const TransferInputDTO& dto) {
    // 所有错误都通过 std::unexpected 返回
    auto sourceAccount = accountRepo_->findById(dto.sourceAccountId);
    if (!sourceAccount) {
        return std::unexpected(UseCaseError::AccountNotFound);
    }

    // ... 业务逻辑
}
```

**Presentation 层**

Controller 负责最后一道防线，处理预期的 `std::expected` 错误：

```cpp
void TransferController::createTransfer(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    try {
        // 1. 解析 JSON
        auto jsonPtr = req->getJsonObject();
        if (!jsonPtr) {
            callback(createErrorResponse(400, "BAD_REQUEST", "Invalid JSON body"));
            return;
        }

        // 2. 调用 Use Case
        auto result = createTransferUseCase_->execute(dto);

        // 3. 处理预期错误
        if (!result) {
            callback(mapUseCaseErrorToHttp(result.error()));
            return;
        }

        // 4. 成功响应
        callback(drogon::HttpResponse::newHttpJsonResponse(successBody));

    } catch (const std::exception& e) {
        // 这里不应该到达，但作为安全边界
        // 全局异常处理器会兜底
        throw;
    }
}
```

### 7.5 生产环境安全规约

1. **绝不返回给前端**：
   - 异常堆栈跟踪
   - SQL 查询语句
   - 文件路径（如 `/home/user/pfh/src/...`）
   - 内存地址
   - 数据库连接字符串
   - JWT 密钥或 token 内容

2. **必须记录到服务端日志**：
   - 完整异常信息 `e.what()`
   - 请求路径和方法
   - TraceId
   - 用户 ID（如果已认证）
   - 可选：堆栈跟踪

3. **前端响应只包含**：
   - 固定的通用错误消息
   - TraceId（用户可提供给客服）
   - 时间戳
   - 请求路径（不泄露参数）

4. **监控告警**：
   - 全局异常处理器触发时，应发送监控告警
   - 统计异常频率，超过阈值自动告警

---

## 8. Final Rules

1. 可预期业务失败用 `std::expected`
2. Domain 不抛业务异常
3. Infrastructure 捕获异常并转换为错误值
4. Presentation 唯一负责 HTTP 状态码映射
5. 错误响应格式稳定
6. 关键操作失败和危险操作必须可审计
7. 测试必须覆盖错误映射和事务回滚路径
8. 全局异常处理器必须配置，作为最后安全边界
9. 生产环境绝不向前端泄露异常堆栈、SQL 或敏感配置
10. 所有未预期异常必须生成 TraceId 并记录到审计日志
