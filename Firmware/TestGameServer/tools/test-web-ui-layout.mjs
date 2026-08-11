import assert from 'node:assert/strict';
import fs from 'node:fs';
import { gunzipSync } from 'node:zlib';

const source = fs.readFileSync(new URL('../src/WebUi.h', import.meta.url), 'utf8');
const match = source.match(/function boardPosition\(index,count\)\{[\s\S]*?\n\}/);
assert.ok(match, 'boardPosition() was not found in WebUi.h');

const pngReferences = [...new Set(Array.from(
  source.matchAll(/['"]([a-z0-9-]+\.png)['"]/g),
  value => value[1],
))];
assert.equal(pngReferences.length, 36,
  'web UI must reference all 36 canonical PNG tile assets');
for (const [tileId, assetName] of [
  ['CORNER-START', 'corner-central-launch.png'],
  ['CORNER-HOLD', 'corner-civic-hold.png'],
  ['CORNER-REST', 'corner-free-plaza.png'],
  ['CORNER-GOTO', 'corner-hold-order.png'],
]) {
  assert.ok(source.includes(`if(id==='${tileId}')return[`),
    `${tileId} visual mapping is missing`);
  assert.ok(source.includes(`'${assetName}'`),
    `${tileId} must render ${assetName}`);
}

const boardPosition = Function(`${match[0]}; return boardPosition;`)();

assert.ok(!source.includes('setInterval(refresh'), 'refresh polling must not use an overlapping interval');
assert.ok(source.includes('refreshInFlight'), 'refresh polling must have a single-flight guard');
assert.ok(source.includes('?since=${state.version}&peers=${state.espnowPeers}&room=${state.roomId}&network=${state.network}'),
          'refresh polling must use state, peer, room, and network-aware conditional requests');
assert.ok(source.includes('/api/sync${query}'), 'refresh polling must use the compact sync projection');
assert.ok(source.includes('/api/board?room=${next.roomId}'), 'static board metadata must use its own endpoint');
assert.ok(source.includes('fullSyncDueAt=Date.now()+30000'), 'browser must periodically request a full compact projection');
assert.ok(source.includes('function projectState('), 'compact assets and events must be projected for rendering');
assert.ok(source.includes('AbortController'), 'HTTP requests must have cancellation and timeout support');
assert.ok(source.includes('&expected=${state.version}'), 'actions must reject stale browser state');
assert.ok(source.includes('/api/settings'), 'settings UI must use the dedicated settings endpoint');
assert.ok(source.includes('botIntervalMs'), 'settings UI must submit the bot interval field');
assert.ok(source.includes('/api/forced-roll'), 'settings UI must use the dedicated forced-roll endpoint');
assert.ok(source.includes('&control=${state.controlVersion||0}'),
  'conditional sync must include the independent web-control revision');
assert.ok(source.includes('&identity=${state.identity?state.identity.revision:0}'),
  'conditional sync must include the independent identity projection revision');
assert.ok(source.includes('&humans=${$(\'#humans\').value}&bots=${$(\'#bots\').value}'),
  'new game requests must freeze separate human and bot seat counts');
assert.ok(source.includes('humans+bots>=2&&humans+bots<=6'),
  'new game controls must enforce the authoritative 2–6 total player limit');
assert.ok(source.includes('<option value="0">0 个机器人</option>'),
  'web setup must allow an all-human room');
assert.ok(source.includes('serverEpochOffsetMs=Number(next.identity.serverEpochMs)-Date.now()'),
  'identity countdown must follow the authority epoch rather than a local five-second timer');
assert.ok(source.includes('function renderIdentityWorkflow('),
  'web UI must render Avatar/Name/Ready and the shared countdown lifecycle');
assert.ok(source.includes('player.avatarUrl?'),
  'ready roster must render only authority-published final avatar URLs');
assert.ok(source.includes('id="player-context-menu"'),
  'player right-click actions must use a dedicated accessible context menu');
assert.ok(source.includes('.context-menu button[hidden]{display:none}'),
  'context-menu button styling must not override the hidden attribute');
assert.ok(source.includes("addEventListener('contextmenu'"),
  'player cards must open destination controls from a right-click gesture');
assert.ok(source.includes('function beginForcedRollSelection('),
  'forced-roll mode must start from the selected player');
assert.ok(source.includes('function renderForcedRollSelection('),
  'legal board cells must be projected as clickable destination candidates');
assert.ok(source.includes('data-player-id="${player.id}"'),
  'rendered player cards must retain their authoritative player id');
assert.ok(source.includes("classList.contains('forced-candidate')"),
  'board clicks must ignore cells outside the legal 2..12 destination set');
assert.ok(!source.includes('id="forced-player"') && !source.includes('id="forced-target"'),
  'forced-roll selection must no longer use settings-modal dropdowns');

const helperBlock = source.match(
  /\/\/ TESTABLE_FORCED_ROLL_HELPERS_BEGIN([\s\S]*?)\/\/ TESTABLE_FORCED_ROLL_HELPERS_END/,
);
assert.ok(helperBlock, 'forced-roll and movement helper block was not found');
const helpers = Function(
  `${helperBlock[1]}; return {forcedRollTargets, confirmedMoveAnimations, nextTokenPosition, playerColor, ownerBadgeModel};`,
)();
const ownerPlayers = [
  {id: 1, name: 'Player Console'},
  {id: 2, name: 'Bot 1'},
  {id: 3, name: 'Bot 2'},
];
assert.equal(helpers.ownerBadgeModel({asset: 0, owner: 0}, ownerPlayers), null,
  'unowned assets do not render an owner badge');
assert.equal(helpers.ownerBadgeModel({asset: 255, owner: 2}, ownerPlayers), null,
  'non-asset tiles never render an owner badge');
assert.deepEqual(
  helpers.ownerBadgeModel({asset: 3, owner: 2, mortgaged: true}, ownerPlayers),
  {id: 2, label: 'P2', name: 'Bot 1', color: '#ff8a72'},
  'mortgaged assets retain the visible owner and full roster identity',
);
assert.deepEqual(
  helpers.ownerBadgeModel({asset: 4, owner: 6}, ownerPlayers),
  {id: 6, label: 'P6', name: 'P6', color: '#ff7f9f'},
  'missing roster entries fall back to the compact player identity',
);
for (const count of [16, 24, 32, 40]) {
  const tiles = Array.from({ length: count }, (_, index) => ({ i: index, id: `T${index}` }));
  const targets = helpers.forcedRollTargets(
    { id: 1, position: count - 3, doubles: 0 }, tiles, count,
  );
  assert.equal(targets.length, 11, `${count}: exactly totals 2..12 are offered`);
  assert.deepEqual(
    targets.map(target => [target.target, target.steps]),
    Array.from({ length: 11 }, (_, offset) => [((count - 3) + offset + 2) % count, offset + 2]),
    `${count}: targets wrap clockwise without changing their literal dice distance`,
  );
}
const doubleLimited = helpers.forcedRollTargets(
  { id: 1, position: 0, doubles: 2 },
  Array.from({ length: 16 }, (_, index) => ({ i: index, id: `T${index}` })),
  16,
);
assert.deepEqual(doubleLimited.map(target => target.steps), [3, 4, 5, 6, 7, 8, 9, 10, 11],
  'third-double risk removes the unavoidable double totals 2 and 12');

const previousMove = {
  roomId: 4, phase: 2, board: { id: 'grid-city-16-v1', size: 16 },
  players: [{ id: 1, position: 14, bankrupt: false }, { id: 2, position: 5, bankrupt: false }],
};
const confirmedMove = {
  roomId: 4, phase: 3, board: { id: 'grid-city-16-v1', size: 16 },
  players: [{ id: 1, position: 3, bankrupt: false }, { id: 2, position: 5, bankrupt: false }],
};
assert.deepEqual(helpers.confirmedMoveAnimations(previousMove, confirmedMove), [
  { playerId: 1, from: 14, target: 3, boardSize: 16, roomId: 4 },
], 'AwaitMoveConfirm completion creates one wraparound token animation');
assert.deepEqual(helpers.confirmedMoveAnimations(
  { ...previousMove, phase: 1 }, confirmedMove,
), [], 'non-confirmation teleports never animate');
assert.deepEqual(helpers.confirmedMoveAnimations(previousMove, {
  ...confirmedMove, roomId: 5,
}), [], 'room changes never animate stale positions');
const animationPositions = [];
let animationPosition = 14;
for (let step = 0; step < 5; step += 1) {
  animationPosition = helpers.nextTokenPosition(animationPosition, 16);
  animationPositions.push(animationPosition);
}
assert.deepEqual(animationPositions, [15, 0, 1, 2, 3],
  'token animation advances exactly one clockwise tile per frame across Start');

const script = source.match(/<script>([\s\S]*?)<\/script>/);
assert.ok(script, 'inline web application script was not found');
assert.doesNotThrow(() => new Function(script[1]), 'inline web application JavaScript must parse');

const html = source.match(/R"GRIDOPOLY_HTML\(([\s\S]*?)\)GRIDOPOLY_HTML";/);
assert.ok(html, 'raw HTML literal was not found');
const generated = fs.readFileSync(new URL('../src/WebUiGzip.h', import.meta.url), 'utf8');
const bytes = Array.from(generated.matchAll(/0x([0-9a-f]{2})/g), match => Number.parseInt(match[1], 16));
assert.ok(bytes.length > 0, 'generated gzip web asset is empty');
assert.equal(gunzipSync(Buffer.from(bytes)).toString('utf8'), html[1],
  'generated gzip web asset is stale; run generate-web-assets.mjs');
assert.match(generated, /kWebUiEtag\[\] = "\\"gridopoly-ui-[0-9a-f]{12}\\"";/,
  'generated web asset must include a content-derived ETag');
assert.match(generated, /kWebUiEtagToken\[\] = "gridopoly-ui-[0-9a-f]{12}";/,
  'generated web asset must include an unquoted ETag comparison token');

for (const count of [16, 24, 32, 40]) {
  const side = count / 4;
  const positions = Array.from({ length: count }, (_, index) => boardPosition(index, count));

  assert.deepEqual(positions[0], [side + 1, side + 1], `${count}: start corner`);
  assert.deepEqual(positions[side], [side + 1, 1], `${count}: bottom-left corner`);
  assert.deepEqual(positions[2 * side], [1, 1], `${count}: top-left corner`);
  assert.deepEqual(positions[3 * side], [1, side + 1], `${count}: top-right corner`);

  for (const [row, column] of positions) {
    assert.ok(row >= 1 && row <= side + 1, `${count}: row ${row} is outside the board`);
    assert.ok(column >= 1 && column <= side + 1, `${count}: column ${column} is outside the board`);
    assert.ok(
      row === 1 || row === side + 1 || column === 1 || column === side + 1,
      `${count}: tile at ${row},${column} is not on the perimeter`,
    );
  }

  assert.equal(new Set(positions.map(position => position.join(','))).size, count, `${count}: duplicate tile position`);
}

console.log('GRIDOPOLY_WEB_UI_LAYOUT_TESTS_PASS');
