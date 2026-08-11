import assert from 'node:assert/strict';
import { performance } from 'node:perf_hooks';

const baseUrl = (process.argv[2] || 'http://192.168.3.126').replace(/\/$/, '');
const durationSeconds = Number.parseInt(process.argv[3] || '900', 10);
const injectSilenceSeconds = Number.parseInt(process.argv[4] || '0', 10);
assert.ok(Number.isInteger(durationSeconds) && durationSeconds >= 60,
  'duration must be at least 60 seconds');
assert.ok(Number.isInteger(injectSilenceSeconds) && injectSilenceSeconds >= 0,
  'injected silence must be zero or a positive number of seconds');

const requestTimeoutMs = 7000;
const startedAt = performance.now();
const endAt = startedAt + durationSeconds * 1000;
const latencies = [];
const failures = new Map();
let successes = 0;
let syncSuccesses = 0;
let fullSyncSuccesses = 0;
let healthSuccesses = 0;
let pageSuccesses = 0;
let status204 = 0;
let state = null;
let lowestVersion = null;
let highestVersion = null;
let initialRoom = null;
let versionRegressions = 0;
let roomChanges = 0;
let consecutiveFailures = 0;
let longestFailureRun = 0;
let outageStartedAt = null;
let longestOutageMs = 0;
let recoveries = 0;
let nextHealthAt = startedAt;
let nextPageAt = startedAt;
let nextFullSyncAt = startedAt;
let nextProgressAt = startedAt + 30000;
let injected = false;
let initialHealth = null;
let finalHealth = null;
let pageEtag = null;

const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

async function timedFetch(path, options = {}) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), requestTimeoutMs);
  const requestStartedAt = performance.now();
  try {
    const response = await fetch(`${baseUrl}${path}`, {
      ...options,
      cache: 'no-store',
      signal: controller.signal,
    });
    return { response, elapsedMs: performance.now() - requestStartedAt };
  } finally {
    clearTimeout(timeout);
  }
}

function recordFailure(error) {
  const name = error?.name || error?.cause?.code || 'Error';
  failures.set(name, (failures.get(name) || 0) + 1);
  consecutiveFailures += 1;
  longestFailureRun = Math.max(longestFailureRun, consecutiveFailures);
  if (outageStartedAt === null) outageStartedAt = performance.now();
}

function recordSuccess(elapsedMs) {
  successes += 1;
  consecutiveFailures = 0;
  latencies.push(elapsedMs);
  if (outageStartedAt !== null) {
    longestOutageMs = Math.max(longestOutageMs, performance.now() - outageStartedAt);
    recoveries += 1;
    outageStartedAt = null;
  }
}

function observeState(next) {
  if (next.schema !== 2) throw new Error(`unexpected schema ${next.schema}`);
  if (initialRoom === null) initialRoom = next.roomId;
  else if (next.roomId !== initialRoom) roomChanges += 1;
  if (highestVersion !== null && next.version < highestVersion) versionRegressions += 1;
  lowestVersion = lowestVersion === null ? next.version : Math.min(lowestVersion, next.version);
  highestVersion = highestVersion === null ? next.version : Math.max(highestVersion, next.version);
  state = next;
}

async function pollSync(forceFull = false) {
  const conditional = !forceFull && state
    ? `?since=${state.version}&peers=${state.espnowPeers}&room=${state.roomId}&network=${state.network}`
    : '';
  const sample = await timedFetch(`/api/sync${conditional}`);
  if (sample.response.status === 204) {
    status204 += 1;
  } else {
    if (sample.response.status !== 200) throw new Error(`sync HTTP ${sample.response.status}`);
    observeState(await sample.response.json());
  }
  syncSuccesses += 1;
  if (forceFull) fullSyncSuccesses += 1;
  recordSuccess(sample.elapsedMs);
}

