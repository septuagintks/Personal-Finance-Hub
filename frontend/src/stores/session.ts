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

type RegisterRequest = components['schemas']['RegisterRequest'];
type LoginRequest = components['schemas']['LoginRequest'];

installRefreshHandler();

export const useSessionStore = defineStore('session', () => {
  const status = ref<'idle' | 'restoring' | 'authenticated' | 'anonymous'>('idle');
  const userId = ref<number | null>(null);
  const expiresAt = ref<number | null>(null);
  let restorePromise: Promise<void> | null = null;
  let stopChannel: (() => void) | null = null;

  const isAuthenticated = computed(
    () => status.value === 'authenticated' && getAccessToken() !== null,
  );
  const isBusy = computed(() => status.value === 'restoring');

  function applyToken(pair: WebTokenPair | WebRegisterResponse): void {
    setAccessToken(pair.accessToken);
    expiresAt.value = Date.now() + pair.expiresIn * 1000;
    status.value = 'authenticated';
  }

  function clear(): void {
    setAccessToken(null);
    clearRefreshState();
    userId.value = null;
    expiresAt.value = null;
    status.value = 'anonymous';
  }

  async function restore(): Promise<void> {
    if (status.value === 'authenticated') return;
    if (restorePromise) return restorePromise;
    status.value = 'restoring';
    restorePromise = serializeRefresh(async () => {
      try {
        const pair = await refreshWeb();
        applyToken(pair);
        broadcastSessionState('authenticated');
      } catch {
        clear();
      }
    }).finally(() => {
      restorePromise = null;
    });
    await restorePromise;
  }

  async function register(payload: RegisterRequest): Promise<void> {
    const result = await registerWeb(payload);
    userId.value = result.userId;
    applyToken(result);
    broadcastSessionState('authenticated');
  }

  async function login(payload: LoginRequest): Promise<void> {
    const result = await loginWeb(payload);
    applyToken(result);
    broadcastSessionState('authenticated');
  }

  async function logout(): Promise<void> {
    try {
      if (isAuthenticated.value) await logoutWeb();
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
    restore,
    register,
    login,
    logout,
    errorMessage,
  };
});
