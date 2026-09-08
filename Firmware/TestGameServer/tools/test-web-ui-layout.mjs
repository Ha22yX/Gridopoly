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
assert.ok(source.includes('&tagBindings=${state.tagBindingRevision||0}'),
  'conditional sync must include the independent Tag-binding revision');
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
assert.ok(source.includes("const playerColors=['#58A7EB','#EF7168','#52DCB7','#F2C453','#C28AE8','#EA8A55'];"),
  'all web player and owner badges must use the frozen six-seat palette');
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
assert.ok(source.includes('id="player-assign-tag"') &&
  source.includes('id="player-clear-tag"') &&
  source.includes('id="tag-binding-modal"'),
  'player right-click actions must expose explicit Tag assign and clear controls');
assert.ok(source.includes('/api/tile-tags') &&
  source.includes('/api/player-tag-binding?playerId=${tagBindingPlayerId}&uid=${uid}&expectedRevision=${tagBindingState.bindingRevision}'),
  'Tag assignment must use the detected global catalog and revision-gated mutation endpoint');
assert.ok(source.includes('/api/player-tag-binding?playerId=${playerId}&expectedRevision=${tagBindingState.bindingRevision}') &&
  source.includes("{method:'DELETE',cache:'no-store'}"),
  'Tag removal must use an explicit revision-gated DELETE');
assert.ok(source.includes('player.tagUid?`<span class="tag-mark">TAG ${esc(player.tagUid)}</span>`'),
  'player cards must expose their authoritative Tag binding without a parallel UI state');
assert.ok(source.includes('tag.currentlySeen') && source.includes('tag.sightings'),
  'the Tag chooser must distinguish current sightings and show module/tile provenance');
assert.ok(source.includes("classList.contains('forced-candidate')"),
  'board clicks must ignore cells outside the legal 2..12 destination set');
assert.ok(!source.includes('id="forced-player"') && !source.includes('id="forced-target"'),
  'forced-roll selection must no longer use settings-modal dropdowns');
assert.ok(source.includes('id="tile-debug-title"'),
  'web UI must expose the temporary tile-module assignment panel');
assert.ok(source.includes('id="tile-debug-context-menu"'),
  'tile assignment controls must open in a dedicated board-cell context menu');
assert.ok(source.includes('element.dataset.tileId=tile.id;'),
  'every rendered board cell must retain its canonical server tile id');
assert.ok(source.includes("$('#board').addEventListener('contextmenu'"),
  'right-clicking the board must have a dedicated tile assignment gesture');
assert.ok(source.includes('openTileDebugContextMenu(tile,event.clientX,event.clientY)'),
  'the board context gesture must open the assignment menu at the pointer');
assert.ok(source.includes('function openTileDebugContextMenu(tile,x,y){') &&
  source.includes('function closeTileDebugContextMenu(){'),
  'tile assignment context-menu lifecycle helpers must be explicit');
