import { describe, expect, it } from 'vitest';
import { createResidentPageWindow } from './resident-page-window';

interface Item {
  id: number;
  value: string;
}

describe('resident page window', () => {
  it('deduplicates incrementally and releases complete older pages', () => {
    const window = createResidentPageWindow<Item>(({ id }) => id, 2, 4);

    expect(
      window.reset([
        { id: 5, value: 'five' },
        { id: 4, value: 'four' },
      ]),
    ).toEqual({
      items: [
        { id: 5, value: 'five' },
        { id: 4, value: 'four' },
      ],
      evicted: false,
    });
    expect(
      window
        .append([
          { id: 4, value: 'duplicate boundary' },
          { id: 3, value: 'three' },
          { id: 3, value: 'duplicate page row' },
        ])
        .items.map(({ id }) => id),
    ).toEqual([5, 4, 3]);

    const update = window.append([
      { id: 2, value: 'two' },
      { id: 1, value: 'one' },
    ]);
    expect(update.evicted).toBe(true);
    expect(update.items.map(({ id }) => id)).toEqual([3, 2, 1]);
  });

  it('enforces an item bound even when one response exceeds it', () => {
    const window = createResidentPageWindow<Item>(({ id }) => id, 4, 2);
    const update = window.reset([
      { id: 3, value: 'three' },
      { id: 2, value: 'two' },
      { id: 1, value: 'one' },
    ]);

    expect(update.evicted).toBe(true);
    expect(update.items.map(({ id }) => id)).toEqual([3, 2]);
  });

  it('keeps the ID index correct after replacement, removal and reset', () => {
    const window = createResidentPageWindow<Item>(({ id }) => id, 2, 4);
    window.reset([{ id: 2, value: 'old' }]);

    expect(window.replace({ id: 2, value: 'new' })).toEqual([{ id: 2, value: 'new' }]);
    expect(window.remove(2)).toEqual([]);
    expect(window.append([{ id: 2, value: 'reloaded' }]).items).toEqual([
      { id: 2, value: 'reloaded' },
    ]);
    expect(window.reset([{ id: 1, value: 'reset' }]).items).toEqual([{ id: 1, value: 'reset' }]);
  });
});
