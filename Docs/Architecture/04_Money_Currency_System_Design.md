# Personal Finance Hub - Money & Currency System Design

Version: 1.0  
Backend: C++23  
Architecture: Clean Architecture + Lightweight DDD

---

## 1. 目的

本文档定义系统的金融原语层。这是整个项目最关键的部分，因为以下所有模块都依赖于它：

- Account
- Transaction
- Transfer
- ExchangeRate
- Report
- Dashboard
- Net worth calculation
- Historical asset reconstruction

设计目标：

- 不使用二进制浮点数表示金额
- 强类型
- 清晰的货币边界
- 历史汇率支持
- 一致的转账构造
- 可预测的舍入行为
- 未来数据来源扩展性

---

## 2. 核心设计原则

1. 金钱不是数字
2. 货币是必需的
3. 跨货币运算必须显式
4. 历史汇率必须保留
5. 转账必须能从三个值中的两个构造
6. 手续费、折扣、返利、损失是调整，不是特殊处理
7. 报表使用基准货币
8. 精度损失必须受控且可见

---

## 3. Decimal 设计

## 3.1 为什么需要 Decimal

金融数值绝不能使用 float 或 double，因为二进制浮点数无法精确表示许多十进制分数。

例如：0.1 + 0.2 != 0.3，这对金钱来说是不可接受的。

---

## 3.2 Decimal 表示方法

推荐方法：

- 使用定点十进制类型
- 或使用多精度十进制库
- 或实现特定领域的整数缩放十进制

对本项目最实用的选项：

- 以整数形式存储值，保持固定标度
- 在类型中明确标度

概念模型示例：

```cpp
class Decimal
{
private:
    __int128_t value; // 缩放后的 128 位整数（跨平台可使用 boost::multiprecision::int128_t）
    int32_t scale;    // 小数点后位数（统一为 10）
};
```

示例：

- 12.34 标度为 2 变为 1234
- 0.0001 标度为 4 变为 1

---

## 3.3 Decimal 职责

Decimal 应支持：

- 加法
- 减法
- 乘法
- 除法
- 比较
- 规范化
- 舍入

Decimal 不应该知道任何关于货币的信息。

---

## 3.4 Decimal 规则与边界

1. **运算精度**：运算必须保留尽可能多的精度。
2. **舍入规则（Rounding Mode）**：金融计算中，除法和乘法（如汇率折算）会产生超出 10 位小数的数值。系统**统一采用银行家舍入法（Half-Even Rounding / Round Half to Even）**作为默认的舍入规则，而不是简单的四舍五入，以减小累计误差。
3. **除法约束**：除法必须显式定义舍入模式。
4. **标度不匹配**：标度不匹配必须确定性处理。
5. **负值支持**：必须允许负值，因为存在冲销和更正。
6. **数值边界**：
   * 数据库中金额字段定义为 `NUMERIC(20,8)`，最大可表示 $999,999,999,999.99999999$。
   * 数据库中汇率字段定义为 `NUMERIC(20,10)` 和 `DECIMAL(30,10)`。
   * 为了统一支持金额（8位小数）和汇率（10位小数）的无损计算，C++ 的 `Decimal` 底层**必须支持至少 10 位小数的精度**。
   * 若使用 `int64_t` 配合 $10^{10}$ 的缩放，其最大值仅约为 $922.33$（922），这对于金额计算是完全不够的。
   * 因此，**`Decimal` 底层必须使用 `__int128_t`（或 `boost::multiprecision::int128_t`）配合 $10^{10}$ 的缩放**。这可以提供极大的数值范围，同时满足金额和汇率的精度要求，避免在跨币种换算时产生累计本金偏差。

---

## 4. 货币设计

## 4.1 货币作为值对象

货币标识金额的单位。

示例：

- CNY
- USD
- EUR
- JPY
- HKD

货币必须不可变。

---

## 4.2 货币表示

推荐表示方法：

```cpp
class Currency
{
private:
    std::string code;
};
```

或使用更强类型的 ISO-4217 代码包装器。

---

## 4.3 货币职责

货币应支持：

- 代码验证
- 相等比较
- 显示格式化
- 报告中基准货币指定

货币不应支持算术运算。

---

## 4.4 货币规则

1. 货币代码必须是有效的 ISO-4217
2. 货币对象应该不可变
3. 货币比较必须精确
4. 货币显示可以本地化，但内部代码必须保持稳定

---

## 4.5 Currency Metadata

`Currency` 只表示稳定的货币代码，不承载展示属性。
前端展示、金额格式化和不同货币精度由 `CurrencyMetadata` 提供。

```cpp
class CurrencyMetadata
{
private:
    Currency currency;
    std::string displayName;
    std::string symbol;
    int32_t precision;
    bool isCrypto;
};
```

示例：

