import { ref } from 'vue';
import { defineStore } from 'pinia';
import {
  correctTransaction,
  createTransaction,
  deleteTransaction,
  getTransaction,
  listTransactions,
  newFinancialIntentKey,
  type CreateTransactionRequest,
  type Transaction,
  type TransactionFilters,
} from '../services/transaction-api';
import { useUserContextStore } from './user-context';

export const useTransactionStore = defineStore('transactions', () => {
  const items = ref<Transaction[]>([]);
  const nextCursor = ref<string | null>(null);
  const listState = ref<'idle' | 'loading' | 'loading-more' | 'ready' | 'error'>('idle');
  const selected = ref<Transaction | null>(null);
  const detailState = ref<'idle' | 'loading' | 'ready' | 'error'>('idle');
  let currentFilters: TransactionFilters = {};
  let lifecycle = 0;
  let listRequest = 0;
  let detailRequest = 0;
  let listController: AbortController | null = null;
  let detailController: AbortController | null = null;
  let actionController: AbortController | null = null;

  function replaceItem(transaction: Transaction): void {
    const index = items.value.findIndex(({ id }) => id === transaction.id);
    if (index >= 0) items.value.splice(index, 1, transaction);
  }

  async function load(filters: TransactionFilters): Promise<boolean> {
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
      const page = await listTransactions(filters, undefined, controller.signal);
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
    const cursor = nextCursor.value;
    const requestLifecycle = lifecycle;
    const request = ++listRequest;
    listController?.abort();
    const controller = new AbortController();
    listController = controller;
    listState.value = 'loading-more';
    try {
      const page = await listTransactions(currentFilters, cursor, controller.signal);
      if (requestLifecycle !== lifecycle || request !== listRequest || controller.signal.aborted) {
        return false;
      }
      const known = new Set(items.value.map(({ id }) => id));
      items.value.push(...page.items.filter(({ id }) => !known.has(id)));
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

  async function loadDetail(transactionId: number): Promise<boolean> {
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
      const transaction = await getTransaction(transactionId, controller.signal);
      if (
        requestLifecycle !== lifecycle ||
        request !== detailRequest ||
        controller.signal.aborted
      ) {
        return false;
      }
      selected.value = transaction;
      replaceItem(transaction);
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
      throw new DOMException('Transaction request is no longer current.', 'AbortError');
    }
    actionController = null;
  }

  async function create(payload: CreateTransactionRequest): Promise<Transaction> {
    const action = beginAction();
    const intentKey = newFinancialIntentKey('transaction');
    const result = await createTransaction(payload, intentKey, action.controller.signal);
    ensureCurrent(action);
    useUserContextStore().invalidateAggregates();
    return result;
  }

  async function correctSelected(payload: CreateTransactionRequest): Promise<Transaction> {
    if (!selected.value) throw new Error('No current transaction is available.');
    const action = beginAction();
    const intentKey = newFinancialIntentKey('correction');
    const result = await correctTransaction(
      selected.value.id,
      payload,
      intentKey,
      action.controller.signal,
    );
    ensureCurrent(action);
    items.value = items.value.filter(({ id }) => id !== selected.value?.id);
    selected.value = result;
    useUserContextStore().invalidateAggregates();
    return result;
  }

  async function deleteSelected(): Promise<void> {
    if (!selected.value) throw new Error('No current transaction is available.');
    const id = selected.value.id;
    const action = beginAction();
    await deleteTransaction(id, action.controller.signal);
    ensureCurrent(action);
    items.value = items.value.filter((item) => item.id !== id);
    selected.value = null;
    detailState.value = 'idle';
    useUserContextStore().invalidateAggregates();
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
