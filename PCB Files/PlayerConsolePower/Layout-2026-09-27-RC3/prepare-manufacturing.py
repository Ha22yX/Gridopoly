"""Normalize captured native CSV/Gerber files. Use on a working copy when reviewing a frozen release."""
from pathlib import Path
from tempfile import TemporaryDirectory
from collections import Counter
import csv
import json
import zipfile
from gerbonara import ExcellonFile, GerberFile

B=Path(__file__).resolve().parent
raw=B/'NativeExports'; out=B/'Manufacturing-RC3'
raw.mkdir(exist_ok=True);out.mkdir(exist_ok=True)
prefix='PlayerPower-R1-RC3'
for suffix in ('Gerber.zip','BOM.csv','CPL.csv'):
    name=f'{prefix}-{suffix}'
    if not (raw/name).exists():
        (raw/name).write_bytes((out/name).read_bytes())
include=['Gerber_TopLayer.GTL','Gerber_BottomLayer.GBL','Gerber_TopSilkscreenLayer.GTO','Gerber_BottomSilkscreenLayer.GBO','Gerber_TopSolderMaskLayer.GTS','Gerber_BottomSolderMaskLayer.GBS','Gerber_TopPasteMaskLayer.GTP','Gerber_BoardOutlineLayer.GKO','Drill_PTH_Through.DRL','Drill_NPTH_Through.DRL']
live=json.loads((B/'final-live-audit.json').read_text(encoding='utf-8'))
with TemporaryDirectory(prefix='gridopoly-rc3-export-') as tmp:
    p=Path(tmp); z=zipfile.ZipFile(raw/f'{prefix}-Gerber.zip')
    for name in include+['Drill_PTH_Through_Via.DRL']:
        (p/name).write_bytes(z.read(name))
    main=ExcellonFile.open(p/'Drill_PTH_Through.DRL');via=ExcellonFile.open(p/'Drill_PTH_Through_Via.DRL');npth=ExcellonFile.open(p/'Drill_NPTH_Through.DRL')
    def k(obj):return tuple(round(float(v),5)for v in [obj.x,obj.y,obj.tool.diameter])
    m=Counter(k(x)for x in main.objects if hasattr(x,'x'));v=Counter(k(x)for x in via.objects if hasattr(x,'x'))
    assert not(v-m),'Separate via file contains unique holes; do not omit'
    missing=[]
    for cv in live['vias']:
        candidates=[q for q in main.objects if hasattr(q,'x')and abs(q.x-cv['x']*.0254)<.003 and abs(q.y-cv['y']*.0254)<.003 and abs(q.tool.diameter-cv['holeDiameter']*.0254)<.003]
        if len(candidates)!=1:missing.append(cv['primitiveId'])
    assert not missing,missing
    layer_counts={name:len(GerberFile.open(p/name).objects)for name in include if not name.endswith('.DRL')}
    with zipfile.ZipFile(out/f'{prefix}-Gerber.zip','w',zipfile.ZIP_DEFLATED)as dest:
        for name in include:dest.writestr(name,z.read(name))
with(raw/f'{prefix}-CPL.csv').open(encoding='utf-16',newline='')as f:rows=list(csv.DictReader(f,delimiter='\t'))
with(out/f'{prefix}-CPL.csv').open('w',encoding='utf-8-sig',newline='')as f:
    w=csv.DictWriter(f,fieldnames=['Designator','Mid X','Mid Y','Layer','Rotation']);w.writeheader()
    for r in rows:
        if r['Designator']!='J3':w.writerow({k:('Top'if k=='Layer'else r[k])for k in w.fieldnames})
with(raw/f'{prefix}-BOM.csv').open(encoding='utf-16',newline='')as f:bom=list(csv.DictReader(f,delimiter='\t'))
with(out/f'{prefix}-BOM.csv').open('w',encoding='utf-8-sig',newline='')as f:
    w=csv.DictWriter(f,fieldnames=['Comment','Designator','Footprint','LCSC Part #','Quantity','Manufacturer Part']);w.writeheader()
    for r in bom:w.writerow({'Comment':r['Comment'],'Designator':r['Designator'],'Footprint':r['Footprint'],'LCSC Part #':r['Supplier Part'],'Quantity':r['Quantity'],'Manufacturer Part':r['Manufacturer Part']})
report={'nativePlacementRows':len(rows),'assembledRows':len(rows)-1,'bareWireTerminalExcluded':'J3','bomQuantity':sum(int(r['Quantity'])for r in bom),'duplicateViaDrillHitsOmitted':sum(v.values()),'totalPlatedDrillObjects':len(main.objects),'totalNonPlatedDrillObjects':len(npth.objects),'matchedVias':len(live['vias']),'missingOrDuplicateVias':missing,'fabricationFiles':include,'gerberObjectCounts':layer_counts}
(B/'manufacturing-export-audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
