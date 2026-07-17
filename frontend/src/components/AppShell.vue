<script setup lang="ts">
import { computed, ref } from 'vue';
import { RouterLink, useRoute, useRouter } from 'vue-router';
import {
  ChartNoAxesCombined,
  LayoutDashboard,
  LogOut,
  Menu,
  ReceiptText,
  ArrowRightLeft,
  Settings2,
  WalletCards,
  X,
} from '@lucide/vue';
import BrandMark from './BrandMark.vue';
import { useSessionStore } from '../stores/session';
import { useUserContextStore } from '../stores/user-context';
import { translate, type MessageKey } from '../i18n/messages';

const route = useRoute();
const router = useRouter();
const session = useSessionStore();
const userContext = useUserContextStore();
const menuOpen = ref(false);

const navigation = computed(() =>
  [
    { key: 'overview', to: '/dashboard', icon: LayoutDashboard },
    { key: 'accounts', to: '/accounts', icon: WalletCards },
    { key: 'transactions', to: '/transactions', icon: ReceiptText },
    { key: 'transfers', to: '/transfers', icon: ArrowRightLeft },
    { key: 'reports', to: '/reports', icon: ChartNoAxesCombined },
    { key: 'settings', to: '/settings', icon: Settings2 },
  ].map((item) => ({
    ...item,
    label: translate(userContext.preference?.locale, item.key as MessageKey),
  })),
);

async function logout(): Promise<void> {
  await session.logout();
  await router.push({ name: 'landing' });
}
</script>

<template>
  <div class="app-frame">
    <aside class="app-sidebar" aria-label="Primary navigation">
      <div class="app-sidebar__brand"><BrandMark /></div>
      <p class="nav-label">{{ translate(userContext.preference?.locale, 'workspace') }}</p>
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
          <span class="privacy-dot"></span
          ><span>{{ translate(userContext.preference?.locale, 'privateWorkspace') }}</span>
        </div>
        <button class="button button--quiet button--full" type="button" @click="logout">
          <LogOut :size="16" /> {{ translate(userContext.preference?.locale, 'signOut') }}
        </button>
      </div>
    </aside>

    <div class="app-main">
      <header class="app-mobile-header">
        <BrandMark compact />
        <button
          class="icon-button"
          type="button"
          aria-label="Open navigation"
          title="Open navigation"
          @click="menuOpen = true"
        >
          <Menu :size="20" />
        </button>
      </header>
      <div v-if="menuOpen" class="mobile-nav-backdrop" @click="menuOpen = false">
        <aside class="mobile-nav" aria-label="Mobile navigation" @click.stop>
          <div class="mobile-nav__top">
            <BrandMark compact /><button
              class="icon-button"
              type="button"
              aria-label="Close navigation"
              title="Close navigation"
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
            <LogOut :size="16" /> {{ translate(userContext.preference?.locale, 'signOut') }}
          </button>
        </aside>
      </div>
      <main class="app-content"><slot /></main>
    </div>
  </div>
</template>
