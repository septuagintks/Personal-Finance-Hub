# Personal Finance Hub Phase 2 后审计整改计划

Version: 1.1
Baseline: `main@43d06fb79bd0c1faa41f4f3fffb0c15c05673fc2`
Status: Ready for implementation
Suggested Branch: `fix/post-phase2-audit-remediation`

---

## 1. 目标与边界

本轮在不进入 Phase 3 的前提下，修复 Phase 1 与 Phase 2 最终交付态审计中确认的数值边界、用户偏好、时区、本地化、前端并发一致性，以及性能和内存资源边界问题。完成后，现有产品能力应满足以下结果：

- 新注册 Web 用户默认使用浏览器报告的有效 IANA 时区；非浏览器客户端仍可省略该字段并使用服务端默认值。
- 所有账务时间输入、自然日筛选和审计时间筛选都以 `UserPreference.timezone` 解释，不再依赖浏览器进程时区。
- 设置页可以搜索并选择服务端支持的完整 IANA 时区集合。
- `numberFormat` 与 `defaultReportPeriod` 不再只是持久化字段，而是实际控制金额展示和报表默认范围。
- 所有静态用户界面文本通过统一 i18n 接口提供完整 `zh-CN` 与 `en-US` 文案；新增语言只需注册语言包并补齐后端模板与契约。
- `Decimal` 的公开构造与全部运算保持一个可证明的安全范围，不可能产生会在取负时触发 C++ signed overflow 的状态。
- 分类与标签写操作按确定顺序执行，不使用“中止前一个请求”模拟事务顺序，页面最终状态与服务端一致。
- 长周期报表、现金流、历史汇率和净值查询均在数据库侧聚合或按需读取，不会把无界历史明细一次性物化到进程内存。
- 认证哈希、HTTP worker 队列、导出结果、标签展开和用户资源数量都有可配置但受安全上限约束的资源预算。
- 转账列表、Dashboard 和净值查询不再随页面项数或账户数形成 `N+1` 数据库往返。
- Outbox、handler receipt 和文件日志具备明确的保留或轮转策略；前端长列表与 formatter 不随会话时长无界增长。

本轮不包含账单导入、支付平台接入、完整加密货币定价、原生客户端或其他 Phase 3 能力。现有金融规则、API 金额字符串、追加式更正、租户隔离、RLS、Outbox 和同源会话边界不得改变。

---

## 2. 已确定的设计决策

### 2.1 时区来源与权威边界

1. `UserPreference.timezone` 是登录后所有业务时间展示和本地时间输入的唯一权威时区。
2. Web 注册页通过 `Intl.DateTimeFormat().resolvedOptions().timeZone` 获取浏览器时区，并作为可选 `preferredTimezone` 提交。
3. 服务端使用自身 IANA tzdb 验证 `preferredTimezone`。字段缺省时按最终 locale 使用现有默认时区；字段存在但无效时返回字段级验证错误，不静默改写。
4. API 中的时间继续使用带时区 RFC 3339，并在服务端规范化为 UTC。`date`、`month` 和 `datetime-local` 只作为界面输入，不直接当作 UTC。
5. 设置页的时区列表来自服务端 tzdb，而不是前端硬编码列表或第三方静态副本，避免浏览器与后端支持集合漂移。
6. 浏览器时区只参与首次注册。已有用户登录、刷新会话或更换设备时，浏览器时区不得覆盖已保存偏好。

### 2.2 本地时间转换

前端建立单一 `zoned-date-time` 服务，负责以下双向转换：

- UTC/RFC 3339 instant -> 指定 IANA 时区的 `datetime-local` 值。
- 指定 IANA 时区中的本地日期时间 -> RFC 3339 instant。
- 指定 IANA 时区中的自然日 -> UTC 半开窗口 `[start, nextDayStart)`。
- 指定 IANA 时区中的月份 -> 报表月份键和自然月范围。

转换固定使用 `@js-temporal/polyfill` 封装 Temporal API，不自行维护固定 offset，也不直接散布第三方 API。相关服务随使用它的路由 chunk 加载，并受总 bundle 与 async chunk 预算约束。不存在的 DST 本地时间必须产生字段错误；重复的 DST 本地时间必须采用明确且测试固定的 disambiguation 规则，不允许由浏览器默认时区隐式决定。

