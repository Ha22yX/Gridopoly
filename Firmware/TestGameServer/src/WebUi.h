#pragma once

#include <Arduino.h>

namespace gridopoly::server {

static const char kWebUi[] PROGMEM = R"GRIDOPOLY_HTML(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Gridopoly 测试主控</title>
  <style>
    :root{color-scheme:dark;--bg:#080c12;--panel:#111923;--panel2:#172231;--line:#2b3a4f;--ink:#eef4ff;--muted:#91a0b6;--accent:#ffcb45;--ok:#58d68d;--danger:#ff727a}
    *{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif}
    header{display:flex;align-items:center;justify-content:space-between;gap:18px;padding:15px 20px;border-bottom:1px solid var(--line);background:#0d131c;position:sticky;top:0;z-index:4}
    h1{font-size:18px;margin:0;letter-spacing:.08em}.header-actions{display:flex;align-items:center;justify-content:flex-end;gap:10px}#status{color:var(--muted);text-align:right}main{display:grid;grid-template-columns:minmax(540px,1fr) 370px;gap:16px;padding:16px;max-width:1500px;margin:auto}
    .panel{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:14px}.board{display:grid;aspect-ratio:1;gap:4px;min-height:560px}
    .tile{--tile-color:#42526a;min-width:0;border:1px solid color-mix(in srgb,var(--tile-color) 50%,#334259);background:var(--panel2);border-radius:7px;padding:8px 5px 5px;display:flex;flex-direction:column;justify-content:space-between;overflow:hidden;position:relative;isolation:isolate}
    .tile::before{content:"";position:absolute;z-index:3;left:0;right:0;top:0;height:5px;background:var(--tile-color);box-shadow:0 1px 8px color-mix(in srgb,var(--tile-color) 55%,transparent)}
    .tile::after{content:"";position:absolute;z-index:1;inset:5px 0 0;background:linear-gradient(180deg,#11192310 12%,#11192388 56%,#111923f5 91%);pointer-events:none}
    .tile-art{position:absolute;z-index:0;inset:5px 0 0;width:100%;height:calc(100% - 5px);object-fit:cover;opacity:.72;filter:saturate(.88) contrast(1.04);pointer-events:none}
    .tile>span{position:relative;z-index:2;text-shadow:0 1px 3px #05080c,0 0 8px #05080c}.tile.active{outline:2px solid var(--accent);box-shadow:0 0 20px #ffcb4540}.tile .n{font-size:10px;color:#d4deec;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.tile.owned .n{padding-right:24px}.tile .owner-badge{position:absolute;z-index:5;top:9px;right:4px;min-width:19px;height:15px;padding:0 3px;border:1px solid #ffffff70;border-radius:5px;display:grid;place-items:center;background:var(--player-color);color:#071018;font:800 8px/1 ui-monospace,Consolas,monospace;text-shadow:none;box-shadow:0 2px 7px #0009}.tile .id{font-size:11px;font-weight:800;line-height:1.15;display:-webkit-box;-webkit-box-orient:vertical;-webkit-line-clamp:2;overflow:hidden}.tile .id small{display:inline;margin-left:4px;font:8px/1 ui-monospace,Consolas,monospace;color:#c1ccdc;letter-spacing:.04em;white-space:nowrap}.tile .meta{font-size:9px;color:#eef4ff;background:#08101acc;border-radius:4px;padding:2px 3px;width:max-content;max-width:100%;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.tile.has-players .meta{max-width:calc(100% - 22px)}
    .chips{position:absolute!important;z-index:4!important;right:4px;bottom:4px;display:flex;gap:2px;flex-wrap:wrap;justify-content:flex-end;max-width:calc(100% - 8px)}.chip{width:16px;height:16px;border-radius:50%;display:grid;place-items:center;font-size:9px;font-style:normal;font-weight:800;color:#071018;background:var(--player-color,#ffcb45);box-shadow:0 1px 4px #0008}
    .center{grid-column:2/-2;grid-row:2/-2;display:grid;place-items:center;text-align:center;border:1px dashed #2d3c51;border-radius:16px;background:radial-gradient(circle,#18263a 0,#0f1722 70%)}
    .center b{font-size:clamp(22px,4vw,56px);letter-spacing:.12em}.center small{display:block;color:var(--muted);margin-top:8px}.stack{display:grid;gap:12px}.players{display:grid;gap:7px}
    .player{display:grid;grid-template-columns:24px 1fr auto;gap:8px;align-items:center;padding:8px;border:1px solid var(--line);border-radius:8px;cursor:context-menu}.player.active{border-color:var(--accent)}.player.forced-armed{box-shadow:inset 3px 0 0 var(--accent)}.forced-player-mark{display:inline-block;margin-left:6px;padding:1px 5px;border:1px solid #ffcb4570;border-radius:999px;color:var(--accent);font:9px/1.4 ui-monospace,Consolas,monospace;vertical-align:1px}
    .badge{width:24px;height:24px;border-radius:50%;display:grid;place-items:center;background:var(--player-color,#6cd5ff);color:#08121c;font-weight:800;overflow:hidden}.badge img{width:100%;height:100%;object-fit:cover}.money{font-variant-numeric:tabular-nums;color:var(--ok)}
    .identity-state{font:700 10px/1.2 ui-monospace,Consolas,monospace;letter-spacing:.05em;color:var(--muted)}.identity-state.ready{color:var(--ok)}.identity-state.generating{color:var(--accent)}
    .controls{display:grid;grid-template-columns:1fr 1fr;gap:8px}button,select,input{border:1px solid #3a4a61;background:var(--panel2);color:var(--ink);padding:10px;border-radius:8px}button{cursor:pointer;font-weight:700}button.primary{background:var(--accent);border-color:var(--accent);color:#15130a}button:disabled{opacity:.35;cursor:not-allowed}
    .newgame{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.newgame button{grid-column:1/-1}.events{height:240px;overflow:auto;font:12px/1.5 ui-monospace,Consolas,monospace;color:#c6d1e1}.event{padding:4px 0;border-bottom:1px solid #202b3a}
    .kv{display:grid;grid-template-columns:auto 1fr;gap:3px 10px;color:var(--muted)}.kv b{color:var(--ink)}.error{color:var(--danger)}
    .settings-open{padding:7px 12px;white-space:nowrap}.modal[hidden],.context-menu[hidden],.selection-banner[hidden]{display:none}.modal{position:fixed;z-index:20;inset:0;display:grid;place-items:center;padding:20px;background:#02050ab8;backdrop-filter:blur(5px)}.settings-card{width:min(520px,100%);max-height:calc(100vh - 40px);overflow:auto;padding:20px;border:1px solid #40516a;border-radius:14px;background:#111923;box-shadow:0 24px 80px #000b}.settings-card h2{margin:0 0 6px;font-size:20px}.settings-card p{margin:0 0 18px;color:var(--muted)}.settings-field{display:grid;gap:7px}.settings-field input,.settings-field select{width:100%;font-size:16px;font-variant-numeric:tabular-nums}.settings-hint{min-height:20px;margin-top:8px;color:var(--muted);font-size:12px}.settings-actions{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:16px}
    .context-menu{position:fixed;z-index:30;width:220px;padding:7px;border:1px solid #40516a;border-radius:10px;background:#111923;box-shadow:0 18px 55px #000c}.context-title{padding:7px 8px 9px;color:var(--muted);font-size:12px;border-bottom:1px solid var(--line);margin-bottom:6px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.context-menu button{display:block;width:100%;border:0;background:transparent;text-align:left;padding:9px}.context-menu button[hidden]{display:none}.context-menu button:hover:not(:disabled),.context-menu button:focus-visible{background:#213149}.context-menu .danger{color:var(--danger)}
    .selection-banner{position:fixed;z-index:15;left:50%;top:72px;transform:translateX(-50%);display:flex;align-items:center;gap:12px;max-width:calc(100vw - 28px);padding:10px 12px 10px 16px;border:1px solid #ffcb4590;border-radius:999px;background:#111923f2;box-shadow:0 10px 36px #000a}.selection-banner b{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.selection-banner button{padding:6px 10px}.tile.forced-candidate{cursor:pointer;outline:2px solid #6cd5ff;box-shadow:0 0 0 3px #6cd5ff22,0 0 24px #6cd5ff55;animation:forced-pulse 1.2s ease-in-out infinite alternate}.tile.forced-candidate::before{height:7px;background:#6cd5ff}.tile.forced-target{outline:2px solid var(--accent);box-shadow:0 0 0 3px #ffcb4522,0 0 24px #ffcb4555}.tile.forced-candidate:hover{transform:translateY(-2px);filter:brightness(1.12)}@keyframes forced-pulse{to{box-shadow:0 0 0 5px #6cd5ff30,0 0 30px #6cd5ff70}}
    .board[data-size="40"] .tile{padding-inline:3px}.board[data-size="40"] .tile .n{font-size:8px}.board[data-size="40"] .tile.owned .n{padding-right:18px}.board[data-size="40"] .tile .owner-badge{top:8px;right:2px;min-width:16px;height:12px;padding:0 2px;border-radius:4px;font-size:7px}.board[data-size="40"] .tile .id{font-size:9px}.board[data-size="40"] .tile .id small{display:none}.board[data-size="40"] .tile .meta{font-size:7px}
    @media(max-width:980px){main{grid-template-columns:1fr}.board{min-height:0}.panel:first-child{padding:8px}.tile-art{opacity:.62}}
  </style>
</head>
<body>
<header><h1>GRIDOPOLY / TEST AUTHORITY</h1><div class="header-actions"><div id="status">连接中…</div><button id="settings-open" class="settings-open" type="button">设置</button></div></header>
<div id="settings-modal" class="modal" hidden role="dialog" aria-modal="true" aria-labelledby="settings-title">
  <section class="settings-card">
    <h2 id="settings-title">测试设置</h2>
    <p>调整机器人每执行一步动作前的等待时间，保存后立即生效并在重启后保留。</p>
    <label class="settings-field" for="bot-interval"><b>机器人动作间隔（毫秒）</b><input id="bot-interval" type="number" min="100" max="10000" step="100" value="1200" inputmode="numeric"></label>
    <div id="settings-hint" class="settings-hint">建议 800–2000 ms；数值越大，机器人行动越慢。</div>
    <div class="settings-actions"><button id="settings-cancel" type="button">取消</button><button id="settings-save" class="primary" type="button">保存设置</button></div>
  </section>
</div>
<div id="player-context-menu" class="context-menu" hidden role="menu" aria-label="玩家操作">
  <div id="player-context-title" class="context-title">玩家</div>
  <button id="player-force-destination" type="button" role="menuitem">指定下一次目的地</button>
  <button id="player-clear-destination" class="danger" type="button" role="menuitem" hidden>取消现有指定</button>
</div>
<div id="forced-selection-banner" class="selection-banner" hidden role="status">
  <b id="forced-selection-text">请选择棋盘上高亮的目的地</b>
  <button id="forced-selection-cancel" type="button">取消</button>
</div>
<main>
  <section class="panel"><div id="board" class="board"></div></section>
  <aside class="stack">
    <section class="panel"><div class="kv"><span>地图</span><b id="map">—</b><span>回合</span><b id="round">—</b><span>阶段</span><b id="phase">—</b><span>当前回合</span><b id="active">—</b><span>等待决策</span><b id="decision">—</b><span>状态版本</span><b id="version">—</b><span>Wi-Fi 玩家屏</span><b id="peers">—</b></div></section>
    <section class="panel"><div id="workflow" class="kv"><span>事务</span><b>无待处理事务</b></div></section>
    <section class="panel"><div id="players" class="players"></div></section>
    <section class="panel controls">
      <button data-action="roll" class="primary">掷骰</button><button data-action="confirm">确认棋子到位</button>
      <button data-action="buy">购买</button><button data-action="decline">放弃并拍卖</button>
      <button data-action="end">结束回合</button><button data-action="holdfee">支付离场费</button>
      <button data-action="mortgage">抵押所选资产</button><button data-action="unmortgage">赎回所选资产</button>
      <button data-action="build">建设所选地产</button><button data-action="sell">出售所选建筑</button>
      <button data-action="paydebt" class="primary">确认支付债务</button><button data-action="bankrupt">确认无法偿还</button>
      <button data-action="bid" class="primary">拍卖出价</button><button data-action="passbid">退出本次拍卖</button>
      <button data-action="cardcontinue" class="primary">继续执行卡片</button><span></span>
      <input id="asset" type="number" min="0" value="0" title="资产索引"><span style="align-self:center;color:var(--muted)">资产索引</span>
      <input id="bid" type="number" min="10" step="10" value="10" title="拍卖金额"><span style="align-self:center;color:var(--muted)">拍卖金额</span>
    </section>
    <section class="panel newgame">
      <select id="humans" aria-label="Human players"><option value="1" selected>1 Human</option><option value="2">2 Humans</option><option value="3">3 Humans</option><option value="4">4 Humans</option><option value="5">5 Humans</option></select>
      <select id="size" aria-label="地图格数"><option>16</option><option>24</option><option selected>32</option><option>40</option></select>
      <select id="bots" aria-label="机器人数量"><option value="0">0 个机器人</option><option value="1">1 个机器人</option><option value="2">2 个机器人</option><option value="3" selected>3 个机器人</option><option value="4">4 个机器人</option><option value="5">5 个机器人</option></select>
      <button id="new">建立测试对局</button>
    </section>
    <section class="panel"><div id="events" class="events"></div></section>
  </aside>
</main>
<script>
const $=selector=>document.querySelector(selector);
const esc=value=>String(value).replace(/[&<>"']/g,char=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
const phases=['大厅','等待掷骰','等待棋子确认','购买决定','拍卖','债务处理','回合结束','游戏结束','等待卡片确认'];
const kinds=['起点','地产','交通枢纽','基础设施','Chance','Community Chest','费用','限制区','自由中庭','前往限制区'];
const actionBits={roll:1,confirm:2,buy:4,decline:8,end:16,mortgage:32,unmortgage:64,build:128,sell:256,holdfee:1024,paydebt:2048,bankrupt:4096,bid:8192,passbid:16384,cardcontinue:65536};
const tileVisuals={
  A1:['Rivet Row','a1-rivet-row.png','#C97852'],A2:['Copper Lane','a2-copper-lane.png','#C97852'],
  B1:['Lantern Avenue','b1-lantern-avenue.png','#63C6E8'],B2:['Tideway Drive','b2-tideway-drive.png','#63C6E8'],B3:['Beacon Boulevard','b3-beacon-boulevard.png','#63C6E8'],
  C1:['Canvas Street','c1-canvas-street.png','#D970AD'],C2:['Bloom Terrace','c2-bloom-terrace.png','#D970AD'],C3:['Aurora Avenue','c3-aurora-avenue.png','#D970AD'],
  D1:['Archive Way','d1-archive-way.png','#EE9B47'],D2:['Forum Drive','d2-forum-drive.png','#EE9B47'],D3:['Meridian Avenue','d3-meridian-avenue.png','#EE9B47'],
  E1:['Pulse Street','e1-pulse-street.png','#E85D63'],E2:['Prism Boulevard','e2-prism-boulevard.png','#E85D63'],E3:['Nova Avenue','e3-nova-avenue.png','#E85D63'],
  F1:['Sunstep Terrace','f1-sunstep-terrace.png','#E7C64B'],F2:['Helix Way','f2-helix-way.png','#E7C64B'],F3:['Horizon Drive','f3-horizon-drive.png','#E7C64B'],
  G1:['Canopy Lane','g1-canopy-lane.png','#54BB78'],G2:['Verdant Avenue','g2-verdant-avenue.png','#54BB78'],G3:['Summit Boulevard','g3-summit-boulevard.png','#54BB78'],
  H1:['Crown Promenade','h1-crown-promenade.png','#5F80D8'],H2:['Grand Meridian','h2-grand-meridian.png','#5F80D8'],
  'T-WEST':['Westline Terminal','transit-westline-terminal.png','#AAB8BD'],'T-NORTH':['Northloop Station','transit-northloop-station.png','#AAB8BD'],
  'T-EAST':['Eastgate Terminal','transit-eastgate-terminal.png','#AAB8BD'],'T-SOUTH':['Southline Depot','transit-southline-depot.png','#AAB8BD'],
  'U-ENERGY':['Metro Grid','utility-metro-grid.png','#50D1B1'],'U-WATER':['Bluewater Works','utility-bluewater-works.png','#50D1B1'],
  'FEE-CITY':['Income Tax','cover-income-tax.png','#F0A65B'],'FEE-DENSITY':['Luxury Tax','cover-luxury-tax.png','#F0A65B']
};
function tileVisual(id){
  if(tileVisuals[id])return tileVisuals[id];
  if(id.startsWith('CARD-CE-'))return['Chance','cover-chance.png','#B486F7'];
  if(id.startsWith('CARD-CF-'))return['Community Chest','cover-community-fund.png','#63C6E8'];
  if(id==='CORNER-START')return['Start','corner-central-launch.png','#FFCB45'];
  if(id==='CORNER-HOLD')return['Hold','corner-civic-hold.png','#E85D63'];
  if(id==='CORNER-REST')return['Free Court','corner-free-plaza.png','#54BB78'];
  if(id==='CORNER-GOTO')return['Go To Hold','corner-hold-order.png','#B486F7'];
  return[id,null,'#42526A'];
}

// TESTABLE_FORCED_ROLL_HELPERS_BEGIN
const playerColors=['#6cd5ff','#ff8a72','#b899ff','#58d68d','#ffcb45','#ff7f9f'];
function playerColor(playerId){
  const normalized=Math.max(1,Number(playerId)||1);
  return playerColors[(normalized-1)%playerColors.length];
}

function ownerBadgeModel(tile,players){
  if(!tile||tile.asset===255)return null;
  const id=Number(tile.owner);
  if(!Number.isInteger(id)||id<=0)return null;
  const label=`P${id}`;
  const player=(players||[]).find(candidate=>Number(candidate.id)===id);
  const rosterName=player&&String(player.name||'').trim();
  return{id,label,name:rosterName||label,color:playerColor(id)};
}

function forcedRollTargets(player,tiles,boardSize){
  if(!player||!Array.isArray(tiles)||!boardSize)return[];
  const targets=[];
  for(let steps=2;steps<=12;steps+=1){
    if(player.doubles>=2&&(steps===2||steps===12))continue;
    const target=(player.position+steps)%boardSize;
    const tile=tiles[target];
    if(tile)targets.push({target,steps,tile});
  }
  return targets;
}

function confirmedMoveAnimations(previous,next){
  if(!previous||!next||previous.roomId!==next.roomId||
      !previous.board||!next.board||previous.board.id!==next.board.id||
      previous.board.size!==next.board.size||previous.phase!==2||next.phase===2)return[];
  const before=new Map((previous.players||[]).map(player=>[player.id,player]));
  return(next.players||[]).filter(player=>{
    const prior=before.get(player.id);
    return prior&&!prior.bankrupt&&!player.bankrupt&&prior.position!==player.position;
  }).map(player=>({playerId:player.id,from:before.get(player.id).position,
    target:player.position,boardSize:next.board.size,roomId:next.roomId}));
}

function nextTokenPosition(current,boardSize){
  return(current+1)%boardSize;
}
// TESTABLE_FORCED_ROLL_HELPERS_END

let state=null;
let boardDefinition=null;
let refreshInFlight=false;
let actionInFlight=false;
let refreshController=null;
let pollTimer=0;
let pollDelay=1000;
let fullSyncDueAt=0;
const visualPlayerPositions=new Map();
const tokenAnimations=new Map();
const tokenStepMs=180;
let forcedSelection=null;
let contextPlayerId=0;
let serverEpochOffsetMs=0;

function hasIdentityFlag(player,flag){
  return!!(Number(player&&player.identityFlags||0)&flag);
}

function identitySeatLabel(player){
  if(hasIdentityFlag(player,32))return['READY','ready'];
  if(hasIdentityFlag(player,4))return['GENERATING','generating'];
  if(hasIdentityFlag(player,8)&&!hasIdentityFlag(player,16))return['NAME SETUP',''];
  return['AVATAR SETUP',''];
}

function identityReadyCount(identity){
  let count=0;
  const mask=Number(identity&&identity.readyMask||0);
  for(let bit=0;bit<6;bit+=1)if(mask&(1<<bit))count+=1;
  return count;
}

function renderIdentityWorkflow(next){
  if(!next||!next.identity||Number(next.identity.phase)===3)return false;
  const identity=next.identity;
  const playerCount=(next.players||[]).length;
  const ready=identityReadyCount(identity);
  if(Number(identity.phase)===2){
    const authorityNow=Date.now()+serverEpochOffsetMs;
    const remaining=Math.max(0,Number(identity.countdownDeadlineEpochMs||0)-authorityNow);
    $('#workflow').innerHTML=`<span>身份流程</span><b>所有席位已就绪</b><span>统一开局倒计时</span><b>${(remaining/1000).toFixed(1)} 秒</b><span>状态</span><b>倒计时不会因玩家断线暂停</b>`;
    return true;
  }
  const avatarCount=identityReadyCount({readyMask:identity.avatarFinalMask});
  const nameCount=identityReadyCount({readyMask:identity.nameFinalMask});
  $('#workflow').innerHTML=`<span>身份流程</span><b>Avatar → Name → Ready</b><span>头像完成</span><b>${avatarCount} / ${playerCount}</b><span>名字完成</span><b>${nameCount} / ${playerCount}</b><span>已就绪</span><b>${ready} / ${playerCount}</b>`;
  return true;
}

function boardPosition(index,count){
  const side=count/4;
  if(index===0)return[side+1,side+1];
  if(index<=side)return[side+1,side+1-index];
  if(index<=2*side)return[2*side+1-index,1];
  if(index<=3*side)return[1,index-2*side+1];
  return[index-3*side+1,side+1];
}

function projectState(next,definition){
  if(!definition||definition.roomId!==next.roomId||definition.board.id!==next.board.id||definition.board.size!==next.board.size){
    throw new Error('board projection mismatch');
  }
  next.events=(next.events||[]).map(event=>({seq:event[0],kind:event[1],actor:event[2],target:event[3],asset:event[4],amount:event[5]}));
  next.tiles=definition.tiles.map((tile,index)=>{
    const asset=tile[2]===255?null:(next.assets[tile[2]]||[0,0,0]);
    return {i:index,id:tile[0],kind:tile[1],asset:tile[2],price:tile[3],owner:asset?asset[0]:0,level:asset?asset[1]:0,mortgaged:!!(asset&&asset[2])};
  });
  return next;
}

async function ensureBoard(next,signal){
  if(boardDefinition&&boardDefinition.roomId===next.roomId&&boardDefinition.board.id===next.board.id&&boardDefinition.board.size===next.board.size)return;
  const response=await fetchWithTimeout(`/api/board?room=${next.roomId}`,{cache:'no-store',signal});
  if(!response.ok)throw new Error(`board HTTP ${response.status}`);
  const received=await response.json();
  if(received.roomId!==next.roomId||received.board.id!==next.board.id||received.board.size!==next.board.size||received.tiles.length!==next.board.size){
    throw new Error('board changed during sync');
  }
  boardDefinition=received;
}

function stopTokenAnimation(playerId){
  const animation=tokenAnimations.get(playerId);
  if(animation)clearTimeout(animation.timer);
  tokenAnimations.delete(playerId);
}

function stopAllTokenAnimations(){
  for(const playerId of tokenAnimations.keys())stopTokenAnimation(playerId);
}

function renderPlayerChips(){
  if(!state)return;
  document.querySelectorAll('#board .tile').forEach(tile=>{
    tile.classList.remove('active','has-players');
    const chips=tile.querySelector('.chips');
    if(chips)chips.replaceChildren();
  });
  state.players.filter(player=>!player.bankrupt).forEach(player=>{
    const position=visualPlayerPositions.has(player.id)?visualPlayerPositions.get(player.id):player.position;
    const tile=document.querySelector(`#board .tile[data-tile-index="${position}"]`);
    if(!tile)return;
    tile.classList.add('active','has-players');
    const chip=document.createElement('i');
    chip.className='chip';
    chip.style.setProperty('--player-color',playerColor(player.id));
    chip.textContent=player.id;
    tile.querySelector('.chips').appendChild(chip);
  });
}

function scheduleTokenStep(animation){
  animation.timer=setTimeout(()=>{
    const authoritative=state&&state.roomId===animation.roomId&&
      state.board.size===animation.boardSize&&state.players.find(player=>player.id===animation.playerId);
    if(!authoritative||authoritative.bankrupt||authoritative.position!==animation.target){
      stopTokenAnimation(animation.playerId);
      if(authoritative)visualPlayerPositions.set(animation.playerId,authoritative.position);
      renderPlayerChips();
      return;
    }
    const current=visualPlayerPositions.get(animation.playerId);
    const nextPosition=nextTokenPosition(Number.isInteger(current)?current:animation.from,
      animation.boardSize);
    visualPlayerPositions.set(animation.playerId,nextPosition);
    renderPlayerChips();
    if(nextPosition===animation.target){
      tokenAnimations.delete(animation.playerId);
      return;
    }
    scheduleTokenStep(animation);
  },tokenStepMs);
}

function startTokenAnimation(plan){
  stopTokenAnimation(plan.playerId);
  visualPlayerPositions.set(plan.playerId,plan.from);
  const animation={...plan,timer:0};
  tokenAnimations.set(plan.playerId,animation);
  scheduleTokenStep(animation);
}

function reconcileVisualPositions(previous,next){
  const sameBoard=previous&&previous.roomId===next.roomId&&previous.board&&
    previous.board.id===next.board.id&&previous.board.size===next.board.size;
  if(!sameBoard){
    stopAllTokenAnimations();
    visualPlayerPositions.clear();
  }
  const plans=new Map(confirmedMoveAnimations(previous,next).map(plan=>[plan.playerId,plan]));
  const present=new Set();
  next.players.forEach(player=>{
    present.add(player.id);
    const activeAnimation=tokenAnimations.get(player.id);
    if(activeAnimation&&activeAnimation.roomId===next.roomId&&
        activeAnimation.boardSize===next.board.size&&activeAnimation.target===player.position&&
        !player.bankrupt)return;
    if(activeAnimation)stopTokenAnimation(player.id);
    const plan=plans.get(player.id);
    if(plan)startTokenAnimation(plan);
    else visualPlayerPositions.set(player.id,player.position);
  });
  for(const playerId of visualPlayerPositions.keys()){
    if(!present.has(playerId)){
      stopTokenAnimation(playerId);
      visualPlayerPositions.delete(playerId);
    }
  }
}

function render(next){
  const previous=state;
  reconcileVisualPositions(previous,next);
  state=next;
  if(next.identity&&Number.isFinite(Number(next.identity.serverEpochMs))){
    serverEpochOffsetMs=Number(next.identity.serverEpochMs)-Date.now();
  }
  $('#status').textContent=next.wifi.connected?`在线 · ${next.wifi.ip}`:'Wi-Fi 未连接';
  $('#map').textContent=next.board.id;
  $('#round').textContent=next.round;
  const identityActive=!next.identity||Number(next.identity.phase)===3;
  $('#phase').textContent=identityActive?(phases[next.phase]||next.phase):
    (Number(next.identity.phase)===2?'开局倒计时':'身份设置');
  $('#active').textContent=identityActive?`P${next.activePlayer}`:'—';
  $('#decision').textContent=identityActive?`P${next.decisionPlayer}`:'—';
  $('#version').textContent=next.identity?`${next.version} · I${next.identity.revision}`:next.version;
  $('#peers').textContent=next.espnowPeers;
  if(renderIdentityWorkflow(next)){
    // Identity setup owns the workflow panel until the authority activates the game.
  }else if(next.card&&next.card.active&&next.card.stage===1){
    $('#workflow').innerHTML=`<span>卡片揭示</span><b>${next.card.deck===1?'Chance':'Community Chest'} #${next.card.catalog}</b><span>玩家</span><b>P${next.card.player}</b><span>状态</span><b>等待继续，尚未执行财务或移动效果</b>`;
  }else if(next.debt.active){
    $('#workflow').innerHTML=`<span>人工筹款</span><b>P${next.debt.debtor} 需支付 ¥${next.debt.amount} 给 ${next.debt.creditor?`P${next.debt.creditor}`:'系统'}</b><span>允许操作</span><b>出售建筑、抵押、交易、确认付款或破产</b>`;
  }else if(next.auction.active){
    if(next.auction.opening){
      $('#workflow').innerHTML=`<span>拍卖揭示</span><b>资产 A${next.auction.asset}</b><span>屏幕就绪</span><b>${maskBits(next.auction.readyMask)} / ${maskBits(next.auction.requiredReadyMask)}</b><span>状态</span><b>等待所有真人玩家</b>`;
    }else{
      $('#workflow').innerHTML=`<span>人工拍卖</span><b>资产 A${next.auction.asset}</b><span>当前价</span><b>¥${next.auction.currentBid}</b><span>最低出价</span><b>¥${next.auction.minimumBid}</b><span>等待</span><b>P${next.auction.bidder}</b>`;
      $('#bid').value=Math.max(Number($('#bid').value)||0,next.auction.minimumBid);
    }
  }else{
    $('#workflow').innerHTML='<span>事务</span><b>无待处理事务</b>';
  }

  const side=next.board.size/4;
  const board=$('#board');
  board.dataset.size=next.board.size;
  board.style.gridTemplateColumns=`repeat(${side+1},1fr)`;
  board.style.gridTemplateRows=`repeat(${side+1},1fr)`;
  const centerStatus=identityActive?`当前 P${next.activePlayer}`:
    (Number(next.identity.phase)===2?'所有玩家已就绪 · 即将开始':'等待真人玩家完成身份设置');
  board.innerHTML=`<div class="center"><div><b>GRIDOPOLY</b><small>${next.board.size} 格 · ${next.players.length} 位玩家 · ${centerStatus}</small></div></div>`;
  next.tiles.forEach(tile=>{
    const [row,column]=boardPosition(tile.i,next.board.size);
    const element=document.createElement('div');
    element.className='tile';
    element.dataset.tileIndex=tile.i;
    element.style.gridRow=row;
    element.style.gridColumn=column;
    const visual=tileVisual(tile.id);
    element.style.setProperty('--tile-color',visual[2]);
    const owner=ownerBadgeModel(tile,next.players);
    if(owner){
      element.classList.add('owned');
      element.style.setProperty('--player-color',owner.color);
    }
    const asset=tile.asset===255?'':`A${tile.asset} · ¥${tile.price} · L${tile.level}${tile.mortgaged?' · 抵押':''}`;
    const art=visual[1]?`<img class="tile-art" src="/assets/tiles/${visual[1]}" alt="" loading="lazy" decoding="async">`:'';
    element.title=`${String(tile.i).padStart(2,'0')} · ${visual[0]} · ${tile.id}${asset?` · ${asset}`:''}${owner?` · 所有者 ${owner.name}`:''}`;
    element.setAttribute('aria-label',element.title);
    element.dataset.baseTitle=element.title;
    const meta=asset?`<span class="meta">${asset}</span>`:'';
    const ownerBadge=owner?`<span class="owner-badge" title="${esc(owner.name)}" aria-label="所有者 ${esc(owner.name)}">${esc(owner.label)}</span>`:'';
    element.innerHTML=`${art}<span class="n">${String(tile.i).padStart(2,'0')} · ${esc(kinds[tile.kind]||tile.kind)}</span>${ownerBadge}<span class="id">${esc(visual[0])}<small>${esc(tile.id)}</small></span>${meta}<span class="chips"></span>`;
    board.appendChild(element);
  });
  renderPlayerChips();

  $('#players').innerHTML=next.players.map(player=>{
    const armed=next.forcedRoll&&next.forcedRoll.active&&next.forcedRoll.player===player.id;
    const mark=armed?`<span class="forced-player-mark">NEXT → ${String(next.forcedRoll.target).padStart(2,'0')}</span>`:'';
    const avatar=player.avatarUrl?`<img src="${esc(player.avatarUrl)}" alt="" loading="lazy" decoding="async">`:player.id;
    const displayName=String(player.name||'').trim()||`P${player.id}`;
    if(!identityActive){
      const identityState=identitySeatLabel(player);
      const role=hasIdentityFlag(player,2)?'BOT':'HUMAN';
      return`<div class="player" data-player-id="${player.id}" style="--player-color:${playerColor(player.id)}"><i class="badge">${avatar}</i><div><b>${esc(displayName)}</b><small style="display:block;color:var(--muted)">${role} · ${player.connected?'在线':'离线'}</small></div><span class="identity-state ${identityState[1]}">${identityState[0]}</span></div>`;
    }
    return`<div class="player ${player.id===next.activePlayer?'active':''} ${armed?'forced-armed':''}" data-player-id="${player.id}" tabindex="0" title="右键指定下一次目的地" style="--player-color:${playerColor(player.id)}"><i class="badge">${avatar}</i><div><b>${esc(displayName)}${mark}</b><small style="display:block;color:var(--muted)">${esc(player.controller)} · ${player.connected?'在线':'离线'} · 格 ${player.position}${player.held?' · 限制区':''}${player.bankrupt?' · 已破产':''}</small></div><span class="money">¥${player.cash}</span></div>`;
  }).join('');
  renderForcedRollSelection();
  $('#events').innerHTML=next.events.slice().reverse().map(event=>`<div class="event">#${event.seq} E${event.kind} · P${event.actor} → P${event.target} · A${event.asset} · ${event.amount}</div>`).join('');
  updateActionButtons();
}

function maskBits(mask){
  return Number(mask||0).toString(2).padStart(6,'0');
}

function updateActionButtons(){
  if(!state)return;
  document.querySelectorAll('[data-action]').forEach(button=>{
    button.disabled=actionInFlight||!(state.actions&actionBits[button.dataset.action]);
  });
}

function setBusy(busy){
  actionInFlight=busy;
  document.querySelectorAll('button,select,input').forEach(control=>{control.disabled=busy;});
  if(!busy){
    updateActionButtons();
    validateNewGameCounts(false);
  }
}

function validateNewGameCounts(announce=true){
  const humans=Number($('#humans').value);
  const bots=Number($('#bots').value);
  const valid=Number.isInteger(humans)&&Number.isInteger(bots)&&humans>=1&&bots>=0&&
    humans+bots>=2&&humans+bots<=6;
  $('#new').disabled=actionInFlight||!valid;
  if(announce){
    $('#status').textContent=valid?`建局配置：${humans} 真人 + ${bots} 机器人`:
      '总人数必须为 2–6，且至少有 1 名真人玩家';
    $('#status').className=valid?'':'error';
  }
  return valid;
}

function forcedRollSupported(){
  return!!(state&&state.forcedRoll&&Number.isInteger(state.controlVersion));
}

function playerCanScheduleDestination(player){
  return!!(player&&(!state.identity||Number(state.identity.phase)===3)&&
    !player.bankrupt&&!player.held&&
    (player.id!==state.activePlayer||state.phase===1));
}

function hidePlayerContextMenu(){
  $('#player-context-menu').hidden=true;
  contextPlayerId=0;
}

function openPlayerContextMenu(playerId,x,y){
  if(actionInFlight||!forcedRollSupported())return;
  const player=state.players.find(candidate=>candidate.id===playerId);
  if(!player)return;
  contextPlayerId=playerId;
  const menu=$('#player-context-menu');
  $('#player-context-title').textContent=`P${player.id} · ${player.name}`;
  const schedule=$('#player-force-destination');
  schedule.disabled=!playerCanScheduleDestination(player);
  schedule.textContent=player.bankrupt?'玩家已破产':player.held?'限制区内不可指定':
    player.id===state.activePlayer&&state.phase!==1?'当前阶段不可指定':'指定下一次目的地';
  const clear=$('#player-clear-destination');
  clear.hidden=!(state.forcedRoll.active&&state.forcedRoll.player===player.id);
  menu.hidden=false;
  menu.style.left='0px';
  menu.style.top='0px';
  const bounds=menu.getBoundingClientRect();
  menu.style.left=`${Math.max(8,Math.min(x,window.innerWidth-bounds.width-8))}px`;
  menu.style.top=`${Math.max(8,Math.min(y,window.innerHeight-bounds.height-8))}px`;
  if(!schedule.disabled)schedule.focus({preventScroll:true});
}

function cancelForcedRollSelection(){
  forcedSelection=null;
  $('#forced-selection-banner').hidden=true;
  document.querySelectorAll('#board .tile').forEach(tile=>{
    tile.classList.remove('forced-candidate');
    if(tile.dataset.baseTitle)tile.title=tile.dataset.baseTitle;
  });
}

function renderForcedRollSelection(){
  if(!state)return;
  document.querySelectorAll('#board .tile').forEach(tile=>{
    tile.classList.remove('forced-candidate','forced-target');
    if(tile.dataset.baseTitle)tile.title=tile.dataset.baseTitle;
  });
  if(state.forcedRoll&&state.forcedRoll.active){
    const armed=document.querySelector(`#board .tile[data-tile-index="${state.forcedRoll.target}"]`);
    if(armed)armed.classList.add('forced-target');
  }
  if(!forcedSelection){
    $('#forced-selection-banner').hidden=true;
    return;
  }
  const player=state.players.find(candidate=>candidate.id===forcedSelection.playerId);
  if(!playerCanScheduleDestination(player)){
    cancelForcedRollSelection();
    $('#status').textContent='玩家状态已变化，请重新选择';
    $('#status').className='error';
    return;
  }
  const targets=forcedRollTargets(player,state.tiles,state.board.size);
  forcedSelection.targets=new Map(targets.map(entry=>[entry.target,entry]));
  for(const entry of targets){
    const tile=document.querySelector(`#board .tile[data-tile-index="${entry.target}"]`);
    if(!tile)continue;
    tile.classList.add('forced-candidate');
    tile.title=`${tile.dataset.baseTitle} · 点击指定 · +${entry.steps}`;
  }
  $('#forced-selection-text').textContent=
    `为 ${player.name} 选择目的地 · 蓝色格均为合法骰距 +2～+12`;
  $('#forced-selection-banner').hidden=false;
}

function beginForcedRollSelection(playerId){
  hidePlayerContextMenu();
  const player=state&&state.players.find(candidate=>candidate.id===playerId);
  if(!forcedRollSupported()||!playerCanScheduleDestination(player))return;
  forcedSelection={playerId,targets:new Map()};
  renderForcedRollSelection();
  $('#board').scrollIntoView({behavior:'smooth',block:'center'});
}

async function submitForcedRollTarget(target){
  if(actionInFlight||!forcedSelection||!forcedSelection.targets.has(target))return;
  const player=forcedSelection.playerId;
  let succeeded=false;
  setBusy(true);
  $('#status').textContent='正在设置下一次目的地…';
  $('#status').className='';
  try{
    const response=await fetchWithTimeout(
      `/api/forced-roll?player=${player}&target=${target}&expected=${state.version}`,
      {method:'POST',cache:'no-store'},8000);
    const result=await response.json();
    if(!response.ok)throw new Error(result.message||result.error||`HTTP ${response.status}`);
    succeeded=true;
    forcedSelection=null;
    $('#status').textContent=`下一次目的地已设置 · P${player} → 格 ${String(target).padStart(2,'0')}`;
    $('#status').className='';
  }catch(error){
    $('#status').textContent=`设置失败：${error.name==='AbortError'?'请求超时':error.message}`;
    $('#status').className='error';
  }finally{
    setBusy(false);
    await refresh(true);
    if(!succeeded)renderForcedRollSelection();
  }
}

async function clearForcedRollTarget(){
  if(actionInFlight||!forcedRollSupported()||!state.forcedRoll.active)return;
  hidePlayerContextMenu();
  cancelForcedRollSelection();
  setBusy(true);
  $('#status').textContent='正在取消指定目的地…';
  $('#status').className='';
  try{
    const response=await fetchWithTimeout('/api/forced-roll?cancel=1',
      {method:'POST',cache:'no-store'},8000);
    const result=await response.json();
    if(!response.ok)throw new Error(result.message||result.error||`HTTP ${response.status}`);
    $('#status').textContent='已取消指定目的地';
  }catch(error){
    $('#status').textContent=`取消失败：${error.name==='AbortError'?'请求超时':error.message}`;
    $('#status').className='error';
  }finally{
    setBusy(false);
    await refresh(true);
  }
}

async function openSettings(){
  if(actionInFlight)return;
  $('#settings-modal').hidden=false;
  $('#settings-hint').textContent='正在读取当前设置…';
  $('#settings-hint').className='settings-hint';
  try{
    const response=await fetchWithTimeout('/api/settings',{cache:'no-store'});
    const settings=await response.json();
    if(!response.ok)throw new Error(settings.error||`HTTP ${response.status}`);
    $('#bot-interval').min=settings.minimumMs;
    $('#bot-interval').max=settings.maximumMs;
    $('#bot-interval').value=settings.botActionIntervalMs;
    $('#settings-hint').textContent=`当前 ${settings.botActionIntervalMs} ms；允许 ${settings.minimumMs}–${settings.maximumMs} ms。`;
    $('#bot-interval').focus();
    $('#bot-interval').select();
  }catch(error){
    $('#settings-hint').textContent=`读取失败：${error.name==='AbortError'?'请求超时':error.message}`;
    $('#settings-hint').className='settings-hint error';
  }
}

function closeSettings(){
  if(actionInFlight)return;
  $('#settings-modal').hidden=true;
}

async function saveSettings(){
  if(actionInFlight)return;
  const input=$('#bot-interval');
  const interval=Number(input.value);
  const minimum=Number(input.min);
  const maximum=Number(input.max);
  if(!Number.isInteger(interval)||interval<minimum||interval>maximum){
    $('#settings-hint').textContent=`请输入 ${minimum}–${maximum} 之间的整数毫秒数。`;
    $('#settings-hint').className='settings-hint error';
    input.focus();
    return;
  }
  setBusy(true);
  $('#settings-hint').textContent='正在保存…';
  $('#settings-hint').className='settings-hint';
  try{
    const response=await fetchWithTimeout(`/api/settings?botIntervalMs=${interval}`,{method:'POST',cache:'no-store'},8000);
    const result=await response.json();
    if(!response.ok)throw new Error(result.error||`HTTP ${response.status}`);
    $('#status').textContent=`机器人动作间隔已设为 ${result.botActionIntervalMs} ms`;
    $('#status').className='';
    $('#settings-modal').hidden=true;
  }catch(error){
    $('#settings-hint').textContent=`保存失败：${error.name==='AbortError'?'请求超时':error.message}`;
    $('#settings-hint').className='settings-hint error';
  }finally{
    setBusy(false);
    scheduleRefresh(0);
  }
}

function scheduleRefresh(delay=pollDelay){
  clearTimeout(pollTimer);
  pollTimer=setTimeout(refresh,document.hidden?5000:delay);
}

window.addEventListener('pagehide',()=>{
  try{navigator.sendBeacon('/api/web-detach');}catch(_error){}
});

async function fetchWithTimeout(path,options={},timeoutMs=6000){
  const controller=new AbortController();
  const timeout=setTimeout(()=>controller.abort(),timeoutMs);
  if(options.signal)options.signal.addEventListener('abort',()=>controller.abort(),{once:true});
  try{
    return await fetch(path,{...options,signal:controller.signal});
  }finally{
    clearTimeout(timeout);
  }
}

async function refresh(force=false){
  if(refreshInFlight||actionInFlight){scheduleRefresh(250);return;}
  refreshInFlight=true;
  refreshController=new AbortController();
  try{
    const requireFull=force||Date.now()>=fullSyncDueAt;
    const query=!requireFull&&state?`?since=${state.version}&peers=${state.espnowPeers}&room=${state.roomId}&network=${state.network}&control=${state.controlVersion||0}&identity=${state.identity?state.identity.revision:0}`:'';
    const response=await fetchWithTimeout(`/api/sync${query}`,{cache:'no-store',signal:refreshController.signal});
    if(response.status===204){
      if(state&&state.identity&&Number(state.identity.phase)===2)renderIdentityWorkflow(state);
      pollDelay=state&&state.identity&&Number(state.identity.phase)===2?250:1000;
      $('#status').textContent=state&&state.wifi.connected?`在线 · ${state.wifi.ip}`:'Wi-Fi 未连接';
      $('#status').className='';
      return;
    }
    if(!response.ok)throw new Error(`HTTP ${response.status}`);
    const next=await response.json();
    if(next.schema!==2)throw new Error('unsupported sync schema');
    await ensureBoard(next,refreshController.signal);
    render(projectState(next,boardDefinition));
    fullSyncDueAt=Date.now()+30000;
    pollDelay=next.identity&&Number(next.identity.phase)===2?250:1000;
    $('#status').className='';
  }catch(error){
    if(error.name==='AbortError'&&actionInFlight)return;
    $('#status').textContent='连接断开';
    $('#status').className='error';
    pollDelay=Math.min(5000,Math.max(1500,pollDelay*2));
  }finally{
    refreshController=null;
    refreshInFlight=false;
    if(!actionInFlight)scheduleRefresh();
  }
}

async function send(path){
  if(actionInFlight||!state)return;
  clearTimeout(pollTimer);
  setBusy(true);
  if(refreshController)refreshController.abort();
  $('#status').textContent='正在提交操作…';
  $('#status').className='';
  try{
    const response=await fetchWithTimeout(path,{method:'POST',cache:'no-store'},8000);
    const result=await response.json();
    if(!response.ok)throw new Error(result.message||result.error||`HTTP ${response.status}`);
    $('#status').textContent=`操作已接受 · 状态 ${result.version}`;
  }catch(error){
    $('#status').textContent=`操作失败：${error.name==='AbortError'?'请求超时':error.message}`;
    $('#status').className='error';
  }finally{
    setBusy(false);
    await refresh(true);
  }
}

document.querySelectorAll('[data-action]').forEach(button=>button.onclick=()=>{
  const argument=button.dataset.action==='bid'?`&arg=${$('#bid').value}`:button.dataset.action==='cardcontinue'&&state.card?`&arg=${state.card.instance}`:'';
  send(`/api/action?action=${button.dataset.action}&player=${state.decisionPlayer}&asset=${$('#asset').value}&expected=${state.version}${argument}`);
});
$('#humans').onchange=()=>validateNewGameCounts();
$('#bots').onchange=()=>validateNewGameCounts();
$('#new').onclick=()=>{
  if(!validateNewGameCounts())return;
  send(`/api/new?size=${$('#size').value}&humans=${$('#humans').value}&bots=${$('#bots').value}`);
};
$('#settings-open').onclick=openSettings;
$('#settings-cancel').onclick=closeSettings;
$('#settings-save').onclick=saveSettings;
$('#players').addEventListener('contextmenu',event=>{
  const card=event.target.closest('.player[data-player-id]');
  if(!card)return;
  event.preventDefault();
  event.stopPropagation();
  openPlayerContextMenu(Number(card.dataset.playerId),event.clientX,event.clientY);
});
$('#players').addEventListener('keydown',event=>{
  if(event.key!=='ContextMenu'&&!(event.shiftKey&&event.key==='F10'))return;
  const card=event.target.closest('.player[data-player-id]');
  if(!card)return;
  event.preventDefault();
  const bounds=card.getBoundingClientRect();
  openPlayerContextMenu(Number(card.dataset.playerId),bounds.left+24,bounds.top+24);
});
$('#player-force-destination').onclick=()=>beginForcedRollSelection(contextPlayerId);
$('#player-clear-destination').onclick=clearForcedRollTarget;
$('#forced-selection-cancel').onclick=cancelForcedRollSelection;
$('#board').addEventListener('click',event=>{
  const tile=event.target.closest('.tile[data-tile-index]');
  if(!tile||!tile.classList.contains('forced-candidate'))return;
  submitForcedRollTarget(Number(tile.dataset.tileIndex));
});
$('#settings-modal').onclick=event=>{if(event.target===$('#settings-modal'))closeSettings();};
$('#bot-interval').addEventListener('keydown',event=>{if(event.key==='Enter')saveSettings();});
document.addEventListener('pointerdown',event=>{
  if(!event.target.closest('#player-context-menu'))hidePlayerContextMenu();
});
document.addEventListener('keydown',event=>{
  if(event.key!=='Escape')return;
  if(forcedSelection)cancelForcedRollSelection();
  else if(!$('#player-context-menu').hidden)hidePlayerContextMenu();
  else if(!$('#settings-modal').hidden)closeSettings();
});
window.addEventListener('resize',hidePlayerContextMenu);
document.addEventListener('visibilitychange',()=>{if(!document.hidden)scheduleRefresh(0);});
window.addEventListener('beforeunload',()=>{clearTimeout(pollTimer);if(refreshController)refreshController.abort();stopAllTokenAnimations();});
scheduleRefresh(0);
</script>
</body>
</html>)GRIDOPOLY_HTML";

}  // namespace gridopoly::server
