const channelName = 'pfh-web-session';
const lockName = 'pfh-web-refresh-lock';
const leaseKey = 'pfh-web-refresh-lease';
const leaseDurationMs = 20_000;

const channel = typeof BroadcastChannel !== 'undefined' ? new BroadcastChannel(channelName) : null;

function sleep(milliseconds: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, milliseconds));
}

function leaseIsAvailable(): boolean {
  try {
    const raw = window.localStorage.getItem(leaseKey);
    if (!raw) return true;
    const lease = JSON.parse(raw) as { expiresAt?: number };
    return typeof lease.expiresAt !== 'number' || lease.expiresAt <= Date.now();
  } catch {
    return true;
  }
}

async function withLease<T>(operation: () => Promise<T>): Promise<T> {
  const owner = `${Date.now()}-${Math.random().toString(36).slice(2)}`;
  while (true) {
    while (!leaseIsAvailable()) await sleep(80);
    window.localStorage.setItem(
      leaseKey,
      JSON.stringify({ owner, expiresAt: Date.now() + leaseDurationMs }),
    );
    await sleep(20 + Math.floor(Math.random() * 30));
    try {
      const confirmed = JSON.parse(window.localStorage.getItem(leaseKey) ?? '{}') as {
        owner?: string;
      };
      if (confirmed.owner === owner) break;
    } catch {
      // A competing tab rewrote malformed lease state; retry acquisition.
    }
  }
  try {
    return await operation();
  } finally {
    try {
      const current = JSON.parse(window.localStorage.getItem(leaseKey) ?? '{}') as {
        owner?: string;
      };
      if (current.owner === owner) window.localStorage.removeItem(leaseKey);
    } catch {
      window.localStorage.removeItem(leaseKey);
    }
  }
}

export async function serializeRefresh<T>(operation: () => Promise<T>): Promise<T> {
  if (typeof navigator !== 'undefined' && 'locks' in navigator) {
    return navigator.locks.request(lockName, { mode: 'exclusive' }, operation);
  }
  return withLease(operation);
}

export function broadcastSessionState(state: 'authenticated' | 'anonymous'): void {
  channel?.postMessage({ type: 'session-state', state });
}

export function onSessionState(
  callback: (state: 'authenticated' | 'anonymous') => void,
): () => void {
  if (!channel) return () => undefined;
  const listener = (event: MessageEvent<{ type?: string; state?: string }>) => {
    if (event.data.type !== 'session-state') return;
    if (event.data.state === 'authenticated' || event.data.state === 'anonymous') {
      callback(event.data.state);
    }
  };
  channel.addEventListener('message', listener);
  return () => channel.removeEventListener('message', listener);
}
