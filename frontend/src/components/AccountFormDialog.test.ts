import { flushPromises, mount } from '@vue/test-utils';
import { beforeAll, describe, expect, it } from 'vitest';
import AccountFormDialog from './AccountFormDialog.vue';

beforeAll(() => {
  Object.defineProperty(HTMLDialogElement.prototype, 'showModal', {
    configurable: true,
    value(this: HTMLDialogElement) {
      this.setAttribute('open', '');
    },
  });
  Object.defineProperty(HTMLDialogElement.prototype, 'close', {
    configurable: true,
    value(this: HTMLDialogElement) {
      this.removeAttribute('open');
    },
  });
});

describe('AccountFormDialog', () => {
  it('keeps create classification automatic and uses the user base currency', async () => {
    const wrapper = mount(AccountFormDialog, {
      props: {
        open: true,
        mode: 'create',
        defaultCurrency: 'USD',
        currencies: [
          { code: 'CNY', symbol: 'CNY', precision: 2, displayName: 'Yuan', isCrypto: false },
          { code: 'USD', symbol: '$', precision: 2, displayName: 'US Dollar', isCrypto: false },
        ],
      },
    });
    await flushPromises();

    await wrapper.get('input[name="account-name"]').setValue('Credit card');
    await wrapper.get('select[name="account-type"]').setValue('credit');
    await wrapper.get('input[name="account-subtype"]').setValue('credit_card');
    await wrapper.get('form').trigger('submit');

    expect(wrapper.emitted('submit')?.[0]?.[0]).toEqual({
      name: 'Credit card',
      type: 'credit',
      subtype: 'credit_card',
      category: null,
      currencyCode: 'USD',
      description: '',
    });
  });
});
