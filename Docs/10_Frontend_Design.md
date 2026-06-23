# Personal Finance Hub - Frontend Architecture & Data Synergy

Version: 1.0

Frontend: Vue 3 (Composition API) + Vite

UI Framework: Element Plus

Charts: ECharts

Language: TypeScript (Strict Mode)

---

## 1. 架构定位与技术栈规范

前端架构同样需要遵循关注点分离原则。我们采用经典的 **Store（状态） - Service（API 封装） - View（页面） - Component（无状态组件）** 四层架构。

### 1.1 核心依赖

- **核心框架**：Vue 3 (`<script setup>` 语法糖)。
- **状态管理**：Pinia（取代 Vuex，提供完美的 TS 类型推断）。
- **网络请求**：Axios（配置全局拦截器）。
- **精度计算**：`decimal.js` 或 `bignumber.js`（**极其重要**，前端严禁使用原生 `Number` 进行金额加减乘除）。
- **路由管理**：Vue Router 4。

---

## 2. 核心规约：前后端数据协同防御

金融系统的前端不能仅仅是一个“展示层”，它必须是数据精度的第一道防线。

### 2.1 精度丢失防御 (The String-Only Rule)

JavaScript 原生数字使用 IEEE 754 双精度浮点数，超过 15 位有效数字或在进行小数运算时（如 `0.1 + 0.2`）会产生精度丢失。

- **规约**：后端 API 返回的所有金额字段（`amount`, `balance`, `rate`）均为 **String** 格式。
- **规约**：前端表单提交给后端的所有金额字段，也必须序列化为 **String**。
- **规约**：前端组件内部如果需要计算（如计算两笔明细的总和），必须使用 `decimal.js`。

```typescript
// 错误示范 (原生 JS)
const total = parseFloat(income) - parseFloat(expense); // 危险！

// 正确示范 (使用 decimal.js)
import Decimal from "decimal.js";
const total = new Decimal(income).minus(new Decimal(expense)).toString();
```

### 2.2 全局 HTTP 状态码拦截 (Axios Interceptors)

配合《08_REST_API_Design.md》，前端通过 Axios 拦截器将后端的 `std::expected` 错误映射为全局 UI 提示。

```typescript
// src/utils/http.ts
import axios from "axios";
import { ElMessage } from "element-plus";
import { useAuthStore } from "@/stores/auth";

const http = axios.create({ baseURL: "/api/v1" });

http.interceptors.response.use(
  (response) => response.data,
  (error) => {
    const status = error.response?.status;
    const data = error.response?.data;

    switch (status) {
      case 401:
        useAuthStore().logout();
        ElMessage.error("登录已过期，请重新登录");
        break;
      case 422:
        // 领域规则冲突 (DomainRuleViolation)
        ElMessage.warning(`操作失败: ${data.message}`);
        break;
      case 404:
        ElMessage.info("请求的数据不存在");
        break;
      case 500:
        ElMessage.error("服务器繁忙，请稍后再试");
        break;
      default:
        ElMessage.error(data?.message || "网络请求异常");
    }
    return Promise.reject(error);
  },
);
```

---

## 3. 状态管理设计 (Pinia Stores)

我们将高频使用且不易变动的数据放入全局状态，减少重复的 HTTP 请求。

### 3.1 认证与偏好 Store (`useAuthStore`)

存储 JWT Token 和用户偏好。基准货币决定所有报表的显示单位，其他偏好决定首页、主题、日期和数字展示。

```typescript
// src/stores/auth.ts
import { defineStore } from "pinia";
import { ref } from "vue";

type ThemeMode = "system" | "light" | "dark";
type DefaultHomePage = "dashboard" | "transactions" | "reports" | "accounts";
type ReportPeriod =
  | "current_month"
  | "last_month"
  | "last_3_months"
  | "current_year"
  | "custom";

export const useAuthStore = defineStore("auth", () => {
  const token = ref<string | null>(localStorage.getItem("jwt"));
  const baseCurrency = ref<string>("CNY"); // 从后端 /api/v1/users/me 获取
  const locale = ref<string>("zh-CN");
  const timezone = ref<string>("Asia/Shanghai");
  const dateFormat = ref<string>("YYYY-MM-DD");
  const numberFormat = ref<string>("1,234.56");
  const theme = ref<ThemeMode>("system");
  const defaultHomePage = ref<DefaultHomePage>("dashboard");
  const defaultReportPeriod = ref<ReportPeriod>("current_month");

  function setToken(newToken: string) {
    token.value = newToken;
    localStorage.setItem("jwt", newToken);
  }

  return {
    token,
    baseCurrency,
    locale,
    timezone,
    dateFormat,
    numberFormat,
    theme,
    defaultHomePage,
    defaultReportPeriod,
    setToken,
  };
});
```

