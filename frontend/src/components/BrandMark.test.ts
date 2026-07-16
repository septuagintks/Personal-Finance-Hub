import { describe, expect, it } from 'vitest';
import { mount } from '@vue/test-utils';
import BrandMark from './BrandMark.vue';

describe('BrandMark', () => {
  it('renders the configured product name without duplicating brand text', () => {
    const wrapper = mount(BrandMark);
    expect(wrapper.text()).toBe("Candy's Ledger");
    expect(wrapper.find('svg').exists()).toBe(true);
  });
});
