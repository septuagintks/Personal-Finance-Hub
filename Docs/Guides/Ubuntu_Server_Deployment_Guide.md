# Personal Finance Hub Ubuntu Server 部署指南

Version: 1.0
Target: Ubuntu 24.04 LTS + Docker Compose
Status: Active

---

## 1. 部署边界

本指南面向单台 Ubuntu 24.04 LTS 服务器上的 PFH 部署：PostgreSQL、Flyway、Backend 和 Web Edge 运行在同一个 Docker Compose 项目中，宿主机 Nginx 负责公网 HTTPS 终止。

```text
Internet HTTPS
      -> host Nginx + Certbot
      -> 127.0.0.1:8081 (Compose web)
      -> app:8080 (Compose internal network)
      -> postgres:5432 (Compose internal network)
```

部署规则：

- 宿主机只开放 SSH、HTTP 和 HTTPS；PostgreSQL 与 Compose Web 端口只绑定回环地址。
- 浏览器生产会话必须经过 HTTPS，不能把 Compose 的明文端口直接暴露公网。
- Backend 使用 request role；background role 独立、只读并只执行批准的跨租户后台查询。
- 迁移、角色初始化和应用启动按顺序执行；已发布 migration 不回写，修正使用新版本。
- 服务器需要能向 Docker registry、Git、FreeCurrencyAPI 和 exchangerate.fun 发起 DNS/HTTPS 出站连接。
- 本指南不覆盖高可用 PostgreSQL、多节点调度、Kubernetes 或外部托管数据库。

---

## 2. 前置条件

准备以下信息：

- Ubuntu 24.04 LTS 服务器和可用的 `sudo` 权限。
- 指向服务器的 DNS A/AAAA 记录，例如 `ledger.example.com`。
- 可从公网访问的 TCP 80/443；SSH 端口按服务器策略保留。
- PFH 仓库的部署凭据，以及已审核的 Git commit 或 release tag。
- FreeCurrencyAPI key。exchangerate.fun 是无 key 的整批备用源。

下文中的 `ledger.example.com` 必须统一替换为实际域名。

安装 Docker Engine、Compose plugin、宿主 Nginx、Certbot 和基础工具：

```bash
sudo apt-get update
sudo apt-get install -y ca-certificates curl gnupg git openssl ufw nginx certbot python3-certbot-nginx

sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg \
  -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc

. /etc/os-release
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu ${UBUNTU_CODENAME:-$VERSION_CODENAME} stable" \
  | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
sudo systemctl enable --now docker nginx

docker --version
sudo docker compose version
nginx -v
certbot --version
```

确认 Certbot 的续期定时器已启用：

```bash
sudo systemctl enable --now certbot.timer
systemctl is-enabled certbot.timer
systemctl is-active certbot.timer
```

服务器时区应使用有效的 IANA 时区；生产环境通常使用 UTC，用户报表时区由用户偏好决定：

```bash
timedatectl status
sudo timedatectl set-timezone Etc/UTC
```

先配置防火墙，再启用它。若 SSH 使用的不是 22 端口，应将 `OpenSSH` 规则替换为实际端口；确认当前 SSH 会话不会被锁定：

```bash
sudo ufw allow OpenSSH
sudo ufw allow 'Nginx Full'
sudo ufw enable
sudo ufw status verbose
```

---

## 3. 固定部署版本

将代码放在固定目录，并部署已审核的 commit；不要直接依赖会移动的工作分支：

```bash
sudo install -d -m 0750 -o "$USER" -g "$(id -gn)" /opt/pfh
git clone git@github.com:septuagintks/Personal-Finance-Hub.git /opt/pfh
cd /opt/pfh

git fetch origin
REVIEWED_COMMIT='<reviewed-commit-or-tag>'
git checkout --detach "$REVIEWED_COMMIT"
git show -s --format='%H%n%P%n%s' HEAD
git status --short --branch
```

如果要验证提交签名，服务器必须预先安装并信任对应的公开签名 key；不能把未知 key 的状态当作已验证：

```bash
git show --show-signature --no-patch HEAD
git verify-commit HEAD
```

---

## 4. 生产配置与 Secret

Compose 默认从项目根目录的 `.env` 读取变量。该文件被 Git 忽略，权限必须限制为仅 root 可读：

```bash
cd /opt/pfh
sudo install -m 0600 -o root -g root /dev/null .env
sudoedit .env
```

填写实际值。以下是变量清单，不是可直接使用的 Secret：

```dotenv
COMPOSE_PROJECT_NAME=pfh

PFH_POSTGRES_ADMIN_PASSWORD='<unique-admin-password>'
PFH_DB_NAME=pfh_prod
PFH_DB_USER=pfh_request
PFH_DB_PASSWORD='<unique-request-password>'
PFH_BACKGROUND_DB_USER=pfh_background
PFH_BACKGROUND_DB_PASSWORD='<different-background-password>'

PFH_JWT_SECRET='<at-least-32-byte-random-secret>'
PFH_PASSWORD_PEPPER=
PFH_FREECURRENCYAPI_API_KEY='<provider-key>'

# Bind host ports to loopback; the public entry is host Nginx.
PFH_POSTGRES_PORT=127.0.0.1:5432
PFH_WEB_PORT=127.0.0.1:8081
```

