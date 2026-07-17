<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue';
import {
  CircleAlert,
  Languages,
  Pencil,
  Plus,
  RefreshCw,
  RotateCcw,
  Save,
  Tags,
  Trash2,
  Workflow,
} from '@lucide/vue';
import AppShell from '../components/AppShell.vue';
import ModalDialog from '../components/ModalDialog.vue';
import { ApiError } from '../services/api-error';
import type { Category, CategoryBoard, CategoryTree } from '../services/metadata-api';
import type { UserPreference } from '../services/user-context-api';
import { useMetadataStore } from '../stores/metadata';
import { useUserContextStore } from '../stores/user-context';

type Tab = 'categories' | 'tags' | 'preferences';
type CategoryRow = Category & { depth: number };

const metadata = useMetadataStore();
const userContext = useUserContextStore();
const tab = ref<Tab>('categories');
const board = ref<CategoryBoard>('expense');
const showDeleted = ref(false);
const pageError = ref('');
const pending = ref(false);

const categoryDialogOpen = ref(false);
const editingCategory = ref<CategoryRow | null>(null);
const categoryForm = reactive({ name: '', parentId: '', sortOrder: 0 });
const tagDialogOpen = ref(false);
const editingTagId = ref<number | null>(null);
const tagName = ref('');

const preferenceForm = reactive<UserPreference>({
  baseCurrency: 'CNY',
  locale: 'zh-CN',
  timezone: 'Asia/Shanghai',
  dateFormat: 'YYYY-MM-DD',
  numberFormat: '1,234.56',
  theme: 'system',
  defaultHomePage: 'dashboard',
  defaultReportPeriod: 'current_month',
});

const zh = computed(() => userContext.preference?.locale === 'zh-CN');
const copy = computed(() =>
  zh.value
    ? {
        title: '设置',
        subtitle: '管理记账分类、标签与显示偏好。',
        categories: '分类',
        tags: '标签',
        preferences: '偏好',
        active: '启用中',
        deleted: '已删除',
      }
    : {
        title: 'Settings',
        subtitle: 'Manage ledger categories, tags, and presentation preferences.',
        categories: 'Categories',
        tags: 'Tags',
        preferences: 'Preferences',
        active: 'Active',
        deleted: 'Deleted',
      },
);

function flatten(nodes: CategoryTree[], depth = 0): CategoryRow[] {
  return nodes.flatMap((node) => [{ ...node, depth }, ...flatten(node.children, depth + 1)]);
}

const allCategoryRows = computed(() => flatten(metadata.categories));
const categoryRows = computed(() =>
  allCategoryRows.value.filter(
    (item) => item.board === board.value && (showDeleted.value || !item.isDeleted),
  ),
);
const parentOptions = computed(() =>
  allCategoryRows.value.filter(
    (item) =>
      item.board === board.value && !item.isDeleted && item.id !== editingCategory.value?.id,
  ),
);
const visibleTags = computed(() =>
  metadata.tags.filter((item) => showDeleted.value || !item.isDeleted),
);

function errorMessage(error: unknown): string {
  return error instanceof ApiError ? error.details.message : 'The request could not be completed.';
}

async function loadMetadata(): Promise<void> {
  pageError.value = '';
  try {
    await Promise.all([metadata.loadCategories(), metadata.loadTags()]);
  } catch (error) {
    pageError.value = errorMessage(error);
  }
}

function openCategoryCreate(): void {
  editingCategory.value = null;
  categoryForm.name = '';
  categoryForm.parentId = '';
  categoryForm.sortOrder = categoryRows.value.length * 10;
  categoryDialogOpen.value = true;
}

function openCategoryEdit(item: CategoryRow): void {
  editingCategory.value = item;
  categoryForm.name = item.name;
  categoryForm.parentId = item.parentId?.toString() ?? '';
  categoryForm.sortOrder = item.sortOrder;
  categoryDialogOpen.value = true;
}

