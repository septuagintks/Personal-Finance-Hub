# Phase 1 S05 金融原语 - 交付记录

**阶段**: P1-S05 金融原语
**状态**: 已完成

---

## 1. 概述

根据 `Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md` 的 P1-S05 规划，
本阶段实现金融系统最核心的不变量：金额、货币、汇率与货币折算。所有实现遵循
`Docs/Architecture/04_Money_Currency_System_Design.md` 与
`Docs/Architecture/08_Exchange_Rate_System_Design.md` 的强约束：

- 金额与汇率绝不使用二进制浮点数
- `Decimal` 底层使用 128 位整数 + 固定标度 `10^10`
- 默认舍入策略统一为银行家舍入 (Half-Even)
- 跨币种运算必须显式，缺失汇率明确报错，不用默认值 `0`/`1`
- 汇率方向明确 (`1 base = rate target`)，反向汇率可重现，USD 为三角折算枢纽

---

## 2. 交付物

| 组件                      | 头文件                                             | 实现                                         | 测试                                              |
| ------------------------- | -------------------------------------------------- | -------------------------------------------- | ------------------------------------------------- |
| Decimal                   | `include/pfh/domain/decimal.h`                     | `src/domain/decimal.cpp`                     | `tests/unit/decimal_test.cpp`                     |
| Currency                  | `include/pfh/domain/currency.h`                    | `src/domain/currency.cpp`                    | `tests/unit/currency_test.cpp`                    |
| Money                     | `include/pfh/domain/money.h`                       | `src/domain/money.cpp`                       | `tests/unit/money_test.cpp`                       |
| ExchangeRate              | `include/pfh/domain/exchange_rate.h`               | `src/domain/exchange_rate.cpp`               | `tests/unit/exchange_rate_test.cpp`               |
| CurrencyConversionService | `include/pfh/domain/currency_conversion_service.h` | `src/domain/currency_conversion_service.cpp` | `tests/unit/currency_conversion_service_test.cpp` |
| 领域错误                  | `include/pfh/domain/domain_error.h`                | (header-only)                                | 覆盖于各测试                                      |

均编入 `pfh_domain` 静态库，无框架依赖。

---

## 3. 各原语要点

### 3.1 Decimal

- `__int128` 存储 `真实值 * 10^10`，标度固定 10 位，满足金额 8 位 + 汇率 10 位无损计算。
- `parse()` 支持正负号、前导 `+`、空白裁剪；拒绝空串、非法字符、多小数点、仅符号。
- `to_string()` 去除尾部多余零。
- `add/subtract/multiply/divide` 全部返回 `DomainResult`，显式暴露溢出与除零。
- 统一 Half-Even 舍入：解析截断、乘法降标、除法。
- 溢出保护：加法符号翻转检测、乘法反向除法校验、`__int128` 范围守卫。
- 底层采用原生 `__int128_t`（维护者确认，架构文档 5.1 待决策项）；`-pedantic` 警告通过
  `__extension__` 与 `#pragma GCC/clang diagnostic ignored "-Wpedantic"` 局部抑制。

### 3.2 Currency

- 不可变值对象，仅承载稳定的 3 字母代码；展示属性归 CurrencyMetadata（后续阶段）。
- `create()` 校验形状（3 个字母）并大写规范化，校验受支持代码。
- 支持 ISO-4217 法币子集（当前 20 个常用币种）与受控加密货币白名单（BTC/ETH/USDT）。
- 不支持任何算术运算；提供 `==`、`<=>` 精确比较与 `pivot_code()`（USD）。

### 3.3 Money

- 绑定 `Decimal` 金额 + `Currency`，不可变。
- 同币种 `add/subtract` 正常；跨币种加减/比较返回 `CurrencyMismatch` 错误。
- `multiply(Decimal)` 标量乘法保留币种（用于费率/因子），跨币种折算须走 ExchangeRate。
- 允许负余额（信用账户场景），`negated/is_zero/is_negative/is_positive` 齐备。
- `to_string()` 输出 `"<amount> <CODE>"`（如 `12.34 USD`）。

### 3.4 ExchangeRate

- 值对象，无身份标识；方向约定 `1 base = rate target`。
- `create()` 校验 base≠target、rate>0（零/负汇率一律拒绝）。
- `inverse()` 反向汇率 `1/rate`，保留时间戳，来源标记 `+inverse`。
- `convert(Money)` 要求输入为 base 币种，输出 target 币种 Money。
- 携带 `fetched_at` 时间戳与 `source` 来源，为历史汇率与审计留出字段。

### 3.5 CurrencyConversionService

- 纯内存领域服务：**不访问 Repository、不打开事务、不发布事件**（见下方设计说明）。
- `cross_rate()` USD 枢纽三角折算：已知 `USD->base`、`USD->target`，推导
  `base->target = r_target / r_base`，取较晚时间戳，来源标记 `TriangularCalculation`；
  两输入非 USD 基准时报错。
- `convert()` 便捷方法，对 `ExchangeRate::convert` 的封装。

---

## 4. 关键设计说明

### 4.1 领域服务不依赖 Repository

架构文档 `01_Technical_Architecture.md` 与 Phase 1 计划 P1-S06 验收标准均要求
**Domain Service 不访问 Repository、不打开事务、不发布事件**。而汇率设计文档
`08_Exchange_Rate_System_Design.md` 示例中的 `findOrCalculateRate(..., IExchangeRateRepository&)`
带有仓储依赖。

