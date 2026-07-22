export const RESIDENT_PAGE_LIMIT = 4;
export const RESIDENT_ITEM_LIMIT = 200;

export interface ResidentPageWindowUpdate<T> {
  items: T[];
  evicted: boolean;
}

export interface ResidentPageWindow<T> {
  reset(items: readonly T[]): ResidentPageWindowUpdate<T>;
  append(items: readonly T[]): ResidentPageWindowUpdate<T>;
  replace(item: T): T[];
  remove(id: string | number): T[];
  clear(): void;
}

export function createResidentPageWindow<T>(
  idOf: (item: T) => string | number,
  pageLimit = RESIDENT_PAGE_LIMIT,
  itemLimit = RESIDENT_ITEM_LIMIT,
): ResidentPageWindow<T> {
  if (!Number.isSafeInteger(pageLimit) || pageLimit <= 0) {
    throw new RangeError('Resident page limit must be a positive integer.');
  }
  if (!Number.isSafeInteger(itemLimit) || itemLimit <= 0) {
    throw new RangeError('Resident item limit must be a positive integer.');
  }

  const pages: T[][] = [];
  const residentIds = new Set<string | number>();
  let itemCount = 0;

  function snapshot(): T[] {
    return pages.flat();
  }

  function forget(page: readonly T[]): void {
    for (const item of page) residentIds.delete(idOf(item));
    itemCount -= page.length;
  }

  function clear(): void {
    pages.length = 0;
    residentIds.clear();
    itemCount = 0;
  }

  function append(items: readonly T[]): ResidentPageWindowUpdate<T> {
    const page: T[] = [];
    for (const item of items) {
      const id = idOf(item);
      if (residentIds.has(id)) continue;
      residentIds.add(id);
      page.push(item);
    }

    if (page.length) {
      pages.push(page);
      itemCount += page.length;
    }

    let evicted = false;
    while (pages.length > 1 && (pages.length > pageLimit || itemCount > itemLimit)) {
      const oldestResidentPage = pages.shift();
      if (!oldestResidentPage) break;
      forget(oldestResidentPage);
      evicted = true;
    }

    if (itemCount > itemLimit) {
      const onlyPage = pages[0];
      if (onlyPage) {
        const removed = onlyPage.splice(itemLimit);
        forget(removed);
        evicted = removed.length > 0 || evicted;
      }
    }

    return { items: snapshot(), evicted };
  }

  function reset(items: readonly T[]): ResidentPageWindowUpdate<T> {
    clear();
    return append(items);
  }

  function replace(item: T): T[] {
    const id = idOf(item);
    for (const page of pages) {
      const index = page.findIndex((candidate) => idOf(candidate) === id);
      if (index >= 0) {
        page.splice(index, 1, item);
        break;
      }
    }
    return snapshot();
  }

  function remove(id: string | number): T[] {
    for (let pageIndex = 0; pageIndex < pages.length; pageIndex += 1) {
      const page = pages[pageIndex];
      if (!page) continue;
      const itemIndex = page.findIndex((item) => idOf(item) === id);
      if (itemIndex < 0) continue;
      page.splice(itemIndex, 1);
      residentIds.delete(id);
      itemCount -= 1;
      if (!page.length) pages.splice(pageIndex, 1);
      break;
    }
    return snapshot();
  }

  return { reset, append, replace, remove, clear };
}
