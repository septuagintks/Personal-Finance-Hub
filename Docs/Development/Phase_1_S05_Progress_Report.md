# Phase 1 S05 金融原语 - 进度报告 (Decimal)

**日期**: 2026-07-06
**阶段**: P1-S05 金融原语
**状态**: ⏳ 进行中 (Decimal 完成，Currency/Money/ExchangeRate/ConversionService 待实现)

---

## 1. 执行概述

根据 `Phase_1_Detailed_Development_Plan.md` 的 P1-S05 规划，本阶段实现金融系统最核心的
不变量。当前已完成金融原语的地基类型 `Decimal`，并通过完整单元测试验证。

依据文档 `04_Money_Currency_System_Design.md` 的强约束：

- 金额与汇率绝不使用二进制浮点数
- `Decimal` 底层使用 128 位整数 + 固定标度 `10^10`
- 默认舍入策略统一为银行家舍入 (Half-Even)
- `Decimal` 不感知货币

---

## 2. 已完成：Decimal 定点数类型

### 2.1 交付物

| 文件 | 说明 | 行数 |
|------|------|------|
| `include/pfh/domain/decimal.h` | Decimal 值对象接口 | ~105 |
| `src/domain/decimal.cpp` | 解析/运算/舍入/溢出实现 | ~355 |
| `tests/unit/decimal_test.cpp` | 单元测试 | ~230 |
| `include/pfh/domain/domain_error.h` | 领域错误类型（新拆分） | ~120 |
| `cmake/Dependencies.cmake` | 依赖解析（FetchContent 回退） | ~65 |

### 2.2 核心特性

- **存储模型**: `__int128` 存储 `真实值 * 10^10`，标度固定为 10 位小数
  - 满足金额 8 位小数 + 汇率 10 位小数的无损计算要求
  - 数值范围远超 `NUMERIC(20,8)` 上限
- **字符串解析** `parse()`: 支持正负号、前导 `+`、空白裁剪，拒绝非法字符、多个小数点、空串
- **规范化输出** `to_string()`: 去除尾部多余零（`42.00` → `42`）
- **四则运算** `add/subtract/multiply/divide`: 全部返回 `DomainResult`，显式暴露溢出与除零
- **Half-Even 舍入**: 解析截断、乘法降标、除法均采用银行家舍入
- **溢出保护**: 加法符号翻转检测、乘法反向除法校验、`__int128` 范围守卫
- **比较与谓词**: `<=>`、`==`、`is_zero/is_negative/is_positive`、`negated/abs`

### 2.3 关键设计决策

- **底层类型**: 采用原生 `__int128_t`（维护者确认，见架构文档 5.1 待决策项）
  - 通过 `__extension__` 在类型别名处消除 `-pedantic` 警告
  - `decimal.cpp` 使用 `#pragma GCC/clang diagnostic ignored "-Wpedantic"` 局部抑制
  - 若未来需 MSVC 编译，仅需替换 `StorageType` 别名，调用方无感
- **领域错误分层**: 将 `DomainError`/`DomainResult` 从 `application/error.h` 拆到
  `domain/domain_error.h`，确保 Domain 层不反向依赖 Application 层
  - `application/error.h` 改为 include 该头文件，向后兼容

### 2.4 测试覆盖

Decimal 单元测试覆盖三类路径（共约 40 个用例）：

- **正常路径**: 整数、小数、负数、10 位小数、大额 `NUMERIC(20,8)` 上限
- **舍入路径**: Half-Even 的向偶取整（`0.00000000005` → `0`，`0.00000000015` → `0.0000000002`）
- **错误路径**: 空串、非法字符、多小数点、仅符号、除零、溢出
- **金融关键用例**:
  - `0.1 + 0.2` 精确等于 `0.3`（二进制浮点无法做到）
  - `1000 * 7.18 = 7180`（金额乘汇率）
  - `1 / 3 = 0.3333333333`（循环小数 Half-Even）
  - `7170 / 1000 = 7.17`（转账模式 B 推导汇率）

---

## 3. 验证结果

依赖通过 CMake FetchContent 自动拉取（spdlog 1.14.1、nlohmann_json 3.11.3、
GoogleTest 1.15.2），无需手动安装 vcpkg。

```text
cmake --build . --config Debug
[1/5] Building CXX object pfh_domain.dir/src/domain/decimal.cpp.obj
[2/5] Linking CXX static library libpfh_domain.a
[3/5] Linking CXX executable pfh_server.exe
[4/5] Building CXX object pfh_unit_tests.dir/decimal_test.cpp.obj
[5/5] Linking CXX executable pfh_unit_tests.exe

ctest -C Debug --output-on-failure
100% tests passed, 0 tests failed out of 71
Total Test time (real) = 2.35 sec
```

- ✅ CMake configure 通过
- ✅ 构建通过（`-Wall -Wextra -Werror -pedantic`）
- ✅ 71 个单元测试全部通过
- ✅ Domain 库 (`pfh_domain`) 独立编译，不链接 spdlog/框架

---

## 4. 验收对照 (P1-S05 Decimal 部分)

- [x] 不使用 `float` 或 `double` 表示金额和汇率
- [x] 除法显式定义舍入模式 (Half-Even)
- [x] 溢出返回错误而非静默回绕
- [x] 金融原语单元测试覆盖正常/边界/错误路径
- [x] Domain 层不依赖外层（错误类型已下沉到 domain）

---

## 5. 下一步

P1-S05 剩余金融原语（按依赖顺序）：

1. **Currency** 值对象 — ISO-4217 代码校验，不可变，精确比较
2. **Money** 值对象 — 绑定 Decimal + Currency，禁止跨币种直接加减
3. **ExchangeRate** 值对象 — base/target 方向、时间戳、来源、反向汇率
4. **CurrencyConversionService** 领域服务 — 直接汇率、反向汇率、USD 枢纽三角折算、缺失汇率错误

对应 tasks.md 任务 ID：17、18、19、20。

---

## 6. 备注

- 本报告仅覆盖 P1-S05 的 Decimal 部分。金融原语全部完成后，将补写
  P1-S05 完整完成报告并回写 tasks.md 剩余任务状态。
- 构建首次配置耗时较长（FetchContent 浅克隆三个依赖），后续增量构建为秒级。

---

**验证人**: Claude Code
**日期**: 2026-07-06
