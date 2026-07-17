import { ref } from 'vue';
import { defineStore } from 'pinia';
import {
  correctTransfer,
  createTransfer,
  deleteTransfer,
  getTransfer,
  listTransfers,
  newTransferIntentKey,
  type CreateTransferRequest,
  type Transfer,
  type TransferFilters,
} from '../services/transfer-api';
import { useTransactionStore } from './transactions';
import { useUserContextStore } from './user-context';

export const useTransferStore = defineStore('transfers', () => {
  const items = ref<Transfer[]>([]);
  const nextCursor = ref<string | null>(null);
  const listState = ref<'idle' | 'loading' | 'loading-more' | 'ready' | 'error'>('idle');
  const selected = ref<Transfer | null>(null);
  const detailState = ref<'idle' | 'loading' | 'ready' | 'error'>('idle');
  let currentFilters: TransferFilters = {};
  let lifecycle = 0;
  let listRequest = 0;
  let detailRequest = 0;
  let listController: AbortController | null = null;
  let detailController: AbortController | null = null;
  let actionController: AbortController | null = null;

  async function load(filters: TransferFilters): Promise<boolean> {
    actionController?.abort();
    actionController = null;
    const requestLifecycle = lifecycle;
    const request = ++listRequest;
    listController?.abort();
    const controller = new AbortController();
    listController = controller;
    currentFilters = { ...filters };
    items.value = [];
    nextCursor.value = null;
    listState.value = 'loading';
    try {
      const page = await listTransfers(filters, undefined, controller.signal);
      if (requestLifecycle !== lifecycle || request !== listRequest || controller.signal.aborted) {
        return false;
      }
      items.value = page.items;
      nextCursor.value = page.nextCursor;
      listState.value = 'ready';
      return true;
    } catch (error) {
      if (requestLifecycle !== lifecycle || request !== listRequest || controller.signal.aborted) {
        return false;
      }
      listState.value = 'error';
      throw error;
    } finally {
      if (listController === controller) listController = null;
    }
  }

  async function loadMore(): Promise<boolean> {
    if (!nextCursor.value || listState.value !== 'ready') return false;
    const requestLifecycle = lifecycle;
    const request = ++listRequest;
    listController?.abort();
    const controller = new AbortController();
    listController = controller;
    listState.value = 'loading-more';
    try {
      const page = await listTransfers(currentFilters, nextCursor.value, controller.signal);
      if (requestLifecycle !== lifecycle || request !== listRequest || controller.signal.aborted) {
        return false;
      }
      const known = new Set(items.value.map(({ transferGroupId }) => transferGroupId));
      items.value.push(...page.items.filter(({ transferGroupId }) => !known.has(transferGroupId)));
      nextCursor.value = page.nextCursor;
      listState.value = 'ready';
      return true;
    } catch (error) {
      if (requestLifecycle !== lifecycle || request !== listRequest || controller.signal.aborted) {
        return false;
      }
      listState.value = 'error';
      throw error;
    } finally {
      if (listController === controller) listController = null;
    }
  }

  async function loadDetail(transferGroupId: number): Promise<boolean> {
    actionController?.abort();
    actionController = null;
    const requestLifecycle = lifecycle;
    const request = ++detailRequest;
    detailController?.abort();
    const controller = new AbortController();
    detailController = controller;
    selected.value = null;
    detailState.value = 'loading';
    try {
      const transfer = await getTransfer(transferGroupId, controller.signal);
      if (
        requestLifecycle !== lifecycle ||
        request !== detailRequest ||
        controller.signal.aborted
      ) {
        return false;
      }
      selected.value = transfer;
      const index = items.value.findIndex(({ transferGroupId: id }) => id === transferGroupId);
      if (index >= 0) items.value.splice(index, 1, transfer);
      detailState.value = 'ready';
      return true;
    } catch (error) {
      if (
        requestLifecycle !== lifecycle ||
        request !== detailRequest ||
        controller.signal.aborted
      ) {
        return false;
      }
      detailState.value = 'error';
      throw error;
    } finally {
      if (detailController === controller) detailController = null;
    }
  }

  function beginAction(): { lifecycle: number; controller: AbortController } {
    actionController?.abort();
    const controller = new AbortController();
    actionController = controller;
    return { lifecycle, controller };
  }

  function ensureCurrent(action: { lifecycle: number; controller: AbortController }): void {
    if (
      action.lifecycle !== lifecycle ||
      actionController !== action.controller ||
      action.controller.signal.aborted
    ) {
      throw new DOMException('Transfer request is no longer current.', 'AbortError');
    }
    actionController = null;
  }

  function invalidateFinancialViews(): void {
    useTransactionStore().clear();
    useUserContextStore().invalidateAggregates();
  }

  async function create(payload: CreateTransferRequest): Promise<Transfer> {
    const action = beginAction();
    const result = await createTransfer(payload, newTransferIntentKey(), action.controller.signal);
    ensureCurrent(action);
    invalidateFinancialViews();
    return result;
  }

  async function correctSelected(payload: CreateTransferRequest): Promise<Transfer> {
    if (!selected.value) throw new Error('No current transfer is available.');
    const originalId = selected.value.transferGroupId;
    const action = beginAction();
    const result = await correctTransfer(
      originalId,
      payload,
      newTransferIntentKey(true),
      action.controller.signal,
    );
    ensureCurrent(action);
    items.value = items.value.filter(({ transferGroupId }) => transferGroupId !== originalId);
    selected.value = result;
    invalidateFinancialViews();
    return result;
  }

  async function deleteSelected(): Promise<void> {
    if (!selected.value) throw new Error('No current transfer is available.');
    const id = selected.value.transferGroupId;
    const action = beginAction();
    await deleteTransfer(id, action.controller.signal);
    ensureCurrent(action);
    items.value = items.value.filter(({ transferGroupId }) => transferGroupId !== id);
    selected.value = null;
    detailState.value = 'idle';
    invalidateFinancialViews();
  }

  function clear(): void {
    lifecycle += 1;
    listRequest += 1;
    detailRequest += 1;
    listController?.abort();
    detailController?.abort();
    actionController?.abort();
    listController = null;
    detailController = null;
    actionController = null;
    items.value = [];
    nextCursor.value = null;
    listState.value = 'idle';
    selected.value = null;
    detailState.value = 'idle';
    currentFilters = {};
  }

  return {
    items,
    nextCursor,
    listState,
    selected,
    detailState,
    load,
    loadMore,
    loadDetail,
    create,
    correctSelected,
    deleteSelected,
    clear,
  };
});
