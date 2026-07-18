import { defineConfig, devices } from '@playwright/test';

const executablePath = process.env.PLAYWRIGHT_EXECUTABLE_PATH;
const fullBrowserMatrix = process.env.PLAYWRIGHT_FULL_BROWSERS === '1';

if (fullBrowserMatrix && executablePath) {
  throw new Error('PLAYWRIGHT_EXECUTABLE_PATH cannot be used with PLAYWRIGHT_FULL_BROWSERS=1');
}

const projects = fullBrowserMatrix
  ? [
      { name: 'chromium', use: { ...devices['Desktop Chrome'] } },
      { name: 'firefox', use: { ...devices['Desktop Firefox'] } },
      { name: 'webkit', use: { ...devices['Desktop Safari'] } },
    ]
  : [
      {
        name: 'local-chromium',
        use: { ...devices['Desktop Chrome'], viewport: { width: 1440, height: 900 } },
      },
    ];

export default defineConfig({
  testDir: './e2e',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  reporter: [['list']],
  use: {
    baseURL: 'https://127.0.0.1:5173',
    ignoreHTTPSErrors: true,
    trace: 'retain-on-failure',
    launchOptions: executablePath ? { executablePath } : {},
  },
  projects,
  webServer: {
    command: 'corepack pnpm dev',
    url: 'https://127.0.0.1:5173',
    ignoreHTTPSErrors: true,
    reuseExistingServer: true,
    timeout: 30_000,
  },
});
