import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {createRequire} from 'node:module';

// Run with Playwright available through NODE_PATH and an optional
// PLAYWRIGHT_EXECUTABLE_PATH. All requests stay on an ephemeral loopback server.
const {chromium} = createRequire(import.meta.url)('playwright');
const output = path.resolve(process.argv[2] || 'web-debug-evidence');
fs.mkdirSync(output, {recursive:true});
const source = fs.readFileSync(new URL('../src/WebUi.h', import.meta.url), 'utf8');
const html = source.match(/R"GRIDOPOLY_HTML\(([\s\S]*?)\)GRIDOPOLY_HTML";/)[1];
const tiles = [
  ['CORNER-START',0,255,0],['A1',1,0,100],['CARD-CF-1',5,255,0],['A2',1,1,120],
  ['CORNER-HOLD',7,255,0],['B1',1,2,140],['T-WEST',2,3,160],['B2',1,4,180],
  ['CORNER-REST',8,255,0],['B3',1,5,200],['U-ENERGY',3,6,160],['C1',1,7,220],
  ['CORNER-GOTO',9,255,0],['C2',1,8,240],['FEE-CITY',6,255,0],['C3',1,9,260],
];
const state = {schema:2,roomId:9916,version:44,network:1,controlVersion:1,
  phase:1,round:3,activePlayer:1,decisionPlayer:1,actions:0,espnowPeers:0,
  wifi:{connected:true,ip:'127.0.0.1'},board:{id:'grid-city-16-debug-test',size:16},
  forcedRoll:{active:true,player:1,target:3,origin:0,steps:3},debt:{active:false},auction:{active:false},card:{active:false},
  players:[{id:1,name:'Alice',controller:'WEB',cash:1500,position:0,held:true,bankrupt:false},
    {id:2,name:'Bob',controller:'WEB',cash:1500,position:3,bankrupt:false}],
  assets:Array.from({length:10},()=>[0,0,0]),events:[]};
