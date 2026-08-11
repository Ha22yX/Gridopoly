import assert from 'node:assert/strict';
import { performance } from 'node:perf_hooks';

const baseUrl = (process.argv[2] || 'http://10.42.0.1').replace(/\/$/, '');
const durationSeconds = Number.parseInt(process.argv[3] || '300', 10);
const concurrency = Number.parseInt(process.argv[4] || '16', 10);
assert.ok(Number.isInteger(durationSeconds) && durationSeconds >= 10);
assert.ok(Number.isInteger(concurrency) && concurrency >= 1 && concurrency <= 64);

async function request(path, options = {}) {
  const started = performance.now();
  const response = await fetch(`${baseUrl}${path}`, {
    ...options,
    cache: 'no-store',
    signal: AbortSignal.timeout(5000),
  });
  const body = response.status === 204 || response.status === 304
    ? null
    : await response.arrayBuffer();
  return { response, body, elapsedMs: performance.now() - started };
}

const beforeResponse = await request('/health');
assert.equal(beforeResponse.response.status, 200);
const before = JSON.parse(Buffer.from(beforeResponse.body).toString('utf8'));
assert.equal(before.platform, 'raspberry-pi');
const initialResponse = await request('/api/sync');
assert.equal(initialResponse.response.status, 200);
const initial = JSON.parse(Buffer.from(initialResponse.body).toString('utf8'));
const conditional = `/api/sync?since=${initial.version}&peers=${initial.espnowPeers}` +
  `&room=${initial.roomId}&network=${initial.network}` +
  `&control=${initial.controlVersion ?? 0}`;
const endAt = performance.now() + durationSeconds * 1000;
const latencies = [];
const statuses = new Map();
const failures = [];
let requests = 0;
let fullSyncs = 0;
let pages = 0;
let boards = 0;
let tileAssets = 0;

async function worker(workerId) {
  let iteration = 0;
  while (performance.now() < endAt) {
    const selector = (workerId + iteration) % 50;
    let path = conditional;
    if (selector === 0) path = '/';
    else if (selector === 1) path = '/api/board';
    else if (selector === 2) path = '/api/sync';
    else if (selector === 3) path = '/assets/tiles/a1-rivet-row.rgb565';
    try {
      const sample = await request(path);
      statuses.set(sample.response.status, (statuses.get(sample.response.status) || 0) + 1);
      if (path === conditional) assert.ok(sample.response.status === 200 || sample.response.status === 204);
      else assert.equal(sample.response.status, 200);
      if (path === '/') pages += 1;
      else if (path === '/api/board') boards += 1;
      else if (path.endsWith('.rgb565')) {
        assert.equal(sample.response.headers.get('content-type'), 'application/octet-stream');
        assert.equal(sample.response.headers.get('cache-control'),
          'public, max-age=31536000, immutable');
        assert.equal(sample.body.byteLength, 128 * 128 * 2);
        tileAssets += 1;
      }
      else if (path === '/api/sync') {
        fullSyncs += 1;
        const body = JSON.parse(Buffer.from(sample.body).toString('utf8'));
        assert.equal(body.roomId, initial.roomId);
        assert.ok(body.version >= initial.version);
      }
      latencies.push(sample.elapsedMs);
      requests += 1;
    } catch (error) {
      failures.push(String(error?.stack || error));
    }
    iteration += 1;
  }
}

await Promise.all(Array.from({ length: concurrency }, (_, index) => worker(index)));
const afterResponse = await request('/health');
assert.equal(afterResponse.response.status, 200);
const after = JSON.parse(Buffer.from(afterResponse.body).toString('utf8'));
assert.equal(after.roomId, before.roomId);
assert.equal(failures.length, 0, failures.slice(0, 3).join('\n'));
assert.equal(after.http.rejected - before.http.rejected, 0);
assert.equal(after.http.sendErrors - before.http.sendErrors, 0);
assert.equal(after.http.timeouts - before.http.timeouts, 0);
assert.equal(after.udp.authFailures - before.udp.authFailures, 0);
assert.equal(after.udp.txErrors - before.udp.txErrors, 0);

latencies.sort((left, right) => left - right);
const percentile = value => latencies[Math.min(latencies.length - 1,
  Math.floor(latencies.length * value))];
const report = {
  baseUrl,
  durationSeconds,
  concurrency,
  requests,
  requestsPerSecond: Number((requests / durationSeconds).toFixed(1)),
  statuses: Object.fromEntries(statuses),
  fullSyncs,
  pages,
  boards,
  tileAssets,
  failures: failures.length,
  latencyMs: {
    p50: Number(percentile(0.50).toFixed(1)),
    p95: Number(percentile(0.95).toFixed(1)),
    p99: Number(percentile(0.99).toFixed(1)),
    max: Number(latencies.at(-1).toFixed(1)),
  },
  httpDelta: Object.fromEntries(Object.keys(after.http).map(key => [
    key,
    Number(after.http[key]) - Number(before.http[key] || 0),
  ])),
};
console.log(JSON.stringify(report, null, 2));
console.log('GRIDOPOLY_PI_HTTP_CONCURRENT_STRESS_PASS');