使用 Secret Manager 或密码管理器生成和保存值；至少检查以下事项：

- 三个数据库密码彼此独立，request/background 用户名不同。
- `PFH_JWT_SECRET` 至少 32 字节，不能使用示例 placeholder。
- Provider key 只写入服务器 Secret，不写入 Git、镜像、日志或交接记录。
- `.env` 不使用未经确认的 shell 展开；包含 `$` 的密码按 Docker Compose `.env` 语法正确引用。
- `PFH_POSTGRES_ADMIN_PASSWORD` 在首次初始化后不能只靠修改 `.env` 轮换；应先在 PostgreSQL 中改密，再同步更新 Secret。
- `PFH_JWT_SECRET` 或密码 pepper 变更会影响现有会话/密码校验，必须按维护窗口和迁移策略执行。

Compose 只输出配置检查结果，不要使用会把完整 Secret 打到终端或日志的命令：

```bash
cd /opt/pfh
if sudo grep -nE 'REPLACE_WITH|<[^>]+>' .env; then
  echo 'Unresolved placeholder in .env' >&2
  exit 1
fi
sudo docker compose config --quiet
```

### 4.1 非 Secret 生产配置

镜像中的 `config.example.json` 只作为默认模板。将不含 Secret 的本地配置放到 `/etc/pfh`，把日志级别调整为 `info`，将 `logging.output` 保持为 `console`，并保留服务端口 `8080`、Scheduler 和超时参数的有效值：

```bash
sudo install -d -m 0750 -o root -g root /etc/pfh
sudo cp config/config.example.json /etc/pfh/config.local.json
sudo chmod 0644 /etc/pfh/config.local.json
sudoedit /etc/pfh/config.local.json
```

不要在该文件中填写数据库密码、JWT secret、pepper 或 Provider key；这些字段由 `PFH_*` 环境变量 overlay 提供。运行时容器是只读文件系统，除 `/tmp` 外没有日志写入目录；只有在另行挂载受控日志卷时才可启用文件输出。`config/README.md` 是完整变量映射的事实来源。

创建仅用于生产的 Compose override。它不进入仓库，包含重启策略、生产环境标识和配置文件只读挂载：

```bash
sudo tee /etc/pfh/compose.production.yaml > /dev/null <<'YAML'
services:
  postgres:
    restart: unless-stopped
  app:
    restart: unless-stopped
    environment:
      PFH_ENVIRONMENT: production
    volumes:
      - /etc/pfh/config.local.json:/app/config/config.local.json:ro
  web:
    restart: unless-stopped
YAML
sudo chmod 0644 /etc/pfh/compose.production.yaml
```

在当前 shell 中定义后续命令使用的 Compose 包装函数；重新登录服务器后需要再次定义：

```bash
cd /opt/pfh
dc() {
  sudo docker compose \
    -f docker-compose.yml \
    -f /etc/pfh/compose.production.yaml "$@"
}
```

---

## 5. HTTPS 反向代理

先确认 DNS 已生效，再创建宿主 Nginx HTTP 代理。`X-Forwarded-Proto` 必须由受信宿主代理用 `$scheme` 覆盖，不能透传客户端同名请求头：

```bash
sudo tee /etc/nginx/sites-available/pfh.conf > /dev/null <<'NGINX'
server {
    listen 80 default_server;
    listen [::]:80 default_server;
    server_name _;
    return 444;
}

server {
    listen 80;
    listen [::]:80;
    server_name ledger.example.com;

    client_max_body_size 1m;

    location / {
        proxy_pass http://127.0.0.1:8081;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
NGINX

sudo ln -sfn /etc/nginx/sites-available/pfh.conf /etc/nginx/sites-enabled/pfh.conf
sudo rm -f /etc/nginx/sites-enabled/default
sudo nginx -t
sudo systemctl reload nginx
```

申请并自动配置证书：

```bash
sudo certbot --nginx --redirect -d ledger.example.com
sudo nginx -t
sudo systemctl reload nginx
sudo certbot renew --dry-run
```

证书配置完成后，外部 HTTPS 请求到内层 Nginx 时必须保留 `X-Forwarded-Proto: https`；HTTP 请求应被重定向到 HTTPS。Secure Cookie、Origin 校验和 Fetch Metadata 依赖这一拓扑。

---

## 6. 构建、迁移与启动

先检查 Compose 合并结果，再构建应用镜像。不要把 Secret 输出到 `docker compose config` 的普通输出中：

```bash
cd /opt/pfh
dc config --quiet
dc build --pull app web
```

按顺序启动数据库、迁移和运行角色：

```bash
dc up -d postgres
dc ps postgres

dc run --rm flyway migrate
dc run --rm flyway info
dc run --rm flyway validate
dc run --rm flyway migrate

dc run --rm role-init
dc up -d app web
dc ps -a
```

验收要求：

