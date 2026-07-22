import { createApp } from 'vue';
import { createPinia } from 'pinia';
import App from './App.vue';
import router from './router';
import { initializeI18n } from './i18n';
import './styles/tokens.css';
import './styles/main.css';

async function bootstrap(): Promise<void> {
  const app = createApp(App);
  app.use(createPinia());
  app.use(await initializeI18n());
  app.use(router);
  app.mount('#app');
}

void bootstrap();