### 3.2 账户缓存 Store (`useAccountStore`)

由于记账表单、转账表单、过滤栏都需要选择账户，账户列表是最需要被全局缓存的实体。

```typescript
// src/stores/account.ts
export const useAccountStore = defineStore("account", () => {
  const accounts = ref<Account[]>([]);

  async function fetchAccounts() {
    if (accounts.value.length === 0) {
      accounts.value = await http.get("/accounts");
    }
  }

  function getAccountById(id: number) {
    return accounts.value.find((a) => a.id === id);
  }

  return { accounts, fetchAccounts, getAccountById };
});
```

### 3.3 分类 Store (`useCategoryStore`)

分类树是记账表单、筛选器和分类管理页的共享数据。

```typescript
export const useCategoryStore = defineStore("category", () => {
  const incomeTree = ref<CategoryNode[]>([]);
  const expenseTree = ref<CategoryNode[]>([]);

  async function fetchTree(board: "income" | "expense") {
    const data = await http.get(`/categories?board=${board}`);
    if (board === "income") incomeTree.value = data;
    if (board === "expense") expenseTree.value = data;
  }

  async function createCategory(payload: {
    board: "income" | "expense";
    name: string;
    parentId?: number | null;
    templateId?: number | null;
  }) {
    await http.post("/categories", payload);
    await fetchTree(payload.board);
  }

  async function deleteCategory(id: number, board: "income" | "expense") {
    await http.delete(`/categories/${id}`);
    await fetchTree(board);
  }

  return { incomeTree, expenseTree, fetchTree, createCategory, deleteCategory };
});
```

交互规则：

1. 记账表单根据交易类型只展示对应 board 的分类
2. 用户删除预设分类时，只删除自己的分类副本
3. 分类树支持拖拽排序时，只提交同一 board 内的排序结果

### 3.4 标签 Store (`useTagStore`)

```typescript
export const useTagStore = defineStore("tag", () => {
  const tags = ref<Tag[]>([]);

  async function fetchTags() {
    tags.value = await http.get("/tags");
  }

  async function createTag(name: string) {
    await http.post("/tags", { name });
    await fetchTags();
  }

  async function attach(transactionId: number, tagIds: number[]) {
    await http.put(`/transactions/${transactionId}/tags`, { tagIds });
  }

  return { tags, fetchTags, createTag, attach };
});
```

Tag 作为筛选和标记维度，不参与金额计算。

### 3.5 偏好设置 Store 数据流

`useAuthStore` 在登录后调用：

```typescript
async function fetchPreferences() {
  const preference = await http.get("/users/me/preferences");
  baseCurrency.value = preference.baseCurrency;
  locale.value = preference.locale;
  timezone.value = preference.timezone;
  dateFormat.value = preference.dateFormat;
  numberFormat.value = preference.numberFormat;
  theme.value = preference.theme;
  defaultHomePage.value = preference.defaultHomePage;
  defaultReportPeriod.value = preference.defaultReportPeriod;
}

async function updatePreferences(payload: UserPreferenceDTO) {
  await http.put("/users/me/preferences", payload);
  await fetchPreferences();
}
```

偏好变更后必须刷新：

- Dashboard Summary
- Report filters
- 金额和日期格式化组件
- 主题状态

### 3.6 货币元数据 Store

```typescript
export const useCurrencyStore = defineStore("currency", () => {
  const currencies = ref<CurrencyMetadata[]>([]);

  async function fetchCurrencies() {
    if (currencies.value.length === 0) {
      currencies.value = await http.get("/currencies");
    }
  }

  function formatAmount(amount: string, code: string) {
    const metadata = currencies.value.find((item) => item.code === code);
    const precision = metadata?.precision ?? 2;
    const symbol = metadata?.symbol ?? code;
    return `${symbol}${new Decimal(amount).toFixed(precision)}`;
  }

  return { currencies, fetchCurrencies, formatAmount };
});
```

---

## 4. 复杂交互：跨币种转账动态表单

转账表单是前端最复杂的业务组件，因为它需要根据 `TransferMode`（出账+汇率、出账+入账、入账+汇率）动态锁定（Disable）其中一个输入框，并触发联动计算。

### 4.1 动态联动逻辑设计 (Vue 3 Composition API)

