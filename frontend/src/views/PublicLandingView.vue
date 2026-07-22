<script setup lang="ts">
import { ArrowRight, ArrowUpRight, LogIn, ShieldCheck, UserRoundPlus } from '@lucide/vue';
import { RouterLink } from 'vue-router';
import BrandMark from '../components/BrandMark.vue';
import LocaleSelect from '../components/LocaleSelect.vue';
import { brand } from '../app/brand';
import { translate } from '../i18n';

const ledgerRows = [
  {
    label: 'landing.everydayAccount',
    amount: '¥ 8,420.00',
    type: 'landing.balance',
    tone: 'positive',
  },
  {
    label: 'landing.monthlyMovement',
    amount: '+ ¥ 2,180.00',
    type: 'landing.thisMonth',
    tone: 'teal',
  },
  {
    label: 'landing.nextReview',
    amount: '12 Jul',
    type: 'landing.scheduled',
    tone: 'neutral',
  },
] as const;
</script>

<template>
  <main class="landing-page">
    <header class="site-header page-width">
      <BrandMark />
      <nav class="site-header__nav" :aria-label="translate('landing.accountNavigation')">
        <LocaleSelect />
        <RouterLink class="text-link" to="/login">{{ translate('landing.signIn') }}</RouterLink>
        <RouterLink
          class="button button--small site-header__create"
          to="/register"
          :aria-label="translate('landing.createAccount')"
          ><UserRoundPlus :size="16" /><span>{{
            translate('landing.createAccount')
          }}</span></RouterLink
        >
      </nav>
    </header>

    <section class="landing-hero page-width" aria-labelledby="landing-title">
      <div class="landing-hero__copy">
        <p class="eyebrow">{{ translate('landing.supportLine') }}</p>
        <h1 id="landing-title">{{ brand.name }}</h1>
        <p class="landing-hero__lead">
          {{ translate('landing.lead', { descriptor: translate('landing.descriptor') }) }}
        </p>
        <div class="landing-hero__actions">
          <RouterLink class="button" to="/register"
            ><ArrowRight :size="17" /> {{ translate('landing.startLedger') }}</RouterLink
          >
          <RouterLink class="button button--quiet" to="/login"
            ><LogIn :size="17" /> {{ translate('landing.signIn') }}</RouterLink
          >
        </div>
        <div class="trust-line">
          <ShieldCheck :size="17" /><span>{{ translate('landing.trust') }}</span>
        </div>
      </div>
      <div class="ledger-preview" :aria-label="translate('landing.previewLabel')">
        <div class="ledger-preview__header">
          <div>
            <span class="section-kicker">{{ translate('landing.currentView') }}</span>
            <h2>{{ translate('landing.quietControl') }}</h2>
          </div>
          <span class="status-mark">{{ translate('landing.liveShape') }}</span>
        </div>
        <div class="preview-total">
          <span>{{ translate('landing.netPosition') }}</span
          ><strong>¥ 24,680.00</strong><small>{{ translate('landing.updatedFromLedger') }}</small>
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
              ><strong>{{ translate(row.label) }}</strong
              ><small>{{ translate(row.type) }}</small></span
            ><span class="preview-row__amount"
              >{{ row.amount }}<ArrowUpRight v-if="row.tone === 'positive'" :size="14"
            /></span>
          </div>
        </div>
      </div>
    </section>

    <section class="landing-strip page-width" :aria-label="translate('landing.productPrinciples')">
      <div>
        <span class="strip-number">01</span><strong>{{ translate('landing.wholePicture') }}</strong
        ><span>{{ translate('landing.wholePictureDetail') }}</span>
      </div>
      <div>
        <span class="strip-number">02</span><strong>{{ translate('landing.exactMath') }}</strong
        ><span>{{ translate('landing.exactMathDetail') }}</span>
      </div>
      <div>
        <span class="strip-number">03</span><strong>{{ translate('landing.stayInCharge') }}</strong
        ><span>{{ translate('landing.stayInChargeDetail') }}</span>
      </div>
    </section>
  </main>
</template>
