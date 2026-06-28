# Personal Finance Hub - REST API Design

Version: 1.0

Backend: C++23

Framework: Drogon (HttpController)

Architecture: Clean Architecture (Presentation Layer)

---

## 1. 接口设计原则与规范

作为 Clean Architecture 的最外层——**表现层（Presentation Layer）**，REST API 模块仅负责处理 HTTP 协议细节（路由、状态码、JSON 序列化），并迅速将控制权移交给 Application 层的 Use Cases 或 Query Services。

### 1.1 核心设计规范

1. **版本化路由**：所有面向前端的 API 统一以 `/api/v1` 开头。
2. **无状态设计**：凭证通过 HTTP Header 的 `Authorization: Bearer <JWT>` 携带，由 Drogon Filter（过滤器）统一拦截校验，并将 `UserId` 注入到请求上下文中。
3. **统一 JSON 响应结构**：

- 成功响应：直接返回数据对象（对象或数组）。
- 失败响应：统一采用标准错误格式：`{"error_code": "STRING", "message": "Readable description"}`。

### 1.2 C++23 std::expected 与 HTTP 状态码映射矩阵

表现层控制器必须将应用层返回的 `std::expected` 错误单向转换为精准的 HTTP 状态码：

| 应用层错误 (UseCaseError / RepositoryStatus) | HTTP 状态码                 | 语义与前端应对                           |
| -------------------------------------------- | --------------------------- | ---------------------------------------- |
| **成功 (Success)**                           | `200 OK` / `201 Created`    | 操作成功，前端直接消费数据               |
| **格式校验失败 (Validation Error)**          | `400 Bad Request`           | 请求 JSON 字段缺失、类型错误或数值不合规 |
| **未认证 / 令牌失效**                        | `401 Unauthorized`          | 踢回登录页                               |
| **已认证但无权限 (AuthorizationError)**      | `403 Forbidden`             | 禁止访问其他用户资源                     |
| **找不到资源 (NotFound)**                    | `404 Not Found`             | 账户、流水或分类 ID 不存在               |
| **冲突 (Conflict)**                          | `409 Conflict`              | 重复分类名、版本冲突、重复提交           |
| **违反金融业务规则 (DomainRuleViolation)**   | `422 Unprocessable Entity`  | 账户已归档、跨币种金额不平衡、汇率非法   |
| **外部服务失败 (ExternalServiceError)**      | `502 Bad Gateway`           | 汇率 API 或同步 Provider 不可用          |
| **系统级故障 (InfrastructureFailure)**       | `500 Internal Server Error` | 数据库死锁、连接超时，前端提示“系统繁忙” |

---

## 2. 账户管理接口 (Account Management APIs)

### 2.1 获取当前用户所有未归档账户

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/accounts`
- **响应负载 (200 OK)**：

```json
[
  {
    "id": 1,
    "name": "招商银行借记卡",
    "type": "savings",
    "subtype": "银行卡",
    "category": "asset",
    "currencyCode": "CNY",
    "isArchived": false
  },
  {
    "id": 2,
    "name": "Chase Checking",
    "type": "savings",
    "subtype": "Checking",
    "category": "asset",
    "currencyCode": "USD",
    "isArchived": false
  }
]
```

### 2.2 获取账户实时余额快照

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/accounts/{id}/balance`
- **响应负载 (200 OK)**：

```json
{
  "accountId": 1,
  "balance": "15430.50",
  "lastTransactionId": 4521,
  "updatedAt": "2026-03-23 12:00:00"
}
```

### 2.3 危险删除账户（彻底物理清除）

- **HTTP 方法**：`DELETE`
- **路径**：`/api/v1/accounts/{id}`
- **请求参数 (Query)**：`?confirmations=3`（安全防御二次确认数）
- **响应负载**：
- 成功：`204 No Content`
- 确认数不足：`400 Bad Request` `{"error_code":"BAD_REQUEST","message":"Dangerous action requires 3 confirmations"}`

---

## 3. 账务流水与转账接口 (Transaction & Transfer APIs)

### 3.1 创建单笔收支流水 (Income/Expense)

- **HTTP 方法**：`POST`
- **路径**：`/api/v1/transactions`
- **请求负载**：

