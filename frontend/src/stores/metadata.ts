import { computed, ref } from 'vue';
import { defineStore } from 'pinia';
import {
  createCategory,
  createTag,
  deleteCategory,
  deleteTag,
  listCategories,
  listTags,
  restoreCategory,
  restoreTag,
  updateCategory,
  updateTag,
  type Category,
  type CategoryTree,
  type CreateCategoryRequest,
  type Tag,
} from '../services/metadata-api';
import { createIntentKeyTracker } from '../services/idempotency';

type MetadataResource = 'categories' | 'tags';

interface QueuedMutation {
  generation: number;
  resource: MetadataResource;
  execute(signal: AbortSignal): Promise<unknown>;
  confirm?: () => void;
  resolve(value: unknown): void;
  reject(reason: unknown): void;
}

export const useMetadataStore = defineStore('metadata', () => {
  const categories = ref<CategoryTree[]>([]);
  const tags = ref<Tag[]>([]);
  const categoryState = ref<'idle' | 'loading' | 'ready' | 'error'>('idle');
  const tagState = ref<'idle' | 'loading' | 'ready' | 'error'>('idle');
  const mutationCount = ref(0);
  const mutationPending = computed(() => mutationCount.value > 0);
  let lifecycle = 0;
  let categoryRequest = 0;
  let tagRequest = 0;
  let categoryController: AbortController | null = null;
  let tagController: AbortController | null = null;
  let currentMutationController: AbortController | null = null;
  let drainingMutations = false;
  const mutationQueue: QueuedMutation[] = [];
  const categoryCreateIntent = createIntentKeyTracker('category');
  const tagCreateIntent = createIntentKeyTracker('tag');

  async function loadCategories(): Promise<boolean> {
    const currentLifecycle = lifecycle;
    const request = ++categoryRequest;
    categoryController?.abort();
    const controller = new AbortController();
    categoryController = controller;
    categoryState.value = 'loading';
    try {
      const result = await listCategories('all', controller.signal);
      if (
        lifecycle !== currentLifecycle ||
        request !== categoryRequest ||
        controller.signal.aborted
      ) {
        return false;
      }
      categories.value = result;
      categoryState.value = 'ready';
      return true;
    } catch (error) {
      if (
        lifecycle !== currentLifecycle ||
        request !== categoryRequest ||
        controller.signal.aborted
      ) {
        return false;
      }
      categories.value = [];
      categoryState.value = 'error';
      throw error;
    } finally {
      if (categoryController === controller) categoryController = null;
    }
  }

  async function loadTags(): Promise<boolean> {
    const currentLifecycle = lifecycle;
    const request = ++tagRequest;
    tagController?.abort();
    const controller = new AbortController();
    tagController = controller;
    tagState.value = 'loading';
    try {
      const result = await listTags('all', controller.signal);
      if (lifecycle !== currentLifecycle || request !== tagRequest || controller.signal.aborted) {
        return false;
      }
      tags.value = result;
      tagState.value = 'ready';
      return true;
    } catch (error) {
      if (lifecycle !== currentLifecycle || request !== tagRequest || controller.signal.aborted) {
        return false;
      }
      tags.value = [];
      tagState.value = 'error';
      throw error;
    } finally {
      if (tagController === controller) tagController = null;
    }
  }

  function cancelledMutation(): DOMException {
    return new DOMException('Metadata request belongs to an expired session.', 'AbortError');
  }

  function ensureMutationCurrent(task: QueuedMutation, controller: AbortController): void {
    if (
      task.generation !== lifecycle ||
      currentMutationController !== controller ||
      controller.signal.aborted
    ) {
      throw cancelledMutation();
    }
  }

  async function reloadMutationResource(
    task: QueuedMutation,
    controller: AbortController,
  ): Promise<void> {
    const load = task.resource === 'categories' ? loadCategories : loadTags;
    let loaded = await load();
    ensureMutationCurrent(task, controller);
    if (!loaded) {
      loaded = await load();
      ensureMutationCurrent(task, controller);
    }
    if (!loaded) throw cancelledMutation();
  }

  function finishMutation(): void {
    mutationCount.value = Math.max(0, mutationCount.value - 1);
  }

  async function drainMutationQueue(): Promise<void> {
    if (drainingMutations) return;
    drainingMutations = true;
    try {
      while (mutationQueue.length) {
        const task = mutationQueue.shift();
        if (!task) continue;
        if (task.generation !== lifecycle) {
          task.reject(cancelledMutation());
          finishMutation();
          continue;
        }

        const controller = new AbortController();
        currentMutationController = controller;
        let mutationSucceeded = false;
        let result: unknown;
        try {
          result = await task.execute(controller.signal);
          ensureMutationCurrent(task, controller);
          mutationSucceeded = true;
          await reloadMutationResource(task, controller);
          task.confirm?.();
          task.resolve(result);
        } catch (error) {
          if (
            task.generation === lifecycle &&
            currentMutationController === controller &&
            !controller.signal.aborted
          ) {
            try {
              await reloadMutationResource(task, controller);
              if (mutationSucceeded) {
                task.confirm?.();
                task.resolve(result);
                continue;
              }
            } catch {
              // Preserve the original mutation error; the resource state records reload failure.
            }
          }
          task.reject(error);
        } finally {
          if (currentMutationController === controller) currentMutationController = null;
          finishMutation();
        }
      }
    } finally {
      drainingMutations = false;
      if (mutationQueue.length) void drainMutationQueue();
    }
  }

  function enqueueMutation<T>(
    resource: MetadataResource,
    execute: (signal: AbortSignal) => Promise<T>,
    confirm?: () => void,
  ): Promise<T> {
    const generation = lifecycle;
    mutationCount.value += 1;
    const pending = new Promise<T>((resolve, reject) => {
      mutationQueue.push({
        generation,
        resource,
        execute,
        confirm,
        resolve: (value) => resolve(value as T),
        reject,
      });
    });
    void drainMutationQueue();
    return pending;
  }

  function createCategoryItem(payload: CreateCategoryRequest): Promise<Category> {
    const intentKey = categoryCreateIntent.keyFor(payload);
    return enqueueMutation(
      'categories',
      (signal) => createCategory(payload, intentKey, signal),
      () => categoryCreateIntent.complete(intentKey),
    );
  }

  function updateCategoryItem(id: number, name: string, sortOrder: number): Promise<Category> {
    return enqueueMutation('categories', (signal) =>
      updateCategory(id, { name, sortOrder }, signal),
    );
  }

  function deleteCategoryItem(id: number): Promise<void> {
    return enqueueMutation('categories', (signal) => deleteCategory(id, signal));
  }

  function restoreCategoryItem(id: number): Promise<void> {
    return enqueueMutation('categories', async (signal) => {
      await restoreCategory(id, signal);
    });
  }

  function createTagItem(name: string): Promise<Tag> {
    const payload = { name };
    const intentKey = tagCreateIntent.keyFor(payload);
    return enqueueMutation(
      'tags',
      (signal) => createTag(payload, intentKey, signal),
      () => tagCreateIntent.complete(intentKey),
    );
  }

  function updateTagItem(id: number, name: string): Promise<Tag> {
    return enqueueMutation('tags', (signal) => updateTag(id, { name }, signal));
  }

  function deleteTagItem(id: number): Promise<void> {
    return enqueueMutation('tags', (signal) => deleteTag(id, signal));
  }

  function restoreTagItem(id: number): Promise<void> {
    return enqueueMutation('tags', async (signal) => {
      await restoreTag(id, signal);
    });
  }

  function clear(): void {
    lifecycle += 1;
    categoryRequest += 1;
    tagRequest += 1;
    categoryController?.abort();
    tagController?.abort();
    currentMutationController?.abort();
    const cancelled = cancelledMutation();
    for (const task of mutationQueue.splice(0)) {
      task.reject(cancelled);
      finishMutation();
    }
    categoryCreateIntent.clear();
    tagCreateIntent.clear();
    categoryController = null;
    tagController = null;
    currentMutationController = null;
    categories.value = [];
    tags.value = [];
    categoryState.value = 'idle';
    tagState.value = 'idle';
  }

  return {
    categories,
    tags,
    categoryState,
    tagState,
    mutationPending,
    loadCategories,
    loadTags,
    createCategoryItem,
    updateCategoryItem,
    deleteCategoryItem,
    restoreCategoryItem,
    createTagItem,
    updateTagItem,
    deleteTagItem,
    restoreTagItem,
    clear,
  };
});
