import axios, { type AxiosError, type AxiosInstance, type InternalAxiosRequestConfig } from 'axios';
import { ApiError, toApiError } from './api-error';

declare module 'axios' {
  interface AxiosRequestConfig {
    _pfhRetry?: boolean;
    _pfhSkipRefresh?: boolean;
    _pfhReplayAfterRefresh?: boolean;
  }

  interface InternalAxiosRequestConfig {
    _pfhRetry?: boolean;
    _pfhSkipRefresh?: boolean;
    _pfhReplayAfterRefresh?: boolean;
  }
}

let accessToken: string | null = null;
let refreshPromise: Promise<string | null> | null = null;
let refreshGeneration = 0;
let refreshController: AbortController | null = null;
let refreshHandler: ((signal: AbortSignal) => Promise<string>) | null = null;
let sessionExpiredHandler: (() => void) | null = null;

export const http: AxiosInstance = axios.create({
  baseURL: import.meta.env.VITE_API_BASE_URL ?? '',
  withCredentials: true,
  headers: { Accept: 'application/json' },
  timeout: 12_000,
});

export function setAccessToken(token: string | null): void {
  accessToken = token;
}

export function getAccessToken(): string | null {
  return accessToken;
}

export function registerRefreshHandler(handler: (signal: AbortSignal) => Promise<string>): void {
  refreshHandler = handler;
}

export function registerSessionExpiredHandler(handler: () => void): void {
  sessionExpiredHandler = handler;
}

export function clearRefreshState(): void {
  refreshGeneration += 1;
  refreshController?.abort();
  refreshController = null;
  refreshPromise = null;
}

function withBearer(config: InternalAxiosRequestConfig): InternalAxiosRequestConfig {
  if (accessToken) {
    config.headers.set('Authorization', `Bearer ${accessToken}`);
  }
  return config;
}

async function refreshOnce(): Promise<string | null> {
  if (!refreshHandler) return null;
  if (!refreshPromise) {
    const generation = refreshGeneration;
    const controller = new AbortController();
    refreshController = controller;
    const pending = refreshHandler(controller.signal)
      .then((token) => {
        if (generation !== refreshGeneration) return null;
        setAccessToken(token);
        return token;
      })
      .catch(() => null)
      .finally(() => {
        if (refreshPromise === pending) {
          refreshPromise = null;
          if (refreshController === controller) refreshController = null;
        }
      });
    refreshPromise = pending;
  }
  return refreshPromise;
}

function canReplayAfterRefresh(config: InternalAxiosRequestConfig): boolean {
  if (config._pfhReplayAfterRefresh) return true;
  const method = config.method?.toUpperCase() ?? 'GET';
  if (['GET', 'HEAD', 'OPTIONS'].includes(method)) return true;
  return config.headers.has('Idempotency-Key');
}

http.interceptors.request.use((config) => withBearer(config));
http.interceptors.response.use(
  (response) => response,
  async (error: AxiosError) => {
    const config = error.config;
    if (
      error.response?.status === 401 &&
      config &&
      !config._pfhRetry &&
      !config._pfhSkipRefresh &&
      canReplayAfterRefresh(config) &&
      !String(config.url).includes('/api/v1/web/auth/refresh')
    ) {
      config._pfhRetry = true;
      const generation = refreshGeneration;
      const token = await refreshOnce();
      if (token) {
        return http(config);
      }
      if (generation === refreshGeneration) {
        setAccessToken(null);
        sessionExpiredHandler?.();
      }
    }
    throw new ApiError(toApiError(error));
  },
);

export function skipRefresh(
  config?: import('axios').AxiosRequestConfig,
): import('axios').AxiosRequestConfig {
  return { ...(config ?? {}), _pfhSkipRefresh: true };
}