```json
{
  "accountId": 1,
  "type": "expense",
  "amount": "45.00",
  "currencyCode": "CNY",
  "categoryCode": "FOOD",
  "description": "午餐打卡"
}
```

- **响应负载 (201 Created)**：`{"status": "success"}`

### 3.2 创建跨账户/跨币种转账 (Transfer)

- **HTTP 方法**：`POST`
- **路径** : `/api/v1/transfers`
- **请求负载 (对应 06 文档的 TransferInputDTO)**：

```json
{
  "sourceAccountId": 2,
  "targetAccountId": 1,
  "mode": "OutgoingAndIncoming",
  "sourceAmount": { "amount": "100.00", "currency": "USD" },
  "targetAmount": { "amount": "718.00", "currency": "CNY" },
  "exchangeRate": null,
  "feeAmount": { "amount": "2.00", "currency": "USD" },
  "description": "资金回国"
}
```

- **响应负载 (201 Created)**：`{"status": "success"}`

---

## 4. 报表与分析接口 (Reporting & Analytics APIs)

基于轻量级 CQRS 架构，报表接口直接透传 QueryService 聚合出的高维 DTO。

### 4.0 获取首页聚合摘要 (Dashboard Summary)

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/reports/dashboard-summary`
- **请求参数 (Query)**：`?startDate=2026-06-01&endDate=2026-07-01`
- **响应负载 (200 OK)**：

```json
{
  "baseCurrency": "CNY",
  "netWorth": {
    "baseCurrency": "CNY",
    "totalAssets": "125000.00",
    "totalLiabilities": "-4500.00",
    "netWorth": "120500.00",
    "generatedAt": "2026-06-23 13:45:12"
  },
  "monthlyIncome": "18000.00",
  "monthlyExpense": "3900.00",
  "assetDistribution": [
    { "label": "Savings", "amount": "82000.00", "percentage": "68.0%" },
    { "label": "Investment", "amount": "43000.00", "percentage": "35.7%" },
    { "label": "Credit", "amount": "-4500.00", "percentage": "-3.7%" }
  ],
  "topExpenseCategories": [
    {
      "categoryId": "12",
      "categoryName": "餐饮",
      "amount": "1200.00",
      "percentage": "30.8%"
    },
    {
      "categoryId": "18",
      "categoryName": "交通",
      "amount": "650.00",
      "percentage": "16.7%"
    }
  ],
  "reportPeriodStart": "2026-06-01",
  "reportPeriodEnd": "2026-07-01",
  "generatedAt": "2026-06-23 13:45:12"
}
```

### 4.1 获取个人净资产综合看板 (Net Worth Summary)

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/reports/net-worth`
- **响应负载 (200 OK)**：

```json
{
  "baseCurrency": "CNY",
  "totalAssets": "125000.00",
  "totalLiabilities": "-4500.00",
  "netWorth": "120500.00",
  "generatedAt": "2026-03-23 13:45:12"
}
```

### 4.2 获取特定时间段的收支趋势 (Cash Flow)

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/reports/cash-flow`
- **请求参数 (Query)**：`?startDate=2026-01&endDate=2026-03&periodType=MONTH`
- **响应负载 (200 OK)**：

```json
{
  "baseCurrency": "CNY",
  "trends": [
    { "period": "2026-01", "income": "15000.00", "expense": "4200.00" },
    { "period": "2026-02", "income": "15000.00", "expense": "6100.00" },
    { "period": "2026-03", "income": "18000.00", "expense": "3900.00" }
  ]
}
```

---

## 4.5 分类、标签、用户偏好与货币接口

### 4.5.1 获取分类树

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/categories`
- **请求参数**：`?board=expense` 或 `?board=income`
- **响应负载 (200 OK)**：

```json
[
  {
    "id": 12,
    "name": "餐饮",
    "board": "expense",
    "source": "system",
    "parentId": null,
    "children": [
      {
        "id": 13,
        "name": "早餐",
        "board": "expense",
        "source": "system",
        "parentId": 12,
        "children": []
      }
    ]
  }
]
```

### 4.5.2 新增或启用分类

- **HTTP 方法**：`POST`
- **路径**：`/api/v1/categories`

