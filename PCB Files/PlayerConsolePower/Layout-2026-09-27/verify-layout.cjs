// Offline checks on captured native data; does not replace electrical or assembly testing.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const base = __dirname;
const read = n => JSON.parse(fs.readFileSync(path.join(base, n), 'utf8'));
const live = read('final-live-audit.json');
const fixed = read('fixed-constraints.json');
const sch = read('schematic-netlist.json');
const pcb = read('PCB1-netlist.json');
const geometry = read('native-layout-geometry.json');
const source = fs.readFileSync(path.join(base, 'PCB1-routed.source.jsonl'), 'utf8').trim().split(/\r?\n/).map(l => {
  const [h, v] = l.split('||'); return {h: JSON.parse(h), v: JSON.parse(v.replace(/\|$/, ''))};
});
const checks = [];
const check = (name, ok, details) => checks.push({name, passed: !!ok, ...(details === undefined ? {} : {details})});
const near = (a, b) => Math.abs(a - b) < 0.001;
check('Native PCB DRC reports zero entries', Array.isArray(live.drc) && live.drc.length === 0);
check('Native document saved', live.saved === true);
check('68 components, all top side', live.components.length === 68 && live.components.every(c => c.layer === 1));
check('243 component pads', live.components.reduce((n, c) => n + c.pads.length, 0) === 243);
const differences = [];
for (const [id, s] of Object.entries(sch.components)) {
  const p = pcb.components[id], c = live.components.find(c => c.uid === id);
  if (!p || !c) { differences.push({id, missing: true}); continue; }
  if (s.props.Designator !== p.props.Designator || c.ref !== s.props.Designator) differences.push({id, designator: true});
  for (const [pin, info] of Object.entries(s.pinInfoMap)) {
    const pads = c.pads.filter(x => x.padNumber === pin);
    if (!pads.length || pads.some(x => (x.net || '') !== (info.net || '')) || (p.pinInfoMap[pin]?.net || '') !== (info.net || '')) differences.push({ref: c.ref, pin, expected: info.net});
  }
}
check('Every schematic pin equals exported PCB and live pad net by unique ID', differences.length === 0, differences);
check('Schematic and PCB component identities match', JSON.stringify(Object.keys(sch.components).sort()) === JSON.stringify(Object.keys(pcb.components).sort()));
for (const f of fixed.frozenComponents) {
  const c = live.components.find(c => c.uid === f.uid);
  check(`Frozen ${f.ref} placement`, c && near(c.x, f.x) && near(c.y, f.y) && near(c.rotation, f.rotation) && c.layer === f.layer);
}
for (const f of [...fixed.frozenMountingPads, ...fixed.frozenBoardOutline]) {
  const s = source.find(s => s.h.id === f.h.id);
  const fields = f.h.type === 'PAD' ? ['centerX', 'centerY', 'padAngle', 'hole', 'defaultPad', 'plated', 'layerId'] : ['path', 'width', 'layerId'];
  check(`Frozen mechanical primitive ${f.h.id}`, s && fields.every(k => JSON.stringify(s.v[k]) === JSON.stringify(f.v[k])));
}
const activeCu = source.filter(s => s.h.type === 'LAYER' && s.v.use && ['TOP', 'BOTTOM', 'INNER'].includes(s.v.layerType));
check('Two active copper layers', activeCu.length === 2 && activeCu.every(s => [1, 2].includes(s.v.layerId)));
check('Every routed trace and via has a net', live.lines.every(l => !!l.net) && live.vias.every(v => !!v.net));
check('Minimum trace width at least 0.15 mm (rounding tolerance 0.001 mm)', live.lines.every(l => l.lineWidth * .0254 >= .149));
check('Via holes nominal 0.30 mm or larger', live.vias.every(v => v.holeDiameter * .0254 >= .299));
check('Every via requests both-side tenting', live.vias.every(v => v.solderMaskExpansion?.topSolderMask < -v.diameter / 2 && v.solderMaskExpansion?.bottomSolderMask < -v.diameter / 2));
const j2 = live.components.find(c => c.ref === 'J2');
const intended = ['SCREEN_DM', 'SCREEN_DP', 'SCREEN_RST', 'SCREEN_TX', 'SCREEN_RX', '', '', 'GND', '', '5V_SCREEN', 'GND', 'GND'];
check('J2 physical pad reversal preserved', intended.every((net, i) => j2.pads.find(p => p.padNumber === String(i + 1))?.net === net));
for (const [ref, net] of [['SW1', 'POWER_KEY_N'], ['SW2', 'SCREEN_RST']]) {
  const p = Object.fromEntries(live.components.find(c => c.ref === ref).pads.map(p => [p.padNumber, p.net]));
  check(`${ref} internally common groups are not shorted signal-to-ground`, p['1'] === net && p['2'] === net && p['3'] === 'GND' && p['4'] === 'GND');
}
const usb = ['USB_HOST_DP', 'USB_HOST_DM', 'SCREEN_DP', 'SCREEN_DM'].map(net => {
  const ls = live.lines.filter(l => l.net === net);
  return {net, totalTraceLengthMm: +ls.reduce((s, l) => s + Math.hypot(l.endX-l.startX, l.endY-l.startY)*.0254, 0).toFixed(4), viaCount: live.vias.filter(v => v.net === net).length};
});
const boxes = Object.entries(geometry.components).map(([ref,c]) => {
  const [l,r,b,t] = geometry.footprints[c.footprint].bbox, a = c.rotation * Math.PI/180;
  const pts = [[l,b],[l,t],[r,b],[r,t]].map(([x,y]) => [c.location[0]+x*Math.cos(a)-y*Math.sin(a), c.location[1]+x*Math.sin(a)+y*Math.cos(a)]);
  return {ref, box:[Math.min(...pts.map(p=>p[0])),Math.max(...pts.map(p=>p[0])),Math.min(...pts.map(p=>p[1])),Math.max(...pts.map(p=>p[1]))]};
});
const overlaps = [];
for(let i=0;i<boxes.length;i++)for(let j=i+1;j<boxes.length;j++){
  const a=boxes[i],b=boxes[j];
  if(Math.min(a.box[1],b.box[1])-Math.max(a.box[0],b.box[0])>.001 && Math.min(a.box[3],b.box[3])-Math.max(a.box[2],b.box[2])>.001)overlaps.push([a.ref,b.ref]);
}
check('Native footprint bounding envelopes do not overlap', overlaps.length===0, overlaps);
function csv(file){
  const text=fs.readFileSync(path.join(base,file),'utf8').replace(/^\uFEFF/,''), rows=[];let row=[],cell='',quoted=false;
  for(let i=0;i<text.length;i++){const c=text[i];if(c==='"'){if(quoted&&text[i+1]==='"'){cell+='"';i++;}else quoted=!quoted;}else if(c===','&&!quoted){row.push(cell);cell='';}else if(c==='\n'&&!quoted){row.push(cell.replace(/\r$/,''));rows.push(row);row=[];cell='';}else cell+=c;}
  if(cell||row.length){row.push(cell);rows.push(row);}const headers=rows.shift();return rows.filter(r=>r.some(Boolean)).map(r=>Object.fromEntries(headers.map((h,i)=>[h,r[i]])));
}
const bom=csv('Manufacturing-RC1/PlayerPower-R1-BOM.csv'), cpl=csv('Manufacturing-RC1/PlayerPower-R1-CPL.csv');
const refs=live.components.filter(c=>c.ref!=='J3').map(c=>c.ref).sort();
const bomRefs=bom.flatMap(r=>r.Designator.split(',').map(s=>s.trim())).sort();
check('BOM contains exactly the 67 assembled references', JSON.stringify(bomRefs)===JSON.stringify(refs)&&bom.every(r=>Number(r.Quantity)===r.Designator.split(',').length&&/^C\d+$/.test(r['LCSC Part #'])));
check('CPL contains exactly the same 67 references, all Top', JSON.stringify(cpl.map(r=>r.Designator).sort())===JSON.stringify(refs)&&cpl.every(r=>r.Layer==='Top'));
const cplErrors=cpl.filter(r=>{const c=live.components.find(c=>c.ref===r.Designator);return !c||Math.abs(parseFloat(r['Mid X'])-c.x*.0254)>.002||Math.abs(parseFloat(r['Mid Y'])-c.y*.0254)>.002||!near(+r.Rotation,c.rotation);}).map(r=>r.Designator);
check('Placement file coordinates/rotations equal final native component data', cplErrors.length===0,cplErrors);
const files = ['PCB1-routed.source.jsonl', 'PCB1-netlist.json', 'schematic-netlist.json', 'final-live-audit.json', 'Manufacturing-RC1/PlayerPower-R1-Gerber.zip', 'Manufacturing-RC1/PlayerPower-R1-BOM.csv', 'Manufacturing-RC1/PlayerPower-R1-CPL.csv'];
const hashes = Object.fromEntries(files.map(n => [n, crypto.createHash('sha256').update(fs.readFileSync(path.join(base, n))).digest('hex')]));
const result = {checkedAt: new Date().toISOString(), snapshotAt: live.capturedAt, scope: 'Captured design and fabrication files; no physical measurements', counts: {parts: live.components.length, pads: 243, traces: live.lines.length, vias: live.vias.length}, passed: checks.filter(c => c.passed).length, failed: checks.filter(c => !c.passed).length, checks, usbCopperTotals: usb, usbMeasurementLimit: 'Total trace length includes branches. Not an end-to-end delay or impedance certification; vias and packages are not included.', hashes};
fs.writeFileSync(path.join(base, 'layout-verification.json'), JSON.stringify(result, null, 2) + '\n');
console.log(JSON.stringify({passed: result.passed, failed: result.failed, failures: checks.filter(c => !c.passed), usb}, null, 2));
if (result.failed) process.exitCode = 1;
