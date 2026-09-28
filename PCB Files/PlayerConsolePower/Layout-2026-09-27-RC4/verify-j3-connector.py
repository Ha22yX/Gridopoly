from pathlib import Path
import json,hashlib,math,zipfile,tempfile
from gerbonara import ExcellonFile
B=Path(__file__).resolve().parent
R=B.parent/'Layout-2026-09-27-RC3'
a=json.loads((B/'final-live-audit.json').read_text(encoding='utf-8'))
b=json.loads((R/'final-live-audit.json').read_text(encoding='utf-8'))
s=json.loads((B/'schematic-netlist.json').read_text(encoding='utf-8'))
p=json.loads((B/'PCB1-netlist.json').read_text(encoding='utf-8'))
checks=[]
def ck(name,passed,detail=None): checks.append(dict(name=name,passed=bool(passed),detail=detail))
j=next(c for c in a['components'] if c['ref']=='J3');pins={x['padNumber']:x for x in j['pads']}
ck('J3 supplier and MPN synchronized',all(n['components']['gge60']['props']['Supplier Part']=='C131337' and n['components']['gge60']['props']['Manufacturer Part']=='B2B-PH-K-S(LF)(SN)' and n['components']['gge60']['props']['Add into BOM']=='yes' for n in [s,p]))
ck('J3 pin1 BAT+, pin2 GND',pins['1']['net']=='BAT_PACK' and pins['2']['net']=='GND')
ck('J3 pitch 2.0 mm within coordinate rounding',abs(math.hypot(pins['1']['x']-pins['2']['x'],pins['1']['y']-pins['2']['y'])*.0254-2)<.003)
ck('J3 stays at (40,5) mm; positive left on top view',abs(j['x']*.0254-40)<.003 and abs(j['y']*.0254+5)<.003 and pins['1']['x']<pins['2']['x'])
changed=[]
for c in a['components']:
 old=next(x for x in b['components'] if x['uid']==c['uid'])
 if c['ref']!='J3' and any(abs(c[k]-old[k])>.001 for k in ['x','y','rotation','layer']):changed.append(c['ref'])
ck('Other 67 component placements unchanged',not changed,changed)
oldnet=json.loads((R/'schematic-netlist.json').read_text(encoding='utf-8'))
def nets(n):return {uid:{k:i['net'] for k,i in c['pinInfoMap'].items()}for uid,c in n['components'].items()}
ck('All electrical pin networks unchanged from RC3',nets(s)==nets(oldnet)==nets(p))
with tempfile.TemporaryDirectory()as tmp:
 name='Drill_PTH_Through.DRL';q=Path(tmp)/name
 q.write_bytes(zipfile.ZipFile(B/'Manufacturing-RC4/PlayerPower-R1-RC4-Gerber.zip').read(name))
 drills=ExcellonFile.open(q).objects
 hits=[]
 for pin in pins.values():
  found=[o for o in drills if hasattr(o,'x') and abs(o.x-pin['x']*.0254)<.003 and abs(o.y-pin['y']*.0254)<.003]
  hits.append({'pin':pin['padNumber'],'matches':len(found),'diametersMm':[o.tool.diameter for o in found]})
 ck('Actual manufacturing drills at both J3 pads are 0.80 mm',all(h['matches']==1 and abs(h['diametersMm'][0]-.8)<.001 for h in hits),hits)
changed_lines=[]
for x in a['lines']:
 old=next((o for o in b['lines']if o['primitiveId']==x['primitiveId']),None)
 if old is None or old['net']!=x['net'] or any(abs(x[k]-old[k])>.001 for k in ['startX','startY','endX','endY','lineWidth','layer']):changed_lines.append(x['primitiveId'])
ck('Only two BAT_PACK tail traces changed',len(a['lines'])==len(b['lines']) and set(changed_lines)=={'3b464ebf2193176a','e9ee3e10640f104e'},changed_lines)
ck('All 82 vias unchanged',a['vias']==b['vias'])
report={'checkedAt':a['capturedAt'],'passed':sum(c['passed']for c in checks),'failed':sum(not c['passed']for c in checks),'checks':checks,'source':'Actual saved netlists, pad coordinates and exported Excellon; not a physical mating test','sha256':{f:hashlib.sha256((B/f).read_bytes()).hexdigest()for f in ['final-live-audit.json','schematic-netlist.json','PCB1-netlist.json','Manufacturing-RC4/PlayerPower-R1-RC4-Gerber.zip']}}
(B/'j3-connector-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2));assert not report['failed']