```vue
<script setup lang="ts">
import { ref, computed, watch } from "vue";
import Decimal from "decimal.js";

// 表单状态
const sourceAmount = ref<string>("");
const targetAmount = ref<string>("");
const exchangeRate = ref<string>("");
const transferMode = ref<
  "OutgoingAndRate" | "OutgoingAndIncoming" | "IncomingAndRate"
>("OutgoingAndRate");

// 监听模式切换，清理不需要的字段以防脏数据提交
watch(transferMode, (newMode) => {
  if (newMode === "OutgoingAndRate") targetAmount.value = "";
  if (newMode === "OutgoingAndIncoming") exchangeRate.value = "";
  if (newMode === "IncomingAndRate") sourceAmount.value = "";
});

// 前端实时预览推导结果（仅作 UI 提示，最终以 C++ 后端推导为准）
const previewTargetAmount = computed(() => {
  if (
    transferMode.value === "OutgoingAndRate" &&
    sourceAmount.value &&
    exchangeRate.value
  ) {
    try {
      return new Decimal(sourceAmount.value)
        .times(new Decimal(exchangeRate.value))
        .toDP(2)
        .toString();
    } catch {
      return "...";
    }
  }
  return targetAmount.value;
});
</script>

<template>
  <el-form>
    <el-radio-group v-model="transferMode">
      <el-radio-button label="OutgoingAndRate">已知出账与汇率</el-radio-button>
      <el-radio-button label="OutgoingAndIncoming"
        >已知出账与入账</el-radio-button
      >
    </el-radio-group>

    <el-form-item label="出账金额 (Source)">
      <el-input
        v-model="sourceAmount"
        :disabled="transferMode === 'IncomingAndRate'"
      />
    </el-form-item>

    <el-form-item label="入账金额 (Target)">
      <el-input
        v-model="targetAmount"
        :disabled="transferMode === 'OutgoingAndRate'"
        :placeholder="previewTargetAmount"
      />
    </el-form-item>

    <el-divider>附加调整</el-divider>
    <el-form-item label="中转手续费 (可选)">
      <el-input v-model="feeAmount" placeholder="如：15.00" />
    </el-form-item>
  </el-form>
</template>
```

---

## 5. 报表展示层设计 (ECharts 协同)

报表页面直接消费《07.5_Reporting_and_Analytics_Design.md》提供的聚合 DTO。前端无需关心汇率换算，只需关注渲染。

### 5.1 响应式图表封装

为了防止 ECharts 在窗口缩放时变形，将其封装为一个通用的 Vue 组件。

```vue
<script setup lang="ts">
import { onMounted, ref, watch } from "vue";
import * as echarts from "echarts";

const props = defineProps<{
  data: { period: string; income: string; expense: string }[];
  baseCurrency: string;
}>();

const chartRef = ref<HTMLElement | null>(null);
let chartInstance: echarts.ECharts | null = null;

const renderChart = () => {
  if (!chartInstance) chartInstance = echarts.init(chartRef.value!);

  const option = {
    tooltip: { trigger: "axis" },
    legend: { data: ["收入", "支出"] },
    xAxis: { type: "category", data: props.data.map((d) => d.period) },
    yAxis: { type: "value", name: `金额 (${props.baseCurrency})` },
    series: [
      {
        name: "收入",
        type: "bar",
        itemStyle: { color: "#67C23A" }, // Element Plus 成功绿
        data: props.data.map((d) => parseFloat(d.income)), // ECharts 渲染必须用数字，此处转换是安全的，因为仅用于像素映射，不用于二次计算
      },
      {
        name: "支出",
        type: "bar",
        itemStyle: { color: "#F56C6C" }, // Element Plus 危险红
        data: props.data.map((d) => parseFloat(d.expense)),
      },
    ],
  };
  chartInstance.setOption(option);
};

// 监听数据变化，重新渲染
watch(() => props.data, renderChart, { deep: true });

onMounted(() => {
  renderChart();
  window.addEventListener("resize", () => chartInstance?.resize());
});
</script>

<template>
  <div ref="chartRef" style="width: 100%; height: 400px;"></div>
</template>
```

### 5.2 首页 Dashboard 数据流

首页直接请求聚合接口：

```typescript
const dashboard = await http.get("/reports/dashboard-summary", {
  params: {
    startDate,
    endDate,
  },
});
```

首页组件只消费 `DashboardSummaryDTO`，不在前端拼装净值、月收入、月支出和 Top 分类。
这样可以避免多个接口在首页竞争加载状态，也减少汇率策略不一致的风险。

---

## 6. 页面结构补充

必须提供以下工作页：

- `CategorySettingsView`: 管理收入/支出分类树，支持新增、删除、启用预设
- `TagSettingsView`: 管理标签和交易标签绑定
- `PreferenceSettingsView`: 管理基准货币、主题、日期格式、默认首页、默认报表周期
- `DashboardView`: 消费 `DashboardSummaryDTO`
- `ReportView`: 使用 `ReportFilterDTO` 查询趋势、分类、标签和账户维度报表

页面约束：

1. 金额展示统一走 `useCurrencyStore.formatAmount`
2. 日期展示统一走用户偏好中的 `dateFormat` 和 `timezone`
3. 分类选择器必须按收入/支出 board 隔离
4. 交易详情页展示 Tag，但不把 Tag 当成分类替代品
