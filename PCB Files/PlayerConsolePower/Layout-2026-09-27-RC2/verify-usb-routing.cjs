const fs=require('fs'),path=require('path'),crypto=require('crypto');
const here=__dirname, prior=path.join(here,'../Layout-2026-09-27');
const a=JSON.parse(fs.readFileSync(path.join(prior,'final-live-audit.json'),'utf8'));
const b=JSON.parse(fs.readFileSync(path.join(here,'final-live-audit.json'),'utf8'));
const checks=[];const check=(name,passed,details)=>checks.push({name,passed,...(details?{details}:{})});
const usb=['USB_HOST_DM','SCREEN_DM'];
const oldLines=new Map(a.lines.map(x=>[x.primitiveId,x]));
const newLines=new Map(b.lines.map(x=>[x.primitiveId,x]));
const changedLines=[...a.lines.filter(l=>!newLines.has(l.primitiveId)||JSON.stringify(l)!==JSON.stringify(newLines.get(l.primitiveId))),...b.lines.filter(l=>!oldLines.has(l.primitiveId))];
check('Only D-minus traces changed',changedLines.every(l=>usb.includes(l.net)));
check('All 68 component placements and pad networks unchanged',a.components.length===b.components.length&&a.components.every(c=>JSON.stringify(c)===JSON.stringify(b.components.find(x=>x.uid===c.uid))));
check('All original GND and non-USB vias retained unchanged',a.vias.filter(v=>!usb.includes(v.net)).every(v=>JSON.stringify(v)===JSON.stringify(b.vias.find(x=>x.primitiveId===v.primitiveId))));
const addedGround=b.vias.filter(v=>v.net==='GND'&&!a.vias.some(x=>x.primitiveId===v.primitiveId));
check('Five additional GND stitching vias',addedGround.length===5);
check('No D-plus vias added',b.vias.filter(v=>['USB_HOST_DP','SCREEN_DP'].includes(v.net)).length===0);
check('Two host and two screen D-minus vias',usb.every(n=>b.vias.filter(v=>v.net===n).length===2));
check('Native strict DRC is empty',Array.isArray(b.drc)&&b.drc.length===0);
const lengths=live=>Object.fromEntries(['USB_HOST_DP','USB_HOST_DM','SCREEN_DP','SCREEN_DM'].map(net=>[net,{vias:live.vias.filter(v=>v.net===net).length,totalCopperMm:live.lines.filter(l=>l.net===net).reduce((s,l)=>s+Math.hypot(l.endX-l.startX,l.endY-l.startY)*.0254,0),bottomCopperMm:live.lines.filter(l=>l.net===net&&l.layer===2).reduce((s,l)=>s+Math.hypot(l.endX-l.startX,l.endY-l.startY)*.0254,0)}]));
const pad=(live,r,n)=>live.components.find(c=>c.ref===r).pads.find(p=>p.padNumber===n);
function route(live,net,from,to){
  const nodes=[],edges=[];
  function node(x,y,z){let i=nodes.findIndex(p=>p.z===z&&Math.hypot(p.x-x,p.y-y)<.005);if(i<0){i=nodes.length;nodes.push({x,y,z});edges.push([]);}return i;}
  function edge(i,j,planar,vertical){edges[i].push({j,w:planar+vertical,planar,vertical});edges[j].push({j:i,w:planar+vertical,planar,vertical});}
  for(const l of live.lines.filter(l=>l.net===net)){const x=l.startX*.0254,y=l.startY*.0254,xx=l.endX*.0254,yy=l.endY*.0254;edge(node(x,y,l.layer),node(xx,yy,l.layer),Math.hypot(xx-x,yy-y),0);}
  for(const v of live.vias.filter(v=>v.net===net))edge(node(v.x*.0254,v.y*.0254,1),node(v.x*.0254,v.y*.0254,2),0,1.6);
  const start=node(from.x*.0254,from.y*.0254,1),goal=node(to.x*.0254,to.y*.0254,1),dist=nodes.map(()=>Infinity),done=new Set(),prev=[];dist[start]=0;
  while(done.size<nodes.length){let u=-1;for(let i=0;i<nodes.length;i++)if(!done.has(i)&&(u<0||dist[i]<dist[u]))u=i;if(u<0||!isFinite(dist[u]))break;done.add(u);if(u===goal)break;for(const e of edges[u])if(dist[u]+e.w<dist[e.j]){dist[e.j]=dist[u]+e.w;prev[e.j]={u,...e};}}
  if(!isFinite(dist[goal]))throw Error('Disconnected measurement graph '+net);
  let planar=0,vertical=0,u=goal;while(u!==start){const e=prev[u];planar+=e.planar;vertical+=e.vertical;u=e.u;}
  return{planarMm:planar,viaTransitions:Math.round(vertical/1.6),geometricMmIncludingNominalVias:planar+vertical};
}
const paths=live=>({J1_A6_to_U7_8:route(live,'USB_HOST_DP',pad(live,'J1','A6'),pad(live,'U7','8')),J1_B6_to_U7_8:route(live,'USB_HOST_DP',pad(live,'J1','B6'),pad(live,'U7','8')),J1_A7_to_U7_7:route(live,'USB_HOST_DM',pad(live,'J1','A7'),pad(live,'U7','7')),J1_B7_to_U7_7:route(live,'USB_HOST_DM',pad(live,'J1','B7'),pad(live,'U7','7')),U7_1_to_J2_2:route(live,'SCREEN_DP',pad(live,'U7','1'),pad(live,'J2','2')),U7_2_to_J2_1:route(live,'SCREEN_DM',pad(live,'U7','2'),pad(live,'J2','1'))});
const before=lengths(a),after=lengths(b),beforePaths=paths(a),afterPaths=paths(b);
check('Host bottom crossing reduced below 1.01 mm',after.USB_HOST_DM.bottomCopperMm<1.01&&after.USB_HOST_DM.bottomCopperMm<before.USB_HOST_DM.bottomCopperMm);
check('Screen bottom crossing reduced below 1.56 mm',after.SCREEN_DM.bottomCopperMm<1.56&&after.SCREEN_DM.bottomCopperMm<before.SCREEN_DM.bottomCopperMm);
check('B7 path no longer changes layer',beforePaths.J1_B7_to_U7_7.viaTransitions===2&&afterPaths.J1_B7_to_U7_7.viaTransitions===0);
const out={scope:'Native geometry comparison and centerline graph. 1.6 mm per via is geometric approximation, not propagation delay, impedance or eye-diagram evidence.',before,after,beforePaths,afterPaths,changes:{removedLines:a.lines.filter(l=>!newLines.has(l.primitiveId)).map(l=>l.primitiveId),addedLines:b.lines.filter(l=>!oldLines.has(l.primitiveId)).map(l=>l.primitiveId),addedGroundVias:addedGround.map(v=>({id:v.primitiveId,xMm:v.x*.0254,yDownMm:-v.y*.0254}))},checks,passed:checks.filter(c=>c.passed).length,failed:checks.filter(c=>!c.passed).length,inputSha256:{rc1:crypto.createHash('sha256').update(fs.readFileSync(path.join(prior,'final-live-audit.json'))).digest('hex'),rc2:crypto.createHash('sha256').update(fs.readFileSync(path.join(here,'final-live-audit.json'))).digest('hex')}};
fs.writeFileSync(path.join(here,'usb-routing-review.json'),JSON.stringify(out,null,2)+'\n');console.log(JSON.stringify(out,null,2));if(out.failed)process.exitCode=1;
