#!/usr/bin/env node

import fs from 'node:fs';
import http from 'node:http';

const port = Number.parseInt(process.argv[2] ?? '18765', 10);
if (!Number.isInteger(port) || port < 1024 || port > 65535) {
  throw new Error(`invalid port: ${process.argv[2]}`);
}

const source = fs.readFileSync(
  new URL('../../../Firmware/TestGameServer/src/WebUi.h', import.meta.url), 'utf8');
const match = source.match(/R"GRIDOPOLY_HTML\(([\s\S]*?)\)GRIDOPOLY_HTML";/);
if (!match) throw new Error('WebUi.h raw HTML literal was not found');
const productionHtml = match[1].replaceAll(
  '/assets/tiles/', 'http://192.168.3.31/assets/tiles/');

function fixture(size) {
  const side = size / 4;
  const tiles = Array.from({length: size}, (_, index) => {
    if (index === 0) return ['CORNER-START', 0, 255, 0];
    if (index === side) return ['CORNER-HOLD', 7, 255, 0];
    if (index === 2 * side) return ['CORNER-REST', 8, 255, 0];
    if (index === 3 * side) return ['CORNER-GOTO', 9, 255, 0];
    if (index === 1) return ['FEE-CITY', 6, 255, 0];
    if (index === size - 2) return ['FEE-DENSITY', 6, 255, 0];
    if (index === 2) return ['A1', 1, 0, 100];
    if (index === 3) return ['A2', 1, 1, 120];
    if (index === side + 1) return ['B1', 1, 2, 140];
    if (index === size - 3) return ['B2', 1, 3, 160];
    return ['CARD-CE-1', 4, 255, 0];
  });
  const state = {
    schema: 2,
    version: 1,
    roomId: 900000 + size,
    network: 1,
    phase: 1,
    round: 1,
    activePlayer: 1,
    decisionPlayer: 1,
    actions: 0,
    espnowPeers: 1,
    wifi: {connected: true, ip: '10.42.0.1'},
    board: {id: `grid-city-${size}-browser-test`, size},
    debt: {active: false},
    auction: {active: false},
    card: {active: false},
    players: [
      {id: 1, name: 'Player Console', controller: 'TEST', connected: true,
        cash: 1000, position: 0, held: false, bankrupt: false},
      {id: 2, name: 'Bot 1', controller: 'BOT', connected: true,
        cash: 1000, position: 3, held: false, bankrupt: false},
      {id: 3, name: 'Bot 2', controller: 'BOT', connected: true,
        cash: 1000, position: size - 3, held: false, bankrupt: false},
    ],
    assets: [[0, 0, 0], [1, 0, 0], [2, 2, 0], [3, 0, 1]],
    events: [],
  };
  const board = {roomId: state.roomId, board: state.board, tiles};
  const injection = `boardDefinition=${JSON.stringify(board)};` +
    `window.__gridMatrixState=${JSON.stringify(state)};` +
    `window.__gridMatrixRerender=owner=>{window.__gridMatrixState.assets[1][0]=owner;` +
    `window.__gridMatrixState.version+=1;render(projectState(structuredClone(window.__gridMatrixState),boardDefinition));};` +
    `render(projectState(structuredClone(window.__gridMatrixState),boardDefinition));` +
    `const ownerChange=document.createElement('button');ownerChange.id='matrix-owner-change';` +
    `ownerChange.type='button';ownerChange.textContent='TEST OWNERSHIP UPDATE';` +
    `ownerChange.addEventListener('click',()=>window.__gridMatrixRerender(2));` +
    `document.querySelector('.center div').appendChild(ownerChange);` +
    'window.__gridMatrixReady=true;';
  return productionHtml.replace(/\nscheduleRefresh\(0\);\n<\/script>/,
    `\n${injection}\n</script>`);
}

