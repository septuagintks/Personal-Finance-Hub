import { readFileSync, readdirSync } from 'node:fs';
import { join, relative, resolve } from 'node:path';
import console from 'node:console';
import process from 'node:process';

const root = resolve(import.meta.dirname, '..');
const source = join(root, 'src');
const failures = [];
const technicalText = /^(?:YYYY|MM|DD)(?:[-/.](?:YYYY|MM|DD)){2}$/;

function files(directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const path = join(directory, entry.name);
    return entry.isDirectory() ? files(path) : [path];
  });
}

for (const path of files(source).filter((value) => value.endsWith('.vue'))) {
  const content = readFileSync(path, 'utf8');
  const template = content.match(/<template>([\s\S]*?)<\/template>/)?.[1] ?? '';
  const displayPath = relative(root, path).replaceAll('\\', '/');
  const withoutInterpolations = template.replaceAll(/\{\{[\s\S]*?\}\}/g, '');

  for (const match of withoutInterpolations.matchAll(/>([^<>]+)</g)) {
    const text = match[1].replaceAll(/\s+/g, ' ').trim();
    if (!text || technicalText.test(text)) continue;
    if (/[A-Za-z\u3400-\u9fff]/u.test(text)) {
      failures.push(`${displayPath}: visible text node is not localized: ${text}`);
    }
  }

  for (const match of template.matchAll(/(?<![:\w-])(?:aria-label|placeholder|title)="([^"]+)"/g)) {
    const value = match[1];
    if (/[A-Za-z\u3400-\u9fff]/u.test(value)) {
      failures.push(`${displayPath}: visible attribute is not localized: ${value}`);
    }
  }

  if (/\bzh\s*\?/.test(content)) {
    failures.push(`${displayPath}: locale-specific conditional copy is not allowed`);
  }
}

if (failures.length) {
  for (const failure of failures) console.error(`ERROR: ${failure}`);
  process.exit(1);
}

console.log('Frontend visible-text localization: PASS');