```json
{
  "board": "expense",
  "name": "咖啡",
  "parentId": 12,
  "templateId": null
}
```

如果 `templateId` 不为空，表示从 `system_category_templates` 启用预设分类。

### 4.5.3 删除分类

- **HTTP 方法**：`DELETE`
- **路径**：`/api/v1/categories/{id}`
- **响应负载**：`204 No Content`

删除分类使用软删除，历史流水保持引用。

### 4.5.4 标签接口

```text
GET    /api/v1/tags
POST   /api/v1/tags
DELETE /api/v1/tags/{id}
PUT    /api/v1/transactions/{id}/tags
```

`PUT /api/v1/transactions/{id}/tags` 请求：

```json
{
  "tagIds": [1, 2, 3]
}
```

Tag 不参与余额计算，只用于过滤、搜索和报表维度扩展。

### 4.5.5 用户偏好接口

```text
GET /api/v1/users/me/preferences
PUT /api/v1/users/me/preferences
```

响应/请求 DTO：

```json
{
  "baseCurrency": "CNY",
  "locale": "zh-CN",
  "timezone": "Asia/Shanghai",
  "dateFormat": "YYYY-MM-DD",
  "numberFormat": "1,234.56",
  "theme": "system",
  "defaultHomePage": "dashboard",
  "defaultReportPeriod": "current_month"
}
```

### 4.5.6 货币元数据接口

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/currencies`

```json
[
  {
    "code": "CNY",
    "symbol": "¥",
    "precision": 2,
    "displayName": "Chinese Yuan",
    "isCrypto": false
  },
  {
    "code": "JPY",
    "symbol": "¥",
    "precision": 0,
    "displayName": "Japanese Yen",
    "isCrypto": false
  },
  {
    "code": "BTC",
    "symbol": "₿",
    "precision": 8,
    "displayName": "Bitcoin",
    "isCrypto": true
  }
]
```

---

## 4.6 OpenAPI / JSON Schema Contract

所有 API 契约必须可生成 OpenAPI 文档。
以下规则是 JSON Schema 的全局约束：

1. 金额字段必须是 `type: string`，并使用十进制字符串格式
2. 汇率字段必须是 `type: string`
3. ID 字段对外使用 integer 或 string，但 UUID 必须使用 string
4. 错误响应统一使用 `ErrorResponse`
5. DTO 字段名使用 lowerCamelCase

核心 Schema：

```yaml
MoneyDTO:
  type: object
  required: [amount, currency]
  properties:
    amount:
      type: string
      pattern: "^-?[0-9]+(\\.[0-9]+)?$"
    currency:
      type: string
      minLength: 3
      maxLength: 8

ErrorResponse:
  type: object
  required: [error_code, message]
  properties:
    error_code:
      type: string
    message:
      type: string
    trace_id:
      type: string

UserPreferenceDTO:
  type: object
  required:
    - baseCurrency
    - locale
    - timezone
    - dateFormat
    - numberFormat
    - theme
    - defaultHomePage
    - defaultReportPeriod

DashboardSummaryDTO:
  type: object
  required:
    - baseCurrency
    - netWorth
    - monthlyIncome
    - monthlyExpense
    - assetDistribution
    - topExpenseCategories
    - reportPeriodStart
    - reportPeriodEnd
    - generatedAt
```

OpenAPI 文档必须作为前后端共同契约；新增接口时，先补 Schema，再实现 Controller 和前端调用。

---

## 5. Drogon HttpController 落地代码实现

在 Drogon 中，通过继承 `drogon::HttpController`，结合宏路由和 `std::expected` 组合优雅的控制流。

### 5.1 转账控制器接口与路由声明

```cpp
// presentation/controllers/TransferController.hpp
#pragma once
#include <drogon/HttpController.h>
#include "application/use_cases/CreateTransferUseCase.hpp"

class TransferController : public drogon::HttpController<TransferController> {
private:
    std::shared_ptr<CreateTransferUseCase> createTransferUseCase_;

public:
    METHOD_LIST_BEGIN
        // 绑定路由：POST /api/v1/transfers，挂载 JWTFilter 进行鉴权
        ADD_METHOD_TO(TransferController::createTransfer, "/api/v1/transfers", drogon::Post, "JwtFilter");
    METHOD_LIST_END