Feature Component 不隐式读取全局 Store。`TransactionFormDialog` 与 `TransferFormDialog` 由页面显式传入 `timeZone`，共享转换服务保持纯函数边界。

### 2.3 数字格式与报表周期

`numberFormat` 收敛为以下受支持值，并由 OpenAPI、Application、数据库约束和前端共享类型保持一致：

| 值 | 分组符 | 小数符 |
| --- | ------ | ------ |
| `1,234.56` | `,` | `.` |
| `1.234,56` | `.` | `,` |
| `1 234,56` | 空格 | `,` |

`locale` 控制语言、数字字形和其他本地化行为；`numberFormat` 明确覆盖金额的分组符和小数符。API、Store 与金融计算仍保存规范十进制字符串，格式化只发生在展示层。

`defaultReportPeriod` 在 Reports 路由没有显式月份参数时生效：

| 值 | 默认范围 |
| --- | -------- |
| `current_month` | 当前自然月 |
| `last_month` | 上一个完整自然月 |
| `last_3_months` | 当前月及之前两个月 |
| `current_year` | 当前年份一月至当前月 |
| `custom` | 用户偏好中持久化的自定义起止月份 |

为完整支持 `custom`，用户偏好增加可空的 `customReportStartMonth` 与 `customReportEndMonth`。两者按自然月保存，只有 `defaultReportPeriod=custom` 时必须同时存在且起始不晚于结束。现存但无法解释的 `custom` 值在迁移时回退为 `current_month`，因为旧实现从未消费该值。

### 2.4 i18n 架构

前端采用 `vue-i18n` Composition API，取代当前导航专用的手写 `translate()`。目录固定为：

```text
frontend/src/i18n/
├── index.ts
├── locale-registry.ts
├── types.ts
└── locales/
    ├── en-US.ts
    └── zh-CN.ts
```

- `en-US` 是结构基准，`zh-CN` 必须通过 TypeScript 类型检查提供完全相同的 key。
- locale 文件使用动态 import 按需加载；公共入口只加载当前语言，切换后缓存已加载语言包。
- 页面、组件、对话框、表单验证、空状态、错误 fallback、Tooltip、ARIA 名称、图表图例和状态标签全部使用 key，不保留用户可见的硬编码英文或中文。
- 用户输入、账户/分类/标签名称、货币代码、IANA 时区 ID、TraceId、事件名和其他服务端业务数据不作为静态文案翻译。
- 登录前 locale 按“用户显式公共选择 -> `navigator.languages` -> `en-US`”解析。公共选择可作为不含身份的界面偏好持久化。
- 登录后 `UserPreference.locale` 覆盖公共 locale；偏好更新成功后立即切换，不要求刷新页面。
- 注册请求使用公共页面当前 locale，不再固定提交 `en-US`。
- 后端 `LocaleTag` 当前仍只开放 `zh-CN` 与 `en-US`。未来增加语言时，必须同时增加前端语言包、OpenAPI 枚举、Application 支持和完整系统分类模板，不能只增加界面文案。

现有用户的个人分类属于用户数据，切换 locale 不自动改名。语言模板只决定新账户初始化内容，避免覆盖用户修改。

### 2.5 Decimal 安全范围

`Decimal` 采用“收紧并强制安全不变量”的方案，而不是让所有一元运算改成可失败接口：

- 正式定义 raw storage 的有效范围为 `[-2^126, 2^126]`，延续现有 parser 的 headroom 设计。
- `from_scaled` 改为返回 `DomainResult<Decimal>`，拒绝超出安全范围的 raw value，不再提供绕过不变量的公开构造。
- `add`、`subtract`、`multiply`、`divide` 和 `round_to_scale` 在返回前统一验证安全范围。
- 新增只在 unsigned domain 工作的 magnitude helper；内部实现不得对未知负值直接执行 `-value`。
- 在该不变量下，`negated()` 与 `abs()` 可以继续保持无失败接口和 `noexcept`。