assert.ok(source.includes('tile.dataset.tileId') &&
  /tileDebugState\.tiles\.find\(candidate=>candidate\.tileId===/.test(source),
  'the selected board tile must resolve its preview from the server-derived catalog');
assert.ok(!source.includes('id="tile-debug-tile"'),
  'the sidebar must not expose a second tile selector after board right-click becomes authoritative');
assert.ok(source.includes('id="tile-debug-revision"') && source.includes('id="tile-debug-updated"'),
  'tile debug panel must display its independent revision and update time');
assert.ok(source.includes('let tileDebugState=normalizeTileDebugData(null);') &&
  !source.includes('state.tileDebug'),
  'temporary module assignment UI state must remain separate from authority game state');
assert.ok(source.includes('/api/tile-debug/assignments'),
  'tile debug panel must load modules, catalog, and authoritative assignment details on demand');
assert.ok(source.includes('/api/tile-debug/assignment?moduleId=${encodeURIComponent(moduleId)}&deviceId=${encodeURIComponent(deviceId)}&tileId=${encodeURIComponent(tileId)}'),
  'tile debug mutation must identify only the online module/device and canonical tile');
assert.ok(!source.includes('&ownerPlayerId=${ownerPlayerId}') &&
  !source.includes('ownerPlayerId=') && !source.includes('id="tile-debug-owner"') &&
  !source.includes('临时所有者') && !source.includes('tileDebugOwnerOptions'),
  'tile debug UI must not expose or submit a forged owner override');
assert.ok(source.includes("'POST',") && source.includes("'DELETE',"),
  'tile debug panel must expose explicit set and clear mutations');
assert.ok(source.includes('仅用于硬件联调，不写入对局或游戏存档。'),
  'tile debug panel must disclose that assignments are isolated from authority state');
assert.ok(source.includes('.tile-debug-form{display:grid;grid-template-columns:1fr;gap:8px}'),
  'the single online-module control must use the full form width');
assert.ok(source.includes('<select id="tile-debug-module">') &&
  !source.includes('id="tile-debug-device"') && !source.includes('id="tile-debug-module-options"') &&
  !source.includes('id="tile-debug-device-options"'),
  'the board-cell menu must use one online-module selector without manual module/device inputs');
assert.ok(source.includes('const onlineModules=tileDebugState.modules.filter(module=>module.online);'),
  'right-click assignment options must be limited to online module leases');
assert.ok(source.includes("const deviceId=module?module.deviceId:'';"),
  'the submitted device id must be derived from the selected online module snapshot');
assert.ok(source.includes("const previousModule=$('#tile-debug-module').value;") &&
  source.includes('const selectedModule=onlineModules.find(module=>module.moduleId===previousModule)||onlineModules[0];'),
  'tile debug refresh must preserve the current legal module selection and otherwise use the first online module');
assert.ok(source.includes('module.leaseRemainingMs') && source.includes('module.source'),
  'the module overview must expose lease lifetime and discovery source');
assert.ok(source.includes('id="tile-debug-context-menu"') &&
  source.includes('id="tile-debug-module"') && source.includes('id="tile-debug-apply"') &&
  source.includes('id="tile-debug-clear"'),
  'the board-cell menu must expose online-module, apply, and clear controls');
assert.ok(source.includes('ownerDisplayName:tileDebugText(assignment.owner_display_name)') &&
  source.includes('ownerRgb:tileDebugCssColor(assignment.owner_color)'),
  'assignment rows must retain server-authoritative owner identity and color fields');
assert.ok(source.includes("if(!event.target.closest('#tile-debug-context-menu'))closeTileDebugContextMenu()"),
  'clicking outside the tile assignment menu must close it');
assert.ok(source.includes("if(!$('#tile-debug-context-menu').hidden)closeTileDebugContextMenu()"),
  'Escape must close the tile assignment context menu');
assert.ok(source.includes("window.addEventListener('resize',closeTileDebugContextMenu)"),
  'resizing the viewport must close the positioned tile assignment menu');
assert.ok(source.includes("$('#board').addEventListener('click',event=>{") &&
  source.includes("classList.contains('forced-candidate')") &&
  source.includes('submitForcedRollTarget(Number(tile.dataset.tileIndex))'),
  'tile assignment right-click must preserve forced-roll destination left-clicks');
assert.ok(source.includes("$('#players').addEventListener('keydown',event=>{") &&
  source.includes("event.key!=='ContextMenu'&&!(event.shiftKey&&event.key==='F10')") &&
  source.includes('beginForcedRollSelection(contextPlayerId)'),
  'tile assignment must preserve the keyboard-accessible forced-roll path');
for (const derivedField of ['displayName', 'kind', 'accentRgb', 'artworkKey', 'purchasePrice']) {
  assert.ok(source.includes(`tile.${derivedField}`),
    `tile debug preview must render server-derived ${derivedField}`);
  assert.ok(!source.includes(`id="tile-debug-${derivedField}"`),
    `tile debug ${derivedField} must not be browser-editable`);
}

const tileDebugHelperBlock = source.match(
  /\/\/ TESTABLE_TILE_DEBUG_HELPERS_BEGIN([\s\S]*?)\/\/ TESTABLE_TILE_DEBUG_HELPERS_END/,
);
assert.ok(tileDebugHelperBlock, 'tile debug projection helper block was not found');
const tileDebugHelpers = Function(
  `${tileDebugHelperBlock[1]}; return {normalizeTileDebugData,tileDebugCssColor,tileDebugSafeIdentifier,tileDebugAssignmentFor,tileDebugUpdatedAtLabel};`,
)();
const tileDebugModel = tileDebugHelpers.normalizeTileDebugData({
  revision: 9,
  updatedAtMs: 1_720_000_000_000,
  modules: [{moduleId: 'module-a', deviceId: 'device-a', assigned: true, online: true,
    lastSeenMs: 1_720_000_000_500, leaseRemainingMs: 12_000, source: 'udp'}],
  tiles: [{tileId: 'A1', mapIndex: 1, displayName: 'Rivet Row', kind: 'Property',
    accentRgb: 0xc97852, artworkKey: 'a1-rivet-row', purchasePrice: 60}],
  players: [{playerId: 2, displayName: 'Bot 1', rgb: '#EF7168'}],
  assignments: [{moduleId: 'module-offline', deviceId: 'device-offline', tile_id: 'A1',
    mapIndex: 1, displayName: 'Rivet Row', kind: 'Property', accent: '#c97852',
    artworkKey: 'a1-rivet-row', purchase_price: 60, owner_player: 2,
    owner_display_name: 'Bot 1', owner_color: '#EF7168', revision: 9,
    updatedAtMs: 1_720_000_000_000}],
});
assert.equal(tileDebugModel.revision, 9, 'tile debug revision is projected independently');
assert.equal(tileDebugModel.modules.length, 2,
  'an assigned but disconnected module remains visible in the assignment overview');
assert.equal(tileDebugModel.modules[1].assigned, true,
  'assignment-only module candidates remain marked as existing assignments');
assert.deepEqual(tileDebugModel.modules[0], {
  moduleId: 'module-a', deviceId: 'device-a', assigned: true, online: true,
  lastSeenMs: 1_720_000_000_500, leaseRemainingMs: 12_000, source: 'udp',
}, 'online module leases preserve discovery timing and source metadata');
assert.equal(tileDebugModel.modules[1].online, false,
  'assignment-only stale modules remain visible in the overview but are not online candidates');
assert.deepEqual(tileDebugModel.tiles[0], {
  tileId: 'A1', mapIndex: 1, displayName: 'Rivet Row', kind: 'Property',
  accentRgb: '#C97852', artworkKey: 'a1-rivet-row', purchasePrice: 60,
}, 'catalog fields remain server-derived and readonly');
assert.deepEqual(tileDebugHelpers.tileDebugAssignmentFor(tileDebugModel, 'module-offline'),
  tileDebugModel.assignments[0], 'module selection resolves its current temporary assignment');
assert.equal(tileDebugModel.assignments[0].ownerPlayerId, 2,
  'snake-case tile firmware DTO owner is normalized for rendering');
assert.equal(Object.hasOwn(tileDebugModel.assignments[0], 'order'), false,
  'temporary assignment DTO must not imply a physical ORDER');
assert.equal(tileDebugHelpers.tileDebugCssColor('javascript:alert(1)'), '#42526A',
  'invalid assignment colors never reach inline CSS');
for (const identifier of ['17', '0xA17', 'tile-01', 'dc:b4:d9:02:d1:dc', 'module_v1.2']) {
  assert.equal(tileDebugHelpers.tileDebugSafeIdentifier(identifier), true,
    `${identifier} must be accepted by the shared safe-id grammar`);
}
for (const identifier of ['', 'space here', '../tile', 'x'.repeat(33)]) {
  assert.equal(tileDebugHelpers.tileDebugSafeIdentifier(identifier), false,
    `${identifier || 'empty id'} must be rejected by the shared safe-id grammar`);
}
assert.equal(tileDebugHelpers.tileDebugUpdatedAtLabel(0), '尚未修改',
  'empty assignment revisions have an explicit timestamp state');

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
  {id: 2, label: 'P2', name: 'Bot 1', color: '#EF7168'},
  'mortgaged assets retain the visible owner and full roster identity',
);
assert.deepEqual(
  helpers.ownerBadgeModel({asset: 4, owner: 6}, ownerPlayers),
  {id: 6, label: 'P6', name: 'P6', color: '#EA8A55'},
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
