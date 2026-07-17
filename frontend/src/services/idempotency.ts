export type IntentScope =
  'account' | 'category' | 'tag' | 'transaction' | 'correction' | 'transfer' | 'dead-letter';

export interface IntentKeyTracker {
  keyFor(intent: unknown): string;
  complete(key: string): void;
  clear(): void;
}

export function newIntentKey(scope: IntentScope): string {
  const random = globalThis.crypto?.randomUUID?.() ?? `${Date.now()}-${Math.random()}`;
  return `${scope}-${random}`;
}

function canonicalize(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(canonicalize);
  if (value === null || typeof value !== 'object') return value;

  const result: Record<string, unknown> = {};
  for (const key of Object.keys(value).sort()) {
    const item = (value as Record<string, unknown>)[key];
    if (item !== undefined) result[key] = canonicalize(item);
  }
  return result;
}

function fingerprint(intent: unknown): string {
  return JSON.stringify(canonicalize(intent)) ?? 'undefined';
}

export function createIntentKeyTracker(scope: IntentScope): IntentKeyTracker {
  let current: { fingerprint: string; key: string } | null = null;
  return {
    keyFor(intent) {
      const nextFingerprint = fingerprint(intent);
      if (current?.fingerprint === nextFingerprint) return current.key;
      const key = newIntentKey(scope);
      current = { fingerprint: nextFingerprint, key };
      return key;
    },
    complete(key) {
      if (current?.key === key) current = null;
    },
    clear() {
      current = null;
    },
  };
}
