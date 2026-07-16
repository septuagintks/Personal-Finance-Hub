import type { components, operations } from '../generated/api-types';
import { http, registerRefreshHandler, skipRefresh } from './http';
import { ApiError, toApiError } from './api-error';
import { serializeRefresh } from './refresh-coordinator';

type RegisterRequest = components['schemas']['RegisterRequest'];
type LoginRequest = components['schemas']['LoginRequest'];
export type WebTokenPair = components['schemas']['WebTokenPair'];
export type WebRegisterResponse = components['schemas']['WebRegisterResponse'];

function readResponse<T>(response: { data: T }): T {
  return response.data;
}

export async function registerWeb(
  payload: RegisterRequest,
  signal?: AbortSignal,
): Promise<WebRegisterResponse> {
  try {
    const response = await http.post<
      operations['registerWebUser']['responses'][201]['content']['application/json']
    >('/api/v1/web/auth/register', payload, skipRefresh({ signal }));
    return readResponse(response);
  } catch (error) {
    throw error instanceof ApiError ? error : new ApiError(toApiError(error));
  }
}

export async function loginWeb(payload: LoginRequest, signal?: AbortSignal): Promise<WebTokenPair> {
  const response = await http.post<
    operations['loginWebUser']['responses'][200]['content']['application/json']
  >('/api/v1/web/auth/login', payload, skipRefresh({ signal }));
  return readResponse(response);
}

export async function refreshWeb(signal?: AbortSignal): Promise<WebTokenPair> {
  const response = await http.post<
    operations['refreshWebSession']['responses'][200]['content']['application/json']
  >('/api/v1/web/auth/refresh', undefined, skipRefresh({ signal }));
  return readResponse(response);
}

export async function logoutWeb(): Promise<void> {
  await http.post('/api/v1/web/auth/logout', undefined, { _pfhReplayAfterRefresh: true });
}

export function installRefreshHandler(): void {
  registerRefreshHandler((signal) =>
    serializeRefresh(async () => {
      const response = await refreshWeb(signal);
      return response.accessToken;
    }),
  );
}