数据库 `NUMERIC(20,8/10)` 边界保持不变。安全范围只修复 Decimal 类型自身的 C++ 行为，不扩大可写入数据库的金额或汇率范围。

### 2.6 元数据写入顺序

分类和标签共享一个 Store 级 FIFO mutation queue：

- 新操作排队执行，不中止已经发出的前一个写请求。
- `clear()` 或用户会话切换会中止当前请求，并通过 generation 使尚未发出的排队操作失效。
- 每次成功写入只刷新受影响的资源；响应状态不确定时，在当前会话仍有效的前提下执行一次权威 reload。
- 分类与标签 create 的 Idempotency-Key 在服务端成功响应前保持不变，网络结果不明确时复用原 key。
- Store 暴露统一 mutation pending 状态；设置页在队列非空时禁用会产生冲突的创建、编辑、删除和恢复动作。

队列是数据一致性的最终保护，UI 禁用只负责防止重复点击和表达进行中状态。

### 2.7 性能与内存审计结论

本轮静态审计与 GCC `-fanalyzer` 未发现已经能够确认的内存泄漏、UAF、悬空引用或 `shared_ptr` 引用环。ECharts、`ResizeObserver`、Object URL、`AbortController` 和 libcurl 响应缓冲也都有既有回收路径或局部上限。该结论只表示未发现确定的内存安全缺陷，不代表当前资源消耗已经受控；ASan/UBSan 动态运行、真实 PostgreSQL `EXPLAIN`、并发 RSS、浏览器 heap profile 和断连压力测试仍是本轮必须补齐的验证。

审计确认以下高风险资源缺口：

1. 现金流趋势允许查询最长 120 个月，并一次物化窗口内全部流水，没有行数或字节预算。长期高频账户可以通过单请求耗尽进程内存。
2. 报表的 10,000 行上限不等于内存上限。单条描述可达 4096 字节、单条流水可带 64 个标签，标签 breakdown 理论上可展开 640,000 个 bucket；CSV 还会同时保留领域对象和完整输出字符串。
3. 历史汇率缓存按币种对载入完整窗口内的小时级快照。32 个目标币种查询 10 年数据时，数量级可达到约 280 万个对象。
4. 登录与注册和普通 API 共用请求 worker。Argon2 单次约使用 19 MiB，未知用户名也执行等成本哈希；缺少认证专用并发限制和应用层速率限制时，未认证流量可以耗尽 CPU、worker 与内存。
5. HTTP 队列只按任务数量准入，请求 body 在准入前复制，且客户端断开不会取消已经排队的同步任务。允许配置到 10,000 个任务时，累计 payload 可能达到数 GiB。

审计同时确认以下中低风险放大路径：

- 历史净值最多逐月调用 120 次 `balances_at()`，每次重新聚合历史流水。
- 转账列表存在 `1 + N` SQL，最大页 200 时可产生 201 次数据库查询。
- Dashboard 与净值按账户分别开启事务并读取余额，且账户、分类和标签没有用户级数量配额。
- 分类树构建为 `O(N^2)`；Outbox 与 handler receipt 没有清理策略，Operations 汇总会扫描累计表。
- Reports 首次进入可能重复发起请求；流水和转账 `loadMore` 永久累加并反复构建全量去重集合。
- `Intl.NumberFormat` / `Intl.DateTimeFormat` 在单元格热路径反复构造；可选文件日志没有轮转。

### 2.8 资源预算与降级原则

