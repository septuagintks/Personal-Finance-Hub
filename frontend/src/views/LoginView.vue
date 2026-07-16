<script setup lang="ts">
import { computed, ref } from 'vue';
import { Eye, EyeOff, LoaderCircle, LogIn } from '@lucide/vue';
import { useRoute, useRouter } from 'vue-router';
import AuthLayout from '../components/AuthLayout.vue';
import { useSessionStore } from '../stores/session';
import { ApiError } from '../services/api-error';

const router = useRouter();
const route = useRoute();
const session = useSessionStore();
const username = ref('');
const password = ref('');
const showPassword = ref(false);
const pending = ref(false);
const errorMessage = ref('');

const redirectTarget = computed(() =>
  typeof route.query.redirect === 'string' ? route.query.redirect : '/dashboard',
);

async function submit(): Promise<void> {
  errorMessage.value = '';
  if (!username.value.trim() || !password.value) {
    errorMessage.value = 'Enter your username and password.';
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
    eyebrow="Welcome back"
    title="Pick up where you left off."
    description="Your ledger is ready when you are. Sign in to return to the decisions that matter."
    alternate-text="New here?"
    alternate-label="Create an account"
    alternate-to="/register"
  >
    <form class="auth-form" novalidate @submit.prevent="submit">
      <div class="form-heading">
        <span class="form-heading__index">01</span><span>Secure sign in</span>
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
            name="password"
            :type="showPassword ? 'text' : 'password'"
            autocomplete="current-password"
            placeholder="Your password"
            :disabled="pending" /><button
            class="input-action"
            type="button"
            :aria-label="showPassword ? 'Hide password' : 'Show password'"
            :title="showPassword ? 'Hide password' : 'Show password'"
            @click="showPassword = !showPassword"
          >
            <EyeOff v-if="showPassword" :size="17" /><Eye v-else :size="17" /></button></span
      ></label>
      <button class="button button--submit" type="submit" :disabled="pending">
        <LoaderCircle v-if="pending" class="spin" :size="17" /><LogIn v-else :size="17" />{{
          pending ? 'Signing in' : 'Sign in'
        }}
      </button>
      <p class="form-footnote">Your refresh session is protected by a secure, HttpOnly cookie.</p>
    </form>
  </AuthLayout>
</template>