async function submitCategory(): Promise<void> {
  pending.value = true;
  pageError.value = '';
  try {
    if (editingCategory.value) {
      await metadata.updateCategoryItem(
        editingCategory.value.id,
        categoryForm.name,
        categoryForm.sortOrder,
      );
    } else {
      await metadata.createCategoryItem({
        board: board.value,
        name: categoryForm.name,
        parentId: categoryForm.parentId ? Number(categoryForm.parentId) : null,
      });
    }
    categoryDialogOpen.value = false;
  } catch (error) {
    pageError.value = errorMessage(error);
  } finally {
    pending.value = false;
  }
}

async function removeCategory(item: CategoryRow): Promise<void> {
  pageError.value = '';
  try {
    await metadata.deleteCategoryItem(item.id);
  } catch (error) {
    pageError.value = errorMessage(error);
  }
}

async function restoreCategory(item: CategoryRow): Promise<void> {
  pageError.value = '';
  try {
    await metadata.restoreCategoryItem(item.id);
  } catch (error) {
    pageError.value = errorMessage(error);
  }
}

function openTagCreate(): void {
  editingTagId.value = null;
  tagName.value = '';
  tagDialogOpen.value = true;
}

function openTagEdit(id: number, name: string): void {
  editingTagId.value = id;
  tagName.value = name;
  tagDialogOpen.value = true;
}

async function submitTag(): Promise<void> {
  pending.value = true;
  pageError.value = '';
  try {
    if (editingTagId.value) await metadata.updateTagItem(editingTagId.value, tagName.value);
    else await metadata.createTagItem(tagName.value);
    tagDialogOpen.value = false;
  } catch (error) {
    pageError.value = errorMessage(error);
  } finally {
    pending.value = false;
  }
}

async function removeTag(id: number): Promise<void> {
  pageError.value = '';
  try {
    await metadata.deleteTagItem(id);
  } catch (error) {
    pageError.value = errorMessage(error);
  }
}

async function restoreTag(id: number): Promise<void> {
  pageError.value = '';
  try {
    await metadata.restoreTagItem(id);
  } catch (error) {
    pageError.value = errorMessage(error);
  }
}

async function savePreferences(): Promise<void> {
  pending.value = true;
  pageError.value = '';
  try {
    await userContext.update({ ...preferenceForm });
  } catch (error) {
    pageError.value = errorMessage(error);
  } finally {
    pending.value = false;
  }
}

watch(
  () => userContext.preference,
  (value) => {
    if (value) Object.assign(preferenceForm, value);
  },
  { immediate: true },
);

onMounted(loadMetadata);
onBeforeUnmount(() => {
  categoryDialogOpen.value = false;
  tagDialogOpen.value = false;
});
</script>

