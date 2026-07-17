import { describe, expect, it } from 'vitest';
import { createIntentKeyTracker } from './idempotency';

describe('intent key tracker', () => {
  it('retains one key for a canonical intent until that key succeeds', () => {
    const tracker = createIntentKeyTracker('transaction');
    const first = tracker.keyFor({ amount: '1.00', tags: [2, 1], description: undefined });
    const retry = tracker.keyFor({ tags: [2, 1], amount: '1.00' });
    const changed = tracker.keyFor({ amount: '2.00', tags: [2, 1] });

    expect(retry).toBe(first);
    expect(changed).not.toBe(first);

    tracker.complete(first);
    expect(tracker.keyFor({ tags: [2, 1], amount: '2.00' })).toBe(changed);

    tracker.complete(changed);
    expect(tracker.keyFor({ amount: '2.00', tags: [2, 1] })).not.toBe(changed);
  });

  it('drops retained intent state when the owning session store is cleared', () => {
    const tracker = createIntentKeyTracker('account');
    const beforeClear = tracker.keyFor({ name: 'Wallet' });
    tracker.clear();
    expect(tracker.keyFor({ name: 'Wallet' })).not.toBe(beforeClear);
  });
});
