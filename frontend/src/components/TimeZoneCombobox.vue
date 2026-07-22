<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { Check, ChevronsUpDown, LoaderCircle, Search } from '@lucide/vue';
import { translate } from '../i18n';
import { listTimeZones, type TimeZoneMetadata } from '../services/user-context-api';

const props = withDefaults(
  defineProps<{
    modelValue: string;
    disabled?: boolean;
  }>(),
  { disabled: false },
);

const emit = defineEmits<{ 'update:modelValue': [value: string] }>();
const root = ref<HTMLElement | null>(null);
const input = ref<HTMLInputElement | null>(null);
const options = ref<TimeZoneMetadata[]>([]);
const query = ref(props.modelValue);
const open = ref(false);
const loading = ref(false);
const failed = ref(false);
const activeIndex = ref(-1);
let controller: AbortController | null = null;

const normalizedQuery = computed(() => query.value.trim().toLocaleLowerCase());
const matches = computed(() => {
  const search = normalizedQuery.value;
  const ranked = options.value
    .filter((item) => {
      if (!search) return true;
      return (
        item.id.toLocaleLowerCase().includes(search) ||
        item.canonicalId.toLocaleLowerCase().includes(search)
      );
    })
    .sort((left, right) => {
      if (!search) return left.id.localeCompare(right.id);
      const leftPrefix = left.id.toLocaleLowerCase().startsWith(search);
      const rightPrefix = right.id.toLocaleLowerCase().startsWith(search);
      return leftPrefix === rightPrefix ? left.id.localeCompare(right.id) : leftPrefix ? -1 : 1;
    });
  return ranked.slice(0, 100);
});

const activeDescendant = computed(() =>
  activeIndex.value >= 0 ? `timezone-option-${activeIndex.value}` : undefined,
);

watch(
  () => props.modelValue,
  (value) => {
    if (!open.value) query.value = value;
  },
);

watch(matches, () => {
  activeIndex.value = matches.value.length ? 0 : -1;
});

async function load(): Promise<void> {
  if (options.value.length || loading.value) return;
  controller?.abort();
  controller = new AbortController();
  loading.value = true;
  failed.value = false;
  try {
    options.value = await listTimeZones(controller.signal);
  } catch {
    if (!controller.signal.aborted) failed.value = true;
  } finally {
    loading.value = false;
    controller = null;
  }
}

async function show(): Promise<void> {
  if (props.disabled) return;
  query.value = '';
  open.value = true;
  await load();
  await nextTick();
  input.value?.select();
}

function select(item: TimeZoneMetadata): void {
  emit('update:modelValue', item.id);
  query.value = item.id;
  open.value = false;
  activeIndex.value = -1;
}

function close(): void {
  query.value = props.modelValue;
  open.value = false;
  activeIndex.value = -1;
}

function handleKeydown(event: KeyboardEvent): void {
  if (event.key === 'Escape') {
    event.preventDefault();
    close();
    return;
  }
  if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
    event.preventDefault();
    if (!open.value) {
      void show();
      return;
    }
    const direction = event.key === 'ArrowDown' ? 1 : -1;
    activeIndex.value = Math.min(
      Math.max(activeIndex.value + direction, 0),
      Math.max(matches.value.length - 1, 0),
    );
    return;
  }
  if (event.key === 'Enter' && open.value && activeIndex.value >= 0) {
    event.preventDefault();
    const item = matches.value[activeIndex.value];
    if (item) select(item);
  }
}

function handleFocusOut(event: FocusEvent): void {
  const next = event.relatedTarget;
  if (!(next instanceof Node) || !root.value?.contains(next)) close();
}

onMounted(load);
onBeforeUnmount(() => controller?.abort());
</script>

<template>
  <div ref="root" class="timezone-combobox" @focusout="handleFocusOut">
    <div class="timezone-combobox__input">
      <Search v-if="open" :size="15" aria-hidden="true" />
      <input
        ref="input"
        v-model="query"
        role="combobox"
        autocomplete="off"
        aria-autocomplete="list"
        aria-controls="timezone-options"
        :aria-expanded="open"
        :aria-activedescendant="activeDescendant"
        :aria-label="translate('settings.timezoneSearch')"
        :disabled="disabled"
        :readonly="!open"
        @click="show"
        @focus="show"
        @keydown="handleKeydown"
      />
      <LoaderCircle v-if="loading" :size="16" class="spin" aria-hidden="true" />
      <ChevronsUpDown v-else :size="16" aria-hidden="true" />
    </div>
    <div v-if="open" id="timezone-options" class="timezone-combobox__options" role="listbox">
      <p v-if="failed" class="timezone-combobox__status" role="alert">
        {{ translate('settings.timezoneLoadFailed') }}
      </p>
      <p v-else-if="!loading && !matches.length" class="timezone-combobox__status">
        {{ translate('settings.timezoneNoMatches') }}
      </p>
      <button
        v-for="(item, index) in matches"
        v-else
        :id="`timezone-option-${index}`"
        :key="item.id"
        type="button"
        role="option"
        :aria-selected="item.id === modelValue"
        :class="{ 'is-active': index === activeIndex }"
        @mouseenter="activeIndex = index"
        @mousedown.prevent="select(item)"
      >
        <span>
          <strong>{{ item.id }}</strong>
          <small v-if="item.isAlias">{{ item.canonicalId }}</small>
        </span>
        <Check v-if="item.id === modelValue" :size="15" aria-hidden="true" />
      </button>
    </div>
  </div>
</template>
