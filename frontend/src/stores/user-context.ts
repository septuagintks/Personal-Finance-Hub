import { ref } from 'vue';
import { defineStore } from 'pinia';
import {
  getUserPreferences,
  listCurrencies,
  updateUserPreferences,
  type CurrencyMetadata,
  type UserPreference,
} from '../services/user-context-api';
import { resolvePublicLocale, setApplicationLocale } from '../i18n';

export const useUserContextStore = defineStore('user-context', () => {
  const preference = ref<UserPreference | null>(null);
  const currencies = ref<CurrencyMetadata[]>([]);
  const status = ref<'idle' | 'loading' | 'ready' | 'error'>('idle');
  const aggregationRevision = ref(0);
  let generation = 0;
  let requestController: AbortController | null = null;
  let preferenceRequest = 0;
  let preferenceController: AbortController | null = null;

  async function applyPresentation(value: UserPreference | null): Promise<void> {
    if (!value || value.theme === 'system') {
      delete document.documentElement.dataset.theme;
    } else {
      document.documentElement.dataset.theme = value.theme;
    }
    await setApplicationLocale(value?.locale ?? resolvePublicLocale());
  }

  async function load(): Promise<boolean> {
    const requestGeneration = ++generation;
    requestController?.abort();
    const controller = new AbortController();
    requestController = controller;
    preference.value = null;
    currencies.value = [];
    status.value = 'loading';

    try {
      const [nextPreference, nextCurrencies] = await Promise.all([
        getUserPreferences(controller.signal),
        listCurrencies(controller.signal),
      ]);
      if (requestGeneration !== generation || controller.signal.aborted) return false;

      preference.value = nextPreference;
      currencies.value = nextCurrencies;
      await applyPresentation(nextPreference);
      if (requestGeneration !== generation || controller.signal.aborted) return false;
      status.value = 'ready';
      return true;
    } catch (error) {
      if (requestGeneration !== generation || controller.signal.aborted) return false;
      controller.abort();
      preference.value = null;
      currencies.value = [];
      status.value = 'error';
      throw error;
    } finally {
      if (requestGeneration === generation) requestController = null;
    }
  }

  async function update(payload: UserPreference): Promise<UserPreference> {
    const requestGeneration = generation;
    const request = ++preferenceRequest;
    preferenceController?.abort();
    const controller = new AbortController();
    preferenceController = controller;
    try {
      const result = await updateUserPreferences(payload, controller.signal);
      if (
        requestGeneration !== generation ||
        request !== preferenceRequest ||
        controller.signal.aborted
      ) {
        throw new DOMException('Preference request is no longer current.', 'AbortError');
      }
      await applyPresentation(result);
      if (
        requestGeneration !== generation ||
        request !== preferenceRequest ||
        controller.signal.aborted
      ) {
        throw new DOMException('Preference request is no longer current.', 'AbortError');
      }
      preference.value = result;
      aggregationRevision.value += 1;
      return result;
    } finally {
      if (preferenceController === controller) preferenceController = null;
    }
  }

  function clear(): void {
    generation += 1;
    preferenceRequest += 1;
    requestController?.abort();
    preferenceController?.abort();
    requestController = null;
    preferenceController = null;
    preference.value = null;
    currencies.value = [];
    status.value = 'idle';
    aggregationRevision.value = 0;
    void applyPresentation(null);
  }

  function invalidateAggregates(): void {
    aggregationRevision.value += 1;
  }

  return {
    preference,
    currencies,
    status,
    aggregationRevision,
    load,
    update,
    invalidateAggregates,
    clear,
  };
});
