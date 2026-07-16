import { afterAll, afterEach, beforeAll } from 'vitest';
import { server } from './server';

// Node 24 does not expose ProgressEvent globally, while MSW's jsdom XHR
// interceptor constructs one when an in-flight request is aborted.
if (typeof globalThis.ProgressEvent === 'undefined') {
  Object.defineProperty(globalThis, 'ProgressEvent', {
    configurable: true,
    value: window.ProgressEvent,
  });
}

beforeAll(() => server.listen({ onUnhandledRequest: 'error' }));
afterEach(() => server.resetHandlers());
afterAll(() => server.close());