| code | symbol | precision | displayName  | isCrypto |
| ---- | ------ | --------- | ------------ | -------- |
| USD  | $      | 2         | US Dollar    | false    |
| CNY  | ¥      | 2         | Chinese Yuan | false    |
| JPY  | ¥      | 0         | Japanese Yen | false    |
| BTC  | ₿      | 8         | Bitcoin      | true     |
| ETH  | Ξ      | 8         | Ethereum     | true     |

规则：

1. `Currency.code` 是计算和持久化的稳定标识
2. `CurrencyMetadata.precision` 决定默认展示小数位，不决定底层存储精度
3. 金额存储仍使用统一 Decimal 标度，展示时再按 metadata 舍入
4. `symbol` 和 `displayName` 只用于 UI 和导出
5. 加密货币可使用非 ISO-4217 代码，但必须进入受控白名单
6. Repository 应提供只读查询接口，前端可以缓存元数据

---

## 5. 金钱设计

## 5.1 金钱作为值对象

金钱是核心值对象。它结合了：

- 金额
- 货币

示例：

```cpp
class Money
{
private:
    Decimal amount;
    Currency currency;
};
```

---

## 5.2 金钱职责

金钱应支持：

- 值访问
- 相同货币加减
- 比较
- 取反
- 零检查
- 格式化
- 货币安全验证

---

## 5.3 金钱运算规则

允许：

```text
100 USD + 50 USD
```

不允许：

```text
100 USD + 100 CNY
```

跨货币运算必须通过转换进行。

---

## 5.4 金钱 API 概念

可能的接口：

```cpp
class Money
{
public:
    const Decimal& amount() const;
    const Currency& currency() const;

    bool isZero() const;
    Money negated() const;

    Money add(const Money& other) const;
    Money subtract(const Money& other) const;

    bool operator==(const Money& other) const;
    bool operator<(const Money& other) const;
};
```

实现必须在算术之前验证货币匹配。

---

## 5.5 金钱规则

1. 金钱不可变
2. 金钱总是包含货币
3. 算术需要货币匹配
4. 比较需要货币匹配
5. 任何转换都必须显式

---

## 6. 汇率设计

## 6.1 汇率作为值对象

汇率描述两种货币之间的转换关系。

示例：

- 1 USD = 7.18 CNY

推荐模型：

```cpp
class ExchangeRate
{
private:
    Currency base;
    Currency target;
    Decimal rate;
    Timestamp fetchedAt;
    std::string provider;
};
```

### Provider 示例

- Manual
- ECB
- Frankfurter
- OpenExchangeRates
- CurrencyLayer

---

## 6.2 方向约定

方向必须明确。使用一个稳定的约定：

```text
1 base currency = rate target currency
```

示例：

```text
1 USD = 7.18 CNY
```

这避免了歧义。

---

## 6.3 汇率职责

汇率应支持：

- 方向清晰
- 转换因子存储
- 来源跟踪
- 时间戳跟踪

---

## 6.4 汇率规则

1. 永远不要仅存储没有方向的数字
2. 汇率必须包含来源和时间戳
3. 历史汇率不能被覆盖
4. 系统必须能够为给定时间选择正确的汇率

---

## 7. 历史汇率策略

## 7.1 为什么需要历史存储

报告和转账重建需要交易时有效的汇率。

示例：

- 2026-01-01，USD/CNY = 7.10
- 2026-06-01，USD/CNY = 7.25

如果用户稍后查看 2026-01 余额，旧汇率仍必须可用。

---

## 7.2 存储规则

汇率记录仅追加。当新汇率到达时，不要更新旧记录。相反，插入新快照。

---

## 7.3 汇率选择策略

转换交易时，系统应能使用以下策略之一：

1. 最新可用汇率
2. 手动指定汇率
3. 交易时汇率
4. 反向计算得出的汇率
5. 选定提供商快照中的汇率

确切的策略是使用情况的一部分，不是值对象本身的一部分。

---

## 7.4 历史汇率查询规则

按历史时间查询汇率时，必须选择“小于等于目标时间的最新一条”。

概念规则：

```sql
WHERE fetched_at <= target_time
ORDER BY fetched_at DESC
LIMIT 1
```

不能简单读取最新插入的汇率记录。
历史报表、转账重建和余额回放都必须按目标时间选择汇率。

---

## 8. 基准货币设计

## 8.1 Why Base Currency Exists

为什么需要基准货币：

用户可能持有多种货币的资产。

示例：

- 1000 USD
- 10000 CNY
- 50000 JPY

仪表板和净资产无法在没有公共单位的情况下显示。

因此报告需要基准货币。

报表需要基准货币来统一计算。

---

## 8.2 基准货币定义

基准货币是用于聚合和报告的货币。

示例：

- CNY
- USD

基准货币可由用户选择。

---

## 8.3 基准货币存储

