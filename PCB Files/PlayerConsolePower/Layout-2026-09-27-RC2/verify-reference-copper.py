# Requires gerbonara and shapely. Run on a review copy; rewrites the JSON report.
from pathlib import Path
from gerbonara import GerberFile
from shapely.geometry import Polygon,Point,LineString,GeometryCollection
from shapely.ops import unary_union
from shapely import make_valid
import json
import hashlib, tempfile, zipfile
root=Path(__file__).resolve().parent.parent
tmp_handle=tempfile.TemporaryDirectory(prefix='gridopoly-usb-reference-')
tmp=Path(tmp_handle.name)
inputs={}
for name, release, target in [('RC1','Layout-2026-09-27','gerber-clean'),('RC2','Layout-2026-09-27-RC2','rc2/gerber-clean')]:
 folder=tmp/target
 folder.mkdir(parents=True)
 zippath=next((root/release/f'Manufacturing-{name}').glob('*Gerber.zip'))
 with zipfile.ZipFile(zippath) as archive:
  for filename in ['Gerber_TopLayer.GTL','Gerber_BottomLayer.GBL']:
   entry=next(x for x in archive.namelist() if Path(x).name==filename)
   payload=archive.read(entry)
   (folder/filename).write_bytes(payload)
   inputs[f'{name}/{filename}']=hashlib.sha256(payload).hexdigest()
res={}
for name,folder in [('RC1',tmp/'gerber-clean'),('RC2',tmp/'rc2/gerber-clean')]:
 live=json.loads((root/('Layout-2026-09-27' if name=='RC1' else 'Layout-2026-09-27-RC2')/'final-live-audit.json').read_text(encoding='utf-8'))
 layers={}; layerstats={}
 for layer,file in [(1,'Gerber_TopLayer.GTL'),(2,'Gerber_BottomLayer.GBL')]:
  copper=GeometryCollection(); pending=[];last=None
  def flush():
   global copper,pending,last
   if pending:
    shape=unary_union(pending)
    copper=copper.union(shape) if last else copper.difference(shape)
    pending=[]
  for obj in GerberFile.open(folder/file).objects:
   for prim in obj.to_primitives():
    ap=prim.to_arc_poly().approximate_arcs(max_error=.001)
    if len(ap.outline)<3:continue
    poly=make_valid(Polygon(ap.outline))
    if prim.polarity_dark!=last:flush();last=prim.polarity_dark
    pending.append(poly)
  flush()
  polys=[g for g in (list(copper.geoms)if hasattr(copper,'geoms')else[copper])if g.geom_type=='Polygon']
  gnd=[]
  for v in live['vias']:
   if v['net']=='GND':gnd.append(Point(v['x']*.0254,v['y']*.0254))
  for c in live['components']:
   for p in c['pads']:
    if p['net']=='GND'and (p['layer']==layer or p['layer']==12):gnd.append(Point(p['x']*.0254,p['y']*.0254))
  gs=[g for g in polys if any(g.covers(p)for p in gnd)]
  layers[layer]=unary_union(gs)
  # Electrical net assignment is from CAD; this only checks geometric contact in exported copper.
  conflicts=[]
  for c in live['components']:
   for p in c['pads']:
    if p['net'] and p['net']!='GND' and (p['layer']==layer or p['layer']==12) and layers[layer].covers(Point(p['x']*.0254,p['y']*.0254)):conflicts.append([c['ref'],p['padNumber'],p['net']])
  layerstats[layer]={'copperPolygons':len(polys),'gndPolygons':len(gs),'knownNonGroundPadInGround':conflicts}
  if conflicts:raise AssertionError(conflicts)
  print(name,layer,layerstats[layer],flush=True)
 metrics={}
 for net in ['USB_HOST_DP','USB_HOST_DM','SCREEN_DP','SCREEN_DM']:
  ls=[l for l in live['lines']if l['net']==net];length=0;covered=0;bottom=0
  for l in ls:
   seg=LineString([(l['startX']*.0254,l['startY']*.0254),(l['endX']*.0254,l['endY']*.0254)])
   length+=seg.length;covered+=seg.intersection(layers[3-l['layer']]).length
   if l['layer']==2:bottom+=seg.length
  metrics[net]={'traceSumMm':length,'bottomTraceMm':bottom,'oppositeGroundCenterlineMm':covered,'oppositeNonGroundCenterlineMm':length-covered}
 bridges={}
 if name=='RC2':
  for label,points in [('screen_power_slot',[(14.9,-59),(14.9,-60.96)]),('host_logic_slot',[(25.6751486496,-62.6751746494),(27.35,-62.75)])]:
   seg=LineString(points);bridges[label]={'topGroundStraightBridge':layers[1].buffer(.002).covers(seg),'lengthMm':seg.length,'uncoveredMm':seg.difference(layers[1]).length}
  viaareas=[]
  for v in [(11.05,-57.8),(13,-56.15),(14.9,-59),(14.9,-60.96),(27.35,-62.75)]:
   viaareas.append({'xyMm':v,'topGroundConnected':layers[1].covers(Point(v)),'bottomGroundConnected':layers[2].covers(Point(v))})
  bridges['newGroundVias']=viaareas
 res[name]={'layers':layerstats,'usbReferenceProjection':metrics,'returnBridges':bridges}
(root/'Layout-2026-09-27-RC2/usb-reference-copper-review.json').write_text(json.dumps({'scope':'2D exported-copper topology, arcs approximated to 1 um; not impedance/field/USB waveform validation','results':res,'inputs':inputs},indent=2),encoding='utf-8')
print(json.dumps(res,indent=2))