按"文档冲突时优先架构文档"的规则，`CurrencyConversionService` 在 P1-S05 只实现**纯计算**
（三角折算、反向、转换）。基于 Repository 的多级降级查询链（直接→逆向→三角→历史）属于
**应用层**职责，待 `IExchangeRateRepository` 就绪（P1-S08）后在应用层查询服务中实现。
此偏差已在此记录，后续检查 S01-S05 时可据此核对是否需回写架构文档示例。

### 4.2 领域错误分层

`DomainError`/`DomainResult` 位于 `domain/domain_error.h`，Domain 层不反向依赖 Application 层。

---

## 5. 验证结果

依赖通过 CMake FetchContent 自动拉取（spdlog 1.14.1、nlohmann_json 3.11.3、GoogleTest 1.15.2）。

```text
cmake configure: 通过（GNU 16.1.0, C++23, Debug）
cmake build:     通过（-Wall -Wextra -Werror -pedantic），无警告
ctest:           116 个单元测试全部通过（S05 新增 45 个）
Total Test time: ~5.1 sec
pfh_domain:      独立静态库，不链接 spdlog/框架
```

金融关键用例验证：

- `0.1 + 0.2` 精确等于 `0.3`
- `1000 USD * 7.18 = 7180`
- `1 / 3 = 0.3333333333`（循环小数 Half-Even）
- 同币种 Money 可加减，跨币种加减/比较报错
- `USD->CNY 8` 反向为 `CNY->USD 0.125`，二次反向回到 `8`
- 三角折算 `7.18 / 0.92 = 7.8043478261`（Half-Even），`8 / 0.5 = 16`
- 三角折算取较晚时间戳；非枢纽输入报错

---

## 6. 验收对照 (P1-S05)

- [x] 不使用 `float` 或 `double` 表示金额和汇率
- [x] `Money` 跨币种直接加减测试失败（返回 CurrencyMismatch）
- [x] 缺失汇率不使用 `0`/`1` 默认值；汇率/折算错误显式返回
- [x] 金融原语单元测试覆盖正常路径、边界路径和错误路径
- [x] `Currency` 校验受控支持的 ISO-4217 法币子集与加密货币白名单，内部代码不可变
- [x] `ExchangeRate` 方向明确，反向汇率可重现，保留时间戳与来源
- [x] `CurrencyConversionService` 支持直接汇率、反向汇率、USD 枢纽三角折算、缺失汇率错误
- [x] Domain 层不依赖外层，领域服务不访问 Repository

---

## 7. 遗留与后续

- **JSON 金额字符串边界**（tasks #18 后半）：`Money` 值对象已完成；"JSON 金额只通过字符串
  进出"属于 API/DTO 边界约束，在 P1-S10 表现层实现与测试。
- **领域服务单元测试**（tasks #12）：金融原语与 `CurrencyConversionService` 已覆盖；
  `TransferDomainService`、`BalanceCalculationService` 属 P1-S06，届时补齐后再勾选 #12。
- **CurrencyMetadata**：展示用元数据（符号、精度、名称）随前端/展示需求在后续阶段引入。
- **Repository 降级查询链**：待 P1-S08 在应用层实现（见 4.1）。

对应 `tasks.md` 已勾选：#16、#17、#18、#19、#20。

---

## 7.1 S05 复查修复记录

对 S05 全部产物复查后修复 6 项（复查日期 2026-07-07）：

1. **[中] `USDT` 无法创建 — 死白名单条目**：形状检查硬拒绝非 3 字符，但白名单含 4 字母
   `USDT`，实测不可达。改为加密代码接受 3-5 字母（法币仍严格 3 字母），白名单补 `USDC`/`WBTC`。
2. **[低] `checked_mul` signed `__int128` 乘法溢出是 UB**：改在无符号域计算并范围检查后再
   转回有符号，含 INT_MIN 边界处理。
3. **[低] `add`/`subtract`/`multiply`/`divide`/`from_integer` 误标 `noexcept`**：错误路径构造
   含 `std::string` 的 `DomainError` 可能分配抛异常，去掉 `noexcept`。
4. **[低] `inverse()` 源标记累加**：`ECB+inverse+inverse` → 改为切换后缀，二次反向还原为 `ECB`。
5. **[低] 测试覆盖缺口**：补 `from_scaled`/`raw_value` 往返、multiply 范围与中间积溢出、
   Money multiply 溢出传播、ExchangeRate convert 溢出传播、双次 inverse 源还原、
   4/5 字母加密币与超长/未知码。
6. **[记录] multiply 中间积范围限制**：操作数各带 `10^10` 缩放，中间积 `lhs*rhs` 携带
   `scale^2`，两个大操作数（等量级约 >1.3e9）会溢出 `__int128`，即便真实结果可表示。
   Phase 1 金额×汇率的实际量级够用；如需更大范围，后续可引入 256 位中间积或先除后乘。

复查后验证：142 个单元测试全部通过，构建 `-Werror -pedantic` 无警告。

---

## 8. 参考文档

- [金额与货币系统设计](../Architecture/04_Money_Currency_System_Design.md)
- [汇率系统设计](../Architecture/08_Exchange_Rate_System_Design.md)
- [技术架构](../Architecture/01_Technical_Architecture.md)
- [Phase 1 详细开发计划](../Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md)
- [任务跟踪](tasks.md)