系统应支持用户级别的偏好设置：

```cpp
class UserPreferences
{
private:
    Currency baseCurrency;
    std::string locale;
    std::string timezone;
    std::string dateFormat;
    std::string numberFormat;
    ThemeMode theme;
    HomePage defaultHomePage;
    ReportPeriod defaultReportPeriod;
};
```

领域层应引入 `UserPreference` 领域概念，或将其作为 `User` 聚合的一部分。
所有全局报表和净值计算必须从该领域概念读取准确的 Base Currency。

持久化层保留 `users.base_currency_code CHAR(3)` 作为基础默认值，同时使用 `user_preferences` 保存扩展偏好。
`UserPreference` 不要求一一对应单一数据表，Repository 负责从 `users` 和 `user_preferences` 组合映射。

偏好字段用途：

- `baseCurrency`: 报表、Dashboard 和净值折算基准
- `locale`: 前端语言与本地化
- `timezone`: 日期边界和默认报表周期计算
- `dateFormat`: 日期展示
- `numberFormat`: 数字展示
- `theme`: 前端主题
- `defaultHomePage`: 登录默认首页
- `defaultReportPeriod`: 报表默认时间范围

---

## 8.4 基准货币规则

Base Currency only affects:

- Reports
- Dashboard
- Statistics

It never changes:

- Account Currency
- Transaction Currency
- Transfer Currency

具体规则：

1. 基准货币仅用于展示和统计，不得修改原始货币设定
2. 基准货币用于报告，不用于存储原始交易
3. 基准货币转换应在可能时使用历史汇率
4. 报告必须说明使用了哪种基准货币
5. 基准货币应该用户可配置
6. Net Worth 等全局汇总必须从 UserPreference/User 聚合读取基准货币

---

## 9. 货币转换设计

## 9.1 转换目标

以可预测的方式将金钱从一种货币转换为另一种。

示例：

```text
100 USD -> 718 CNY
```

---

## 9.2 转换结果

转换结果应包含：

- 源金钱
- 目标金钱
- 使用的汇率
- 舍入损失（如有）
- 转换时间戳

概念上：

```cpp
class ConversionResult
{
private:
    Money source;
    Money target;
    ExchangeRate rateUsed;
    Decimal roundingDifference;
    std::string provider;
};
```

`provider` 字段用于后期审计。

---

## 9.3 转换规则

1. 转换必须始终记录使用的汇率
2. 舍入必须显式
3. 转换应保留可追溯性
4. 转换应是确定的

---

## 10. 转账设计

转账不是单个原始金额字段。它是一个业务过程。

设计目标：

- 用户输入三个值中的两个
- 系统推导出第三个
- 保证一致性

三个值分别是：

1. 出账金额
2. 入账金额
3. 汇率

---

## 11. 转账聚合体

## 11.1 聚合体概念

转账应建模为一个聚合体，包含：

- 出账交易
- 入账交易
- 可选汇率
- 调整交易

推荐形状：

```cpp
class TransferAggregate
{
public:
    Transaction outgoing;
    Transaction incoming;
    std::optional<ExchangeRate> rate;
    std::vector<Transaction> adjustments;
};
```

---

## 11.2 同币种转账

示例：

- 银行 A 发送 1000 CNY
- 银行 B 接收 1000 CNY

出账和入账具有相同金额和相同货币。这是最简单的情况。

---

## 11.3 跨币种转账

示例：

- 出账：1000 USD
- 入账：7180 CNY
- 汇率：7.18

转账聚合体必须在选定的汇率下保证一致性。

---

## 11.4 转账计算模式

系统应支持三种模式。

### 模式 A：出账 + 汇率 => 入账

用户提供：

- 出账金额
- 汇率

系统推导入账金额。

示例：

```text
1000 USD + 7.18 = 7180 CNY
```

---

### 模式 B：出账 + 入账 => 汇率

用户提供：

- 出账金额
- 入账金额

系统推导汇率。

示例：

```text
1000 USD and 7170 CNY => 7.17
```

---

### 模式 C：入账 + 汇率 => 出账

用户提供：

- 入账金额
- 汇率

系统推导出账金额。

示例：

```text
7170 CNY and 7.17 => 1000 USD
```

---

## 11.5 转账构建器

专用构建器可使 API 更清晰。

概念示例：

```cpp
class TransferBuilder
{
public:
    TransferBuilder& outgoing(const Money& money);
    TransferBuilder& incoming(const Money& money);
    TransferBuilder& rate(const ExchangeRate& rate);

    TransferAggregate build();
};
```

构建器应验证是否存在足够的数据来推导缺失部分。

---

## 11.6 转账一致性规则

转账聚合体必须确保：

- 两方在选定规则下是平衡的
- 舍入行为是受控的
- 任何差异都表示为调整

---

## 12. 调整设计

## 12.1 为什么需要调整

