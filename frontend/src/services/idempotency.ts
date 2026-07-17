export type IntentScope = 'account' | 'category' | 'tag' | 'dead-letter';

export function newIntentKey(scope: IntentScope): string {
  const random = globalThis.crypto?.randomUUID?.() ?? `${Date.now()}-${Math.random()}`;
  return `${scope}-${random}`;
}
