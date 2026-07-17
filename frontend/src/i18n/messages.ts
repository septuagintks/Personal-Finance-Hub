export type SupportedLocale = 'zh-CN' | 'en-US';

const messages = {
  'en-US': {
    overview: 'Overview',
    accounts: 'Accounts',
    transactions: 'Transactions',
    transfers: 'Transfers',
    reports: 'Reports',
    settings: 'Settings',
    maintenance: 'Maintenance',
    operations: 'Operations',
    signOut: 'Sign out',
    workspace: 'Workspace',
    privateWorkspace: 'Private workspace',
  },
  'zh-CN': {
    overview: '概览',
    accounts: '账户',
    transactions: '流水',
    transfers: '转账',
    reports: '报表',
    settings: '设置',
    maintenance: '维护',
    operations: '运维',
    signOut: '退出登录',
    workspace: '工作区',
    privateWorkspace: '私人账本',
  },
} as const;

export type MessageKey = keyof (typeof messages)['en-US'];

export function supportedLocale(value: string | undefined): SupportedLocale {
  return value === 'zh-CN' ? 'zh-CN' : 'en-US';
}

export function translate(locale: string | undefined, key: MessageKey): string {
  return messages[supportedLocale(locale)][key];
}