1. 所有可能随用户数据、时间跨度、标签数量或请求体增长的路径都必须同时受“条目数 + 估算/实际字节数 + 时间跨度”中适用维度约束，不能只依靠分页或 Drogon 默认限制。
2. 可在 SQL 中聚合、分桶、投影或批量读取的数据，不先读取完整 Domain 对象再在 Application 层压缩。Repository 为这类读模型提供专用查询，不扭曲写侧聚合。
3. CSV 等大结果使用有界流式输出或有界临时介质；超过同步导出预算时返回稳定错误，不在内存中同时保留输入全集和输出全集。
4. 认证使用独立并发闸门和速率限制。未知用户的恒定成本防枚举语义保留，但总并发必须受控。
5. HTTP 队列实施任务数与累计 payload 字节双预算。取消只能作为节省资源的优化，业务正确性仍由事务和幂等保证。
6. 所有上限提供保守默认值和不可绕过的安全上限；配置错误在启动时失败，不允许把容量设为等同无界。
7. 超限、限流、取消、队列拒绝和保留清理都必须产生低基数指标与结构化日志，不能记录密码、Token、完整账务描述或导出内容。
8. 优化前后以真实 PostgreSQL `EXPLAIN (ANALYZE, BUFFERS)`、峰值 RSS、查询次数、响应大小和浏览器 heap 作为验收依据，不能只以功能测试通过判定完成。

---

## 3. 数据库与 API 变更

### 3.1 Migration

计划增加两个只向前迁移：

| Migration | 内容 |
| --------- | ---- |
| `V11__complete_user_preferences.sql` | 增加自定义报表起止月份、规范 `number_format`、补充组合 CHECK 约束并处理旧 `custom` 值 |
| `V12__seed_en_us_category_templates.sql` | 增加与 `zh-CN` 结构等价的完整 `en-US` root/child 系统分类模板 |

V1-V10 不得修改。V11/V12 必须覆盖空库迁移、V10 升级、重复 validate/no-op 和失败回滚。

### 3.2 OpenAPI

OpenAPI 预计增加或修改：

- `RegisterRequest.preferredTimezone?: string`。
- `NumberFormat` enum，并由 `UserPreference.numberFormat` 引用。
- `customReportStartMonth` 与 `customReportEndMonth`，使用严格 `YYYY-MM` schema 或等价月份类型。
- `TimeZoneMetadata` 与 `GET /api/v1/timezones`。
- 现有生成类型重新生成，漂移门禁继续生效。

### 3.3 Application 与 Persistence

- 注册命令、bootstrap port 和 PostgreSQL/In-Memory registration defaults adapter 接收可选时区。
- PostgreSQL 与 In-Memory adapter 使用相同 locale fallback、时区优先级和语言模板完整性语义。
- UserPreference Domain、DTO、Controller、Repository 与 migration 同步增加自定义月份字段和 NumberFormat 约束。
- 时区元数据 Query Service 只公开 tzdb 名称，不返回环境路径或其他运行信息。

---

## 4. 实施顺序

### 4.1 R01 契约与测试骨架

开发内容：

1. 为五项问题先增加可失败的回归测试，保持测试在修复前能准确暴露偏差。
2. 更新 OpenAPI schema、生成前端类型并建立 V11/V12 migration 骨架。
3. 固定 `NumberFormat`、自定义报表月份和 `preferredTimezone` 的跨层类型。
4. 增加 i18n key 完整性测试和用户可见裸文本扫描门禁，允许业务数据与技术标识白名单。

验收：契约验证脚本通过；新增行为测试在对应实现完成前按预期失败；既有生成文件无手工修改。

### 4.2 R02 Decimal 不变量修复

开发内容：

1. 增加 unsigned magnitude helper 与统一范围检查。
2. 修改 `from_scaled`、算术、字符串转换和 NUMERIC fit 检查。
3. 更新所有调用方与注释，删除“可使用完整 signed range”等不再成立的描述。
4. 覆盖边界值、两个 `-2^126` 相加、最小值构造拒绝、正负乘除和 Half-Even 舍入。

验收：所有 Decimal API 对任意可构造值都无 signed overflow；Unit 测试和 Linux UBSan 通过；正常金额行为与字符串结果不变。

### 4.3 R03 i18n 基础与全部文本迁移

开发内容：

1. 引入 `vue-i18n`、locale registry、类型化语言包和登录前 locale 状态。
2. 按公共入口、认证、Shell、账户、元数据、流水、转账、Dashboard、报表、维护、Operations 和通用组件顺序迁移全部静态文本。
3. 把 API error code、field error 和本地校验映射到 i18n key；未知错误保留脱敏服务端 fallback。
4. 为语言切换、插值、ARIA、图表和动态状态补测试。
5. 增加完整 `en-US` 系统分类模板，并使 In-Memory registration defaults 与 PostgreSQL 语义一致。

