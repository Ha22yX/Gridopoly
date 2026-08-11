import dgram from 'node:dgram';

const localAddress = process.argv[2];
const timeoutMs = Number(process.argv[3] || 7000);
const targetAddress = process.argv[4] || '224.0.0.251';
if (!localAddress) {
  throw new Error('usage: node mdns-probe.mjs <local-ip> [timeout-ms] [target-ip]');
}

const socket = dgram.createSocket({type: 'udp4', reuseAddr: true});
const labels = ['gridopoly-test', 'local'];
const qname = labels.flatMap(label => [Buffer.byteLength(label), ...Buffer.from(label)]);
const query = Buffer.from([
  0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
  ...qname, 0,
  0, 1, 0x80, 1,
]);
let packets = 0;

socket.on('message', (message, remote) => {
  packets++;
  console.log(`MDNS_RX from=${remote.address}:${remote.port} bytes=${message.length}`);
});
socket.on('error', error => {
  console.error(`MDNS_ERROR ${error.message}`);
  process.exitCode = 1;
});
socket.bind(5353, '0.0.0.0', () => {
  socket.addMembership('224.0.0.251', localAddress);
  socket.setMulticastInterface(localAddress);
  socket.send(query, 5353, targetAddress);
});
setTimeout(() => {
  console.log(`MDNS_DONE packets=${packets}`);
  socket.close();
}, timeoutMs);