const edits=[];
let rejectNext=false;
const server=http.createServer(async(req,res)=>{
  const url=new URL(req.url,'http://127.0.0.1');
  const json=(body,status=200)=>{res.writeHead(status,{'content-type':'application/json','cache-control':'no-store'});res.end(JSON.stringify(body));};
  if(url.pathname==='/'){res.writeHead(200,{'content-type':'text/html; charset=utf-8'});res.end(html);return;}
  if(url.pathname==='/api/sync'){json(state);return;}
  if(url.pathname==='/api/board'){json({roomId:state.roomId,board:state.board,tiles});return;}
  if(url.pathname==='/api/player-devices'){json({devices:[],automaticPlayers:false});return;}
  if(url.pathname==='/api/tile-debug/assignments'){
    json({roomId:state.roomId,boardId:state.board.id,boardSize:16,modules:[{moduleId:'reader-a',deviceId:'device-a',online:true,leaseRemainingMs:12000}],assignments:[],
      tiles:tiles.map((t,i)=>({tileId:t[0],mapIndex:i,displayName:t[0],kind:t[1]===1?'Property':'Other',purchasePrice:t[3]}))});return;
  }
  if(url.pathname.startsWith('/api/debug/')){
    edits.push({path:url.pathname,query:Object.fromEntries(url.searchParams),method:req.method});
    if(rejectNext){rejectNext=false;json({ok:false,message:'persist_failed'},500);return;}
    if(Number(url.searchParams.get('roomId'))!==state.roomId||Number(url.searchParams.get('expectedVersion'))!==state.version){
      json({ok:false,message:'version_changed'},409);return;
    }
    if(url.pathname.endsWith('player-cash'))state.players[Number(url.searchParams.get('playerId'))-1].cash=Number(url.searchParams.get('cash'));
    else state.assets[tiles.find(t=>t[0]===url.searchParams.get('tileId'))[2]][0]=Number(url.searchParams.get('ownerId'));
    ++state.version;
    await new Promise(resolve=>setTimeout(resolve,250));
    json({ok:true,changed:true,roomId:state.roomId,version:state.version});return;
  }
  if(url.pathname.startsWith('/assets/tiles/')){
    const file=new URL('../../../Assets/GridCity/StreetV3/device/'+path.basename(url.pathname),import.meta.url);
    if(fs.existsSync(file)){res.writeHead(200,{'content-type':'image/png'});res.end(fs.readFileSync(file));return;}
  }
  res.writeHead(404);res.end();
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({headless:true,...(process.env.PLAYWRIGHT_EXECUTABLE_PATH?
  {executablePath:process.env.PLAYWRIGHT_EXECUTABLE_PATH}:{})});
const errors=[];
const page=await browser.newPage({viewport:{width:1440,height:1050},deviceScaleFactor:1});
page.on('pageerror',error=>errors.push(error.message));
const screenshot=async name=>page.screenshot({path:path.join(output,name+'.png')});
const openCash=async()=>{
  await page.locator('.player[data-player-id="1"]').click({button:'right'});
  await page.locator('#player-context-menu #debug-cash').waitFor({state:'visible'});
};
const openOwner=async()=>{
  await page.locator('.tile[data-tile-id="A1"]').click({button:'right'});
  await page.locator('#tile-debug-context-menu #debug-owner').waitFor({state:'visible'});
};
const nextSync=()=>page.waitForResponse(response=>new URL(response.url()).pathname==='/api/sync');
const saved=kind=>page.waitForFunction(kind=>document.querySelector(`#debug-${kind}-status`).textContent.includes('已从服务器回读'),kind);
const boundsInside=async(selector,width,height)=>{
  const bounds=await page.locator(selector).boundingBox();
  assert(bounds.x>=0&&bounds.y>=0&&bounds.x+bounds.width<=width&&bounds.y+bounds.height<=height);
  assert(await page.locator(selector).evaluate(element=>element.scrollWidth<=element.clientWidth));
};
try{
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  await page.locator('.tile[data-tile-id="A1"]').waitFor();
  assert.equal(await page.locator('#debug-edit-modal').count(),0,'no secondary editor dialog');
  assert.equal(await page.locator('#player-debug-cash,#tile-debug-owner').count(),0,'no launcher menu items');
  await openOwner();
  assert.equal(edits.length,0,'opening the inline editor never mutates');
  assert.equal(await page.locator('#tile-debug-module').isVisible(),true,'hardware section remains in the same menu');
  await page.locator('#debug-owner').selectOption('2');
  await page.locator('#debug-owner').focus();
  await page.evaluate(()=>{window.__ownerInput=document.querySelector('#debug-owner');});
  await nextSync();await nextSync();
  assert(await page.evaluate(()=>window.__ownerInput===document.querySelector('#debug-owner')&&document.activeElement===window.__ownerInput));
  assert.equal(await page.locator('#debug-owner').inputValue(),'2');
  assert.match(await page.locator('#debug-owner-preview').textContent(),/当前：银行.*待保存：P2/);
  await screenshot('desktop-owner-inline');
  await page.keyboard.press('Escape');
  assert.equal(edits.length,0,'closing discards an unsubmitted owner draft');
  await openOwner();
  assert.equal(await page.locator('#debug-owner').inputValue(),'0');
  await page.keyboard.press('Escape');
  await openCash();
  assert.equal(await page.locator('#player-force-destination').isDisabled(),true,'held player cannot schedule movement');
  assert.equal(await page.locator('#player-clear-destination').isVisible(),true);
  await page.locator('#debug-cash').fill('5432');
  await page.locator('#debug-cash').focus();
  await page.evaluate(()=>{window.__cashInput=document.querySelector('#debug-cash');});
  await nextSync();await nextSync();
  assert(await page.evaluate(()=>window.__cashInput===document.querySelector('#debug-cash')&&document.activeElement===window.__cashInput));
  assert.equal(await page.locator('#debug-cash').inputValue(),'5432');
  await screenshot('desktop-cash-inline');
  for(const invalid of ['','-1','1000001','1.5']){
    await page.locator('#debug-cash').fill(invalid);
    assert.equal(await page.locator('#debug-cash-save').isDisabled(),true,invalid);
  }
  await page.locator('#debug-cash').fill('5432');
  await page.locator('#debug-cash-save').evaluate(button=>{button.click();button.click();});
  assert.equal(await page.locator('#debug-cash-save').isDisabled(),true,'save disables reentry');
  await saved('cash');
  assert.equal(edits.length,1);
  assert.deepEqual(edits[0],{path:'/api/debug/player-cash',query:{roomId:'9916',expectedVersion:'44',playerId:'1',cash:'5432'},method:'POST'});
  assert.equal(state.players[0].cash,5432);
  assert.equal(await page.locator('#player-context-menu').isVisible(),true,'success stays in place');
  assert.equal(await page.locator('#player-force-destination').isDisabled(),true,'setBusy(false) must restore held restriction');
  state.forcedRoll.active=false;++state.controlVersion;
  await nextSync();
  await page.waitForFunction(()=>document.querySelector('#player-clear-destination').hidden);
  assert.equal(await page.locator('#debug-cash').inputValue(),'5432');
  assert.match(await page.locator('#debug-cash-preview').textContent(),/当前：¥5,432/);
  await screenshot('desktop-cash-saved');
  await page.keyboard.press('Escape');
  await openOwner();
  await page.locator('#debug-owner').selectOption('2');
  await page.locator('#debug-owner-save').click();
  await saved('owner');
  assert.equal(state.assets[0][0],2);
  assert.equal(edits[1].path,'/api/debug/asset-owner');
  assert.equal(edits[1].query.tileId,'A1');
  assert.equal(await page.locator('#tile-debug-context-menu').isVisible(),true);
  assert.match(await page.locator('#debug-owner-preview').textContent(),/当前：P2/);
  await page.keyboard.press('Escape');
  await page.locator('.tile[data-tile-id="CORNER-START"]').click({button:'right'});
  assert.equal(await page.locator('#debug-owner-editor').isVisible(),false,'nontradable corner hides owner editing');
  assert.equal(await page.locator('#tile-debug-module').isVisible(),true);
  await page.keyboard.press('Escape');
  await openCash();
  await page.locator('#debug-cash').fill('9876');
  ++state.version;state.players[0].cash=6000;
  await page.waitForFunction(()=>document.querySelector('#debug-cash-save').disabled);
  assert.equal(await page.locator('#debug-cash').inputValue(),'9876','stale poll preserves draft');
  assert.match(await page.locator('#debug-cash-status').textContent(),/状态已变化/);
  assert.match(await page.locator('#debug-cash-preview').textContent(),/当前：¥6,000.*待保存：¥9,876/);
  await screenshot('desktop-cash-stale');
  await page.locator('#debug-cash-reset').click();
  assert.equal(await page.locator('#debug-cash').inputValue(),'6000');
  assert.equal(await page.locator('#debug-cash-save').isDisabled(),false);
  assert.equal(edits.length,2,'explicit reset never posts');
  ++state.roomId;state.players[0].name='Carol';
  await page.waitForFunction(()=>document.querySelector('#debug-cash-save').disabled);
  assert.match(await page.locator('#player-context-title').textContent(),/Alice/,'old room is not silently relabelled');
  assert.equal(await page.locator('#player-assign-tag').isDisabled(),true,'old context cannot issue actions in new room');
  await page.locator('#debug-cash-reset').click();
  assert.match(await page.locator('#player-context-title').textContent(),/Carol/,'explicit reload updates identity label');
  assert.equal(await page.locator('#player-assign-tag').isDisabled(),false);
  assert(await page.evaluate(()=>contextPlayerRoomId===state.roomId));
  await page.keyboard.press('Escape');
  await openOwner();
  await page.locator('#debug-owner').selectOption('1');
  ++state.roomId;state.board.id='grid-city-16-debug-next';
  [tiles[1],tiles[3]]=[tiles[3],tiles[1]];
  await page.waitForFunction(()=>document.querySelector('#debug-owner-save').disabled);
  assert.equal(await page.locator('#tile-debug-context-menu').isVisible(),true,'new room displays an inline stale warning');
  assert.equal(await page.locator('#debug-owner').inputValue(),'1');
  assert.equal(await page.locator('#tile-debug-apply').isDisabled(),true,'stale menu cannot assign hardware');
  assert.equal(await page.locator('#debug-owner-reset').isDisabled(),true,'cross-room tile mapping must be reopened');
  assert.match(await page.locator('#debug-owner-status').textContent(),/重新右键/);
  await page.keyboard.press('Escape');
  await openOwner();
  assert.equal(await page.locator('#debug-owner').inputValue(),'2');
  assert.match(await page.locator('#tile-debug-context-title').textContent(),/^03/,'reopening reloads canonical tile location');
  await page.keyboard.press('Escape');
  await openCash();rejectNext=true;
  await page.locator('#debug-cash').fill('88');
  await page.locator('#debug-cash-save').click();
  await page.waitForFunction(()=>document.querySelector('#debug-cash-status').textContent.includes('原值已保留'));
  assert.equal(state.players[0].cash,6000);
  assert.equal(await page.locator('#debug-cash').inputValue(),'88');
  assert.equal(await page.locator('#player-context-menu').isVisible(),true);
  await screenshot('desktop-cash-error');
  await page.locator('h1').click();
  assert.equal(await page.locator('#player-context-menu').isVisible(),false,'outside click closes without resubmitting');
  assert.equal(edits.length,3);
  await page.setViewportSize({width:390,height:844});
  for(const [kind,open,menu] of [['cash',openCash,'#player-context-menu'],['owner',openOwner,'#tile-debug-context-menu']]){
    await open();
    await boundsInside(menu,390,844);
    if(kind==='cash')await page.locator('#debug-cash').fill('6500');
    else await page.locator('#debug-owner').selectOption('1');
    await page.locator(`#debug-${kind}-save`).scrollIntoViewIfNeeded();
    await screenshot('mobile-'+kind+'-inline');
    await page.setViewportSize({width:390,height:640});
    await boundsInside(menu,390,640);
    assert.equal(await page.locator(`#debug-${kind}`).inputValue(),kind==='cash'?'6500':'1','resize retains draft');
    await page.keyboard.press('Escape');
    await page.setViewportSize({width:390,height:844});
  }
  await openCash();state.players[0].bankrupt=true;++state.version;
  await page.waitForFunction(()=>document.querySelector('#debug-cash-save').disabled);
  await page.evaluate(()=>{setBusy(true);setBusy(false);});
  assert.equal(await page.locator('#player-force-destination').isDisabled(),true,'bankruptcy restriction survives busy reset');
  assert.equal(await page.locator('#debug-cash-save').isDisabled(),true);
  await page.keyboard.press('Escape');
  assert.deepEqual(errors,[]);
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({ok:true,edits,errors,viewports:[[1440,1050],[390,844],[390,640]]},null,2));
  console.log('GRIDOPOLY_WEB_DEBUG_BROWSER_TESTS_PASS inline=true polling_preserves_draft=true');
}finally{
  await browser.close();await new Promise(resolve=>server.close(resolve));
}
