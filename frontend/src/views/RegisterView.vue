<script setup lang="ts">
import { ref } from 'vue';
import { ArrowRight, Eye, EyeOff, LoaderCircle } from '@lucide/vue';
import { useRouter } from 'vue-router';
import AuthLayout from '../components/AuthLayout.vue';
import { useSessionStore } from '../stores/session';
import { ApiError } from '../services/api-error';

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
    errorMessage.value = 'Complete all required fields.';
    return;
  }
  if (password.value !== confirmation.value) {
    errorMessage.value = 'Passwords do not match.';
    return;
  }
  pending.value = true;
  try {
    await session.register({
      username: username.value.trim(),
      password: password.value,
      baseCurrency: 'CNY',
      preferredLocale: 'en-US',
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
    eyebrow="A clean start"
    title="Make room for better decisions."
    description="Create a private ledger with a few deliberate defaults. You can shape the details once you are inside."
    alternate-text="Already have an account?"
    alternate-label="Sign in"
    alternate-to="/login"
  >
    <form class="auth-form" novalidate @submit.prevent="submit">
      <div class="form-heading">
        <span class="form-heading__index">01</span><span>Create your ledger</span>
      </div>
      <div v-if="errorMessage" class="form-alert" role="alert">{{ errorMessage }}</div>
      <label class="field"
        ><span>Username</span
        ><input
          v-model="username"
          name="username"
          autocomplete="username"
          inputmode="email"
          placeholder="you@example.com"
          :disabled="pending"
      /></label>
      <label class="field"
        ><span>Password</span
        ><span class="input-wrap"
          ><input
            v-model="password"
            name="new-password"
            :type="showPassword ? 'text' : 'password'"
            autocomplete="new-password"
            placeholder="At least 12 characters"
            :disabled="pending" /><button
            class="input-action"
            type="button"
            :aria-label="showPassword ? 'Hide password' : 'Show password'"
            :title="showPassword ? 'Hide password' : 'Show password'"
            @click="showPassword = !showPassword"
          >
            <EyeOff v-if="showPassword" :size="17" /><Eye v-else :size="17" /></button></span
      ></label>
      <label class="field"
        ><span>Confirm password</span
        ><input
          v-model="confirmation"
          name="new-password-confirmation"
          type="password"
          autocomplete="new-password"
          placeholder="Repeat your password"
          :disabled="pending"
      /></label>
      <button class="button button--submit" type="submit" :disabled="pending">
        <LoaderCircle v-if="pending" class="spin" :size="17" /><ArrowRight v-else :size="17" />{{
          pending ? 'Creating ledger' : 'Create private ledger'
        }}
      </button>
      <p class="form-footnote">
        By continuing, you create a USER session. Operator access is never self-service.
      </p>
    </form>
  </AuthLayout>
</template>