    TransferController(std::shared_ptr<CreateTransferUseCase> useCase)
        : createTransferUseCase_(useCase) {}

    // 处理创建转账的异步请求
    void createTransfer(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

```

### 5.2 控制器核心逻辑落地 (C++23 风格)

```cpp
// presentation/controllers/TransferController.cpp
#include "presentation/controllers/TransferController.hpp"
#include <json/json.h>

void TransferController::createTransfer(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    auto jsonPtr = req->getJsonObject();
    if (!jsonPtr) {
        Json::Value err;
        err["error_code"] = "BAD_REQUEST";
        err["message"] = "Invalid JSON body";
        callback(drogon::HttpResponse::newJsonObjectResponse(err));
        return;
    }

    // 1. 从 JWT 拦截器注入的请求上下文中安全获取 UserId
    // (假设在 JwtFilter 中通过 req->attributes()->insert("user_id", ...) 注入)
    // int64_t currentUserId = req->attributes()->get<int64_t>("user_id");

    // 2. 将 Json 转换为应用层要求的 TransferInputDTO (防御性解析)
    TransferInputDTO dto;
    try {
        const auto& json = *jsonPtr;
        dto.sourceAccountId = json["sourceAccountId"].asInt64();
        dto.targetAccountId = json["targetAccountId"].asInt64();

        std::string modeStr = json["mode"].asString();
        if (modeStr == "OutgoingAndIncoming") dto.mode = TransferMode::OutgoingAndIncoming;
        // ... 其他模式映射

        // 解析嵌套对象 Money
        if (json.isMember("sourceAmount") && !json["sourceAmount"].isNull()) {
            dto.sourceAmount = Money(
                Decimal(json["sourceAmount"]["amount"].asString()),
                Currency(json["sourceAmount"]["currency"].asString())
            );
        }
        // ... 同理解析 targetAmount, feeAmount, exchangeRate
        dto.description = json["description"].asString();

    } catch (const std::exception& e) {
        Json::Value err;
        err["error_code"] = "BAD_REQUEST";
        err["message"] = std::string("Field parsing failed: ") + e.what();
        auto resp = drogon::HttpResponse::newJsonObjectResponse(err);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // 3. 调用应用层 Use Case 执行命令并捕捉 C++23 std::expected
    auto result = createTransferUseCase_->execute(dto);

    // 4. 强类型错误向 HTTP 状态码的单向映射
    if (!result) {
        Json::Value errBody;
        auto response = drogon::HttpResponse::newJsonObjectResponse(errBody);

        switch (result.error()) {
            case UseCaseError::AccountNotFound:
                errBody["error_code"] = "NOT_FOUND";
                errBody["message"] = "Source or target account not found.";
                response->setStatusCode(drogon::k404NotFound);
                break;

            case UseCaseError::DomainRuleViolation:
                errBody["error_code"] = "DOMAIN_RULE_VIOLATION";
                errBody["message"] = "Financial rule restriction. Check your amount, currency balance or exchange rate.";
                response->setStatusCode(drogon::k422UnprocessableEntity); // 422 表达语义合规但逻辑冲突
                break;

            case UseCaseError::InfrastructureFailure:
                errBody["error_code"] = "INTERNAL_SERVER_ERROR";
                errBody["message"] = "Database atomic transaction failed. Please retry later.";
                response->setStatusCode(drogon::k500InternalServerError);
                break;
        }

        response->setJsonObject(errBody);
        callback(response);
    } else {
        // 5. 成功流响应 (201 Created)
        Json::Value successBody;
        successBody["status"] = "success";
        auto response = drogon::HttpResponse::newJsonObjectResponse(successBody);
        response->setStatusCode(drogon::k201Created);
        callback(response);
    }
}

```

---

## 6. 身份认证与安全规约 (Authentication & Security)

### 6.1 JWT Access Token 规范

**有效期**

- 推荐：15 分钟
- 短期有效期降低 token 泄漏风险，配合 Refresh Token 机制保证用户体验。

**Payload 结构**

JWT Payload 必须控制精简，**严禁**放入邮箱、手机号、密码哈希等敏感信息。

推荐结构：

```json
{
  "iss": "pfh-api",
  "aud": "pfh-client",
  "sub": "userId",
  "sid": "sessionId",
  "jti": "unique-token-id",
  "roles": ["USER"],
  "iat": 1710000000,
  "nbf": 1710000000,
  "exp": 1710000900
}
```

字段语义：

- `iss` (Issuer): 签发方，固定为 `pfh-api`
- `aud` (Audience): 受众方，固定为 `pfh-client`
- `sub` (Subject): 用户 ID
- `sid` (Session ID): 登录会话标识，用于会话撤销
- `jti` (JWT ID): 单个 token 的唯一 ID，用于黑名单撤销
- `roles`: 用户角色数组，预留 RBAC 扩展
- `iat` (Issued At): 签发时间戳
- `nbf` (Not Before): 生效时间戳
- `exp` (Expiration): 过期时间戳

**签名算法**

- 如果使用对称加密（HMAC），必须采用 HS256 或更高强度算法。
- 密钥必须足够长且随机（至少 256 bit）。
- 更正式的方案推荐使用非对称算法（RS256 / EdDSA），服务端私钥签发，网关或资源服务用公钥验证。

---

### 6.2 Refresh Token 机制

**PFH 必须采用 Refresh Token 机制，不得使用单 Token 方案。**

单 Token 方案在登出和泄漏控制上都比较弱；既然后续已经预留 Redis，就应将 `jti` 黑名单和 `sid` 会话撤销设计进去。

**Token 格式**

- Refresh Token 推荐使用**不透明随机字符串**，不要用 JWT。
- 长度至少 32 字节，使用密码学安全的随机生成器（如 `/dev/urandom`、`std::random_device`）。

**有效期**

- 普通登录：7-30 天
- 可按"记住我"选项调整为更长时间（如 90 天）

**存储规则**

- 服务端**只存 Refresh Token 的哈希值**，不存明文。
- 推荐使用 SHA256 或更强的哈希算法。
- 数据库表结构示例：

```sql
CREATE TABLE refresh_tokens (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    token_hash VARCHAR(64) NOT NULL UNIQUE,
    session_id VARCHAR(64) NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    revoked_at TIMESTAMPTZ,
    INDEX (user_id, session_id),
    INDEX (expires_at)
);
```

**Refresh Token 轮换 (Rotation)**

每次刷新时执行 Refresh Token 轮换，增强安全性：

1. 客户端提交旧 refresh token
2. 服务端验证旧 token 有效且未作废
3. 作废旧 refresh token（设置 `revoked_at`）
4. 签发新 refresh token 和新 access token
5. 返回新 token 对给客户端

**泄漏检测**

如果检测到已作废的 refresh token 被再次使用，说明可能发生了 token 泄漏：

- 立即撤销整个 `sid` 或 token family
- 记录安全事件到 `audit_logs`
- 可选：通知用户异常登录

---

### 6.3 Redis 黑名单机制

**Access Token 黑名单**

登出时：

1. 删除或标记该 `sid` 对应的 refresh token 记录
2. 将当前 access token 的 `jti` 加入 Redis 黑名单
3. TTL 设置为 `access_token.exp - now`（自动过期清理）

Redis Key 设计：

```
jwt:deny:pfh-api:<jti>
```

**注意事项**

- 不建议用原始 JWT 或 JWT hash 当黑名单 key，直接用 `iss + jti` 更清晰。
- 黑名单只需存在性检查，value 可以为空字符串或撤销时间戳。
- 每次验证 JWT 时，必须先检查 `jti` 是否在黑名单中。

**Session 撤销**

撤销整个 `sid` 时：

```
jwt:revoke:sid:<sessionId>
```

所有包含该 `sid` 的 access token 均视为无效，即使未单独加入黑名单。

---

### 6.4 JwtFilter 规则

**放行路径**

以下接口无需 JWT 验证：

- 登录：`POST /api/v1/auth/login`
- 注册：`POST /api/v1/auth/register`
- 刷新 token：`POST /api/v1/auth/refresh`
- 公开资源（如货币元数据）：`GET /api/v1/currencies`

**验证流程**

其他接口统一要求：

```
Authorization: Bearer <access_token>
```

验证步骤：

1. 提取 Bearer token
2. 验证签名
3. 检查 `exp` 是否过期
4. 检查 `nbf` 是否生效
5. 验证 `iss` 和 `aud` 是否匹配
6. 检查 `jti` 是否在 Redis 黑名单中
7. 检查 `sid` 是否被撤销
8. 将 `sub` (userId) 注入到请求上下文

**校验失败统一返回**

状态码：`401 Unauthorized`

响应体：

```json
{
  "code": "UNAUTHORIZED",
  "message": "Invalid or expired access token",
  "path": "/api/v1/...",
  "timestamp": "2026-06-25T..."
}
```

**安全原则**

- 不要把 token 解析失败的具体原因暴露得太细，比如"签名错误""密钥不匹配"。
- 统一使用 `Invalid or expired access token` 等模糊描述。
- 避免泄露签名算法、密钥位置、解析器版本等内部信息。

---

### 6.5 密钥配置

**环境变量注入**

JWT Secret / 私钥**绝对不得**写在代码或普通配置文件中。

必须使用环境变量，例如：

```bash
PFH_JWT_SECRET=<强随机密钥>
PFH_PASSWORD_PEPPER=<可选 pepper>
```

**密钥强度要求**

- 如果使用 HMAC（HS256），密钥必须至少 256 bit（32 字节），且由密码学安全的随机生成器生成。
- 更正式的方案可以用 RS256 / EdDSA：
  - 服务端私钥签发
  - 网关或资源服务用公钥验证
  - 私钥妥善保管，定期轮换

**密钥轮换**

- 支持多版本密钥共存，验证时尝试所有活跃密钥。
- 签发时使用最新密钥。
- 逐步淘汰旧密钥，确保所有使用旧密钥签发的 token 过期后再彻底移除。

---

### 6.6 认证接口设计

**登录**

```
POST /api/v1/auth/login
```

请求：

```json
{
  "username": "user@example.com",
  "password": "plaintext-password"
}
```

响应（200 OK）：

```json
{
  "accessToken": "eyJhbGc...",
  "refreshToken": "random-opaque-string",
  "expiresIn": 900,
  "tokenType": "Bearer"
}
```

**刷新 Token**

```
POST /api/v1/auth/refresh
```

请求：

```json
{
  "refreshToken": "random-opaque-string"
}
```

响应（200 OK）：

```json
{
  "accessToken": "eyJhbGc...",
  "refreshToken": "new-random-opaque-string",
  "expiresIn": 900,
  "tokenType": "Bearer"
}
```

**登出**

```
POST /api/v1/auth/logout
```

请求头：

```
Authorization: Bearer <access_token>
```

请求体：

```json
{
  "refreshToken": "random-opaque-string"
}
```

响应：`204 No Content`

服务端操作：

1. 验证 access token，提取 `jti` 和 `sid`
2. 将 `jti` 加入 Redis 黑名单
3. 作废数据库中对应的 refresh token
4. 写入审计日志

---

## 7. 安全与防御性边界提示

1. **大数损失防御**：在 JSON 反序列化时，**严禁**允许前端将金额作为数字类型传输（如 `"amount": 45.12` 会因 JsonCpp 内部转为 double 导致二进制精度丢失）。所有金额输入和输出，在 JSON 中**必须映射为纯字符串**（如 `"amount": "45.12"`）。
2. **越权防御**：在 `CreateTransferUseCase` 中，读取账户后，必须在应用层强校验 `account.getUserId() == currentUserId`。严禁仅凭前端传入的 `sourceAccountId` 就盲目扣款。
3. **频率限制预留**：对于创建流水和转账接口，后续可在路由宏上挂载 `RateLimiterFilter`，防止前端异常重试导致的表爆满。
4. **密钥隔离**：所有敏感密钥（JWT Secret、Password Pepper、数据库密码）必须通过环境变量注入，绝不写入代码或配置文件。
5. **Token 泄漏防御**：采用 Refresh Token 轮换机制，检测到旧 token 重用时立即撤销整个会话。
6. **黑名单自动清理**：Redis 黑名单 key 必须设置 TTL，避免内存泄漏。
