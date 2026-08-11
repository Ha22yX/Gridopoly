#!/usr/bin/env node

import http from 'node:http';
import net from 'node:net';

const base = new URL(process.argv[2] ?? 'http://127.0.0.1');
const idleCount = Number.parseInt(process.argv[3] ?? '16', 10);
const limitMs = Number.parseInt(process.argv[4] ?? '750', 10);
const host = base.hostname;
const port = Number.parseInt(base.port || '80', 10);

if (!Number.isInteger(idleCount) || idleCount < 1 || idleCount > 256 ||
    !Number.isInteger(limitMs) || limitMs < 1) {
  throw new Error('usage: stress-http-idle-keepalive.mjs <base-url> [idle-count] [latency-limit-ms]');
}

function openIdleConnection() {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({host, port});
    let response = '';
    const timeout = setTimeout(() => {
      socket.destroy();
      reject(new Error('idle connection did not receive response headers'));
    }, 5000);
    socket.setNoDelay(true);
    socket.once('error', reject);
    socket.on('data', chunk => {
      response += chunk.toString('latin1');
      if (response.includes('\r\n\r\n')) {
        clearTimeout(timeout);
        socket.removeAllListeners('error');
        socket.on('error', () => {});
        socket.on('data', () => {});
        resolve(socket);
      }
    });
    socket.once('connect', () => {
      socket.write(`GET /health HTTP/1.1\r\nHost: ${host}\r\nConnection: keep-alive\r\n\r\n`);
    });
  });
}

function probeHealth() {
  return new Promise((resolve, reject) => {
    const started = performance.now();
    const request = http.get({host, port, path: '/health', agent: false}, response => {
      let body = '';
      response.setEncoding('utf8');
      response.on('data', chunk => { body += chunk; });
      response.on('end', () => {
        try {
          const parsed = JSON.parse(body);
          if (response.statusCode !== 200 || parsed.ok !== true) {
            reject(new Error(`unexpected health response ${response.statusCode}`));
            return;
          }
          resolve(performance.now() - started);
        } catch (error) {
          reject(error);
        }
      });
    });
    request.setTimeout(8000, () => request.destroy(new Error('health probe timeout')));
    request.once('error', reject);
  });
}

const sockets = await Promise.all(Array.from({length: idleCount}, openIdleConnection));
await new Promise(resolve => setTimeout(resolve, 100));
let occupiedLatencyMs;
try {
  occupiedLatencyMs = await probeHealth();
} finally {
  for (const socket of sockets) socket.destroy();
}
await new Promise(resolve => setTimeout(resolve, 200));
const recoveredLatencyMs = await probeHealth();

const result = {
  baseUrl: base.origin,
  idleKeepAliveConnections: idleCount,
  limitMs,
  occupiedLatencyMs: Number(occupiedLatencyMs.toFixed(1)),
  recoveredLatencyMs: Number(recoveredLatencyMs.toFixed(1)),
};
console.log(JSON.stringify(result, null, 2));

if (occupiedLatencyMs > limitMs || recoveredLatencyMs > limitMs) {
  console.error('GRIDOPOLY_PI_HTTP_IDLE_KEEPALIVE_STRESS_FAIL');
  process.exitCode = 1;
} else {
  console.log('GRIDOPOLY_PI_HTTP_IDLE_KEEPALIVE_STRESS_PASS');
}
