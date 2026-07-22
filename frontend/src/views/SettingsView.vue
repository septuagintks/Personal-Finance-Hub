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
import TimeZoneCombobox from '../components/TimeZoneCombobox.vue';
import { translate } from '../i18n';
import { ApiError } from '../services/api-error';
import type { Category, CategoryBoard, CategoryTree } from '../services/metadata-api';
import type { UserPreference } from '../services/user-context-api';
import { useMetadataStore } from '../stores/metadata';
import { useUserContextStore } from '../stores/user-context';
import { monthInTimeZone, shiftReportMonth } from '../services/zoned-date-time';

type Tab = 'categories' | 'tags' | 'preferences';
type CategoryRow = Category & { depth: number };

const metadata = useMetadataStore();
const userContext = useUserContextStore();
const tab = ref<Tab>('categories');
const board = ref<CategoryBoard>('expense');
const showDeleted = ref(false);
const pageError = ref('');
const preferenceValidationError = ref('');
const preferencePending = ref(false);
const pending = computed(() => preferencePending.value || metadata.mutationPending);

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
  customReportStartMonth: null,
  customReportEndMonth: null,
});

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
  return error instanceof ApiError ? error.details.message : translate('settings.requestFailed');
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
  pageError.value = '';
  try {
    if (editingTagId.value) await metadata.updateTagItem(editingTagId.value, tagName.value);
    else await metadata.createTagItem(tagName.value);
    tagDialogOpen.value = false;
  } catch (error) {
    pageError.value = errorMessage(error);
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
  preferenceValidationError.value = '';
  if (preferenceForm.defaultReportPeriod === 'custom') {
    if (!preferenceForm.customReportStartMonth || !preferenceForm.customReportEndMonth) {
      preferenceValidationError.value = translate('settings.customPeriodRequired');
      return;
    }
    if (preferenceForm.customReportStartMonth > preferenceForm.customReportEndMonth) {
      preferenceValidationError.value = translate('settings.customPeriodOrder');
      return;
    }
    if (
      preferenceForm.customReportEndMonth >
      shiftReportMonth(preferenceForm.customReportStartMonth, 119)
    ) {
      preferenceValidationError.value = translate('settings.customPeriodTooLong');
      return;
    }
  }
  preferencePending.value = true;
  pageError.value = '';
  try {
    await userContext.update({
      ...preferenceForm,
      customReportStartMonth:
        preferenceForm.defaultReportPeriod === 'custom'
          ? preferenceForm.customReportStartMonth
          : null,
      customReportEndMonth:
        preferenceForm.defaultReportPeriod === 'custom'
          ? preferenceForm.customReportEndMonth
          : null,
    });
  } catch (error) {
    pageError.value = errorMessage(error);
  } finally {
    preferencePending.value = false;
  }
}

watch(
  () => userContext.preference,
  (value) => {
    if (value) Object.assign(preferenceForm, value);
  },
  { immediate: true },
);

