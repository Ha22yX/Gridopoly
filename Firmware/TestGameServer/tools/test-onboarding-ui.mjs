import assert from 'node:assert/strict';
import fs from 'node:fs';

const source=fs.readFileSync(new URL('../src/WebUi.h',import.meta.url),'utf8');
const block=source.match(/\/\/ TESTABLE_ONBOARDING_HELPERS_BEGIN([\s\S]*?)\/\/ TESTABLE_ONBOARDING_HELPERS_END/);
assert.ok(block);
const i18n=source.match(/\/\/ TESTABLE_I18N_BEGIN([\s\S]*?)\/\/ TESTABLE_I18N_END/)[1];
const helpers=Function(`${i18n}\n${block[1]};return {normalizePlayerDevices,newGameRoster,pairingView,gameResult};`)();
const screens=Array.from({length:4},(_,index)=>({deviceId:index+1,online:true,eligible:true}));
for(let humans=1;humans<=4;humans+=1){
  const model=helpers.newGameRoster(screens.slice(0,humans),true);
  assert.equal(model.valid,true);
  assert.equal(model.humans,humans);
  assert.equal(model.bots,4-humans);
}
assert.equal(helpers.newGameRoster([],true).valid,false,'no silently created all-bot game');
assert.equal(helpers.newGameRoster(screens,false).valid,false,'stale device cache cannot enable new game');
assert.equal(helpers.newGameRoster([...screens,{deviceId:5,online:true,eligible:true}],true).valid,false,'excess screens require explicit removal');
assert.equal(helpers.newGameRoster([screens[0],screens[0]],true).valid,false,'duplicate identities are surfaced');
assert.equal(helpers.newGameRoster([{deviceId:1,online:false},screens[1]],true).humans,1);
const normalized=helpers.normalizePlayerDevices({devices:[null,{},
  {deviceId:123,displayName:'screen',online:true,seatId:2,capabilities:1},
  {deviceId:456,online:'true'}]});
assert.equal(normalized.length,2);
assert.equal(normalized[0].deviceId,'123');
assert.equal(normalized[0].seatId,2);
assert.equal(normalized[1].online,false);
assert.equal(helpers.newGameRoster([{deviceId:1,online:true,eligible:false}],true).valid,false,'unsupported old firmware does not create an unfinishable room');
const pairing={enabled:true,roomId:7,state:'awaiting_tag',activePlayerId:2,
  requiredHumanMask:3,boundMask:1,moduleId:'tile-a',readerMapIndex:8,canRetry:true};
assert.equal(helpers.pairingView(pairing,8),null,'previous room pairing is never projected');
assert.equal(helpers.pairingView({...pairing,enabled:false},7),null);
const view=helpers.pairingView(pairing,7);
assert.equal(view.required,2);
assert.equal(view.bound,1);
assert.equal(view.activePlayerId,2);
assert.equal(view.readerMapIndex,8);
assert.match(helpers.pairingView({...pairing,state:'already_bound'},7).message,/已属于其他玩家/);
assert.match(helpers.pairingView({...pairing,state:'success_remove'},7).message,/拿回/);
assert.match(helpers.pairingView({...pairing,state:'multiple_tags'},7).message,/只放入一个/);
assert.equal(helpers.pairingView({...pairing,boundMask:255},7).bound,2,'bots and unrelated bits never inflate pairing progress');
assert.equal(helpers.pairingView({...pairing,state:'complete'},7).complete,true);
assert.equal(helpers.gameResult({phase:6,winnerPlayerId:2,players:[]}),null);
assert.equal(helpers.gameResult({phase:7,winnerPlayerId:2,players:[{id:2,name:'Robin'}]}),'Robin 获胜');
assert.equal(helpers.gameResult({phase:7,winnerPlayerId:0,players:[]}),'本局已结束');
console.log('Onboarding UI roster regression PASS');
