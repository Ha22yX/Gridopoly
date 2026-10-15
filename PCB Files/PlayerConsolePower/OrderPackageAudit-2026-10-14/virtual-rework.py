from pathlib import Path
from zipfile import ZipFile
from collections import defaultdict
import sys,json,re
sys.path.insert(0,r'C:/Users/kicof/AppData/Local/Temp/gridopoly-player-short-20261014/deps')
from shapely import from_wkt
from shapely.geometry import box, Point, LineString
from shapely.ops import unary_union
base=Path(r'C:/Users/kicof/AppData/Local/Temp/gridopoly-order-independent-20261014')
zipf=Path(r'C:/Users/kicof/Downloads/Gridopoly_PCB1_20260927_204456.zip')
z=ZipFile(zipf); fp=json.loads(z.read('FlyingProbeTesting.json'))
orig_top=from_wkt((base/'order-TopLayer.GTL-full.wkt').read_text());bottom=from_wkt((base/'order-BottomLayer.GBL-full.wkt').read_text())
# A 0.10 mm gap across each 0.254 mm spoke; 0.40 mm width includes complete spoke.
# Absolute millimetres in the original top Gerber coordinate system.
cuts={
 'upper':{'center':[41.00068,-4.07416],'size':[.40,.10]},
 'lower':{'center':[41.00068,-5.92836],'size':[.40,.10]},
 'right':{'center':[41.92778,-5.00126],'size':[.10,.40]},
}
shapes={}
for name,c in cuts.items():
 x,y=c['center'];w,h=c['size'];shapes[name]=box(x-w/2,y-h/2,x+w/2,y+h/2)
 c['boundsMm']=list(shapes[name].bounds)
 c['removedTopCopperMm2']=shapes[name].intersection(orig_top).area
 c['clearanceFromPadOuterCircleMm']=shapes[name].distance(Point(41.00068,-5.00126))-.8001
 c['clearanceFromDrillOuterCircleMm']=shapes[name].distance(Point(41.00068,-5.00126))-.4
 c['bottomCopperOverlapMm2']=shapes[name].intersection(bottom).area
cutter=unary_union(list(shapes.values()));top=orig_top.difference(cutter)
# Do NOT export rewritten Gerber or manufacturing files. WKT is a virtual analysis surface only.
(base/'virtual-rework-top-copper.wkt').write_text(top.wkt)
nodes=[];layers={}
for label,c in [('top',top),('bottom',bottom)]:
 polys=list(c.geoms) if hasattr(c,'geoms')else[c];layers[label]=list(range(len(nodes),len(nodes)+len(polys)));nodes.extend(polys)
parent=list(range(len(nodes)))
def root(i):
 while parent[i]!=i:i=parent[i]
 return i
def hit(layer,p):return[n for n in layers[layer]if nodes[n].covers(Point(p))]
def connect(ids):
 for i in ids[1:]:parent[root(i)]=root(ids[0])
holes=[]
for name in ['Drill_PTH_Through.DRL','Drill_PTH_Through_Via.DRL']:
 for line in z.read(name).decode().splitlines():
  m=re.fullmatch(r'X(-?[0-9.]+)Y(-?[0-9.]+)',line)
  if m:
   p=tuple(map(float,m.groups()));holes.append(p);connect(hit('top',p)+hit('bottom',p))
rows=[dict(zip(fp['pins']['fields'],v))for v in fp['pins']['rows']]
# Ground and battery directly from the order's own named flying-probe pins.
checks={};netroots=defaultdict(set);missing=[];rootnets=defaultdict(set)
for row in rows:
 label=row['PIN_NAME'];net=row['NET_NAME'];p=[row['PIN_X']*.0254,row['PIN_Y']*.0254]
 sides=['top','bottom']if row['PIN_TYPE']=='DIP'else(['top']if row['LAYER']=='T'else['bottom'])
 ids=[n for side in sides for n in hit(side,p)];rs=sorted(set(root(i)for i in ids))
 if net:
  netroots[net].update(rs)
  for r in rs:rootnets[r].add(net)
  if not ids:missing.append(label)
 if label in ['J3_1','J3_2','C2_1','C2_2','C19_1','U1_2','U1_3','U1_4','U10_1','U10_2','U10_3','U10_4','C3_1']:
  checks[label]={'net':net,'pointMm':p,'roots':rs}
shorts=[{'root':r,'nets':sorted(nets)}for r,nets in rootnets.items()if len(nets)>1]
opens=[{'net':net,'roots':sorted(rs)}for net,rs in netroots.items()if len(rs)>1]
bat_nodes=['J3_2','C2_1','C19_1','U1_2','U1_3','U10_2','U10_3'];gnd_nodes=['J3_1','C2_2','U1_4','U10_1','U10_4']
bat_roots=set(r for k in bat_nodes for r in checks[k]['roots']);gnd_roots=set(r for k in gnd_nodes for r in checks[k]['roots'])
report={
 'analysis':'VIRTUAL_GEOMETRY_ONLY_NOT_VALIDATED_PHYSICAL_REWORK',
 'sourceZipSha256':'2344f34c45307a879c9a3274bddd53681152dcf42d9fb47637cb690d554405b6',
 'coordinates':'mm; top/component view; original Gerber X/Y; negative Y is down from board top',
 'cuts':cuts,'checks':checks,
 'targetChecks':{'batteryGroundSeparated':not(bat_roots&gnd_roots),'batteryBranchRetained':len(bat_roots)==1 and all(checks[k]['roots']for k in bat_nodes),'groundBranchRetained':len(gnd_roots)==1 and all(checks[k]['roots']for k in gnd_nodes),'padAnnulusUncut':all(c['clearanceFromPadOuterCircleMm']>0 for c in cuts.values()),'bottomLayerUnchanged':True},
 'allNamedFlyingProbeNetChecks':{'nets':len(netroots),'shorts':shorts,'opens':opens,'missingNamedPins':missing},
 'limitations':['Only virtual top copper is removed; no physical removal, EDA edit, or manufacturing Gerber generated.','A cut into the opposite/bottom layer would be invalid; clearance through FR4 thickness and tooling control are not modeled.','Requires clear photos of actual J3 and possibly removing J3 for access.','Component functionality and absence of prior electrical damage cannot be guaranteed after this rework.','No immediate power-on instruction; verify resistance, isolation and remaining connections on real hardware first.']
}
(base/'virtual-rework-results.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
