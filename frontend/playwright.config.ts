import { defineConfig, devices } from '@playwright/test';

const executablePath = process.env.PLAYWRIGHT_EXECUTABLE_PATH;

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
  projects: [
    {
      name: 'local-chromium',
      use: { ...devices['Desktop Chrome'], viewport: { width: 1440, height: 900 } },
    },
  ],
  webServer: {
    command: 'corepack pnpm dev',
    url: 'https://127.0.0.1:5173',
    ignoreHTTPSErrors: true,
    reuseExistingServer: true,
    timeout: 30_000,
  },
});
