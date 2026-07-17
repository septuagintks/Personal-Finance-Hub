import { computed, ref } from 'vue';
import { defineStore } from 'pinia';
import type { components } from '../generated/api-types';
import {
  installRefreshHandler,
  loginWeb,
  logoutWeb,
  refreshWeb,
  registerWeb,
  type WebRegisterResponse,
  type WebTokenPair,
} from '../services/auth-api';
import {
  clearRefreshState,
  getAccessToken,
  registerSessionExpiredHandler,
  setAccessToken,
} from '../services/http';
import {
  broadcastSessionState,
  onSessionState,
  serializeRefresh,
} from '../services/refresh-coordinator';
import { ApiError } from '../services/api-error';
import { useAccountStore } from './accounts';
import { useUserContextStore } from './user-context';
import { useMetadataStore } from './metadata';
import { useTransactionStore } from './transactions';
import { useTransferStore } from './transfers';

type RegisterRequest = components['schemas']['RegisterRequest'];
type LoginRequest = components['schemas']['LoginRequest'];
const availableHomeRoutes: Record<string, string> = {
  dashboard: '/dashboard',
  accounts: '/accounts',
  transactions: '/transactions',
  reports: '/reports',
};

installRefreshHandler();

export const useSessionStore = defineStore('session', () => {
  const userContext = useUserContextStore();
  const accounts = useAccountStore();
  const metadata = useMetadataStore();
  const transactions = useTransactionStore();
  const transfers = useTransferStore();
  const status = ref<'idle' | 'restoring' | 'authenticated' | 'anonymous'>('idle');
  const userId = ref<number | null>(null);
  const expiresAt = ref<number | null>(null);
  let restorePromise: Promise<void> | null = null;
  let stopChannel: (() => void) | null = null;
  let lifecycleGeneration = 0;
  let lifecycleController: AbortController | null = null;

  const isAuthenticated = computed(
    () => status.value === 'authenticated' && getAccessToken() !== null,
  );
  const isBusy = computed(() => status.value === 'restoring');

  const defaultHomeRoute = computed(() => {
    const configured = userContext.preference?.defaultHomePage ?? 'dashboard';
    return availableHomeRoutes[configured] ?? '/dashboard';
  });

  function beginAuthentication(): { generation: number; controller: AbortController } {
    lifecycleGeneration += 1;
    lifecycleController?.abort();
    const controller = new AbortController();
    lifecycleController = controller;
    setAccessToken(null);
    clearRefreshState();
    userContext.clear();
    accounts.clear();
    metadata.clear();
    transactions.clear();
    transfers.clear();
    userId.value = null;
    expiresAt.value = null;
    status.value = 'restoring';
    return { generation: lifecycleGeneration, controller };
  }

  function isCurrent(generation: number, controller: AbortController): boolean {
    return lifecycleGeneration === generation && !controller.signal.aborted;
  }

  async function completeAuthentication(
    attempt: { generation: number; controller: AbortController },
    pair: WebTokenPair | WebRegisterResponse,
    nextUserId: number | null = null,
  ): Promise<boolean> {
    if (!isCurrent(attempt.generation, attempt.controller)) return false;
    setAccessToken(pair.accessToken);
    expiresAt.value = Date.now() + pair.expiresIn * 1000;
    const contextLoaded = await userContext.load();
    if (!contextLoaded || !isCurrent(attempt.generation, attempt.controller)) return false;

    userId.value = nextUserId;
    status.value = 'authenticated';
    lifecycleController = null;
    return true;
  }

  function clear(): void {
    lifecycleGeneration += 1;
    lifecycleController?.abort();
    lifecycleController = null;
    setAccessToken(null);
    clearRefreshState();
    userContext.clear();
    accounts.clear();
    metadata.clear();
    transactions.clear();
    transfers.clear();
    userId.value = null;
    expiresAt.value = null;
    status.value = 'anonymous';
  }

  async function restore(): Promise<void> {
    if (status.value === 'authenticated') return;
    if (restorePromise) return restorePromise;
    const attempt = beginAuthentication();
    restorePromise = (async () => {
      try {
        const pair = await serializeRefresh(() => refreshWeb(attempt.controller.signal));
        if (await completeAuthentication(attempt, pair)) {
          broadcastSessionState('authenticated');
        }
      } catch {
        if (isCurrent(attempt.generation, attempt.controller)) clear();
      }
    })().finally(() => {
      restorePromise = null;
    });
    await restorePromise;
  }

  async function register(payload: RegisterRequest): Promise<void> {
    const attempt = beginAuthentication();
    try {
      const result = await registerWeb(payload, attempt.controller.signal);
      if (await completeAuthentication(attempt, result, result.userId)) {
        broadcastSessionState('authenticated');
      }
    } catch (error) {
      if (!isCurrent(attempt.generation, attempt.controller)) return;
      clear();
      throw error;
    }
  }

  async function login(payload: LoginRequest): Promise<void> {
    const attempt = beginAuthentication();
    try {
      const result = await loginWeb(payload, attempt.controller.signal);
      if (await completeAuthentication(attempt, result)) {
        broadcastSessionState('authenticated');
      }
    } catch (error) {
      if (!isCurrent(attempt.generation, attempt.controller)) return;
      clear();
      throw error;
    }
  }

  async function logout(): Promise<void> {
    const shouldRevokeServerSession = isAuthenticated.value;
    try {
      if (shouldRevokeServerSession) {
        await serializeRefresh(async () => {
          try {
            await logoutWeb();
          } catch (error) {
            if (!(error instanceof ApiError) || error.details.status !== 401) throw error;
            const pair = await refreshWeb();
            setAccessToken(pair.accessToken);
            await logoutWeb();
          }
        });
      }
    } finally {
      clear();
      broadcastSessionState('anonymous');
    }
  }

  function startChannel(): void {
    if (stopChannel) return;
    stopChannel = onSessionState((state) => {
      if (state === 'anonymous') clear();
    });
  }

  function errorMessage(error: unknown): string {
    if (error instanceof ApiError) return error.details.message;
    return 'Something unexpected happened.';
  }

  startChannel();
  registerSessionExpiredHandler(() => {
    clear();
    broadcastSessionState('anonymous');
  });

  return {
    status,
    userId,
    expiresAt,
    isAuthenticated,
    isBusy,
    defaultHomeRoute,
    restore,
    register,
    login,
    logout,
    errorMessage,
  };
});