真实的转账可能涉及：

- 手续费
- 折扣
- 返利
- 价差损失
- 平台补偿
- 银行补贴

这些不应混入主转账金额中。

---

## 12.2 调整作为交易

调整应由具有特殊语义角色的普通交易实体表示。

示例：

- 费用交易
- 返利交易
- 损失交易

转账中的手续费或汇兑损耗必须作为独立的 Adjustment 交易。
也可以按业务要求归类为明确的 Expense 类别，例如“金融手续费”。
不得隐藏在转账主金额中。

---

## 12.3 调整规则

1. 调整必须可审计
2. 调整必须在相关时与转账关联
3. 调整不能隐藏在原始金额字段内
4. 根据业务含义，调整可影响转账的任何一方

---

## 13. 舍入设计

## 13.1 为什么舍入很重要

跨币种转换常产生循环小数。

示例：

```text
100 USD / 7.123 = 14.037...
```

系统必须决定如何舍入。

---

## 13.2 舍入策略

舍入必须显式且可配置。

```cpp
enum class RoundingMode
{
    HalfEven,
    HalfUp,
    Floor,
    Ceil,
    Truncate
};
```

推荐的舍入策略：

- 四舍五入（round half up）
- 银行家舍入（round half even）
- 截断（truncate）
- 向下取整（floor）
- 向上取整（ceil）

对于财务报告，`银行家舍入` 通常是一个好默认值，但最终选择应与产品目标保持一致。

---

## 13.3 舍入规则

1. 舍入必须被记录或至少可重现
2. 不同操作只有在明确记录时才能使用不同策略
3. 相同输入在相同策略下必须始终产生相同输出

---

## 13.4 TransferMode 定义

```cpp
enum class TransferMode
{
    OutgoingAndRate,
    OutgoingAndIncoming,
    IncomingAndRate
};
```

用于：TransferBuilder、TransferAggregate、TransferGroup 统一三份文档。

TransferAggregate 是领域层的转账聚合，TransferGroup 是持久化/元数据承载体，用来存模式、汇率、快照时间等。
TransferGroup 不是业务实体，不应建成 Domain Entity。

---

## 14. 精度和标度设计

## 14.1 金额标度

项目应支持以下精度：

- 法定货币
- 加密货币未来扩展
- 汇率乘法

实用默认值：

- 金额标度：8 位小数
- 汇率标度：10 位小数

---

## 14.2 精度规则

1. 存储具有足够精度的原始值
2. 仅在必要边界处舍入
3. 不要在转换中无声丢失精度

---

## 15. 报告货币规则

报告必须遵循这些规则：

1. 每个报告有一个基准货币
2. 报告中的所有金额必须转换为该基准货币
3. 相关时报告结果必须包含汇率策略或快照基础
4. 历史报告必须使用历史汇率

---

## 16. 净值计算

净值计算：

净值是所有账户余额转换为基准货币后的总和。

概念上：

```text
Net Worth = Σ(convert(account_balance_i, base_currency))
```

要求：

- 在UI中使用账户余额，不使用原始交易和
- 当报告日期是历史日期时使用历史汇率
- 根据报表设置包括所有活跃和已归档的账户
- 基准货币必须来自 UserPreference/User 聚合

---

## 17. API 使用情况含义

这个设计暗示几个应用层使用情况：

- 创建金钱
- 验证货币
- 转换金钱
- 创建同币种转账
- 创建跨币种转账
- 从金额推导汇率
- 从汇率推导金额
- 计算报告总计
- 重建余额快照

这些应该在应用层，而不是值对象内部。

---

## 18. 建议的类摘要

### 值对象

- Decimal
- Currency
- CurrencyMetadata
- Money
- ExchangeRate
- ConversionResult

### 实体

- User
- Account
- Transaction
- Category

### 聚合体

- TransferAggregate

### 领域服务

- CurrencyConversionService
- TransferDomainService
- BalanceCalculationService

### 应用用例

- CreateTransactionUseCase
- CreateTransferUseCase
- DeleteTransactionUseCase
- GenerateMonthlyReportUseCase
- RefreshExchangeRateUseCase

### 构建器

- TransferBuilder

---

## 19. 未来扩展点

未来可能添加的内容：

- 汇率提供商插件
- 多个汇率来源
- 汇率置信度分数
- 汇率质量排名
- 加密货币定价源
- 税批次货币逻辑
- 银行费用分类
- 交割日期与交易日期支持

---

## 20. 最终规则

1. 金钱总是包含货币
2. 货币是不可变的
3. Decimal 永不使用 float 或 double
4. 跨货币数学运算需要汇率
5. 转账必须能从三个值中的两个构造
6. 调整是一流的概念
7. 历史汇率仅限追加
8. 报告始终使用基准货币
9. 舍入必须显式
10. 转换必须可重现
