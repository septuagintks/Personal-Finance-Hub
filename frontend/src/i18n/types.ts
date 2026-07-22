import type { enUS } from './locales/en-US';

export type SupportedLocale = 'zh-CN' | 'en-US';
export type MessageKey = keyof typeof enUS;
export type LocaleMessages = Record<MessageKey, string>;
export type MessageParameters = Record<string, string | number>;