<template>
  <AppShell>
    <div class="content-header">
      <div>
        <p class="eyebrow">Ledger / Settings</p>
        <h1>{{ copy.title }}</h1>
        <p class="content-header__copy">{{ copy.subtitle }}</p>
      </div>
      <button class="button button--quiet" type="button" @click="loadMetadata">
        <RefreshCw :size="16" /> {{ zh ? '刷新' : 'Refresh' }}
      </button>
    </div>

    <div v-if="pageError" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>{{ zh ? '操作未完成' : 'Request not completed' }}</strong>
        <p>{{ pageError }}</p>
      </div>
    </div>

    <div class="settings-tabs" role="tablist" :aria-label="zh ? '设置页面' : 'Settings section'">
      <button
        id="settings-tab-categories"
        role="tab"
        type="button"
        :class="{ 'is-active': tab === 'categories' }"
        :aria-selected="tab === 'categories'"
        aria-controls="settings-panel-categories"
        @click="tab = 'categories'"
      >
        <Workflow :size="16" /> {{ copy.categories }}
      </button>
      <button
        id="settings-tab-tags"
        role="tab"
        type="button"
        :class="{ 'is-active': tab === 'tags' }"
        :aria-selected="tab === 'tags'"
        aria-controls="settings-panel-tags"
        @click="tab = 'tags'"
      >
        <Tags :size="16" /> {{ copy.tags }}
      </button>
      <button
        id="settings-tab-preferences"
        role="tab"
        type="button"
        :class="{ 'is-active': tab === 'preferences' }"
        :aria-selected="tab === 'preferences'"
        aria-controls="settings-panel-preferences"
        @click="tab = 'preferences'"
      >
        <Languages :size="16" /> {{ copy.preferences }}
      </button>
    </div>

    <section
      v-if="tab === 'categories'"
      id="settings-panel-categories"
      class="settings-section"
      role="tabpanel"
      aria-labelledby="settings-tab-categories"
    >
      <div class="settings-toolbar">
        <div class="segmented-control" :aria-label="zh ? '分类类型' : 'Category board'">
          <button
            type="button"
            :class="{ 'is-active': board === 'expense' }"
            @click="board = 'expense'"
          >
            {{ zh ? '支出' : 'Expense' }}
          </button>
          <button
            type="button"
            :class="{ 'is-active': board === 'income' }"
            @click="board = 'income'"
          >
            {{ zh ? '收入' : 'Income' }}
          </button>
        </div>
        <label class="settings-toggle">
          <input v-model="showDeleted" type="checkbox" />
          <span>{{ zh ? '显示已删除' : 'Show deleted' }}</span>
        </label>
        <button class="button button--small" type="button" @click="openCategoryCreate">
          <Plus :size="15" /> {{ zh ? '新建分类' : 'New category' }}
        </button>
      </div>
      <div class="settings-list" :aria-busy="metadata.categoryState === 'loading'">
        <div v-if="metadata.categoryState === 'loading'" class="account-detail-loading">
          <RefreshCw :size="17" class="spin" /> {{ zh ? '载入分类' : 'Loading categories' }}
        </div>
        <div
          v-for="item in categoryRows"
          v-else
          :key="item.id"
          class="settings-row"
          :class="{ 'is-deleted': item.isDeleted }"
        >
          <div class="settings-row__name" :style="{ paddingInlineStart: `${item.depth * 22}px` }">
            <strong>{{ item.name }}</strong>
            <small
              >{{ item.source === 'system' ? 'System' : 'Custom' }} · {{ item.sortOrder }}</small
            >
          </div>
          <span class="status-badge" :class="{ 'status-badge--archived': item.isDeleted }">
            {{ item.isDeleted ? copy.deleted : copy.active }}
          </span>
          <div class="settings-row__actions">
            <button
              v-if="!item.isDeleted"
              class="icon-button"
              type="button"
              :title="zh ? '编辑' : 'Edit'"
              @click="openCategoryEdit(item)"
            >
              <Pencil :size="16" />
            </button>
            <button
              v-if="!item.isDeleted"
              class="icon-button icon-button--danger"
              type="button"
              :title="zh ? '删除' : 'Delete'"
              @click="removeCategory(item)"
            >
              <Trash2 :size="16" />
            </button>
            <button
              v-else
              class="icon-button"
              type="button"
              :title="zh ? '恢复' : 'Restore'"
              @click="restoreCategory(item)"
            >
              <RotateCcw :size="16" />
            </button>
          </div>
        </div>
        <div
          v-if="metadata.categoryState !== 'loading' && !categoryRows.length"
          class="account-empty"
        >
          <Workflow :size="26" /><strong>{{
            zh ? '没有匹配的分类' : 'No matching categories'
          }}</strong>
        </div>
      </div>
    </section>

    <section
      v-else-if="tab === 'tags'"
      id="settings-panel-tags"
      class="settings-section"
      role="tabpanel"
      aria-labelledby="settings-tab-tags"
    >
      <div class="settings-toolbar">
        <label class="settings-toggle">
          <input v-model="showDeleted" type="checkbox" />
          <span>{{ zh ? '显示已删除' : 'Show deleted' }}</span>
        </label>
        <button class="button button--small" type="button" @click="openTagCreate">
          <Plus :size="15" /> {{ zh ? '新建标签' : 'New tag' }}
        </button>
      </div>
      <div class="settings-list" :aria-busy="metadata.tagState === 'loading'">
        <div
          v-for="item in visibleTags"
          :key="item.id"
          class="settings-row"
          :class="{ 'is-deleted': item.isDeleted }"
        >
          <div class="settings-row__name">
            <strong>{{ item.name }}</strong
            ><small>#{{ item.id }}</small>
          </div>
          <span class="status-badge" :class="{ 'status-badge--archived': item.isDeleted }">
            {{ item.isDeleted ? copy.deleted : copy.active }}
          </span>
          <div class="settings-row__actions">
            <button
              v-if="!item.isDeleted"
              class="icon-button"
              type="button"
              :title="zh ? '重命名' : 'Rename'"
              @click="openTagEdit(item.id, item.name)"
            >
              <Pencil :size="16" />
            </button>
            <button
              v-if="!item.isDeleted"
              class="icon-button icon-button--danger"
              type="button"
              :title="zh ? '删除' : 'Delete'"
              @click="removeTag(item.id)"
            >
              <Trash2 :size="16" />
            </button>
            <button
              v-else
              class="icon-button"
              type="button"
              :title="zh ? '恢复' : 'Restore'"
              @click="restoreTag(item.id)"
            >
              <RotateCcw :size="16" />
            </button>
          </div>
        </div>
        <div v-if="metadata.tagState !== 'loading' && !visibleTags.length" class="account-empty">
          <Tags :size="26" /><strong>{{ zh ? '还没有标签' : 'No tags yet' }}</strong>
        </div>
      </div>
    </section>

    <section
      v-else
      id="settings-panel-preferences"
      class="settings-section settings-preferences"
      role="tabpanel"
      aria-labelledby="settings-tab-preferences"
    >
      <form class="preference-form" @submit.prevent="savePreferences">
        <label class="field"
          ><span>{{ zh ? '基准币种' : 'Base currency' }}</span
          ><select v-model="preferenceForm.baseCurrency">
            <option
              v-for="currency in userContext.currencies"
              :key="currency.code"
              :value="currency.code"
            >
              {{ currency.code }} · {{ currency.displayName }}
            </option>
          </select></label
        >
        <label class="field"
          ><span>{{ zh ? '语言' : 'Language' }}</span
          ><select v-model="preferenceForm.locale">
            <option value="zh-CN">简体中文</option>
            <option value="en-US">English (US)</option>
          </select></label
        >
        <label class="field"
          ><span>{{ zh ? '时区' : 'Timezone' }}</span
          ><select v-model="preferenceForm.timezone">
            <option value="Asia/Shanghai">Asia/Shanghai</option>
            <option value="UTC">UTC</option>
            <option value="America/New_York">America/New_York</option>
            <option value="Europe/London">Europe/London</option>
            <option value="Asia/Tokyo">Asia/Tokyo</option>
          </select></label
        >
        <label class="field"
          ><span>{{ zh ? '日期格式' : 'Date format' }}</span
          ><select v-model="preferenceForm.dateFormat">
            <option value="YYYY-MM-DD">YYYY-MM-DD</option>
            <option value="DD/MM/YYYY">DD/MM/YYYY</option>
            <option value="MM/DD/YYYY">MM/DD/YYYY</option>
          </select></label
        >
        <label class="field"
          ><span>{{ zh ? '数字格式' : 'Number format' }}</span
          ><select v-model="preferenceForm.numberFormat">
            <option value="1,234.56">1,234.56</option>
            <option value="1.234,56">1.234,56</option>
            <option value="1 234,56">1 234,56</option>
          </select></label
        >
        <label class="field"
          ><span>{{ zh ? '主题' : 'Theme' }}</span
          ><select v-model="preferenceForm.theme">
            <option value="system">{{ zh ? '跟随系统' : 'System' }}</option>
            <option value="light">{{ zh ? '浅色' : 'Light' }}</option>
            <option value="dark">{{ zh ? '深色' : 'Dark' }}</option>
          </select></label
        >
        <label class="field"
          ><span>{{ zh ? '默认首页' : 'Default home page' }}</span
          ><select v-model="preferenceForm.defaultHomePage">
            <option value="dashboard">{{ zh ? '概览' : 'Overview' }}</option>
            <option value="accounts">{{ zh ? '账户' : 'Accounts' }}</option>
            <option value="transactions">{{ zh ? '流水' : 'Transactions' }}</option>
            <option value="reports">{{ zh ? '报表' : 'Reports' }}</option>
          </select></label
        >
        <label class="field"
          ><span>{{ zh ? '默认报表周期' : 'Default report period' }}</span
          ><select v-model="preferenceForm.defaultReportPeriod">
            <option value="current_month">{{ zh ? '本月' : 'Current month' }}</option>
            <option value="last_month">{{ zh ? '上月' : 'Last month' }}</option>
            <option value="last_3_months">{{ zh ? '最近三个月' : 'Last 3 months' }}</option>
            <option value="current_year">{{ zh ? '本年' : 'Current year' }}</option>
            <option value="custom">{{ zh ? '自定义' : 'Custom' }}</option>
          </select></label
        >
        <div class="preference-actions">
          <button class="button" type="submit" :disabled="pending">
            <Save :size="16" /> {{ zh ? '保存偏好' : 'Save preferences' }}
          </button>
        </div>
      </form>
    </section>

    <ModalDialog
      :open="categoryDialogOpen"
      :title="
        editingCategory ? (zh ? '编辑分类' : 'Edit category') : zh ? '新建分类' : 'New category'
      "
      @close="categoryDialogOpen = false"
    >
      <form class="settings-dialog-form" @submit.prevent="submitCategory">
        <label class="field"
          ><span>{{ zh ? '名称' : 'Name' }}</span
          ><input v-model.trim="categoryForm.name" required maxlength="128"
        /></label>
        <label v-if="!editingCategory" class="field"
          ><span>{{ zh ? '上级分类' : 'Parent category' }}</span
          ><select v-model="categoryForm.parentId">
            <option value="">{{ zh ? '无（一级分类）' : 'None (root)' }}</option>
            <option v-for="item in parentOptions" :key="item.id" :value="item.id">
              {{ '· '.repeat(item.depth) }}{{ item.name }}
            </option>
          </select></label
        >
        <label v-if="editingCategory" class="field"
          ><span>{{ zh ? '排序' : 'Sort order' }}</span
          ><input v-model.number="categoryForm.sortOrder" type="number" required
        /></label>
        <div class="modal-actions">
          <button class="button button--quiet" type="button" @click="categoryDialogOpen = false">
            {{ zh ? '取消' : 'Cancel' }}</button
          ><button class="button" type="submit" :disabled="pending">
            <Save :size="16" /> {{ zh ? '保存' : 'Save' }}
          </button>
        </div>
      </form>
    </ModalDialog>

    <ModalDialog
      :open="tagDialogOpen"
      :title="editingTagId ? (zh ? '重命名标签' : 'Rename tag') : zh ? '新建标签' : 'New tag'"
      @close="tagDialogOpen = false"
    >
      <form class="settings-dialog-form" @submit.prevent="submitTag">
        <label class="field"
          ><span>{{ zh ? '名称' : 'Name' }}</span
          ><input v-model.trim="tagName" required maxlength="64"
        /></label>
        <div class="modal-actions">
          <button class="button button--quiet" type="button" @click="tagDialogOpen = false">
            {{ zh ? '取消' : 'Cancel' }}</button
          ><button class="button" type="submit" :disabled="pending">
            <Save :size="16" /> {{ zh ? '保存' : 'Save' }}
          </button>
        </div>
      </form>
    </ModalDialog>
  </AppShell>
</template>
