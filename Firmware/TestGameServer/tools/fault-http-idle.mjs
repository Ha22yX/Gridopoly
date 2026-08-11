import assert from 'node:assert/strict';
import net from 'node:net';

const target = new URL(process.argv[2] || 'http://192.168.3.126');
const slotCount = Number.parseInt(process.argv[3] || '8', 10);
assert.ok(Number.isInteger(slotCount) && slotCount > 0 && slotCount <= 16);

function openIdleSocket() {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({
      host: target.hostname,
      port: Number(target.port || 80),
    });
    const timeout = setTimeout(() => {
      socket.destroy();
      reject(new Error('idle socket connect timeout'));
    }, 3000);
    socket.once('connect', () => {
      clearTimeout(timeout);
      socket.setNoDelay(true);
      resolve(socket);
    });
    socket.once('error', reject);
  });
}

const sockets = [];
try {
  sockets.push(...await Promise.all(Array.from({ length: slotCount }, openIdleSocket)));
  const started = performance.now();
  const response = await fetch(`${target.origin}/health`, {
    cache: 'no-store',
    signal: AbortSignal.timeout(5000),
  });
  const body = await response.json();
  const elapsedMs = performance.now() - started;
  assert.equal(response.status, 200);
  assert.equal(body.ok, true);
  assert.ok(elapsedMs < 5000);
  console.log(JSON.stringify({ idleSockets: sockets.length, elapsedMs, http: body.http }, null, 2));
  console.log('GRIDOPOLY_HTTP_IDLE_FAULT_PASS');
} finally {
  for (const socket of sockets) socket.destroy();
}
