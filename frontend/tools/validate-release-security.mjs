import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, extname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import console from 'node:console';
import process from 'node:process';

const frontendRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const projectRoot = resolve(frontendRoot, '..');
const distRoot = resolve(frontendRoot, process.argv[2] ?? 'dist');
const failures = [];
const secretPatterns = [
  ['private key', /-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----/],
  ['FreeCurrencyAPI key', /fca_live_[A-Za-z0-9]{20,}/],
  ['GitHub token', /gh[oprsu]_[A-Za-z0-9]{30,}/],
  ['AWS access key', /AKIA[0-9A-Z]{16}/],
  ['JWT', /eyJ[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}/],
];

function filesUnder(root) {
  const result = [];
  for (const name of readdirSync(root)) {
    const path = join(root, name);
    if (statSync(path).isDirectory()) result.push(...filesUnder(path));
    else result.push(path);
  }
  return result;
}

function scan(path, label) {
  const data = readFileSync(path);
  if (data.includes(0)) return;
  const text = data.toString('utf8');
  for (const [name, pattern] of secretPatterns) {
    if (pattern.test(text)) failures.push(`${label} contains a high-confidence ${name} pattern`);
  }
}

const sourceFiles = execFileSync(
  'git',
  ['ls-files', '-z', '--cached', '--others', '--exclude-standard'],
  {
    cwd: projectRoot,
    encoding: 'utf8',
  },
)
  .split('\0')
  .filter(Boolean);
for (const name of sourceFiles) {
  if (/\.(?:pem|p12|pfx|key)$/i.test(name)) {
    failures.push(`tracked secret-bearing file extension: ${name}`);
    continue;
  }
  const path = join(projectRoot, name);
  if (!existsSync(path)) continue;
  if (statSync(path).size <= 2 * 1024 * 1024) scan(path, name);
}

for (const path of filesUnder(distRoot)) {
  const name = relative(distRoot, path).replaceAll('\\', '/');
  if (extname(path) === '.map') failures.push(`production source map exists: ${name}`);
  if (/\.(?:html|js|css|json)$/i.test(path)) {
    const text = readFileSync(path, 'utf8');
    if (text.includes('sourceMappingURL=')) failures.push(`source map reference exists: ${name}`);
    scan(path, `dist/${name}`);
  }
}

for (const path of filesUnder(join(frontendRoot, 'src')).filter((item) => item.endsWith('.vue'))) {
  const text = readFileSync(path, 'utf8');
  if (/\bv-html\s*=/.test(text)) {
    failures.push(`${relative(projectRoot, path)} renders untrusted HTML with v-html`);
  }
}

if (failures.length) {
  for (const failure of failures) console.error(`ERROR: ${failure}`);
  process.exit(1);
}
console.log(
  `Release security scan: PASS source-files=${sourceFiles.length} ` +
    `dist-files=${filesUnder(distRoot).length}`,
);
