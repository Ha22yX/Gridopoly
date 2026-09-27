'use strict';
// Read-only offline verification. Run with Node.js; no packages, EDA bridge, network or devices required.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const root=__dirname;
const read=n=>JSON.parse(fs.readFileSync(path.join(root,n),'utf8').replace(/^\uFEFF/,''));
const hash=n=>crypto.createHash('sha256').update(fs.readFileSync(path.join(root,n))).digest('hex');
function records(name){return fs.readFileSync(path.join(root,name),'utf8').split(/\r?\n/).filter(x=>x.trim()).map(x=>{const at=x.indexOf('||');if(at<0)throw Error('Invalid native source record');return {h:JSON.parse(x.slice(0,at)),v:JSON.parse(x.slice(at+2).replace(/\|$/,''))};});}
const pages=['P1-power.source.jsonl','P2-usb-interfaces.source.jsonl'].map(records);
const attrs=pages.map(rs=>{const m={};for(const {h,v}of rs)if(h.type==='ATTR'){m[v.parentId]??={};m[v.parentId][v.key]=v.value;}return m;});
const allSymbols=pages.flatMap((rs,i)=>rs.filter(r=>r.h.type==='COMPONENT'&&attrs[i][r.h.id]?.Designator).map(r=>({id:r.h.id,...attrs[i][r.h.id]})));
const native=read('native-netlist.json');const nets=Object.fromEntries(Object.values(native.components).map(p=>[p.props.Designator,p]));
const parts=Object.fromEntries(allSymbols.map(s=>{const p=nets[s.Designator]?.props??s;return[s.Designator,{supplierId:p['Supplier Part'],footprint:{name:p.FootprintName},otherProperty:p}]}));
const expected=read('expected-connectivity.json');const pin=(r,p)=>nets[r]?.pinInfoMap[p]?.net;
const failures=[];let connected=0,nc=0;const desiredRefs=[];
for(const page of Object.values(expected.pages))for(const [ref,e]of Object.entries(page.components)){
 if(ref==='J2')continue;desiredRefs.push(ref);if(!nets[ref]){failures.push({ref,error:'missing component'});continue;}
 for(const [p,wanted]of Object.entries(e.pins)){const actual=pin(ref,p);if(actual!==(wanted===null?'':wanted))failures.push({ref,p,wanted,actual});else if(wanted===null)nc++;else connected++;}
 for(const p of Object.keys(nets[ref].pinInfoMap))if(!(p in e.pins))failures.push({ref,p,error:'unexpected pin'});
}
for(const ref of Object.keys(nets))if(!desiredRefs.includes(ref))failures.push({ref,error:'unexpected netlist component'});
const geometry=read('j2-pin-geometry.json');if(geometry.sourceSha256!==hash('P2-usb-interfaces.source.jsonl'))failures.push({error:'J2 geometry is not bound to this source snapshot; recapture through EDA API'});
const wireIds=new Set(pages[1].filter(r=>r.h.type==='WIRE').map(r=>r.h.id));const lines=pages[1].filter(r=>r.h.type==='LINE'&&wireIds.has(r.v.lineGroup));
const j2=geometry.pins.map(p=>{const wanted=expected.pages.P2.components.J2.pins[p.pinNumber];const near=(a,b)=>Math.abs(a-b)<1e-6;const ns=[...new Set(lines.filter(r=>near(r.v.startX,p.x)&&near(-r.v.startY,p.y)||near(r.v.endX,p.x)&&near(-r.v.endY,p.y)).map(r=>attrs[1][r.v.lineGroup]?.NET))];const pass=wanted===null?p.noConnected&&ns.length===0:!p.noConnected&&ns.length===1&&ns[0]===wanted;return {pin:p.pinNumber,pass};});
for(const p of j2)if(!p.pass)failures.push({ref:'J2',...p});
const refs=allSymbols.map(p=>p.Designator),ids=allSymbols.map(p=>p['Unique ID']);if(new Set(refs).size!==refs.length||new Set(ids).size!==ids.length||ids.some(x=>!x))failures.push({error:'Duplicate/missing designator or unique ID'});
const checks=[];function test(name,pass,detail){checks.push({name,pass:!!pass,detail});}
const cidMap={U1:'C54313',U2:'C919459',U3:'C79401',U4:'C19653',U5:'C132554',U6:'C51118',U7:'C128396',U8:'C51118',U9:'C5190383',U10:'C2682616',R21:'C25804',R22:'C23162',R23:'C23162',R24:'C25804',R25:'C25804',R26:'C25804',R27:'C25804',R28:'C25804',C18:'C1591',C19:'C1591',C20:'C1591'};const ex=expected;for(const page of Object.values(ex.pages))for(const[ref,e]of Object.entries(page.components))if(e.cid)cidMap[ref]=e.cid;for(const[ref,cid]of Object.entries(cidMap))test(ref+' exact supplier identity',parts[ref]?.supplierId===cid,{expected:cid,actual:parts[ref]?.supplierId});
for(const ref of ['C18','C19','C20'])test(ref+' decoupling/timing capacitor',parts[ref].otherProperty.Value==='100nF'&&parts[ref].footprint.name==='C0603',{value:parts[ref].otherProperty.Value,footprint:parts[ref].footprint.name});
test('U9 VDD and VREFP use screen-switched 3.3V',pin('U9','12')==='SCREEN_IO3V3'&&pin('U9','15')==='SCREEN_IO3V3'&&pin('U8','1')==='5V_SCREEN'&&pin('U8','5')==='SCREEN_IO3V3','Reuse U8; no VBAT/5V on bridge supply.');
test('U9 GPIO6 boot strap has external 10k pullup',pin('U9','3')===pin('R24','2')&&pin('R24','1')==='SCREEN_IO3V3'&&parts.R24.otherProperty.Value==='10kΩ','GPIO6 must stay high during power-on/reset.');
test('U9 UART crosses screen TX/RX',pin('U9','10')==='SCREEN_TX'&&pin('U9','11')==='SCREEN_RX','FPC7 screen TX -> U9 RX; U9 TX -> FPC6 screen RX; separately verified J2 physical symbol endpoints.');
test('Charger status goes to input GPIO0/GPIO1',pin('U1','9')===pin('U9','9')&&pin('U1','7')===pin('U9','16')&&pin('R26','2')==='PGOOD_N'&&pin('R27','2')==='CHG_N'&&pin('R26','1')==='SCREEN_IO3V3'&&pin('R27','1')==='SCREEN_IO3V3','Open-drain CHG and PGOOD pulled up to switched rail; GPIO input firmware contract required.');
test('U10 senses BAT_PACK and is always powered while battery is present',pin('U10','2')==='BAT_PACK'&&pin('U10','3')==='BAT_PACK'&&pin('C19','1')==='BAT_PACK'&&pin('C19','2')==='GND','Not SYS_RAW or boost voltage.');
test('U10 ground/strap pins and EP connected', ['1','4','6','9'].every(p=>pin('U10',p)==='GND')&&pin('U10','5')==='','CTG/GND/QSTRT/EP grounded; ALRT unused polling.');
test('I2C data/clock use correct pins and switched pullups',pin('U9','1')===pin('U10','8')&&pin('U9','2')===pin('U10','7')&&pin('R22','2')==='BAT_SDA'&&pin('R23','2')==='BAT_SCL'&&pin('R22','1')==='SCREEN_IO3V3'&&pin('R23','1')==='SCREEN_IO3V3','4.7k pullups to 3.3V. At screen off no active pullup; keep MAX17048 EnSleep disabled.');
test('No external NTC; TS has requested fixed 10k',!parts.J4&&pin('U1','1')==='CHG_TS_BIAS'&&pin('R21','1')==='CHG_TS_BIAS'&&pin('R21','2')==='GND'&&parts.R21.otherProperty.Value==='10kΩ','User explicitly removed external temperature sensing. No claim of cell temperature protection.');
const pre=read('verification-baseline.json');const differences=[];let retained=0;for(const page of Object.values(pre.pages))for(const[ref,e]of Object.entries(page.components)){if(['J2','J4'].includes(ref))continue;for(const[p,net]of Object.entries(e.pins)){if(ref==='U1'&&['1','7','9'].includes(p))continue;const wanted=net==='USB_SW_3V3'?'SCREEN_IO3V3':net===null?'':net;retained++;if(pin(ref,p)!==wanted)differences.push({ref,p,wanted,actual:pin(ref,p)});}}
test('Existing power / USB / undervoltage connectivity preserved',!differences.length,{retainedPins:retained,differences});

const result={schema:'gridopoly.player_power_offline_verification.v1',scope:'Snapshot connection/identity review; no ERC clearance, physical verification or manufacturing approval.',nativeSymbols:allSymbols.length,nativeNetlistComponents:Object.keys(nets).length,connectedPinsMatched:connected,unconnectedPinsMatched:nc,j2PinsMatched:j2.filter(p=>p.pass).length,electricalChecks:checks.length,electricalChecksPassed:checks.filter(c=>c.pass).length,failures:[...failures,...checks.filter(c=>!c.pass)],sourceHashes:Object.fromEntries(['P1-power.source.jsonl','P2-usb-interfaces.source.jsonl','native-netlist.json','expected-connectivity.json'].map(n=>[n,hash(n)]))};
console.log(JSON.stringify(result,null,2));if(result.failures.length)process.exitCode=1;
