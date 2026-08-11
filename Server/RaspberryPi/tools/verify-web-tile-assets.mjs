#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';

const base = new URL(process.argv[2] ?? 'http://127.0.0.1');
const project = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const assetDirectory = path.join(project, 'Assets', 'GridCity', 'StreetV3', 'device');
const rgb565Directory = path.join(project, 'Assets', 'GridCity', 'StreetV3', 'device-rgb565');
const manifestPath = path.join(project, 'Assets', 'GridCity', 'StreetV3', 'manifests',
  'grid-city-street-assets-v3.json');
const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
const manifestAssets = manifest.assets ?? [];
const keys = manifestAssets.map(asset => asset.key);
const expectedPngNames = keys.map(key => `${key}.png`).sort();
const expectedRgb565Names = keys.map(key => `${key}.rgb565`).sort();
const rgb565Names = fs.readdirSync(rgb565Directory).filter(name => name.endsWith('.rgb565')).sort();
const failures = [];
let bytes = 0;
const cornerResults = [];

function pngTruth(asset) {
  const candidate = asset.source
    ? path.resolve(project, asset.source)
    : path.join(assetDirectory, `${asset.key}.png`);
  const relative = path.relative(project, candidate);
  if (relative.startsWith('..') || path.isAbsolute(relative)) {
    failures.push(`${asset.key}: PNG truth escapes project`);
  }
  return candidate;
}

if (manifestAssets.length !== 36) failures.push(`expected 36 manifest assets, found ${manifestAssets.length}`);
if (new Set(keys).size !== keys.length) failures.push('manifest contains duplicate asset keys');
for (const key of keys) {
  if (!/^[a-z0-9-]+$/.test(key)) failures.push(`unsafe manifest asset key: ${key}`);
}
if (JSON.stringify(rgb565Names) !== JSON.stringify(expectedRgb565Names)) {
  failures.push('RGB565 directory does not exactly match all manifest assets');
}
for (const asset of manifestAssets) {
  const name = `${asset.key}.png`;
  const localPath = pngTruth(asset);
  if (!fs.existsSync(localPath)) {
    failures.push(`${name}: local PNG truth is missing`);
    continue;
  }
  const response = await fetch(new URL(`/assets/tiles/${name}`, base));
  if (response.status !== 200) {
    failures.push(`${name}: HTTP ${response.status}`);
    continue;
  }
  if (response.headers.get('content-type') !== 'image/png') {
    failures.push(`${name}: content-type ${response.headers.get('content-type')}`);
  }
  if (response.headers.get('cache-control') !== 'public, max-age=31536000, immutable') {
    failures.push(`${name}: cache-control ${response.headers.get('cache-control')}`);
  }
  const remote = Buffer.from(await response.arrayBuffer());
  bytes += remote.length;
  const localHash = createHash('sha256').update(fs.readFileSync(localPath)).digest('hex');
  const remoteHash = createHash('sha256').update(remote).digest('hex');
  if (localHash !== remoteHash) failures.push(`${name}: SHA-256 mismatch`);
  if (asset.kind === 'corner') {
    cornerResults.push({key: asset.key, status: response.status, bytes: remote.length,
      sha256: remoteHash, matches: localHash === remoteHash});
  }
}

for (const name of rgb565Names) {
  const response = await fetch(new URL(`/assets/tiles/${name}`, base));
  if (response.status !== 200) {
    failures.push(`${name}: HTTP ${response.status}`);
    continue;
  }
  if (response.headers.get('content-type') !== 'application/octet-stream') {
    failures.push(`${name}: content-type ${response.headers.get('content-type')}`);
  }
  if (response.headers.get('cache-control') !== 'public, max-age=31536000, immutable') {
    failures.push(`${name}: cache-control ${response.headers.get('cache-control')}`);
  }
  const remote = Buffer.from(await response.arrayBuffer());
  bytes += remote.length;
  if (remote.length !== 128 * 128 * 2) failures.push(`${name}: invalid byte count ${remote.length}`);
  const localHash = createHash('sha256')
    .update(fs.readFileSync(path.join(rgb565Directory, name))).digest('hex');
  const remoteHash = createHash('sha256').update(remote).digest('hex');
  if (localHash !== remoteHash) failures.push(`${name}: SHA-256 mismatch`);
}

const traversal = await fetch(new URL('/assets/tiles/..%2Fstate.bin', base));
if (traversal.status !== 404) failures.push(`encoded traversal returned HTTP ${traversal.status}`);
for (const target of [
  '/assets/tiles/missing-tile.png',
  '/assets/tiles/corner_central_launch.png',
  '/assets/tiles/Corner-central-launch.png',
  '/assets/tiles/.png',
  '/assets/tiles/missing-tile.rgb565',
  '/assets/tiles/a1_rivet_row.rgb565',
  '/assets/tiles/A1-rivet-row.rgb565',
  '/assets/tiles/.rgb565',
]) {
  const rejected = await fetch(new URL(target, base));
  if (rejected.status !== 404) failures.push(`${target}: expected 404, received ${rejected.status}`);
}
const page = await fetch(base);
if (page.status !== 200 || page.headers.get('content-encoding') !== 'gzip') {
  failures.push(`root page status/encoding ${page.status}/${page.headers.get('content-encoding')}`);
}

if (cornerResults.length !== 4 || cornerResults.some(corner => !corner.matches)) {
  failures.push(`corner verification incomplete: ${cornerResults.length}/4`);
}

console.log(JSON.stringify({
  baseUrl: base.origin,
  assets: expectedPngNames.length,
  playerConsoleAssets: rgb565Names.length,
  corners: cornerResults,
  bytes,
  etag: page.headers.get('etag'),
  traversalStatus: traversal.status,
  failures,
}, null, 2));

if (failures.length !== 0) {
  console.error('GRIDOPOLY_WEB_TILE_ASSETS_FAIL');
  process.exitCode = 1;
} else {
  console.log('GRIDOPOLY_WEB_TILE_ASSETS_PASS');
}
