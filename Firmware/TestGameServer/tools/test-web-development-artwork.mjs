import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';

// Uses the real published PNGs and an isolated game fixture; never changes a Pi game.
const {chromium}=createRequire(import.meta.url)('playwright');
const output=path.resolve(process.argv[2]||'web-artwork-evidence');
fs.mkdirSync(output,{recursive:true});
const source=fs.readFileSync(new URL('../src/WebUi.h',import.meta.url),'utf8');
const html=source.match(/R"GRIDOPOLY_HTML\(([\s\S]*?)\)GRIDOPOLY_HTML";/)[1];
const manifest=JSON.parse(fs.readFileSync(new URL('../../../Assets/GridCity/DevelopmentV1/manifests/development-v1.json',import.meta.url)));
const visualBlock=source.slice(source.indexOf('const tileVisuals='),source.indexOf('// Only retain artwork'));
const artworkUrl=Function(`${visualBlock};return developmentArtworkUrl;`)();
const files=new Map();
for(const asset of manifest.assets.filter(asset=>asset.level>0)){
  const url=`/assets/development/v1/web/${asset.key}-l${asset.level}.png`;
  assert.equal(artworkUrl({id:asset.tile_id,kind:1,asset:0,level:asset.level}),url);
  files.set(url,new URL('../../../Assets/GridCity/DevelopmentV1/'+asset.files.tile_png.path,import.meta.url));
}
assert.equal(files.size,110,'all 22 properties have five published development images');
for(const tile of [
  {id:'A1',kind:1,asset:0,level:0},{id:'A1',kind:1,asset:0,level:6},
  {id:'A1',kind:1,asset:0,level:1.5},{id:'A1',kind:1,asset:255,level:1},
  {id:'UNKNOWN',kind:1,asset:0,level:1},{id:'T-WEST',kind:2,asset:0,level:1},
  {id:'U-ENERGY',kind:3,asset:0,level:5},
])assert.equal(artworkUrl(tile),'','non-buildable or invalid levels retain default art');

const tiles=[['CORNER-START',0,255,0],['A1',1,0,100],['CARD-CF-1',5,255,0],['A2',1,1,120],
  ['CORNER-HOLD',7,255,0],['B1',1,2,140],['T-WEST',2,3,160],['B2',1,4,180],
  ['CORNER-REST',8,255,0],['B3',1,5,200],['U-ENERGY',3,6,160],['C1',1,7,220],
  ['CORNER-GOTO',9,255,0],['C2',1,8,240],['FEE-CITY',6,255,0],['C3',1,9,260]];
const state={schema:2,roomId:9916,version:44,network:1,controlVersion:1,phase:1,round:3,
  activePlayer:1,decisionPlayer:1,actions:0,espnowPeers:0,wifi:{connected:true,ip:'127.0.0.1'},
  board:{id:'grid-city-16-test',size:16},debt:{active:false},auction:{active:false},card:{active:false},
  players:[{id:1,name:'Alice',controller:'WEB',cash:1500,position:0,bankrupt:false}],
  assets:Array.from({length:10},()=>[0,0,0]),events:[]};
