// Run with Playwright on NODE_PATH; all HTTP is intercepted, no real game changes.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {chromium}=require('playwright');

(async()=>{
  const source=fs.readFileSync(path.join(__dirname,'../src/WebUi.h'),'utf8');
  const html=source.match(/R"GRIDOPOLY_HTML\(([\s\S]*?)\)GRIDOPOLY_HTML";/)[1];
  const executablePath=process.env.GRIDOPOLY_BROWSER;
  const browser=await chromium.launch({headless:true,...(executablePath?{executablePath}:{})});
  try{
    const page=await browser.newPage({viewport:{width:1400,height:1000}});
    const errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    let devices=[1,2].map(id=>({deviceId:id,displayName:`Screen ${id}`,seatId:id,
      online:true,eligible:true,capabilities:3}));
    let next={schema:2,version:1,roomId:7,network:1,controlVersion:1,tagBindingRevision:1,
      phase:1,round:1,activePlayer:1,decisionPlayer:1,actions:0,espnowPeers:2,
      bankInventory:{buildings:24,landmarks:9},
      wifi:{connected:true,ip:'10.42.0.1'},board:{id:'grid-city-32-v1',size:32},
      debt:{active:false},auction:{active:false},card:{active:false},forcedRoll:{active:false},
      identity:{phase:1,revision:1,readyMask:12,avatarFinalMask:12,nameFinalMask:12,
        humanCount:2,botCount:2,serverEpochMs:Date.now()},
      tagPairing:{enabled:true,roomId:7,revision:1,sessionId:11,state:'awaiting_tag',
        activePlayerId:1,moduleId:'tile-corner-01',readerMapIndex:8,requiredHumanMask:3,
        boundMask:0,identityEditableMask:0,canRetry:true},
      players:[1,2,3,4].map(id=>({id,name:id<3?'':`Bot ${id-2}`,controller:id<3?'real':'bot',
        connected:true,cash:800,position:0,identityFlags:id<3?1:58,tagUid:'',avatarUrl:''})),
      events:[],assets:[]};
    const posts=[];
    let newGameFailure='';
    const json=(route,value,status=200)=>route.fulfill({status,contentType:'application/json',body:JSON.stringify(value)});
    await page.route('http://gridopoly.test/**',async route=>{
      const url=new URL(route.request().url());
      if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:html});
      if(url.pathname==='/api/player-devices')return json(route,{devices,maxPlayers:4});
      if(url.pathname==='/api/sync')return json(route,next);
      if(url.pathname==='/api/board')return json(route,{roomId:next.roomId,board:next.board,
        tiles:Array.from({length:32},(_,i)=>[i%8===0?['CORNER-START','CORNER-HOLD','CORNER-REST','CORNER-GOTO'][i/8]:'A1',i%8===0?0:1,255,0])});
      if(url.pathname==='/api/tile-debug/assignments')return json(route,{roomId:next.roomId,
        boardSize:32,boardId:next.board.id,revision:1,updatedAtMs:0,modules:[],tiles:[],assignments:[]});
      if(url.pathname==='/api/new'){
        posts.push(url);
        if(newGameFailure)return json(route,{ok:false,message:newGameFailure},409);
        next.roomId+=1;next.tagPairing.roomId=next.roomId;
        return json(route,{ok:true,version:next.version});
      }
      if(url.pathname==='/api/tag-pairing'){
        posts.push(url);
        next.tagPairing.revision+=1;
        next.tagPairing.moduleId='tile-long-02';
        next.tagPairing.readerMapIndex=9;
        return json(route,next.tagPairing);
      }
      if(url.pathname==='/api/action'){
        posts.push(url);
        return json(route,{ok:true,version:next.version});
      }
      if(url.pathname.startsWith('/assets/tiles/')){
        const file=path.resolve(__dirname,'../../../Assets/GridCity/StreetV3/device',path.basename(url.pathname));
        if(fs.existsSync(file))return route.fulfill({contentType:'image/png',body:fs.readFileSync(file)});
      }
      return route.fulfill({status:404,body:'fixture unavailable'});
    });
    await page.goto('http://gridopoly.test/');
    await page.waitForFunction(()=>document.querySelector('#newgame-roster').textContent.includes('2 名真人'));
    assert.equal(await page.locator('#new').isEnabled(),true);
    assert.match(await page.locator('#bank-inventory').innerText(),/建筑 24 · 地标 9/);
    assert.match(await page.locator('#workflow').innerText(),/请把本人的棋子/);
    assert.equal(await page.locator('.pairing-target').count(),1);
    assert.equal(await page.locator('.pairing-target').getAttribute('data-tile-index'),'8');
    await page.locator('#new').click();
    await page.waitForFunction(()=>!actionInFlight);
    assert.equal(posts[0].searchParams.get('autoPlayers'),'1');
    assert.equal(posts[0].searchParams.has('humans'),false);
    assert.equal(posts[0].searchParams.has('bots'),false);
    assert.match(await page.locator('#newgame-result').innerText(),/新游戏已创建/);
    await page.locator('#pairing-retry').click();
    await page.waitForFunction(()=>document.querySelector('.pairing-target')?.dataset.tileIndex==='9');
    assert.equal(posts[1].searchParams.get('operation'),'retry');
    assert.equal(posts[1].searchParams.get('roomId'),'8');
    assert.equal(posts[1].searchParams.get('sessionId'),'11');
    assert.equal(posts[1].searchParams.get('expectedRevision'),'1');
    assert.notEqual(Number(posts[1].searchParams.get('requestId')),0);

    for(const [status,message] of [['already_bound','已属于其他玩家'],['multiple_tags','只放入一个'],
      ['success_remove','请拿回自己的棋子'],['reader_offline','配对格子已离线']]){
      next.tagPairing.state=status;next.tagPairing.revision+=1;
      await page.evaluate(()=>refresh(true));
      assert.match(await page.locator('#workflow').innerText(),new RegExp(message));
    }
    next.tagPairing.state='awaiting_tag';next.tagPairing.boundMask=1;
    next.tagPairing.identityEditableMask=1;next.tagPairing.activePlayerId=2;
    next.players[0].tagUid='12345678';
    await page.evaluate(()=>refresh(true));
    assert.match(await page.locator('[data-player-id="1"] .identity-state').innerText(),/AVATAR/);
    assert.match(await page.locator('[data-player-id="2"] .identity-state').innerText(),/配对中/);
    next.phase=7;next.winnerPlayerId=2;next.identity.phase=3;next.players[1].name='Robin';
    next.tagPairing.state='complete';next.tagPairing.canRetry=false;
    await page.evaluate(()=>refresh(true));
    assert.match(await page.locator('#workflow').innerText(),/Robin 获胜/);
    assert.equal(await page.locator('#pairing-actions').isVisible(),false);
    assert.equal(await page.locator('.pairing-target').count(),0);
    assert.equal(await page.locator('[data-action="roll"]').isDisabled(),true);

    next.phase=5;next.actions=256;next.bankInventory.buildings=0;
    await page.evaluate(()=>refresh(true));
    assert.match(await page.locator('#bank-inventory').innerText(),/建筑 0 · 地标 9/);
    const priorPosts=posts.length;
    page.once('dialog',dialog=>dialog.dismiss());
    await page.locator('[data-action="sellgroup"]').click();
    assert.equal(posts.length,priorPosts,'cancelled group sale must not send a mutation');
    page.once('dialog',async dialog=>{
      assert.match(dialog.message(),/整个地区组的全部建筑和地标/);
      await dialog.accept();
    });
    await page.locator('[data-action="sellgroup"]').click();
    await page.waitForFunction(()=>!actionInFlight);
    assert.equal(posts.at(-1).searchParams.get('action'),'sell');
    assert.equal(posts.at(-1).searchParams.get('arg'),'1');
    assert.equal(posts.at(-1).searchParams.get('expected'),'1');

    // A rejected creation must remain visible after the automatic state refresh.
    // Otherwise the screen stays in the old room while the web UI just says online.
    const previousRoom=next.roomId;
    newGameFailure='no_eligible_players';
    await page.locator('#size').selectOption('24');
    await page.locator('#new').click();
    await page.waitForFunction(()=>!actionInFlight&&!refreshInFlight);
    assert.equal(posts.at(-1).searchParams.get('size'),'24');
    assert.equal(next.roomId,previousRoom);
    assert.match(await page.locator('#newgame-result').innerText(),/新建游戏失败.*在线玩家屏/);
    await page.evaluate(()=>refresh(true));
    assert.match(await page.locator('#newgame-result').innerText(),/新建游戏失败.*在线玩家屏/);
    newGameFailure='';
    await page.locator('#new').click();
    await page.waitForFunction(()=>!actionInFlight&&!refreshInFlight);
    assert.equal(next.roomId,previousRoom+1);
    assert.match(await page.locator('#newgame-result').innerText(),/新游戏已创建/);
    assert.equal(await page.locator('#newgame-result').getAttribute('class'),'');

    devices.push({deviceId:3,online:true,eligible:true},{deviceId:4,online:true,eligible:true},
      {deviceId:5,online:true,eligible:true});
    await page.evaluate(()=>{playerDevicesDueAt=0;return refresh(true);});
    assert.equal(await page.locator('#new').isDisabled(),true);
    await page.setViewportSize({width:390,height:844});
    const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>window.innerWidth);
    assert.equal(overflow,false,'mobile page must not overflow horizontally');
    if(process.env.GRIDOPOLY_SCREENSHOT)await page.screenshot({path:process.env.GRIDOPOLY_SCREENSHOT,fullPage:true});
    assert.deepEqual(errors,[]);
    console.log('Onboarding browser regression PASS (intercepted HTTP, no hardware)');
  }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
