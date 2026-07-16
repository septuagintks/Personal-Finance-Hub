<script setup lang="ts">
import { ArrowRight, ArrowUpRight, LogIn, ShieldCheck, UserRoundPlus } from '@lucide/vue';
import { RouterLink } from 'vue-router';
import BrandMark from '../components/BrandMark.vue';
import { brand } from '../app/brand';

const ledgerRows = [
  { label: 'Everyday account', amount: '¥ 8,420.00', type: 'Balance', tone: 'positive' },
  { label: 'Monthly movement', amount: '+ ¥ 2,180.00', type: 'This month', tone: 'teal' },
  { label: 'Next review', amount: '12 Jul', type: 'Scheduled', tone: 'neutral' },
];
</script>

<template>
  <main class="landing-page">
    <header class="site-header page-width">
      <BrandMark />
      <nav class="site-header__nav" aria-label="Account navigation">
        <RouterLink class="text-link" to="/login">Sign in</RouterLink>
        <RouterLink class="button button--small" to="/register"
          ><UserRoundPlus :size="16" /> Create account</RouterLink
        >
      </nav>
    </header>

    <section class="landing-hero page-width" aria-labelledby="landing-title">
      <div class="landing-hero__copy">
        <p class="eyebrow">{{ brand.supportLine }}</p>
        <h1 id="landing-title">{{ brand.name }}</h1>
        <p class="landing-hero__lead">
          {{ brand.descriptor }} See what is moving, keep the details honest, and make the next
          decision with less noise.
        </p>
        <div class="landing-hero__actions">
          <RouterLink class="button" to="/register"
            ><ArrowRight :size="17" /> Start a private ledger</RouterLink
          >
          <RouterLink class="button button--quiet" to="/login"
            ><LogIn :size="17" /> Sign in</RouterLink
          >
        </div>
        <div class="trust-line">
          <ShieldCheck :size="17" /><span
            >Same-origin sessions · Decimal-safe amounts · Your data stays yours</span
          >
        </div>
      </div>
      <div class="ledger-preview" aria-label="Ledger overview preview">
        <div class="ledger-preview__header">
          <div>
            <span class="section-kicker">Current view</span>
            <h2>Quietly in control</h2>
          </div>
          <span class="status-mark">Live shape</span>
        </div>
        <div class="preview-total">
          <span>Net position</span><strong>¥ 24,680.00</strong
          ><small>Updated from your ledger</small>
        </div>
        <div class="preview-bars" aria-hidden="true">
          <span style="height: 38%"></span><span style="height: 58%"></span
          ><span style="height: 46%"></span><span style="height: 72%"></span
          ><span style="height: 64%"></span><span style="height: 86%"></span
          ><span style="height: 78%"></span>
        </div>
        <div class="preview-list">
          <div v-for="row in ledgerRows" :key="row.label" class="preview-row">
            <span class="preview-row__dot" :class="`preview-row__dot--${row.tone}`"></span
            ><span class="preview-row__label"
              ><strong>{{ row.label }}</strong
              ><small>{{ row.type }}</small></span
            ><span class="preview-row__amount"
              >{{ row.amount }}<ArrowUpRight v-if="row.tone === 'positive'" :size="14"
            /></span>
          </div>
        </div>
      </div>
    </section>

    <section class="landing-strip page-width" aria-label="Product principles">
      <div>
        <span class="strip-number">01</span><strong>See the whole picture</strong
        ><span>Accounts, movement, and context in one calm workspace.</span>
      </div>
      <div>
        <span class="strip-number">02</span><strong>Keep the math exact</strong
        ><span>Amounts remain decimal strings from entry to report.</span>
      </div>
      <div>
        <span class="strip-number">03</span><strong>Stay in charge</strong
        ><span>Clear ownership, private sessions, deliberate actions.</span>
      </div>
    </section>
  </main>
</template>