- V1-V10 全部成功，第二次 `migrate` 为 no-op。
- `role-init` 以 exit code 0 完成；request/background 角色不重名、不互为成员，属性和授权符合迁移指南。
- `app`、`web` 和 `postgres` 显示 healthy；Flyway 和 role-init 显示已成功退出。
- Backend 只在 Compose 网络暴露 `8080`，宿主只通过回环 `8081` 访问 Web。

由于 `app` 依赖 Flyway 和 `role-init` 的成功退出，最后的 `dc up -d app web` 可能再次运行这两个幂等的一次性服务；这是启动门禁的一部分，不是重复迁移故障。

---

## 7. 部署后检查

先检查本机入口，再检查公网 HTTPS：

```bash
curl --fail --silent http://127.0.0.1:8081/livez
curl --fail --silent http://127.0.0.1:8081/readyz
curl --fail --silent https://ledger.example.com/livez
curl --fail --silent https://ledger.example.com/readyz
curl --fail --silent -D /tmp/pfh-currencies.headers \
  -o /tmp/pfh-currencies.json \
  https://ledger.example.com/api/v1/currencies
grep -Ei 'HTTP/|etag:|x-trace-id:' /tmp/pfh-currencies.headers
rm -f /tmp/pfh-currencies.headers /tmp/pfh-currencies.json
```

`/readyz` 在数据库或必要后台组件尚未就绪时可以返回 503；先查看状态并等待，不要通过关闭检查来“修复”部署：

```bash
dc ps
dc logs --tail=100 app web postgres
```

上线前确认：

```bash
sudo ss -lntp
sudo ufw status verbose
```

公网只应提供 SSH、80 和 443；`5432` 与 `8081` 应绑定 `127.0.0.1`，Backend 不应有宿主监听端口。不要把包含 Cookie、Token、数据库 URL 或 Provider URL 的原始日志复制到工单或 Git。

通过 HTTPS 完成一次注册、登录、refresh、退出和一个最小财务读写闭环；浏览器应看到 Secure/HttpOnly/SameSite Cookie，前端存储中不得出现 Access/Refresh Token。

---

## 8. 备份、升级与回滚

### 8.1 备份

迁移或升级前先创建 PostgreSQL custom-format dump，并限制备份目录权限：

```bash
sudo install -d -m 0700 /var/backups/pfh
backup="/var/backups/pfh/pfh-$(date -u +%Y%m%dT%H%M%SZ).dump"
set -o pipefail
dc exec -T postgres sh -c 'pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" -Fc' \
  | sudo tee "$backup" > /dev/null
sudo chmod 0600 "$backup"
sudo sha256sum "$backup"
```

恢复必须先在独立 PostgreSQL 实例或新 volume 中验证；不要对正在服务的数据库执行 `docker compose down -v` 或直接覆盖数据目录。custom-format dump 不包含数据库角色和密码，恢复后必须重新执行 `role-init` 并复核权限。完整 migration 兼容和恢复规则见 [Database Migration Guide](Database_Migration_Guide.md)。

### 8.2 升级

升级使用维护窗口，先停止 Web/API，完成备份和迁移，再启动新版本：

```bash
dc stop app web

git fetch origin
REVIEWED_COMMIT='<new-reviewed-commit-or-tag>'
git checkout --detach "$REVIEWED_COMMIT"
git show -s --format='%H%n%s' HEAD

dc build --pull app web
dc run --rm flyway migrate
dc run --rm flyway validate
dc run --rm role-init
dc up -d app web
dc ps -a
```

不要用旧镜像回滚数据库 schema。只有在新版本与当前 schema 明确兼容时才回退应用 commit；否则使用已验证备份恢复到隔离实例，或按 append-only migration 规则发布 forward fix。

### 8.3 停止与清理

```bash
dc down
```

该命令保留 named volume。生产环境禁止使用 `dc down -v`，除非已确认要永久删除全部数据库数据并拥有可验证备份。

---

## 9. 故障排查边界

1. 先看 `dc ps`、`/livez`、`/readyz` 和最近脱敏日志。
2. Flyway 失败时保留失败状态，检查 checksum 和数据库状态；不要手工修改 `flyway_schema_history`。
3. `role-init` 失败时修正角色名、密码和成员关系；不要关闭启动权限校验或复用 superuser。
4. Web 登录失败时优先检查公网 TLS、Host、`X-Forwarded-Proto` 和 Secure Cookie，不要改成非 Secure Cookie。
5. Provider 失败时允许系统使用整批备用或历史降级；不要把 key 写入日志，也不要将部分汇率伪装为完整成功。
6. 任何数据恢复先在隔离实例验证计数、迁移状态、health/readiness、登录和核心读路径，再安排切换。

---

## 10. 相关文档

- [Linux Development Workflow](Linux_Development_Workflow.md)
- [Database Migration Guide](Database_Migration_Guide.md)
- [Configuration Guide](../../config/README.md)
- [REST API OpenAPI](../Architecture/10_REST_API_OpenAPI.json)
- [Testing Strategy](../Architecture/16_Testing_Strategy.md)
- [Phase 2 S09-S12 最终交付摘要](../Archive/Phase_2_S09-S12_Delivery_Summary.md)
