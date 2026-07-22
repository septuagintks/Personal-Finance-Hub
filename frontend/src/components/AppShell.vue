<script setup lang="ts">
import { computed, ref } from 'vue';
import { RouterLink, useRoute, useRouter } from 'vue-router';
import {
  ChartNoAxesCombined,
  Activity,
  LayoutDashboard,
  LogOut,
  Menu,
  ReceiptText,
  ArrowRightLeft,
  Settings2,
  Wrench,
  WalletCards,
  X,
} from '@lucide/vue';
import BrandMark from './BrandMark.vue';
import { useSessionStore } from '../stores/session';
import { translate, type MessageKey } from '../i18n';

const route = useRoute();
const router = useRouter();
const session = useSessionStore();
const menuOpen = ref(false);

const navigation = computed(() =>
  [
    { key: 'nav.overview', to: '/dashboard', icon: LayoutDashboard },
    { key: 'nav.accounts', to: '/accounts', icon: WalletCards },
    { key: 'nav.transactions', to: '/transactions', icon: ReceiptText },
    { key: 'nav.transfers', to: '/transfers', icon: ArrowRightLeft },
    { key: 'nav.reports', to: '/reports', icon: ChartNoAxesCombined },
    { key: 'nav.settings', to: '/settings', icon: Settings2 },
    { key: 'nav.maintenance', to: '/maintenance', icon: Wrench },
    ...(session.isOperator ? [{ key: 'nav.operations', to: '/operations', icon: Activity }] : []),
  ].map((item) => ({
    ...item,
    label: translate(item.key as MessageKey),
  })),
);

async function logout(): Promise<void> {
  await session.logout();
  await router.push({ name: 'landing' });
}
</script>

<template>
  <div class="app-frame">
    <aside class="app-sidebar" :aria-label="translate('nav.primary')">
      <div class="app-sidebar__brand"><BrandMark /></div>
      <p class="nav-label">{{ translate('nav.workspace') }}</p>
      <nav class="app-nav">
        <RouterLink
          v-for="item in navigation"
          :key="item.label"
          :to="item.to"
          class="app-nav__item"
          :class="{
            'app-nav__item--active': route.path === item.to || route.path.startsWith(`${item.to}/`),
          }"
        >
          <component :is="item.icon" :size="18" stroke-width="1.8" />
          <span>{{ item.label }}</span>
        </RouterLink>
      </nav>
      <div class="sidebar-foot">
        <div class="privacy-note">
          <span class="privacy-dot"></span><span>{{ translate('nav.privateWorkspace') }}</span>
        </div>
        <button class="button button--quiet button--full" type="button" @click="logout">
          <LogOut :size="16" /> {{ translate('nav.signOut') }}
        </button>
      </div>
    </aside>

    <div class="app-main">
      <header class="app-mobile-header">
        <BrandMark compact />
        <button
          class="icon-button"
          type="button"
          :aria-label="translate('nav.open')"
          :title="translate('nav.open')"
          @click="menuOpen = true"
        >
          <Menu :size="20" />
        </button>
      </header>
      <div v-if="menuOpen" class="mobile-nav-backdrop" @click="menuOpen = false">
        <aside class="mobile-nav" :aria-label="translate('nav.mobile')" @click.stop>
          <div class="mobile-nav__top">
            <BrandMark compact /><button
              class="icon-button"
              type="button"
              :aria-label="translate('nav.close')"
              :title="translate('nav.close')"
              @click="menuOpen = false"
            >
              <X :size="20" />
            </button>
          </div>
          <nav class="app-nav">
            <RouterLink
              v-for="item in navigation"
              :key="item.label"
              :to="item.to"
              class="app-nav__item"
              :class="{
                'app-nav__item--active':
                  route.path === item.to || route.path.startsWith(`${item.to}/`),
              }"
              @click="menuOpen = false"
            >
              <component :is="item.icon" :size="18" /><span>{{ item.label }}</span>
            </RouterLink>
          </nav>
          <button class="button button--quiet button--full" type="button" @click="logout">
            <LogOut :size="16" /> {{ translate('nav.signOut') }}
          </button>
        </aside>
      </div>
      <main class="app-content"><slot /></main>
    </div>
  </div>
</template>
