import { computed, ref } from 'vue';
import { defineStore } from 'pinia';
import {
  archiveAccount,
  createAccount,
  deleteAccount,
  getAccount,
  getAccountBalance,
  listAccounts,
  restoreAccount,
  updateAccount,
  type Account,
  type AccountBalance,
  type AccountListStatus,
  type CreateAccountRequest,
  type UpdateAccountRequest,
} from '../services/account-api';

export const useAccountStore = defineStore('accounts', () => {
  const items = ref<Account[]>([]);
  const listStatus = ref<AccountListStatus>('active');
  const listState = ref<'idle' | 'loading' | 'ready' | 'error'>('idle');
  const selected = ref<Account | null>(null);
  const selectedBalance = ref<AccountBalance | null>(null);
  const selectedEtag = ref<string | null>(null);
  const detailState = ref<'idle' | 'loading' | 'ready' | 'error'>('idle');
  let lifecycleGeneration = 0;
  let listGeneration = 0;
  let detailGeneration = 0;
  let listController: AbortController | null = null;
  let detailController: AbortController | null = null;
  let actionController: AbortController | null = null;

  const selectedId = computed(() => selected.value?.id ?? null);

  function isCurrent(
    lifecycle: number,
    requestGeneration: number,
    currentGeneration: number,
    controller: AbortController,
  ): boolean {
    return (
      lifecycleGeneration === lifecycle &&
      requestGeneration === currentGeneration &&
      !controller.signal.aborted
    );
  }

  function beginAction(): { lifecycle: number; controller: AbortController } {
    actionController?.abort();
    const controller = new AbortController();
    actionController = controller;
    return { lifecycle: lifecycleGeneration, controller };
  }

  function actionIsCurrent(action: { lifecycle: number; controller: AbortController }): boolean {
    return (
      lifecycleGeneration === action.lifecycle &&
      actionController === action.controller &&
      !action.controller.signal.aborted
    );
  }

  function staleAction(): never {
    throw new DOMException('Account request is no longer current.', 'AbortError');
  }

  function replaceItem(account: Account): void {
    const index = items.value.findIndex(({ id }) => id === account.id);
    const matchesCurrentList =
      listStatus.value === 'all' ||
      (listStatus.value === 'active' && !account.isArchived) ||
      (listStatus.value === 'archived' && account.isArchived);
    if (index >= 0 && matchesCurrentList) {
      items.value.splice(index, 1, account);
    } else if (index >= 0) {
      items.value.splice(index, 1);
    } else if (matchesCurrentList) {
      items.value.push(account);
    }
  }

  async function loadList(status: AccountListStatus = listStatus.value): Promise<boolean> {
    const lifecycle = lifecycleGeneration;
    const requestGeneration = ++listGeneration;
    listController?.abort();
    const controller = new AbortController();
    listController = controller;
    listStatus.value = status;
    listState.value = 'loading';
    try {
      const result = await listAccounts(status, controller.signal);
      if (!isCurrent(lifecycle, requestGeneration, listGeneration, controller)) return false;
      items.value = result;
      listState.value = 'ready';
      return true;
    } catch (error) {
      if (!isCurrent(lifecycle, requestGeneration, listGeneration, controller)) return false;
      items.value = [];
      listState.value = 'error';
      throw error;
    } finally {
      if (listController === controller) listController = null;
    }
  }

  async function loadDetail(accountId: number): Promise<boolean> {
    const lifecycle = lifecycleGeneration;
    const requestGeneration = ++detailGeneration;
    detailController?.abort();
    const controller = new AbortController();
    detailController = controller;
    selected.value = null;
    selectedBalance.value = null;
    selectedEtag.value = null;
    detailState.value = 'loading';
    try {
      const [resource, balance] = await Promise.all([
        getAccount(accountId, controller.signal),
        getAccountBalance(accountId, controller.signal),
      ]);
      if (!isCurrent(lifecycle, requestGeneration, detailGeneration, controller)) return false;
      selected.value = resource.account;
      selectedBalance.value = balance;
      selectedEtag.value = resource.etag;
      replaceItem(resource.account);
      detailState.value = 'ready';
      return true;
    } catch (error) {
      if (!isCurrent(lifecycle, requestGeneration, detailGeneration, controller)) return false;
      selected.value = null;
      selectedBalance.value = null;
      selectedEtag.value = null;
      detailState.value = 'error';
      throw error;
    } finally {
      if (detailController === controller) detailController = null;
    }
  }

  async function create(payload: CreateAccountRequest): Promise<Account> {
    const action = beginAction();
    const account = await createAccount(payload, action.controller.signal);
    if (!actionIsCurrent(action)) return staleAction();
    replaceItem(account);
    actionController = null;
    return account;
  }

  async function updateSelected(payload: UpdateAccountRequest): Promise<Account> {
    if (!selected.value || !selectedEtag.value) {
      throw new Error('No current account resource is available.');
    }
    const action = beginAction();
    const resource = await updateAccount(
      selected.value.id,
      selectedEtag.value,
      payload,
      action.controller.signal,
    );
    if (!actionIsCurrent(action)) return staleAction();
    selected.value = resource.account;
    selectedEtag.value = resource.etag;
    replaceItem(resource.account);
    actionController = null;
    return resource.account;
  }

  async function archiveSelected(): Promise<void> {
    if (!selected.value || !selectedEtag.value) {
      throw new Error('No current account resource is available.');
    }
    const accountId = selected.value.id;
    const action = beginAction();
    await archiveAccount(accountId, selectedEtag.value, action.controller.signal);
    if (!actionIsCurrent(action)) return staleAction();
    actionController = null;
    await loadDetail(accountId);
  }

  async function restoreSelected(): Promise<void> {
    if (!selected.value || !selectedEtag.value) {
      throw new Error('No current account resource is available.');
    }
    const accountId = selected.value.id;
    const action = beginAction();
    await restoreAccount(accountId, selectedEtag.value, action.controller.signal);
    if (!actionIsCurrent(action)) return staleAction();
    actionController = null;
    await loadDetail(accountId);
  }

  async function permanentlyDeleteSelected(): Promise<void> {
    if (!selected.value) throw new Error('No current account resource is available.');
    const accountId = selected.value.id;
    const action = beginAction();
    await deleteAccount(accountId, action.controller.signal);
    if (!actionIsCurrent(action)) return staleAction();
    items.value = items.value.filter(({ id }) => id !== accountId);
    selected.value = null;
    selectedBalance.value = null;
    selectedEtag.value = null;
    detailState.value = 'idle';
    actionController = null;
  }

  function clear(): void {
    lifecycleGeneration += 1;
    listGeneration += 1;
    detailGeneration += 1;
    listController?.abort();
    detailController?.abort();
    actionController?.abort();
    listController = null;
    detailController = null;
    actionController = null;
    items.value = [];
    listStatus.value = 'active';
    listState.value = 'idle';
    selected.value = null;
    selectedBalance.value = null;
    selectedEtag.value = null;
    detailState.value = 'idle';
  }

  return {
    items,
    listStatus,
    listState,
    selected,
    selectedBalance,
    selectedEtag,
    selectedId,
    detailState,
    loadList,
    loadDetail,
    create,
    updateSelected,
    archiveSelected,
    restoreSelected,
    permanentlyDeleteSelected,
    clear,
  };
});
