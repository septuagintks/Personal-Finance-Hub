import { readFileSync, readdirSync, statSync } from 'node:fs';
import { gzipSync } from 'node:zlib';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import console from 'node:console';
import process from 'node:process';

const frontendRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const distRoot = resolve(frontendRoot, process.argv[2] ?? 'dist');
const budgets = JSON.parse(readFileSync(join(frontendRoot, 'bundle-budgets.json'), 'utf8'));
const manifestPath = join(distRoot, '.vite', 'manifest.json');
const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));

function filesUnder(root) {
  const result = [];
  for (const name of readdirSync(root)) {
    const path = join(root, name);
    if (statSync(path).isDirectory()) result.push(...filesUnder(path));
    else result.push(path);
  }
  return result;
}

function gzipBytes(path) {
  return gzipSync(readFileSync(path), { level: 9 }).byteLength;
}

const entryKey = Object.keys(manifest).find((key) => manifest[key].isEntry);
if (!entryKey) throw new Error('Vite manifest does not contain an application entry.');

const initialFiles = new Set();
function collectInitial(key) {
  const item = manifest[key];
  if (!item || initialFiles.has(item.file)) return;
  initialFiles.add(item.file);
  for (const imported of item.imports ?? []) collectInitial(imported);
}
collectInitial(entryKey);

const allFiles = filesUnder(distRoot);
const javascript = allFiles.filter((path) => path.endsWith('.js'));
const stylesheets = allFiles.filter((path) => path.endsWith('.css'));
const initialJavascript = javascript.filter((path) =>
  initialFiles.has(relative(distRoot, path).replaceAll('\\', '/')),
);
const asyncJavascript = javascript.filter((path) => !initialJavascript.includes(path));

const initialJsGzip = initialJavascript.reduce((total, path) => total + gzipBytes(path), 0);
const totalJsGzip = javascript.reduce((total, path) => total + gzipBytes(path), 0);
const cssGzip = stylesheets.reduce((total, path) => total + gzipBytes(path), 0);
const largestAsync = asyncJavascript
  .map((path) => ({ path, bytes: gzipBytes(path) }))
  .sort((left, right) => right.bytes - left.bytes)[0];

const failures = [];
if (initialJsGzip > budgets.initialJsGzipBytes) {
  failures.push(`initial JS gzip ${initialJsGzip} exceeds ${budgets.initialJsGzipBytes}`);
}
if (totalJsGzip > budgets.totalJsGzipBytes) {
  failures.push(`total JS gzip ${totalJsGzip} exceeds ${budgets.totalJsGzipBytes}`);
}
if ((largestAsync?.bytes ?? 0) > budgets.maxAsyncChunkGzipBytes) {
  failures.push(
    `async chunk ${relative(distRoot, largestAsync.path)} gzip ${largestAsync.bytes} exceeds ${budgets.maxAsyncChunkGzipBytes}`,
  );
}
if (cssGzip > budgets.cssGzipBytes) {
  failures.push(`CSS gzip ${cssGzip} exceeds ${budgets.cssGzipBytes}`);
}
if (!javascript.some((path) => /[\\/]charts-[^\\/]+\.js$/.test(path))) {
  failures.push('ECharts is not isolated in a route-only charts chunk');
}

if (failures.length) {
  for (const failure of failures) console.error(`ERROR: ${failure}`);
  process.exit(1);
}

console.log(
  `Frontend bundle budgets: PASS initial-js=${initialJsGzip}B total-js=${totalJsGzip}B ` +
    `largest-async=${largestAsync?.bytes ?? 0}B css=${cssGzip}B`,
);
