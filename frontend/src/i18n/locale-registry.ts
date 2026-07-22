import type { LocaleMessages, SupportedLocale } from './types';

const loaders: Record<SupportedLocale, () => Promise<{ default: LocaleMessages }>> = {
  'en-US': () => import('./locales/en-US'),
  'zh-CN': () => import('./locales/zh-CN'),
};

export async function loadLocaleMessages(locale: SupportedLocale): Promise<LocaleMessages> {
  return (await loaders[locale]()).default;
}