async function pollHealth() {
  const sample = await timedFetch('/health');
  if (sample.response.status !== 200) throw new Error(`health HTTP ${sample.response.status}`);
  const body = await sample.response.json();
  if (initialHealth === null) initialHealth = body;
  finalHealth = body;
  healthSuccesses += 1;
  recordSuccess(sample.elapsedMs);
}

async function pollPage() {
  const headers = pageEtag ? { 'If-None-Match': pageEtag } : {};
  const sample = await timedFetch('/', { headers });
  if (pageEtag) {
    if (sample.response.status !== 304) throw new Error(`page ETag HTTP ${sample.response.status}`);
  } else {
    if (sample.response.status !== 200) throw new Error(`page HTTP ${sample.response.status}`);
    pageEtag = sample.response.headers.get('etag');
    if (!pageEtag) throw new Error('page ETag missing');
    await sample.response.arrayBuffer();
  }
  pageSuccesses += 1;
  recordSuccess(sample.elapsedMs);
}

while (performance.now() < endAt) {
  const now = performance.now();
  if (!injected && injectSilenceSeconds > 0 && syncSuccesses >= 5) {
    injected = true;
    console.log(`GRIDOPOLY_SOAK_INJECT silenceSeconds=${injectSilenceSeconds}`);
    await delay(injectSilenceSeconds * 1000);
    continue;
  }
  try {
    if (now >= nextFullSyncAt) {
      nextFullSyncAt = now + 30000;
      await pollSync(true);
    } else {
      await pollSync(false);
    }
  } catch (error) {
    recordFailure(error);
  }
  if (now >= nextHealthAt) {
    nextHealthAt = now + 15000;
    try { await pollHealth(); } catch (error) { recordFailure(error); }
  }
  if (now >= nextPageAt) {
    nextPageAt = now + 60000;
    try { await pollPage(); } catch (error) { recordFailure(error); }
  }
  if (now >= nextProgressAt) {
    nextProgressAt = now + 30000;
    console.log(`GRIDOPOLY_SOAK_PROGRESS elapsed=${Math.round((now - startedAt) / 1000)}s ` +
      `ok=${successes} fail=${[...failures.values()].reduce((a, b) => a + b, 0)} ` +
      `run=${consecutiveFailures} version=${state?.version ?? 0}`);
  }
  await delay(1000);
}

if (outageStartedAt !== null) longestOutageMs = Math.max(longestOutageMs, performance.now() - outageStartedAt);
latencies.sort((left, right) => left - right);
const percentile = fraction => latencies.length === 0 ? null
  : latencies[Math.min(latencies.length - 1, Math.floor(latencies.length * fraction))];
const totalFailures = [...failures.values()].reduce((sum, value) => sum + value, 0);
const report = {
  baseUrl,
  durationSeconds,
  injectSilenceSeconds,
  requests: { successes, failures: totalFailures, syncSuccesses, fullSyncSuccesses,
    healthSuccesses, pageSuccesses, status204 },
  failureKinds: Object.fromEntries(failures),
  outage: { recoveries, longestFailureRun, longestOutageMs: Math.round(longestOutageMs) },
  latencyMs: {
    p50: percentile(0.50) === null ? null : Number(percentile(0.50).toFixed(1)),
    p95: percentile(0.95) === null ? null : Number(percentile(0.95).toFixed(1)),
    max: latencies.length === 0 ? null : Number(latencies.at(-1).toFixed(1)),
  },
  state: { roomId: initialRoom, lowestVersion, highestVersion, versionRegressions, roomChanges },
  health: { before: initialHealth, after: finalHealth },
};
console.log(JSON.stringify(report, null, 2));

assert.ok(successes > 0, 'no request ever succeeded');
assert.equal(versionRegressions, 0, 'state version regressed');
assert.equal(roomChanges, 0, 'room changed during read-only soak');
assert.ok(longestOutageMs <= 45000, `outage exceeded self-heal budget: ${longestOutageMs}ms`);
assert.ok(consecutiveFailures === 0, 'server was still unreachable at the end of the soak');
console.log('GRIDOPOLY_HTTP_SOAK_PASS');
