<script setup lang="ts">
import { computed, ref } from 'vue';
import { Eye, EyeOff, LoaderCircle, LogIn } from '@lucide/vue';
import { useRoute, useRouter } from 'vue-router';
import AuthLayout from '../components/AuthLayout.vue';
import { useSessionStore } from '../stores/session';
import { ApiError } from '../services/api-error';
import { translate } from '../i18n';

const router = useRouter();
const route = useRoute();
const session = useSessionStore();
const username = ref('');
const password = ref('');
const showPassword = ref(false);
const pending = ref(false);
const errorMessage = ref('');

const redirectTarget = computed(() =>
  typeof route.query.redirect === 'string' ? route.query.redirect : session.defaultHomeRoute,
);

async function submit(): Promise<void> {
  errorMessage.value = '';
  if (!username.value.trim() || !password.value) {
    errorMessage.value = translate('auth.login.required');
    return;
  }
  pending.value = true;
  try {
    await session.login({ username: username.value.trim(), password: password.value });
    await router.push(redirectTarget.value);
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
    :eyebrow="translate('auth.login.eyebrow')"
    :title="translate('auth.login.title')"
    :description="translate('auth.login.description')"
    :alternate-text="translate('auth.login.alternateText')"
    :alternate-label="translate('auth.login.alternateLabel')"
    alternate-to="/register"
  >
    <form class="auth-form" novalidate @submit.prevent="submit">
      <div class="form-heading">
        <span class="form-heading__index">01</span
        ><span>{{ translate('auth.login.heading') }}</span>
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
            name="password"
            :type="showPassword ? 'text' : 'password'"
            autocomplete="current-password"
            :placeholder="translate('auth.login.passwordPlaceholder')"
            :disabled="pending" /><button
            class="input-action"
            type="button"
            :aria-label="translate(showPassword ? 'auth.hidePassword' : 'auth.showPassword')"
            :title="translate(showPassword ? 'auth.hidePassword' : 'auth.showPassword')"
            @click="showPassword = !showPassword"
          >
            <EyeOff v-if="showPassword" :size="17" /><Eye v-else :size="17" /></button></span
      ></label>
      <button class="button button--submit" type="submit" :disabled="pending">
        <LoaderCircle v-if="pending" class="spin" :size="17" /><LogIn v-else :size="17" />{{
          translate(pending ? 'auth.login.pending' : 'auth.login.submit')
        }}
      </button>
      <p class="form-footnote">{{ translate('auth.login.footnote') }}</p>
    </form>
  </AuthLayout>
</template>