watch(
  () => preferenceForm.defaultReportPeriod,
  (period) => {
    preferenceValidationError.value = '';
    if (period === 'custom') {
      const month = monthInTimeZone(preferenceForm.timezone);
      preferenceForm.customReportStartMonth ??= month;
      preferenceForm.customReportEndMonth ??= month;
    }
  },
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
        <p class="eyebrow">{{ translate('settings.eyebrow') }}</p>
        <h1>{{ translate('settings.title') }}</h1>
        <p class="content-header__copy">{{ translate('settings.subtitle') }}</p>
      </div>
      <button
        class="button button--quiet"
        type="button"
        :disabled="metadata.mutationPending"
        @click="loadMetadata"
      >
        <RefreshCw :size="16" /> {{ translate('common.refresh') }}
      </button>
    </div>

    <div v-if="pageError" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>{{ translate('common.requestNotCompleted') }}</strong>
        <p>{{ pageError }}</p>
      </div>
    </div>

    <div class="settings-tabs" role="tablist" :aria-label="translate('settings.sectionLabel')">
      <button
        id="settings-tab-categories"
        role="tab"
        type="button"
        :class="{ 'is-active': tab === 'categories' }"
        :aria-selected="tab === 'categories'"
        aria-controls="settings-panel-categories"
        @click="tab = 'categories'"
      >
        <Workflow :size="16" /> {{ translate('settings.categories') }}
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
        <Tags :size="16" /> {{ translate('settings.tags') }}
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
        <Languages :size="16" /> {{ translate('settings.preferences') }}
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
        <div class="segmented-control" :aria-label="translate('settings.categoryBoard')">
          <button
            type="button"
            :class="{ 'is-active': board === 'expense' }"
            @click="board = 'expense'"
          >
            {{ translate('settings.expense') }}
          </button>
          <button
            type="button"
            :class="{ 'is-active': board === 'income' }"
            @click="board = 'income'"
          >
            {{ translate('settings.income') }}
          </button>
        </div>
        <label class="settings-toggle">
          <input v-model="showDeleted" type="checkbox" />
          <span>{{ translate('settings.showDeleted') }}</span>
        </label>
        <button
          class="button button--small"
          type="button"
          :disabled="pending"
          @click="openCategoryCreate"
        >
          <Plus :size="15" /> {{ translate('settings.newCategory') }}
        </button>
      </div>
      <div class="settings-list" :aria-busy="metadata.categoryState === 'loading'">
        <div v-if="metadata.categoryState === 'loading'" class="account-detail-loading">
          <RefreshCw :size="17" class="spin" /> {{ translate('settings.loadingCategories') }}
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
              >{{
                translate(
                  item.source === 'system' ? 'settings.systemSource' : 'settings.customSource',
                )
              }}
              · {{ item.sortOrder }}</small
            >
          </div>
          <span class="status-badge" :class="{ 'status-badge--archived': item.isDeleted }">
            {{ translate(item.isDeleted ? 'settings.deleted' : 'settings.active') }}
          </span>
          <div class="settings-row__actions">
            <button
              v-if="!item.isDeleted"
              class="icon-button"
              type="button"
              :title="translate('common.edit')"
              :aria-label="translate('common.edit')"
              :disabled="pending"
              @click="openCategoryEdit(item)"
            >
              <Pencil :size="16" />
            </button>
            <button
              v-if="!item.isDeleted"
              class="icon-button icon-button--danger"
              type="button"
              :title="translate('common.delete')"
              :aria-label="translate('common.delete')"
              :disabled="pending"
              @click="removeCategory(item)"
            >
              <Trash2 :size="16" />
            </button>
            <button
              v-else
              class="icon-button"
              type="button"
              :title="translate('common.restore')"
              :aria-label="translate('common.restore')"
              :disabled="pending"
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
          <Workflow :size="26" /><strong>{{ translate('settings.noCategories') }}</strong>
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
          <span>{{ translate('settings.showDeleted') }}</span>
        </label>
        <button
          class="button button--small"
          type="button"
          :disabled="pending"
          @click="openTagCreate"
        >
          <Plus :size="15" /> {{ translate('settings.newTag') }}
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
            {{ translate(item.isDeleted ? 'settings.deleted' : 'settings.active') }}
          </span>
          <div class="settings-row__actions">
            <button
              v-if="!item.isDeleted"
              class="icon-button"
              type="button"
              :title="translate('common.rename')"
              :aria-label="translate('common.rename')"
              :disabled="pending"
              @click="openTagEdit(item.id, item.name)"
            >
              <Pencil :size="16" />
            </button>
            <button
              v-if="!item.isDeleted"
              class="icon-button icon-button--danger"
              type="button"
              :title="translate('common.delete')"
              :aria-label="translate('common.delete')"
              :disabled="pending"
              @click="removeTag(item.id)"
            >
              <Trash2 :size="16" />
            </button>
            <button
              v-else
              class="icon-button"
              type="button"
              :title="translate('common.restore')"
              :aria-label="translate('common.restore')"
              :disabled="pending"
              @click="restoreTag(item.id)"
            >
              <RotateCcw :size="16" />
            </button>
          </div>
        </div>
        <div v-if="metadata.tagState !== 'loading' && !visibleTags.length" class="account-empty">
          <Tags :size="26" /><strong>{{ translate('settings.noTags') }}</strong>
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
        <label class="field">
          <span>{{ translate('settings.baseCurrency') }}</span>
          <select v-model="preferenceForm.baseCurrency" :disabled="pending">
            <option
              v-for="currency in userContext.currencies"
              :key="currency.code"
              :value="currency.code"
            >
              {{ currency.code }} · {{ currency.displayName }}
            </option>
          </select>
        </label>
        <label class="field">
          <span>{{ translate('common.language') }}</span>
          <select v-model="preferenceForm.locale" :disabled="pending">
            <option value="zh-CN">{{ translate('common.simplifiedChinese') }}</option>
            <option value="en-US">{{ translate('common.englishUS') }}</option>
          </select>
        </label>
        <label class="field">
          <span>{{ translate('settings.timezone') }}</span>
          <TimeZoneCombobox v-model="preferenceForm.timezone" :disabled="pending" />
        </label>
        <label class="field">
          <span>{{ translate('settings.dateFormat') }}</span>
          <select v-model="preferenceForm.dateFormat" :disabled="pending">
            <option value="YYYY-MM-DD">YYYY-MM-DD</option>
            <option value="DD/MM/YYYY">DD/MM/YYYY</option>
            <option value="MM/DD/YYYY">MM/DD/YYYY</option>
          </select>
        </label>
        <label class="field">
          <span>{{ translate('settings.numberFormat') }}</span>
          <select v-model="preferenceForm.numberFormat" :disabled="pending">
            <option value="1,234.56">1,234.56</option>
            <option value="1.234,56">1.234,56</option>
            <option value="1 234,56">1 234,56</option>
          </select>
        </label>
        <label class="field">
          <span>{{ translate('settings.theme') }}</span>
          <select v-model="preferenceForm.theme" :disabled="pending">
            <option value="system">{{ translate('settings.theme.system') }}</option>
            <option value="light">{{ translate('settings.theme.light') }}</option>
            <option value="dark">{{ translate('settings.theme.dark') }}</option>
          </select>
        </label>
        <label class="field">
          <span>{{ translate('settings.defaultHomePage') }}</span>
          <select v-model="preferenceForm.defaultHomePage" :disabled="pending">
            <option value="dashboard">{{ translate('nav.overview') }}</option>
            <option value="accounts">{{ translate('nav.accounts') }}</option>
            <option value="transactions">{{ translate('nav.transactions') }}</option>
            <option value="reports">{{ translate('nav.reports') }}</option>
          </select>
        </label>
        <label class="field">
          <span>{{ translate('settings.defaultReportPeriod') }}</span>
          <select v-model="preferenceForm.defaultReportPeriod" :disabled="pending">
            <option value="current_month">{{ translate('settings.period.currentMonth') }}</option>
            <option value="last_month">{{ translate('settings.period.lastMonth') }}</option>
            <option value="last_3_months">{{ translate('settings.period.last3Months') }}</option>
            <option value="current_year">{{ translate('settings.period.currentYear') }}</option>
            <option value="custom">{{ translate('settings.period.custom') }}</option>
          </select>
        </label>
        <template v-if="preferenceForm.defaultReportPeriod === 'custom'">
          <label class="field">
            <span>{{ translate('settings.customReportStart') }}</span>
            <input
              v-model="preferenceForm.customReportStartMonth"
              type="month"
              :max="preferenceForm.customReportEndMonth ?? undefined"
              :disabled="pending"
              required
            />
          </label>
          <label class="field">
            <span>{{ translate('settings.customReportEnd') }}</span>
            <input
              v-model="preferenceForm.customReportEndMonth"
              type="month"
              :min="preferenceForm.customReportStartMonth ?? undefined"
              :disabled="pending"
              required
            />
          </label>
        </template>
        <p v-if="preferenceValidationError" class="form-alert preference-form__error" role="alert">
          {{ preferenceValidationError }}
        </p>
        <div class="preference-actions">
          <button class="button" type="submit" :disabled="pending">
            <Save :size="16" /> {{ translate('settings.savePreferences') }}
          </button>
        </div>
      </form>
    </section>

    <ModalDialog
      :open="categoryDialogOpen"
      :title="translate(editingCategory ? 'settings.editCategory' : 'settings.newCategory')"
      @close="categoryDialogOpen = false"
    >
      <form class="settings-dialog-form" @submit.prevent="submitCategory">
        <label class="field">
          <span>{{ translate('settings.name') }}</span>
          <input v-model.trim="categoryForm.name" required maxlength="128" :disabled="pending" />
        </label>
        <label v-if="!editingCategory" class="field">
          <span>{{ translate('settings.parentCategory') }}</span>
          <select v-model="categoryForm.parentId" :disabled="pending">
            <option value="">{{ translate('settings.noParent') }}</option>
            <option v-for="item in parentOptions" :key="item.id" :value="item.id">
              {{ '· '.repeat(item.depth) }}{{ item.name }}
            </option>
          </select>
        </label>
        <label v-if="editingCategory" class="field">
          <span>{{ translate('settings.sortOrder') }}</span>
          <input
            v-model.number="categoryForm.sortOrder"
            type="number"
            required
            :disabled="pending"
          />
        </label>
        <div class="modal-actions">
          <button
            class="button button--quiet"
            type="button"
            :disabled="pending"
            @click="categoryDialogOpen = false"
          >
            {{ translate('common.cancel') }}</button
          ><button class="button" type="submit" :disabled="pending">
            <Save :size="16" /> {{ translate('common.save') }}
          </button>
        </div>
      </form>
    </ModalDialog>

    <ModalDialog
      :open="tagDialogOpen"
      :title="translate(editingTagId ? 'settings.renameTag' : 'settings.newTag')"
      @close="tagDialogOpen = false"
    >
      <form class="settings-dialog-form" @submit.prevent="submitTag">
        <label class="field">
          <span>{{ translate('settings.name') }}</span>
          <input v-model.trim="tagName" required maxlength="64" :disabled="pending" />
        </label>
        <div class="modal-actions">
          <button
            class="button button--quiet"
            type="button"
            :disabled="pending"
            @click="tagDialogOpen = false"
          >
            {{ translate('common.cancel') }}</button
          ><button class="button" type="submit" :disabled="pending">
            <Save :size="16" /> {{ translate('common.save') }}
          </button>
        </div>
      </form>
    </ModalDialog>
  </AppShell>
</template>