验收：`zh-CN` 与 `en-US` 下不存在缺 key、裸用户可见文本或混合语言框架文案；新注册两种 locale 均得到对应完整分类树；切换 locale 不改写既有个人分类。

### 4.4 R04 浏览器时区初始化与完整选择器

开发内容：

1. 注册页检测浏览器 IANA 时区并提交 `preferredTimezone`。
2. 服务端校验并在 bootstrap 事务中持久化该时区。
3. 实现 `GET /api/v1/timezones`，覆盖 canonical zones、有效 aliases、排序和 ETag。
4. 设置页改为支持键盘和搜索的时区 combobox，保留当前有效值并本地化周边文案。

验收：在浏览器时区为 `UTC`、`America/New_York` 和 `Asia/Shanghai` 的测试中，新用户偏好与浏览器一致；非法或服务端不可用时区返回稳定字段错误；完整列表不依赖硬编码五项。

### 4.5 R05 账务时间与筛选修复

开发内容：

1. 实现共享 IANA 本地时间转换服务及 DST 规则。
2. 替换 Transaction/Transfer 创建与更正中的 `toLocalInput` 和 `new Date(localValue)`。
3. 替换 Transaction/Transfer 自然日筛选边界。
4. 替换 Maintenance audit 的 `datetime-local` 边界。
5. 检查所有 `new Date()`、`toISOString()` 和 `getTimezoneOffset()` 使用点，保留只处理真实 instant 的调用。

验收：浏览器时区与用户偏好时区不同时，输入、回显、详情展示、筛选请求和月份归属一致；月末、年末、DST 跳变和半小时/四十五分钟 offset 时区均有测试。

### 4.6 R06 展示格式与默认报表周期

开发内容：

1. 让所有金额展示显式接收 `locale + numberFormat`，更新表格、详情、Dashboard、报表、维护结果和图表 Tooltip。
2. 完成 V11、UserPreference 全链路和 Settings 自定义月份控件。
3. Reports 在没有有效 route query 时按用户偏好生成默认月份；显式 URL 参数始终优先。
4. locale、timezone、numberFormat、base currency 或默认周期改变时，只失效受影响的展示与报表投影。

验收：三种数字格式在正数、负数、零、8 位小数和大整数上结果准确；五种默认报表周期均产生正确范围；刷新和深链接保持确定性。

### 4.7 R07 元数据 mutation queue

开发内容：

1. 以 FIFO queue 替换 `beginAction()` 中的 superseding abort。
2. 增加会话 generation、当前 controller、队列 pending 和结果不确定后的 reload。
3. 设置页统一接入 pending 状态，防止并发提交与重复点击。
4. 增加分类与标签交叉操作、首个已提交后第二个失败、会话中途清理和 Idempotency-Key 复用测试。

验收：操作按点击顺序到达服务端；任何完成/失败/abort 组合后，当前会话页面都能收敛到服务端权威状态；用户切换不会写回上一用户结果。

### 4.8 R08 高风险资源边界

开发内容：

1. 为现金流、报表分析、CSV 和历史汇率增加跨度、条目、标签展开量与响应字节回归测试；先固定超限错误契约。
2. 将现金流趋势改为 PostgreSQL 侧按用户时区自然月分桶，只返回固定数量的月度投影。
3. 将报表 breakdown 改为 SQL 聚合或有界增量聚合；CSV 改为流式/分块输出，并设置独立的最大行数与最大输出字节数。
4. 历史汇率只查询每个业务时间桶所需的最近有效汇率，禁止加载窗口内全部小时快照。
5. 为登录/注册增加独立 Argon2 并发闸门及低基数速率限制，保持不存在账号的等成本验证。
6. HTTP worker queue 在复制 body 前执行任务数和累计 payload 字节准入；任务完成、拒绝与异常路径必须准确归还预算。
7. 将所有资源配置限制在经过验证的安全范围，并在配置示例与 Operations 指标中公开当前容量和拒绝计数。

