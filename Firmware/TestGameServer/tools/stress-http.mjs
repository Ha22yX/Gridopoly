import assert from 'node:assert/strict';
import { performance } from 'node:perf_hooks';

const baseUrl = (process.argv[2] || 'http://192.168.3.126').replace(/\/$/, '');
const iterations = Number.parseInt(process.argv[3] || '180', 10);
assert.ok(Number.isInteger(iterations) && iterations > 0, 'iterations must be a positive integer');

async function timedFetch(path, timeoutMs = 10000, options = {}) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);
  const started = performance.now();
  try {
    const response = await fetch(`${baseUrl}${path}`, {
      ...options,
      cache: 'no-store',
      signal: controller.signal,
    });
    return { response, elapsedMs: performance.now() - started };
  } finally {
    clearTimeout(timeout);
  }
}

async function readHealth() {
  const { response, elapsedMs } = await timedFetch('/health');
  assert.equal(response.status, 200, 'health endpoint must return 200');
  return { body: await response.json(), elapsedMs };
}

const before = await readHealth();
const initial = await timedFetch('/api/sync');
assert.equal(initial.response.status, 200, 'initial compact sync endpoint must return 200');
const initialBytes = (await initial.response.clone().arrayBuffer()).byteLength;
assert.ok(initialBytes <= 2048, `compact sync exceeds one reliable response window (${initialBytes} bytes)`);
const state = await initial.response.json();
assert.equal(state.schema, 2, 'compact sync schema must be v2');
const board = await timedFetch(`/api/board?room=${state.roomId}`);
assert.equal(board.response.status, 200, 'compact board endpoint must return 200');
const boardBytes = (await board.response.clone().arrayBuffer()).byteLength;
assert.ok(boardBytes <= 2048, `compact board exceeds one reliable response window (${boardBytes} bytes)`);
const boardBody = await board.response.json();
assert.equal(boardBody.tiles.length, state.board.size, 'board projection tile count mismatch');
const conditionalPath = `/api/sync?since=${state.version}&peers=${state.espnowPeers}` +
  `&room=${state.roomId}&network=${state.network}`;

const latencies = [];
let notModified = 0;
for (let index = 0; index < iterations; index += 1) {
  const sample = await timedFetch(conditionalPath);
  assert.ok(sample.response.status === 200 || sample.response.status === 204,
    `state poll ${index} returned ${sample.response.status}`);
  if (sample.response.status === 204) notModified += 1;
  else await sample.response.arrayBuffer();
  latencies.push(sample.elapsedMs);
  if ((index + 1) % 30 === 0) {
    const fullSync = await timedFetch('/api/sync');
    assert.equal(fullSync.response.status, 200, 'full compact sync request must return 200');
    const fullBody = await fullSync.response.json();
    assert.equal(fullBody.roomId, state.roomId, 'room changed during read-only stress test');
    await readHealth();
  }
}

const page = await timedFetch('/', 15000);
assert.equal(page.response.status, 200, 'web page must return 200');
const pageBytes = (await page.response.arrayBuffer()).byteLength;
assert.ok(pageBytes > 8000, `web page is unexpectedly short (${pageBytes} bytes)`);
const pageEtag = page.response.headers.get('etag');
assert.ok(pageEtag, 'web page must provide an ETag');
const etagResponse = await timedFetch('/', 8000, { headers: { 'If-None-Match': pageEtag } });
assert.equal(etagResponse.response.status, 304, 'unchanged web page must use a small 304 response');
const after = await readHealth();

latencies.sort((left, right) => left - right);
const percentile = value => latencies[Math.min(latencies.length - 1, Math.floor(latencies.length * value))];
const hasHeapTelemetry = Number.isFinite(Number(before.body.heap)) &&
  Number.isFinite(Number(after.body.heap));
const heapDelta = hasHeapTelemetry ? Number(after.body.heap) - Number(before.body.heap) : null;
const httpKeys = new Set([
  ...Object.keys(before.body.http || {}),
  ...Object.keys(after.body.http || {}),
]);
const httpDelta = Object.fromEntries([...httpKeys].map(key => [
  key,
  Number(after.body.http?.[key] || 0) - Number(before.body.http?.[key] || 0),
]));
const networkDelta = Object.fromEntries(['probeSuccesses', 'probeTimeouts', 'probeErrors',
  'serviceRestarts', 'staReconnects', 'reconnectAttempts'].map(key => [
  key,
  Number(after.body.networkHealth?.[key] || 0) - Number(before.body.networkHealth?.[key] || 0),
]));
assert.equal(httpDelta.rejected, 0, 'HTTP slot rejection increased during stress');
assert.equal(httpDelta.sendErrors, 0, 'HTTP send errors increased during stress');
const timeoutDelta = after.body.platform === 'raspberry-pi'
  ? Number(httpDelta.timeouts || 0)
  : Number(httpDelta.responseTimeouts || 0);
assert.equal(timeoutDelta, 0, 'HTTP response timeouts increased during stress');
assert.equal(networkDelta.serviceRestarts, 0, 'network service restarted during healthy stress');
assert.equal(networkDelta.staReconnects, 0, 'STA reconnected during healthy stress');
const report = {
  baseUrl,
  iterations,
  notModified,
  payloadBytes: { sync: initialBytes, board: boardBytes, page: pageBytes },
  latencyMs: {
    p50: Number(percentile(0.50).toFixed(1)),
    p95: Number(percentile(0.95).toFixed(1)),
    max: Number(latencies.at(-1).toFixed(1)),
    page: Number(page.elapsedMs.toFixed(1)),
  },
  heap: {
    before: before.body.heap ?? null,
    after: after.body.heap ?? null,
    delta: heapDelta,
    min: after.body.minHeap ?? null,
    maxAlloc: after.body.maxAllocHeap ?? null,
  },
  http: after.body.http,
  httpDelta,
  networkHealth: after.body.networkHealth,
  networkDelta,
  espnow: after.body.espnow,
};

console.log(JSON.stringify(report, null, 2));
console.log('GRIDOPOLY_HTTP_STRESS_PASS');
