import { ref } from 'vue';
import { defineStore } from 'pinia';
import {
  getUserPreferences,
  listCurrencies,
  type CurrencyMetadata,
  type UserPreference,
} from '../services/user-context-api';

export const useUserContextStore = defineStore('user-context', () => {
  const preference = ref<UserPreference | null>(null);
  const currencies = ref<CurrencyMetadata[]>([]);
  const status = ref<'idle' | 'loading' | 'ready' | 'error'>('idle');
  let generation = 0;
  let requestController: AbortController | null = null;

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

  function clear(): void {
    generation += 1;
    requestController?.abort();
    requestController = null;
    preference.value = null;
    currencies.value = [];
    status.value = 'idle';
  }

  return { preference, currencies, status, load, clear };
});
