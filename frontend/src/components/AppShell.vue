<script setup lang="ts">
import { ref } from 'vue';
import { RouterLink, useRoute, useRouter } from 'vue-router';
import {
  ChartNoAxesCombined,
  LayoutDashboard,
  LogOut,
  Menu,
  ReceiptText,
  Settings2,
  WalletCards,
  X,
} from '@lucide/vue';
import BrandMark from './BrandMark.vue';
import { useSessionStore } from '../stores/session';

const route = useRoute();
const router = useRouter();
const session = useSessionStore();
const menuOpen = ref(false);

const navigation = [
  { label: 'Overview', to: '/dashboard', icon: LayoutDashboard },
  { label: 'Accounts', to: '/accounts', icon: WalletCards },
  { label: 'Transactions', to: '/transactions', icon: ReceiptText, disabled: true },
  { label: 'Reports', to: '/reports', icon: ChartNoAxesCombined, disabled: true },
  { label: 'Settings', to: '/settings', icon: Settings2, disabled: true },
];

async function logout(): Promise<void> {
  await session.logout();
  await router.push({ name: 'landing' });
}
</script>

<template>
  <div class="app-frame">
    <aside class="app-sidebar" aria-label="Primary navigation">
      <div class="app-sidebar__brand"><BrandMark /></div>
      <p class="nav-label">Workspace</p>
      <nav class="app-nav">
        <RouterLink
          v-for="item in navigation"
          :key="item.label"
          :to="item.disabled ? route.fullPath : item.to"
          class="app-nav__item"
          :class="{
            'app-nav__item--active': route.path === item.to || route.path.startsWith(`${item.to}/`),
            'app-nav__item--disabled': item.disabled,
          }"
          :aria-disabled="item.disabled || undefined"
          @click="item.disabled ? $event.preventDefault() : undefined"
        >
          <component :is="item.icon" :size="18" stroke-width="1.8" />
          <span>{{ item.label }}</span>
          <span v-if="item.disabled" class="nav-soon">Soon</span>
        </RouterLink>
      </nav>
      <div class="sidebar-foot">
        <div class="privacy-note">
          <span class="privacy-dot"></span><span>Private workspace</span>
        </div>
        <button class="button button--quiet button--full" type="button" @click="logout">
          <LogOut :size="16" /> Sign out
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
              :to="item.disabled ? route.fullPath : item.to"
              class="app-nav__item"
              :class="{
                'app-nav__item--active':
                  route.path === item.to || route.path.startsWith(`${item.to}/`),
                'app-nav__item--disabled': item.disabled,
              }"
              :aria-disabled="item.disabled || undefined"
              @click="item.disabled ? $event.preventDefault() : (menuOpen = false)"
            >
              <component :is="item.icon" :size="18" /><span>{{ item.label }}</span
              ><span v-if="item.disabled" class="nav-soon">Soon</span>
            </RouterLink>
          </nav>
          <button class="button button--quiet button--full" type="button" @click="logout">
            <LogOut :size="16" /> Sign out
          </button>
        </aside>
      </div>
      <main class="app-content"><slot /></main>
    </div>
  </div>
</template>
