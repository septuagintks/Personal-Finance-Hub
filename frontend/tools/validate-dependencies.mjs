import { existsSync, readFileSync, realpathSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import console from 'node:console';
import process from 'node:process';

const frontendRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const projectRoot = resolve(frontendRoot, '..');
const rootPackage = JSON.parse(readFileSync(join(projectRoot, 'package.json'), 'utf8'));
const webPackage = JSON.parse(readFileSync(join(frontendRoot, 'package.json'), 'utf8'));
const vcpkg = JSON.parse(readFileSync(join(projectRoot, 'vcpkg.json'), 'utf8'));
const lockfile = readFileSync(join(projectRoot, 'pnpm-lock.yaml'), 'utf8');
const failures = [];

if (!/^pnpm@\d+\.\d+\.\d+$/.test(rootPackage.packageManager ?? '')) {
  failures.push('root packageManager must pin an exact pnpm version');
}

const direct = { ...webPackage.dependencies, ...webPackage.devDependencies };
for (const [name, version] of Object.entries(direct)) {
  if (!/^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/.test(version)) {
    failures.push(`${name} must use an exact version, received ${version}`);
  }
}
if ('element-plus' in direct || lockfile.includes('element-plus@')) {
  failures.push('unused Element Plus dependency must not remain in the release graph');
}

const approvedLicenses = new Set([
  '0BSD',
  'Apache-2.0',
  'BSD-2-Clause',
  'BSD-3-Clause',
  'ISC',
  'MIT',
]);
const licenseFacts = [];
for (const name of Object.keys(webPackage.dependencies).sort()) {
  const installed = JSON.parse(
    readFileSync(join(frontendRoot, 'node_modules', name, 'package.json'), 'utf8'),
  );
  const license = Array.isArray(installed.license)
    ? installed.license.join(' OR ')
    : installed.license;
  if (!license || !approvedLicenses.has(license)) {
    failures.push(`${name}@${installed.version} has unapproved or missing license ${license}`);
  }
  if (installed.version !== webPackage.dependencies[name]) {
    failures.push(
      `${name} installed version ${installed.version} differs from ${webPackage.dependencies[name]}`,
    );
  }
  licenseFacts.push(`${name}@${installed.version}:${license}`);
}

const productionLicenses = new Map();
function resolvePackageRoot(name, parentRoot) {
  const require = createRequire(join(parentRoot, 'package.json'));
  for (const modulesRoot of require.resolve.paths(name) ?? []) {
    const candidate = join(modulesRoot, name);
    const manifestPath = join(candidate, 'package.json');
    if (existsSync(manifestPath)) {
      const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
      if (manifest.name === name) return realpathSync(candidate);
    }
  }
  throw new Error(`Could not resolve package root for ${name}`);
}

function collectProductionLicenses(dependencies, parentRoot) {
  for (const name of Object.keys(dependencies ?? {})) {
    const packageRoot = resolvePackageRoot(name, parentRoot);
    const manifest = JSON.parse(readFileSync(join(packageRoot, 'package.json'), 'utf8'));
    const key = `${name}@${manifest.version}`;
    if (!productionLicenses.has(key)) {
      const license = Array.isArray(manifest.license)
        ? manifest.license.join(' OR ')
        : manifest.license;
      productionLicenses.set(key, license);
      if (!license || !approvedLicenses.has(license)) {
        failures.push(`${key} has unapproved or missing production license ${license}`);
      }
      collectProductionLicenses(manifest.dependencies, packageRoot);
    }
  }
}
collectProductionLicenses(webPackage.dependencies, frontendRoot);

if (!/^[0-9a-f]{40}$/.test(vcpkg['builtin-baseline'] ?? '')) {
  failures.push('vcpkg builtin-baseline must be an immutable 40-character commit hash');
}

if (failures.length) {
  for (const failure of failures) console.error(`ERROR: ${failure}`);
  process.exit(1);
}
console.log(
  `Dependency policy: PASS direct=[${licenseFacts.join(', ')}] ` +
    `production-packages=${productionLicenses.size}`,
);
