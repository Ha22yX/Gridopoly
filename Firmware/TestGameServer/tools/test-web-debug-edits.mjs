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
  forcedRoll:{active:false},debt:{active:false},auction:{active:false},card:{active:false},
  players:[{id:1,name:'Alice',controller:'WEB',cash:1500,position:0,bankrupt:false},
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
    json({roomId:state.roomId,boardId:state.board.id,boardSize:16,modules:[],assignments:[],
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
    await new Promise(resolve=>setTimeout(resolve,100));
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
  await page.locator('#player-debug-cash').click();
  await page.locator('#debug-cash').waitFor({state:'visible'});
};
const openOwner=async()=>{
  await page.locator('.tile[data-tile-id="A1"]').click({button:'right'});
  await page.locator('#tile-debug-owner').click();
  await page.locator('#debug-owner').waitFor({state:'visible'});
};
try{
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  await page.locator('.tile[data-tile-id="A1"]').waitFor();
  await page.locator('.tile[data-tile-id="A1"]').click({button:'right'});
  assert.equal(edits.length,0,'opening a context menu never mutates');
  await screenshot('desktop-tile-menu');
  await page.locator('#tile-debug-owner').click();
  assert.equal(await page.locator('#debug-cash-field').isVisible(),false);
  await page.locator('#debug-owner').selectOption('2');
  assert.match(await page.locator('#debug-edit-preview').textContent(),/银行 → P2/);
  await screenshot('desktop-owner-form');
  await page.locator('#debug-edit-cancel').click();
  assert.equal(edits.length,0,'cancel preserves ownership');
  await openCash();
  assert.equal(await page.locator('#debug-owner-field').isVisible(),false);
  await page.locator('#debug-cash').fill('5432');
  await screenshot('desktop-cash-form');
  for(const invalid of ['','-1','1000001','1.5']){
    await page.locator('#debug-cash').fill(invalid);
    assert.equal(await page.locator('#debug-edit-save').isDisabled(),true,invalid);
  }
  await page.locator('#debug-cash').fill('5432');
  await page.locator('#debug-edit-save').click();
  await page.locator('#debug-edit-modal').waitFor({state:'hidden'});
  assert.equal(edits.length,1);
  assert.deepEqual(edits[0],{path:'/api/debug/player-cash',query:{roomId:'9916',expectedVersion:'44',playerId:'1',cash:'5432'},method:'POST'});
  assert.equal(state.players[0].cash,5432);
  await openOwner();
  await page.locator('#debug-owner').selectOption('2');
  await page.locator('#debug-edit-save').click();
  await page.locator('#debug-edit-modal').waitFor({state:'hidden'});
  assert.equal(state.assets[0][0],2);
  assert.equal(edits[1].path,'/api/debug/asset-owner');
  assert.equal(edits[1].query.tileId,'A1');
  await page.locator('.tile[data-tile-id="CORNER-START"]').click({button:'right'});
  assert.equal(await page.locator('#tile-debug-owner').isVisible(),false,'nontradable corner has no ownership action');
  await page.keyboard.press('Escape');
  await openCash();
  ++state.version;
  await page.waitForFunction(()=>document.querySelector('#debug-edit-save').disabled);
  assert.match(await page.locator('#debug-edit-status').textContent(),/状态已变化/);
  await page.locator('#debug-edit-cancel').click();
  await openCash();
  ++state.roomId;
  await page.waitForFunction(()=>document.querySelector('#debug-edit-save').disabled);
  await page.locator('#debug-edit-cancel').click();
  await openCash();rejectNext=true;
  await page.locator('#debug-cash').fill('88');
  await page.locator('#debug-edit-save').click();
  await page.waitForFunction(()=>document.querySelector('#debug-edit-status').textContent.includes('原值已保留'));
  assert.equal(state.players[0].cash,5432);
  await page.locator('#debug-edit-cancel').click();
  await page.setViewportSize({width:390,height:844});
  await page.locator('.tile[data-tile-id="A1"]').click({button:'right'});
  const menuBounds=await page.locator('#tile-debug-context-menu').boundingBox();
  assert(menuBounds.x>=0&&menuBounds.y>=0&&menuBounds.x+menuBounds.width<=390&&menuBounds.y+menuBounds.height<=844);
  await screenshot('mobile-tile-menu');
  await page.keyboard.press('Escape');
  for(const [kind,open] of [['cash',openCash],['owner',openOwner]]){
    await open();
    const bounds=await page.locator('#debug-edit-modal .settings-card').boundingBox();
    assert(bounds.x>=0&&bounds.y>=0&&bounds.x+bounds.width<=390&&bounds.y+bounds.height<=844);
    await page.locator('#debug-edit-save').scrollIntoViewIfNeeded();
    await screenshot('mobile-'+kind+'-form');
    await page.keyboard.press('Escape');
  }
  assert.deepEqual(errors,[]);
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({ok:true,edits,errors,viewports:[[1440,1050],[390,844]]},null,2));
  console.log('GRIDOPOLY_WEB_DEBUG_BROWSER_TESTS_PASS');
}finally{
  await browser.close();await new Promise(resolve=>server.close(resolve));
}