const requests=[],mutations=[];
let rejectOnce='',slowUrl='',failedUrl='',active=0,maxActive=0;
const server=http.createServer(async(req,res)=>{
  const url=new URL(req.url,'http://127.0.0.1');
  const json=body=>{res.writeHead(200,{'content-type':'application/json'});res.end(JSON.stringify(body));};
  if(req.method!=='GET'){mutations.push(req.url);res.writeHead(405);res.end();return;}
  if(url.pathname==='/'){res.writeHead(200,{'content-type':'text/html; charset=utf-8'});res.end(html);return;}
  if(url.pathname==='/api/sync'){
    if(url.search){res.writeHead(204);res.end();}else json(state);
    return;
  }
  if(url.pathname==='/api/board'){json({roomId:state.roomId,board:state.board,tiles});return;}
  if(url.pathname==='/api/player-devices'){json({devices:[],automaticPlayers:false});return;}
  if(url.pathname==='/api/tile-debug/assignments'){json({modules:[],assignments:[],tiles:[]});return;}
  if(files.has(url.pathname)){
    const event={url:url.pathname,at:Date.now(),status:200};requests.push(event);
    ++active;maxActive=Math.max(maxActive,active);res.on('close',()=>--active);
    if(rejectOnce===url.pathname){
      rejectOnce='';event.status=503;res.writeHead(503,{'Retry-After':'1','Cache-Control':'no-store'});res.end();return;
    }
    if(failedUrl===url.pathname){event.status=404;res.writeHead(404);res.end();return;}
    if(slowUrl===url.pathname)await new Promise(resolve=>setTimeout(resolve,800));
    if(res.destroyed)return;
    res.writeHead(200,{'content-type':'image/png','Cache-Control':'public, max-age=31536000, immutable'});
    res.end(fs.readFileSync(files.get(url.pathname)));return;
  }
  if(url.pathname.startsWith('/assets/tiles/')){
    const file=new URL('../../../Assets/GridCity/StreetV3/device/'+path.basename(url.pathname),import.meta.url);
    if(fs.existsSync(file)){res.writeHead(200,{'content-type':'image/png'});res.end(fs.readFileSync(file));return;}
  }
  res.writeHead(404);res.end();
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({headless:true,...(process.env.PLAYWRIGHT_EXECUTABLE_PATH?{executablePath:process.env.PLAYWRIGHT_EXECUTABLE_PATH}:{})});
const page=await browser.newPage({viewport:{width:1440,height:1100}}),errors=[];
page.on('pageerror',error=>errors.push(error.message));
const update=levels=>page.evaluate(levels=>{
  render({...state,version:state.version+1,tiles:state.tiles.map(tile=>({...tile,level:levels[tile.id]??tile.level}))});
},levels);
const loaded=async(id,level)=>{
  const url=artworkUrl({id,kind:1,asset:0,level});
  await page.waitForFunction(({id,url})=>{
    const image=document.querySelector(`.tile[data-tile-id="${id}"] img.tile-art`);
    return image?.dataset.artworkUrl===url&&image.src.startsWith('blob:')&&image.complete&&image.naturalWidth===160;
  },{id,url});
  const actual=await page.evaluate(async id=>Array.from(new Uint8Array(await(await fetch(document.querySelector(`.tile[data-tile-id="${id}"] img.tile-art`).src)).arrayBuffer())),id);
  assert.equal(createHash('sha256').update(Buffer.from(actual)).digest('hex'),
    createHash('sha256').update(fs.readFileSync(files.get(url))).digest('hex'),'displayed blob is the exact published level image');
};
try{
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  await page.locator('.tile[data-tile-id="A1"]').waitFor();
  await page.evaluate(()=>{fullSyncDueAt=Date.now()+300000;setUiLanguage('en');});
  assert.equal(requests.length,0,'empty board does not download development images');
  const coordinates=await page.locator('.tile').evaluateAll(nodes=>nodes.map(n=>[Number(n.dataset.tileIndex),Number(n.style.gridRow),Number(n.style.gridColumn)]));
  assert.deepEqual(coordinates.filter(([i])=>[0,1,4,8,12,15].includes(i)),[[0,5,5],[1,4,5],[4,1,5],[8,1,1],[12,5,1],[15,5,4]]);

  rejectOnce=artworkUrl({id:'A1',kind:1,asset:0,level:1});
  await update({A1:1});await loaded('A1',1);
  assert.equal(requests[0].status,503);
  assert(requests[1].at-requests[0].at>=950,'Retry-After is respected');
  for(const level of [2,3,4,5]){await update({A1:level});await loaded('A1',level);}
  // Building, hotel conversion and selling all follow the current authority level.
  await update({A1:2});await loaded('A1',2);
  await update({A1:0});
  assert.equal(await page.locator('[data-tile-id="A1"] img').getAttribute('src'),'/assets/tiles/a1-rivet-row.png');

  slowUrl=artworkUrl({id:'A2',kind:1,asset:1,level:5});
  await update({A2:5});
  while(!requests.some(r=>r.url===slowUrl))await new Promise(resolve=>setTimeout(resolve,30));
  await update({A2:1});await loaded('A2',1);
  assert.equal(await page.locator('[data-tile-id="A2"] img').getAttribute('data-artwork-url'),artworkUrl({id:'A2',kind:1,asset:1,level:1}),'stale hotel response cannot overwrite a newer level');

  failedUrl=artworkUrl({id:'B1',kind:1,asset:2,level:3});await update({B1:3});
  while(!requests.some(r=>r.url===failedUrl))await new Promise(resolve=>setTimeout(resolve,30));
  assert.equal(await page.locator('[data-tile-id="B1"] img').getAttribute('src'),'/assets/tiles/b1-lantern-avenue.png','unavailable art leaves a visible base image');
  failedUrl='';await loaded('B1',3);

  await update({A1:1,A2:2,B1:3,B2:4,B3:5});
  for(const [id,level] of [['A1',1],['A2',2],['B1',3],['B2',4],['B3',5]])await loaded(id,level);
  assert.equal(maxActive,1,'one artwork transfer at a time leaves server capacity for modules');
  const count=requests.length;
  await page.evaluate(()=>{setUiLanguage('zh-CN');setUiLanguage('en');render({...state});});
  await loaded('B3',5);
  assert.equal(requests.length,count,'poll/render and language changes reuse current blobs');
  await page.screenshot({path:path.join(output,'houses-and-hotel-desktop.png')});
  await page.setViewportSize({width:390,height:844});
  await page.screenshot({path:path.join(output,'houses-and-hotel-mobile.png')});
  await update({A1:0,A2:0,B1:0,B2:0,B3:0});
  assert.equal(await page.evaluate(()=>developmentArtwork.size),0,'unused blobs are released when buildings are removed');
  assert.deepEqual(errors,[]);assert.deepEqual(mutations,[]);
  const report={ok:true,catalogVariants:files.size,requests,maxActive,errors,mutations};
  fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));
  console.log('GRIDOPOLY_WEB_DEVELOPMENT_ARTWORK_PASS '+JSON.stringify({catalogVariants:files.size,requests:requests.length,maxActive}));
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