验收：最大合法请求的峰值 RSS 在确定预算内；超限请求稳定返回 `413`、`422` 或 `429` 类错误且服务保持可用；认证洪泛不能占满普通业务 worker；队列累计 body 不超过配置预算。

### 4.9 R09 查询放大与数据生命周期

开发内容：

1. 将历史净值的逐月 `balances_at()` 改为一次按月批量查询。
2. 将转账列表的 group 明细改为单次批量加载，消除每页 `1 + N` SQL。
3. 为 Dashboard 与净值提供跨账户批量余额投影，禁止按账户独立开启事务。
4. 为账户、分类和标签增加跨 Domain、Application、API 与数据库一致的用户级数量配额。
5. 使用 parent 索引一次构建分类树，消除 `O(N^2)` 扫描。
6. 为已发布 Outbox、handler receipt 和幂等记录建立可观测、可批量、安全重试的 retention job；Operations 统计使用有界窗口或预聚合。
7. 文件日志改为大小和文件数受限的 rotating sink，保留 stderr 默认行为。

验收：转账页、Dashboard、净值和历史报表的 SQL 次数不随页面项数、账户数或月份数线性增长；清理任务不删除未发布/未完成记录且可幂等重跑；磁盘占用有确定上限。

### 4.10 R10 前端长期会话资源治理

开发内容：

1. 合并 Reports 首次 route 规范化与加载，只发起一个有效请求。
2. Transaction/Transfer Store 增量维护 ID 索引并限制驻留页数；长列表使用窗口化渲染或等价的有界 DOM 方案。
3. 按 `locale + numberFormat + currency + timeZone + options` 缓存 `Intl` formatter，并在偏好切换时复用有限键空间。
4. 审计所有 watcher、事件监听器、图表、observer、Object URL、timer 和请求控制器的 mount/unmount 生命周期。
5. 对路由往返、连续 `loadMore`、偏好反复切换和大列表滚动执行浏览器 heap 回归。

验收：长列表驻留对象和 DOM 节点有固定上限；重复进入/离开页面后 heap 能回落；首次 Reports 不重复请求；formatter 构造次数不与单元格数量同阶增长。

### 4.11 R11 动态内存与压力验证

开发内容：

1. 在 Linux GCC/Clang 上执行 ASan、UBSan 与核心测试，覆盖 Decimal、报表、认证、EventBus、Outbox 和 HTTP adapter。
2. 使用真实 PostgreSQL 数据规模 fixture 对关键 SQL 执行 `EXPLAIN (ANALYZE, BUFFERS)`，记录计划、查询次数与索引命中。
3. 对认证、队列饱和、CSV、120 个月趋势、最大标签集合和客户端断连执行并发测试，采集峰值 RSS、CPU、拒绝率和恢复时间。
4. 使用浏览器 heap snapshot 验证长列表、图表和路由生命周期。
5. 将可重复基准脚本纳入 `tools/` 或测试套件，测试数据不包含真实用户信息。

验收：无 sanitizer 告警；所有场景满足计划中固定的资源预算；压力解除后 worker、队列、RSS 与浏览器 heap 回落到允许基线；关键 SQL 不出现未接受的顺序扫描或线性往返。

### 4.12 R12 全量回归与文档收口

开发内容：

1. 执行后端 Debug/Release、PostgreSQL OFF/ON、Flyway、RLS、Runtime、ASan 和 UBSan。
2. 执行前端 quality、bundle、安全、Vitest、Playwright 三浏览器和 Accessibility。
3. 使用“浏览器时区 != 用户偏好时区”以及两种 locale 运行关键 E2E。
4. 回写 Architecture、OpenAPI、测试策略、Phase 2 结果文档和当前测试基线。
5. 将本计划中的长期规则改写到事实来源后删除本文件，不保留整改过程叙事。

验收：第 6 节全部门禁通过，性能基线与安全上限进入测试和运维事实来源，工作区无临时报告、生成漂移、秘密或未归档任务文件。

---

## 5. 推荐提交边界

每个提交保持可构建、可回滚，建议至少使用以下边界：

