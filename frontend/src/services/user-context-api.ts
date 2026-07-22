import type { components } from '../generated/api-types';
import { http } from './http';

export type UserPreference = components['schemas']['UserPreference'];
export type CurrencyMetadata = components['schemas']['CurrencyMetadata'];
export type TimeZoneMetadata = components['schemas']['TimeZoneMetadata'];

export async function getUserPreferences(signal?: AbortSignal): Promise<UserPreference> {
  const response = await http.get<UserPreference>('/api/v1/users/me/preferences', { signal });
  return response.data;
}

export async function updateUserPreferences(
  payload: UserPreference,
  signal?: AbortSignal,
): Promise<UserPreference> {
  const response = await http.put<UserPreference>('/api/v1/users/me/preferences', payload, {
    _pfhReplayAfterRefresh: true,
    signal,
  });
  return response.data;
}

export async function listCurrencies(signal?: AbortSignal): Promise<CurrencyMetadata[]> {
  const response = await http.get<CurrencyMetadata[]>('/api/v1/currencies', { signal });
  return response.data;
}

export async function listTimeZones(signal?: AbortSignal): Promise<TimeZoneMetadata[]> {
  const response = await http.get<TimeZoneMetadata[]>('/api/v1/timezones', { signal });
  return response.data;
}
