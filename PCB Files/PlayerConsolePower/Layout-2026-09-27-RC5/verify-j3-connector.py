"""Verify RC5 battery polarity, retained layout and actual exported holes.

Drawing/photo inference is separate from physical mating or voltage measurement.
"""
from pathlib import Path
import json,hashlib,math,zipfile,tempfile
from gerbonara import ExcellonFile
B=Path(__file__).resolve().parent
R=B.parent/'Layout-2026-09-27-RC4'
a=json.loads((B/'final-live-audit.json').read_text(encoding='utf-8'))
b=json.loads((R/'final-live-audit.json').read_text(encoding='utf-8'))
s=json.loads((B/'schematic-netlist.json').read_text(encoding='utf-8'))
p=json.loads((B/'PCB1-netlist.json').read_text(encoding='utf-8'))
checks=[]
def ck(name,passed,detail=None):checks.append(dict(name=name,passed=bool(passed),detail=detail))
j=next(c for c in a['components'] if c['ref']=='J3');pins={x['padNumber']:x for x in j['pads']}
ck('J3 supplier and MPN synchronized',all(n['components']['gge60']['props']['Supplier Part']=='C131337' and n['components']['gge60']['props']['Manufacturer Part']=='B2B-PH-K-S(LF)(SN)' and n['components']['gge60']['props']['Add into BOM']=='yes' for n in [s,p]))
ck('J3 pin1 GND/black, pin2 BAT_PACK/red',pins['1']['net']=='GND' and pins['2']['net']=='BAT_PACK')
ck('J3 pitch 2.0 mm within coordinate rounding',abs(math.hypot(pins['1']['x']-pins['2']['x'],pins['1']['y']-pins['2']['y'])*.0254-2)<.003)
ck('J3 at (40,5) mm; negative left, positive right on top view',abs(j['x']*.0254-40)<.003 and abs(j['y']*.0254+5)<.003 and pins['1']['x']<pins['2']['x'])
changed=[]
for c in a['components']:
 old=next(x for x in b['components'] if x['uid']==c['uid'])
 if any(abs(c[k]-old[k])>.001 for k in ['x','y','rotation','layer']):changed.append(c['ref'])
ck('All 68 component placements unchanged from RC4',not changed,changed)
oldnet=json.loads((R/'schematic-netlist.json').read_text(encoding='utf-8'))
def nets(n):return {uid:{k:i['net'] for k,i in c['pinInfoMap'].items()}for uid,c in n['components'].items()}
expected=nets(oldnet);expected['gge60']={'1':'GND','2':'BAT_PACK'}
ck('Only J3 pin networks swapped; every SCH/PCB pin matches',nets(s)==expected==nets(p))
with tempfile.TemporaryDirectory()as tmp:
 name='Drill_PTH_Through.DRL';q=Path(tmp)/name
 q.write_bytes(zipfile.ZipFile(B/'Manufacturing-RC5/PlayerPower-R1-RC5-Gerber.zip').read(name))
 drills=ExcellonFile.open(q).objects;hits=[]
 for pin in pins.values():
  found=[o for o in drills if hasattr(o,'x') and abs(o.x-pin['x']*.0254)<.003 and abs(o.y-pin['y']*.0254)<.003]
  hits.append({'pin':pin['padNumber'],'matches':len(found),'diametersMm':[o.tool.diameter for o in found]})
 ck('Actual manufacturing holes at J3 are both 0.80 mm',all(h['matches']==1 and abs(h['diametersMm'][0]-.8)<.001 for h in hits),hits)
oldlines={x['primitiveId']:x for x in b['lines']};newlines={x['primitiveId']:x for x in a['lines']}
removed=[x for k,x in oldlines.items()if k not in newlines];added=[x for k,x in newlines.items()if k not in oldlines]
modified=[k for k in oldlines.keys()&newlines.keys()if oldlines[k]!=newlines[k]]
ck('Only three BAT_PACK tail traces replaced',len(removed)==len(added)==3 and not modified and all(x['net']=='BAT_PACK' and x['layer']==2 for x in added+removed),{'removed':removed,'added':added,'modifiedIds':modified})
ck('All 82 vias unchanged',a['vias']==b['vias'])
ck('Description no longer states previous polarity',all('pin 1 GND/BAT-, pin 2 BAT_PACK/BAT+' in n['components']['gge60']['props']['Description'] for n in [s,p]))
source=(B/'PCB1-routed.source.jsonl').read_text(encoding='utf-8');texts=[]
for line in source.splitlines():
 h,v=line.split('||');h=json.loads(h);v=json.loads(v.removesuffix('|'))
 if h['type']=='STRING':texts.append(v)
minus=[v for v in texts if v.get('text')=='BAT-'];plus=[v for v in texts if v.get('text')=='BAT+']
ck('Silkscreen polarity is left minus and right plus',len(minus)==len(plus)==1 and minus[0]['x']<pins['1']['x']<pins['2']['x']<plus[0]['x'],{'minus':minus,'plus':plus})
report={'checkedAt':a['capturedAt'],'passed':sum(c['passed']for c in checks),'failed':sum(not c['passed']for c in checks),'checks':checks,'source':'Actual saved netlists, pad coordinates and exported Excellon; not a physical mating test','sha256':{f:hashlib.sha256((B/f).read_bytes()).hexdigest()for f in ['final-live-audit.json','schematic-netlist.json','PCB1-netlist.json','Manufacturing-RC5/PlayerPower-R1-RC5-Gerber.zip']}}
(B/'j3-connector-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'passed':report['passed'],'failed':report['failed'],'failures':[c for c in checks if not c['passed']]},indent=2));assert not report['failed']
