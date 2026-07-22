<script setup lang="ts">
import { ref } from 'vue';
import { ArrowRight, Eye, EyeOff, LoaderCircle } from '@lucide/vue';
import { useRouter } from 'vue-router';
import AuthLayout from '../components/AuthLayout.vue';
import { useSessionStore } from '../stores/session';
import { ApiError } from '../services/api-error';
import { currentLocale, translate } from '../i18n';

const router = useRouter();
const session = useSessionStore();
const username = ref('');
const password = ref('');
const confirmation = ref('');
const showPassword = ref(false);
const pending = ref(false);
const errorMessage = ref('');

async function submit(): Promise<void> {
  errorMessage.value = '';
  if (!username.value.trim() || !password.value || !confirmation.value) {
    errorMessage.value = translate('auth.register.required');
    return;
  }
  if (password.value !== confirmation.value) {
    errorMessage.value = translate('auth.register.passwordMismatch');
    return;
  }
  pending.value = true;
  try {
    const preferredTimezone = Intl.DateTimeFormat().resolvedOptions().timeZone;
    await session.register({
      username: username.value.trim(),
      password: password.value,
      baseCurrency: 'CNY',
      preferredLocale: currentLocale(),
      ...(preferredTimezone ? { preferredTimezone } : {}),
    });
    await router.push(session.defaultHomeRoute);
  } catch (error) {
    errorMessage.value =
      error instanceof ApiError ? error.details.message : session.errorMessage(error);
  } finally {
    pending.value = false;
  }
}
</script>

<template>
  <AuthLayout
    :eyebrow="translate('auth.register.eyebrow')"
    :title="translate('auth.register.title')"
    :description="translate('auth.register.description')"
    :alternate-text="translate('auth.register.alternateText')"
    :alternate-label="translate('auth.register.alternateLabel')"
    alternate-to="/login"
  >
    <form class="auth-form" novalidate @submit.prevent="submit">
      <div class="form-heading">
        <span class="form-heading__index">01</span
        ><span>{{ translate('auth.register.heading') }}</span>
      </div>
      <div v-if="errorMessage" class="form-alert" role="alert">{{ errorMessage }}</div>
      <label class="field"
        ><span>{{ translate('auth.username') }}</span
        ><input
          v-model="username"
          name="username"
          autocomplete="username"
          inputmode="email"
          :placeholder="translate('auth.emailPlaceholder')"
          :disabled="pending"
      /></label>
      <label class="field"
        ><span>{{ translate('auth.password') }}</span
        ><span class="input-wrap"
          ><input
            v-model="password"
            name="new-password"
            :type="showPassword ? 'text' : 'password'"
            autocomplete="new-password"
            :placeholder="translate('auth.register.passwordPlaceholder')"
            :disabled="pending" /><button
            class="input-action"
            type="button"
            :aria-label="translate(showPassword ? 'auth.hidePassword' : 'auth.showPassword')"
            :title="translate(showPassword ? 'auth.hidePassword' : 'auth.showPassword')"
            @click="showPassword = !showPassword"
          >
            <EyeOff v-if="showPassword" :size="17" /><Eye v-else :size="17" /></button></span
      ></label>
      <label class="field"
        ><span>{{ translate('auth.register.confirmPassword') }}</span
        ><input
          v-model="confirmation"
          name="new-password-confirmation"
          type="password"
          autocomplete="new-password"
          :placeholder="translate('auth.register.confirmPlaceholder')"
          :disabled="pending"
      /></label>
      <button class="button button--submit" type="submit" :disabled="pending">
        <LoaderCircle v-if="pending" class="spin" :size="17" /><ArrowRight v-else :size="17" />{{
          translate(pending ? 'auth.register.pending' : 'auth.register.submit')
        }}
      </button>
      <p class="form-footnote">
        {{ translate('auth.register.footnote') }}
      </p>
    </form>
  </AuthLayout>
</template>