function forcedRollFixture() {
  const tiles = [
    ['CORNER-START', 0, 255, 0], ['A1', 1, 0, 100], ['CARD-CF-1', 5, 255, 0],
    ['A2', 1, 1, 120], ['CORNER-HOLD', 7, 255, 0], ['B1', 1, 2, 140],
    ['T-WEST', 2, 3, 160], ['B2', 1, 4, 180], ['CORNER-REST', 8, 255, 0],
    ['B3', 1, 5, 200], ['U-ENERGY', 3, 6, 160], ['C1', 1, 7, 220],
    ['CORNER-GOTO', 9, 255, 0], ['C2', 1, 8, 240], ['FEE-CITY', 6, 255, 0],
    ['C3', 1, 9, 260],
  ];
  const state = {
    schema: 2, version: 44, roomId: 9916, network: 1, controlVersion: 1,
    forcedRoll: {active: false, player: 0, target: 255, steps: 0, origin: 255},
    phase: 5, round: 3, activePlayer: 1, decisionPlayer: 1, actions: 0,
    espnowPeers: 1, wifi: {connected: true, ip: '10.42.0.1'},
    board: {id: 'grid-city-16-forced-roll-browser-test', size: 16},
    debt: {active: true, debtor: 1, creditor: 4, asset: 1, amount: 30},
    auction: {active: false}, card: {active: false},
    players: [
      {id: 1, name: 'Player Console', controller: 'WIFI-UDP', connected: true,
        cash: 478, position: 3, doubles: 1, held: false, bankrupt: false},
      {id: 2, name: 'Bot 1', controller: 'BOT', connected: true,
        cash: 482, position: 5, doubles: 0, held: false, bankrupt: false},
      {id: 3, name: 'Bot 2', controller: 'BOT', connected: true,
        cash: 751, position: 14, doubles: 2, held: false, bankrupt: false},
      {id: 4, name: 'Bot 3', controller: 'BOT', connected: true,
        cash: 259, position: 4, doubles: 0, held: true, bankrupt: false},
    ],
    assets: Array.from({length: 10}, () => [0, 0, 0]), events: [],
  };
  const board = {roomId: state.roomId, board: state.board, tiles};
  const injection = `boardDefinition=${JSON.stringify(board)};` +
    `window.__forcedMockState=${JSON.stringify(state)};window.__forcedRequests=[];` +
    `window.fetch=async(path,options={})=>{const url=new URL(path,location.href);` +
    `if(url.pathname==='/api/sync')return new Response(JSON.stringify(window.__forcedMockState),{status:200,headers:{'content-type':'application/json'}});` +
    `if(url.pathname==='/api/settings')return new Response(JSON.stringify({botActionIntervalMs:1200,minimumMs:100,maximumMs:10000}),{status:200,headers:{'content-type':'application/json'}});` +
    `if(url.pathname==='/api/forced-roll'){window.__forcedRequests.push(url.search);` +
    `if(url.searchParams.get('cancel')==='1'){window.__forcedMockState.forcedRoll={active:false,player:0,target:255,steps:0,origin:255};}` +
    `else{const player=Number(url.searchParams.get('player'));const target=Number(url.searchParams.get('target'));const owner=window.__forcedMockState.players.find(item=>item.id===player);const steps=(target+16-owner.position)%16;window.__forcedMockState.forcedRoll={active:true,player,target,steps,origin:owner.position};}` +
    `window.__forcedMockState.controlVersion+=1;return new Response(JSON.stringify({ok:true,version:window.__forcedMockState.version,controlVersion:window.__forcedMockState.controlVersion,forcedRoll:window.__forcedMockState.forcedRoll}),{status:200,headers:{'content-type':'application/json'}});}` +
    `throw new Error('unexpected mock request '+url.pathname);};` +
    `render(projectState(structuredClone(window.__forcedMockState),boardDefinition));window.__gridMatrixReady=true;`;
  return productionHtml.replace(/\nscheduleRefresh\(0\);\n<\/script>/,
    `\n${injection}\n</script>`);
}

const server = http.createServer((request, response) => {
  if (request.url === '/forced') {
    const body = forcedRollFixture();
    response.writeHead(200, {
      'content-type': 'text/html; charset=utf-8',
      'cache-control': 'no-store',
      'content-length': Buffer.byteLength(body),
    });
    response.end(body);
    return;
  }
  const size = Number.parseInt((request.url ?? '').replace(/^\//, ''), 10);
  if (![16, 24, 32, 40].includes(size)) {
    response.writeHead(404, {'content-type': 'text/plain; charset=utf-8'});
    response.end('not found');
    return;
  }
  const body = fixture(size);
  response.writeHead(200, {
    'content-type': 'text/html; charset=utf-8',
    'cache-control': 'no-store',
    'content-length': Buffer.byteLength(body),
  });
  response.end(body);
});

server.listen(port, '127.0.0.1', () => {
  console.log(`GRIDOPOLY_WEB_BOARD_MATRIX_READY http://127.0.0.1:${port}`);
});
