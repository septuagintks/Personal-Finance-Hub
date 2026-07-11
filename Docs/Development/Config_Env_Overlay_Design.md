# 配置环境变量 Overlay 设计

**任务编号**: tasks.md #9f
**关联阶段**: P1-S04 配置加载补充
**状态**: 待实现

---

## 1. 背景

S04 已实现 `JsonConfigLoader`，能从 JSON 文件加载完整配置。但生产部署场景下，敏感信息（JWT secret、数据库密码）和环境特定参数（数据库 host/port）不应硬编码在配置文件中，而应通过**环境变量注入**。

当前 `JsonConfigLoader` 只读 JSON 文件，无法响应环境变量。这导致：
1. 部署时必须修改配置文件或维护多个环境副本。
2. 密钥等敏感信息可能被意外提交到版本控制。
3. 容器化部署（Docker/Kubernetes）的标准做法（用 env 注入配置）无法实施。

---

## 2. 设计目标

1. **Overlay 优先级**：环境变量 > JSON 文件 > 默认值。
2. **关键字段支持**：优先覆盖敏感字段与部署参数，非所有字段都需要 env 映射。
3. **向后兼容**：若无环境变量，行为与当前 JSON-only 完全一致。
4. **清晰命名**：环境变量命名遵循 `PFH_<SECTION>_<KEY>` 约定（如 `PFH_JWT_SECRET`）。

---

## 3. 环境变量映射表

### 3.1 关键字段（优先实现）

| 配置路径              | 环境变量名                    | 类型   | 说明                     |
| --------------------- | ----------------------------- | ------ | ------------------------ |
| `jwt.secret`          | `PFH_JWT_SECRET`              | string | JWT 签名密钥（强制覆盖） |
| `database.host`       | `PFH_DB_HOST`                 | string | 数据库主机地址           |
| `database.port`       | `PFH_DB_PORT`                 | int    | 数据库端口               |
| `database.name`       | `PFH_DB_NAME`                 | string | 数据库名                 |
| `database.user`       | `PFH_DB_USER`                 | string | 数据库用户               |
| `database.password`   | `PFH_DB_PASSWORD`             | string | 数据库密码（强制覆盖）   |
| `exchange_rate.api_key` | `PFH_EXCHANGE_RATE_API_KEY` | string | 汇率 API 密钥            |
| `environment`         | `PFH_ENVIRONMENT`             | string | 运行环境（dev/prod）     |

### 3.2 可选字段（后续按需扩展）

| 配置路径           | 环境变量名           | 类型   |
| ------------------ | -------------------- | ------ |
| `server.host`      | `PFH_SERVER_HOST`    | string |
| `server.port`      | `PFH_SERVER_PORT`    | int    |
| `logging.level`    | `PFH_LOG_LEVEL`      | string |
| `logging.output`   | `PFH_LOG_OUTPUT`     | string |

---

## 4. 实现要点

### 4.1 读取顺序

```cpp
AppConfig config;

// 1. 先从 JSON 文件加载（提供默认值和非敏感配置）
auto json_result = load_from_json(config_path);
if (!json_result) {
    return std::unexpected(json_result.error());
}
config = *json_result;

// 2. 再用环境变量 overlay 关键字段
apply_env_overlay(config);

// 3. 最后校验（拒绝占位符密钥等）
validate_config(config);
```

### 4.2 `apply_env_overlay` 逻辑

```cpp
void apply_env_overlay(AppConfig& config) {
    // JWT secret (强制覆盖，部署必须)
    if (auto val = get_env("PFH_JWT_SECRET"); val) {
        config.jwt.secret = *val;
    }

    // 数据库密码（强制覆盖）
    if (auto val = get_env("PFH_DB_PASSWORD"); val) {
        config.database.password = *val;
    }

    // 数据库连接参数（可选覆盖）
    if (auto val = get_env("PFH_DB_HOST"); val) {
        config.database.host = *val;
    }
    if (auto val = get_env("PFH_DB_PORT"); val) {
        config.database.port = parse_uint16(*val);
    }
    // ... 其余字段类似
}
```

### 4.3 辅助函数

```cpp
[[nodiscard]] std::optional<std::string> get_env(const char* name) {
    const char* val = std::getenv(name);
    return val ? std::optional<std::string>(val) : std::nullopt;
}
```

---

## 5. 测试要点

1. **无环境变量**：行为与当前 JSON-only 完全一致。
2. **部分 env 覆盖**：`PFH_JWT_SECRET` 覆盖 JSON，其余字段保留 JSON 值。
3. **全 env 覆盖**：关键字段全部从 env 读取，JSON 仅提供非敏感默认值。
4. **校验与 env 交互**：env 提供的 secret 仍然是占位符时，校验依然拒绝。

---

## 6. 部署示例

### Docker Compose

```yaml
services:
  pfh:
    image: pfh:latest
    environment:
      PFH_ENVIRONMENT: production
      PFH_JWT_SECRET: ${PFH_JWT_SECRET}  # 从宿主 env 注入
      PFH_DB_HOST: postgres
      PFH_DB_PASSWORD: ${PFH_DB_PASSWORD}
      PFH_EXCHANGE_RATE_API_KEY: ${EXCHANGE_API_KEY}
    volumes:
      - ./config.json:/app/config/config.json:ro  # 非敏感默认值
```

### Kubernetes Secret

```yaml
apiVersion: v1
kind: Secret
metadata:
  name: pfh-secrets
stringData:
  jwt-secret: "actual-production-secret"
  db-password: "db-prod-password"
---
apiVersion: apps/v1
kind: Deployment
spec:
  template:
    spec:
      containers:
      - name: pfh
        env:
        - name: PFH_JWT_SECRET
          valueFrom:
            secretKeyRef:
              name: pfh-secrets
              key: jwt-secret
        - name: PFH_DB_PASSWORD
          valueFrom:
            secretKeyRef:
              name: pfh-secrets
              key: db-password
```

---

## 7. 验收标准

- [ ] `JsonConfigLoader` 增加 `apply_env_overlay` 私有方法，覆盖 3.1 节关键字段。
- [ ] `get_env` 辅助函数实现。
- [ ] 校验逻辑（拒绝占位符密钥）在 env overlay 之后执行。
- [ ] 单元测试覆盖：无 env、部分 env、全 env、env 占位符校验拒绝。
- [ ] `config/README.md` 补充环境变量使用说明。
- [ ] main.cpp 启动日志输出"已加载环境变量覆盖：X 个字段"。

---

## 8. 后续扩展

- **敏感字段脱敏日志**：启动日志中，密钥类字段只输出 `***` 或前缀（如 `jwt.secret=REPL***`）。
- **env 文件支持**：从 `.env` 文件加载环境变量（可用 dotenv 库，或手动解析）。
- **类型校验增强**：env 提供的 `PFH_DB_PORT` 若不是有效 uint16，报错提示具体环境变量名。
