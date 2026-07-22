import { describe, expect, it } from 'vitest';
import { setApplicationLocale, translate } from './index';

describe('application localization', () => {
  it('loads and switches complete locale packs at runtime', async () => {
    await setApplicationLocale('en-US');
    expect(translate('nav.settings')).toBe('Settings');
    expect(translate('auth.emailPlaceholder')).toBe('you@example.com');
    expect(document.documentElement.lang).toBe('en-US');

    await setApplicationLocale('zh-CN');
    expect(translate('nav.settings')).toBe('设置');
    expect(document.documentElement.lang).toBe('zh-CN');
  });

  it('falls back to English for unsupported locale tags', async () => {
    await setApplicationLocale('fr-FR');
    expect(translate('common.save')).toBe('Save');
    expect(document.documentElement.lang).toBe('en-US');
  });
});