1. `test: add post-phase2 audit regression coverage`
2. `fix: enforce Decimal safe storage invariant`
3. `feat: establish typed frontend localization`
4. `feat: complete English and Chinese product copy`
5. `feat: initialize and select IANA timezones`
6. `fix: interpret ledger time in user timezone`
7. `feat: apply number and report period preferences`
8. `fix: serialize metadata mutations`
9. `fix: bound report and request resource usage`
10. `perf: batch ledger projections and retain operations data`
11. `perf: bound frontend long-session resources`
12. `test: add memory and saturation regression gates`
13. `docs: record post-phase2 remediation results`

涉及 migration、OpenAPI 和对应实现的提交不得拆成会留下永久不一致状态的独立提交。所有 commit 按项目规则使用无密码签名子密钥。

---

## 6. 验证矩阵

| 层次 | 必须覆盖 |
| ---- | -------- |
| Decimal Unit | 安全范围边界、raw factory、加减乘除、abs/negated、字符串、NUMERIC fit、Half-Even |
| Application Unit | 注册时区、locale fallback、NumberFormat、自定义报表月份、无效组合 |
| PostgreSQL Integration | V10 -> V12、空库 V1 -> V12、两种 locale 模板、Preference round-trip、失败回滚 |
| API | `preferredTimezone`、时区列表、Preference 新字段、字段错误、OpenAPI generated types |
| Frontend Unit | i18n key、locale 切换、数字格式、报表周期、IANA/DST 转换、mutation queue |
| Component | searchable timezone combobox、custom period fields、pending/disabled、localized ARIA/error |
| Browser E2E | `UTC` browser + `Asia/Shanghai` preference，以及反向组合；`zh-CN` / `en-US` 全关键路径 |
| Runtime | Web 注册得到浏览器时区、真实 PostgreSQL 偏好、Transaction/Transfer 时间 round-trip |
| Static/Security | 无裸文本、无 Token 持久化、生成无漂移、bundle/依赖/许可证/source map/secret 门禁 |
| Query Scale | 120 个月现金流/净值、最大转账页、跨账户 Dashboard、长期汇率窗口的固定 SQL 次数和 `EXPLAIN` |
| Resource Limits | 报表行/字节/标签预算、HTTP 队列任务/字节预算、用户资源配额、超限错误契约 |
| Saturation | Argon2 并发与限流、队列饱和、客户端断连、压力解除后的 worker/RSS 恢复 |
| Retention | Outbox/receipt 清理边界、幂等重跑、未完成记录保护、日志文件轮转上限 |
| Frontend Memory | 长列表窗口、路由往返、图表销毁、formatter cache 和 heap 回落 |
| Sanitizer | Linux GCC/Clang ASan/UBSan 覆盖 Decimal、核心 Domain/Application、报表、认证和异步基础设施 |

三浏览器测试至少覆盖 Chromium、Firefox 和 WebKit。时区测试不得依赖执行机器本地时区，必须显式设置浏览器 `timezoneId` 和 API 偏好 fixture。

---

## 7. 完成定义

只有同时满足以下条件，本轮整改才可结束：

1. 原有五项功能审计问题与本计划第 2.7 节全部性能/内存问题均有实现、自动化回归测试和当前架构规则。
2. `zh-CN` 与 `en-US` 所有静态产品文本、ARIA 和错误状态完整可切换。
3. 浏览器时区与用户偏好不同时，账务时间和筛选仍保持业务语义正确。
4. Preference 中不存在保存后无效果的字段或无法表达的数据组合。
5. Decimal 对全部可构造值无 signed overflow，UBSan 无相关报告。
6. 元数据重叠写入与会话切换后不会留下跨用户或陈旧页面状态。
7. 报表、认证、HTTP 队列、用户资源、后台表和前端长会话均有明确且经过压力验证的资源上限。
8. 关键读路径的 SQL 次数不会随账户数、月份数或页面明细数形成 `N+1`，真实 PostgreSQL 执行计划已复核。
9. Windows 本机门禁通过，并在 Linux/macOS 目标环境完成 PostgreSQL ON、Docker、三浏览器、压力和 sanitizer 复核。
10. 文档已按单一事实来源收口，本临时计划已删除或归档为简短结果投影。
