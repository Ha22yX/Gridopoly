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
const languageHelpers=Function(`${source.match(/\/\/ TESTABLE_I18N_BEGIN([\s\S]*?)\/\/ TESTABLE_I18N_END/)[1]};return {tr,set:language=>uiLanguage=language,messages:englishMessages};`)();
for(const [zh,en] of Object.entries(languageHelpers.messages)){
  assert(!/\p{Script=Han}/u.test(en),'English catalog has no untranslated messages');
  assert.deepEqual([...zh.matchAll(/\{\d+\}/g)].map(m=>m[0]).sort(),[...en.matchAll(/\{\d+\}/g)].map(m=>m[0]).sort(),'translations preserve all interpolation slots');
}
languageHelpers.set('en');
assert.equal(languageHelpers.tr('constructor'),'constructor','unknown keys do not resolve prototype properties');
assert.equal(languageHelpers.tr(['在线 · ',''],'小明'),'Online · 小明','interpolated user data stays unchanged');
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
let settingsFail=false;
const settings={botActionIntervalMs:1200,minimumMs:100,maximumMs:10000};
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
  if(url.pathname==='/api/settings'){json(settingsFail?{error:'temporarily_unavailable'}:settings,settingsFail?503:200);return;}
  if(url.pathname==='/api/tile-tags'){json({roomId:state.roomId,tagRevision:5,bindingRevision:3,tags:[{uid:'1234ABCD',currentlySeen:true,lastSeenMs:5,boundPlayerId:2,sightings:[{currentlySeen:true,moduleId:'reader-a',mapIndex:1,tileId:'A1'}]}],bindings:[]});return;}
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
const page=await browser.newPage({viewport:{width:1440,height:1050}});
page.on('pageerror',error=>errors.push(error.message));
const switchTo=language=>page.locator('#ui-language').selectOption(language);
const assertEnglish=async()=>{
  const leaks=await page.evaluate(()=>{
    const result=[],walker=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);
    while(walker.nextNode()){
      const n=walker.currentNode,p=n.parentElement;
      if(p.closest('script,style,#ui-language')||!p.checkVisibility()||p.closest('[hidden]'))continue;
      if(/\p{Script=Han}/u.test(n.textContent))result.push(n.textContent);
    }
    for(const el of document.querySelectorAll('[title],[aria-label]')){
      if(!el.checkVisibility()||el.closest('[hidden]'))continue;
      for(const a of ['title','aria-label'])if(/\p{Script=Han}/u.test(el.getAttribute(a)||''))result.push(el.getAttribute(a));
    }
    return result;
  });
  assert.deepEqual(leaks,[],'English UI must not contain untranslated interface messages');
};
const screenshot=name=>page.screenshot({path:path.join(output,name+'.png')});
const openCash=()=>page.locator('.player[data-player-id="1"]').click({button:'right'});
const openOwner=()=>page.locator('.tile[data-tile-id="A1"]').click({button:'right'});
try{
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  await page.locator('.tile[data-tile-id="A1"]').waitFor();
  assert.equal(await page.locator('html').getAttribute('lang'),'zh-CN');
  await switchTo('en');await assertEnglish();
  assert.equal(await page.title(),'Gridopoly Test Console');
  assert.equal(await page.locator('#phase').textContent(),'Awaiting roll');
  assert.equal(await page.locator('#settings-open').textContent(),'Settings');
  await screenshot('desktop-english');
  await page.reload();await page.locator('.player').first().waitFor();
  assert.equal(await page.locator('#ui-language').inputValue(),'en','language survives reload');
  await assertEnglish();

  await openCash();await page.locator('#debug-cash').fill('7654');
  await page.evaluate(()=>window.cashInput=document.querySelector('#debug-cash'));
  await switchTo('zh-CN');
  assert.equal(await page.locator('#debug-cash').inputValue(),'7654');
  assert.equal(await page.locator('#player-context-menu').isVisible(),true);
  await switchTo('en');await assertEnglish();
  assert(await page.evaluate(()=>window.cashInput===document.querySelector('#debug-cash')));
  assert.match(await page.locator('#debug-cash-preview').textContent(),/Draft: ¥7,654/);
  assert.equal(edits.length,0,'language switch does not submit edits');
  rejectNext=true;await page.locator('#debug-cash-save').click();
  await page.waitForFunction(()=>document.querySelector('#debug-cash-status').textContent.includes('original value'));
  await switchTo('zh-CN');assert.match(await page.locator('#debug-cash-status').textContent(),/原值已保留/);
  await switchTo('en');await assertEnglish();await screenshot('cash-error-english');
  await page.keyboard.press('Escape');
  await openOwner();await page.locator('#debug-owner').selectOption('2');
  const tileTitle=await page.locator('#tile-debug-context-title').textContent();
  await switchTo('zh-CN');await switchTo('en');await assertEnglish();
  assert.equal(await page.locator('#tile-debug-context-title').textContent(),tileTitle,'language switch retains the selected tile title');
  assert.equal(await page.locator('#debug-owner').inputValue(),'2');
  assert.equal(await page.locator('#debug-owner option[value="0"]').textContent(),'Bank');
  await screenshot('owner-english');await page.keyboard.press('Escape');

  await page.locator('#settings-open').click();
  await page.waitForFunction(()=>document.querySelector('#settings-hint').textContent.includes('Current:'));
  await page.locator('#bot-interval').fill('2300');
  // The header is intentionally behind the modal; keyboard/script switches
  // exercise the same handler without dismissing it or making a request.
  await page.evaluate(()=>setUiLanguage('zh-CN'));
  assert.equal(await page.locator('#bot-interval').inputValue(),'2300');
  await page.evaluate(()=>setUiLanguage('en'));await assertEnglish();
  await screenshot('settings-english');
  await page.locator('#bot-interval').fill('99999');await page.locator('#settings-save').click();
  await page.evaluate(()=>setUiLanguage('zh-CN'));assert.match(await page.locator('#settings-hint').textContent(),/请输入/);
  await page.evaluate(()=>setUiLanguage('en'));await assertEnglish();
  await page.locator('#settings-cancel').click();
  settingsFail=true;await page.locator('#settings-open').click();
  await page.waitForFunction(()=>document.querySelector('#settings-hint').textContent.includes('Read failed'));
  await assertEnglish();await page.locator('#settings-cancel').click();settingsFail=false;

  await openCash();await page.locator('#player-assign-tag').click();
  await page.waitForFunction(()=>document.querySelector('#tag-binding-select').value==='1234ABCD');
  await assertEnglish();await page.evaluate(()=>setUiLanguage('zh-CN'));
  await page.evaluate(()=>setUiLanguage('en'));await assertEnglish();
  assert.equal(await page.locator('#tag-binding-select').inputValue(),'1234ABCD');
  await screenshot('tag-assignment-english');await page.locator('#tag-binding-cancel').click();

  // Exhaust the status branches without sending actions to any live game.
  await page.evaluate(()=>{clearTimeout(pollTimer);if(refreshController)refreshController.abort();});
  const result=await page.evaluate(()=>{
    const checks=[];
    const scan=()=>{const text=document.querySelector('#workflow').innerText;checks.push(text);if(/\p{Script=Han}/u.test(text))throw Error(text);};
    const original=structuredClone(state);
    for(const phase of [0,1,2,3,4,5,6,7,8]){const next=structuredClone(original);next.phase=phase;render(next);scan();}
    for(const scenario of ['debt','auction','opening','card','winner']){
      const next=structuredClone(original);
      if(scenario==='debt')next.debt={active:true,debtor:1,amount:500,creditor:0};
      if(scenario==='auction'||scenario==='opening')next.auction={active:true,opening:scenario==='opening',asset:1,readyMask:1,requiredReadyMask:3,currentBid:50,minimumBid:60,bidder:1};
      if(scenario==='card')next.card={active:true,stage:1,deck:1,catalog:1,player:1};
      if(scenario==='winner'){next.phase=7;next.winnerPlayerId=1;}
      render(next);scan();
    }
    for(const phase of [1,2])for(const pairingState of ['waiting_reader','clear_required','awaiting_tag','success_remove','already_bound','multiple_tags','reader_offline','timed_out','cancelled','complete','unknown']){
      const next=structuredClone(original);
      next.identity={phase,revision:1,readyMask:0,avatarFinalMask:0,nameFinalMask:0,countdownDeadlineEpochMs:Date.now()+5000,serverEpochMs:Date.now()};
      next.tagPairing={enabled:true,roomId:next.roomId,state:pairingState,activePlayerId:1,moduleId:'reader-a',readerMapIndex:1,requiredHumanMask:3,boundMask:1,canRetry:true};
      render(next);scan();
    }
    for(const orderStatus of ['probing','ready','unanchored','stale','conflict','legacy'])for(const moduleRole of ['long','corner','unknown'])for(const orderConflict of ['unknown_module_role','invalid_corner_count','invalid_corner_spacing','incomplete_ring','ring_open','ring_or_tile_conflict','ring_online_set_mismatch','ring_size_mismatch','ring_board_layout_mismatch','multiple_downstream','cycle']){
      const label=tileDebugOrderLabel({orderCapable:true,orderStatus,moduleRole,orderConflict,orderIndex:2,closedRing:true,orderRingSize:16,orderChainId:'ring-a',orderUpstreamModuleId:'reader-b'});
      if(/\p{Script=Han}/u.test(label))throw Error(label);
    }
    render(original);return checks.length;
  });
  assert.equal(result,36);
  await page.evaluate(()=>{state.players[0].name='小明 <img onerror=alert(1)>';render(state);setUiLanguage('zh-CN');setUiLanguage('en');});
  assert.match(await page.locator('.player[data-player-id="1"]').textContent(),/小明 <img onerror=alert\(1\)>/,'user names are not translated or injected');
  assert.equal(await page.locator('.player[data-player-id="1"] img').count(),0);
  await page.evaluate(()=>{state.players[0].name='Alice';render(state);});
  await page.evaluate(()=>{i18nText($('#status'),()=>tr('连接断开'));$('#status').className='error';});
  await switchTo('zh-CN');await switchTo('en');await switchTo('zh-CN');
  assert.equal(await page.locator('#status').textContent(),'连接断开','repeated switches preserve disconnected feedback');
  await switchTo('en');
  for(const width of [1440,390,320]){
    await page.setViewportSize({width,height:844});await assertEnglish();
    assert(await page.locator('header').evaluate(el=>el.scrollWidth<=el.clientWidth),'header fits');
    await openOwner();
    const bounds=await page.locator('#tile-debug-context-menu').boundingBox();
    assert(bounds.x>=0&&bounds.x+bounds.width<=width);
    assert(await page.locator('#tile-debug-context-menu').evaluate(el=>el.scrollWidth<=el.clientWidth));
    await screenshot(`owner-${width}`);await page.keyboard.press('Escape');
  }
  await switchTo('zh-CN');assert.equal(await page.locator('#settings-open').textContent(),'设置');
  assert.equal(edits.length,1,'only the explicit rejected test save was submitted');
  const blocked=await browser.newPage();
  await blocked.addInitScript(()=>{Object.defineProperty(window,'localStorage',{get(){throw new Error('storage blocked');}});});
  await blocked.goto(`http://127.0.0.1:${server.address().port}`);await blocked.locator('.player').first().waitFor();
  await blocked.locator('#ui-language').selectOption('en');assert.equal(await blocked.locator('#settings-open').textContent(),'Settings');
  await blocked.close();assert.deepEqual(errors,[]);
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({ok:true,workflowBranches:result,orderBranches:198,viewports:[1440,390,320],errors,edits},null,2));
  console.log('GRIDOPOLY_WEB_LANGUAGE_TESTS_PASS persistence=true drafts=true english=true');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
