import { createI18n } from 'vue-i18n';
import { loadLocaleMessages } from './locale-registry';
import type { MessageKey, MessageParameters, SupportedLocale } from './types';

const publicLocaleKey = 'pfh.public-locale';

export function supportedLocale(value: string | null | undefined): SupportedLocale | null {
  if (!value) return null;
  if (value === 'zh-CN' || value.toLowerCase().startsWith('zh')) return 'zh-CN';
  if (value === 'en-US' || value.toLowerCase().startsWith('en')) return 'en-US';
  return null;
}

function storedPublicLocale(): SupportedLocale | null {
  try {
    return supportedLocale(window.localStorage.getItem(publicLocaleKey));
  } catch {
    return null;
  }
}

export function resolvePublicLocale(): SupportedLocale {
  const stored = storedPublicLocale();
  if (stored) return stored;
  for (const candidate of navigator.languages ?? [navigator.language]) {
    const resolved = supportedLocale(candidate);
    if (resolved) return resolved;
  }
  return 'en-US';
}

const initialLocale = resolvePublicLocale();
export const i18n = createI18n({
  legacy: false,
  locale: initialLocale,
  fallbackLocale: 'en-US',
  messages: {},
  missingWarn: false,
  fallbackWarn: false,
});

const loadedLocales = new Set<SupportedLocale>();
let localeRequest = 0;

export async function setApplicationLocale(
  value: string | null | undefined,
  options: { persistPublic?: boolean } = {},
): Promise<SupportedLocale> {
  const request = ++localeRequest;
  const locale = supportedLocale(value) ?? 'en-US';
  if (!loadedLocales.has(locale)) {
    i18n.global.setLocaleMessage(locale, await loadLocaleMessages(locale));
    loadedLocales.add(locale);
  }
  if (request !== localeRequest) return locale;
  i18n.global.locale.value = locale;
  document.documentElement.lang = locale;
  if (options.persistPublic) {
    try {
      window.localStorage.setItem(publicLocaleKey, locale);
    } catch {
      // A blocked storage API must not prevent language switching.
    }
  }
  return locale;
}

export async function initializeI18n(): Promise<typeof i18n> {
  await setApplicationLocale(initialLocale);
  return i18n;
}

export function currentLocale(): SupportedLocale {
  return supportedLocale(i18n.global.locale.value) ?? 'en-US';
}

export function translate(key: MessageKey, parameters?: MessageParameters): string {
  return parameters ? i18n.global.t(key, parameters) : i18n.global.t(key);
}

export type { MessageKey, SupportedLocale } from './types';
