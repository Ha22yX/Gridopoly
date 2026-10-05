const fs=require('fs'),crypto=require('crypto');
const t=__dirname+'/',r=require('path').resolve(__dirname,'../Layout-2026-09-27-RC5')+'/';
const j=p=>JSON.parse(fs.readFileSync(p,'utf8'));
const stable=x=>JSON.stringify(x,(k,v)=>v&&typeof v==='object'&&!Array.isArray(v)?Object.fromEntries(Object.entries(v).sort(([a],[b])=>a.localeCompare(b))):v);
const a=j(t+'current-snapshot.json'),b=j(r+'final-live-audit.json');
const changes={};for(const k of ['components','lines','vias']){const normal=v=>(v||[]).map(x=>stable(x)).sort();const aa=normal(a[k]),bb=normal(b[k]);changes[k]={current:aa.length,frozen:bb.length,equal:stable(aa)===stable(bb),added:aa.filter(x=>!bb.includes(x)).slice(0,4),removed:bb.filter(x=>!aa.includes(x)).slice(0,4)}}
const nets=f=>Object.entries(j(f).components).map(([uid,c])=>({uid,ref:c.props.Designator,props:c.props,pins:Object.fromEntries(Object.entries(c.pinInfoMap).map(([p,v])=>[p,v.net||'']))})).sort((a,b)=>a.uid.localeCompare(b.uid));
for(const n of ['PCB1-netlist.json','schematic-netlist.json'])changes[n]={equal:stable(nets(t+n))===stable(nets(r+n))};
for(const [n,o]of [['native-layout-geometry.json','native-layout-geometry.json'],['PCB1.source.jsonl','PCB1-routed.source.jsonl'],['P1.source.jsonl','P1-power.source.jsonl'],['P2.source.jsonl','P2-usb-interfaces.source.jsonl']]){
 changes[n]={bytesEqual:fs.readFileSync(t+n).equals(fs.readFileSync(r+o))};
 if(n.endsWith('.jsonl')){
  const normalized=p=>fs.readFileSync(p,'utf8').trim().split(/\r?\n/).map(l=>{const[h,v]=l.split('||');const hd=JSON.parse(h);delete hd.ticket;return {h:hd,v:JSON.parse(v.replace(/\|$/,''))}}).filter(x=>x.h.type!=='DOCHEAD').map(x=>stable(x)).sort();
  changes[n].samePrimitivesIgnoringDocheadAndTicket=stable(normalized(t+n))===stable(normalized(r+o));
 }
}
changes.pours={current:a.pours.length,note:'Older snapshot has no pours field. Both pours are included in the identical PCB source primitive comparison above.'};
const dif=[];for(const [uid,c]of Object.entries(j(t+'schematic-netlist.json').components)){const p=j(t+'PCB1-netlist.json').components[uid],live=a.components.find(z=>z.uid===uid);for(const[n,v]of Object.entries(c.pinInfoMap)){if(!p||p.pinInfoMap[n]?.net!==v.net||!live?.pads.some(x=>x.padNumber===n)||live.pads.filter(x=>x.padNumber===n).some(x=>(x.net||'')!==(v.net||'')))dif.push([c.props.Designator,n,v.net])}}
const passed=dif.length===0&&['components','lines','vias','PCB1-netlist.json','schematic-netlist.json'].every(k=>changes[k].equal)&&changes['native-layout-geometry.json'].bytesEqual&&['PCB1.source.jsonl','P1.source.jsonl','P2.source.jsonl'].every(k=>changes[k].samePrimitivesIgnoringDocheadAndTicket);
const result={at:new Date().toISOString(),passed,changes,schPcbPadDifferences:dif};fs.writeFileSync(t+'current-vs-rc5.json',JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));if(!passed)process.exitCode=1;
