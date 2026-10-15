from pathlib import Path
from zipfile import ZipFile
from collections import Counter
import sys,json,hashlib,re
sys.path.insert(0,r'C:/Users/kicof/AppData/Local/Temp/gridopoly-player-short-20261014/deps')
from gerbonara import GerberFile
from shapely.geometry import Polygon,Point,box,LineString
from shapely import make_valid
from shapely.ops import unary_union
base=Path(r'C:/Users/kicof/AppData/Local/Temp/gridopoly-order-independent-20261014')
files={'order':Path(r'C:/Users/kicof/Downloads/Gridopoly_PCB1_20260927_204456.zip'), 'rc5':Path(r'Z:/Files/我的项目/Gridopoly/PCB Files/PlayerConsolePower/Layout-2026-09-27-RC5/NativeExports/PlayerPower-R1-RC5-Gerber.zip')}
pt={'J3.1_GND':(38.99916,-5.00126),'J3.2_BAT':(41.00068,-5.00126),'C2.1_BAT':(56,-63.5),'C2.2_GND':(56,-61.5),'U1.4_GND':(53.5,-63.25),'C3.1_SYS_RAW':(47.4,-62.9)}
reports={}
for key,zipf in files.items():
 nodes=[]; layers={};raw={};counts={}
 for layer in ['TopLayer.GTL','BottomLayer.GBL']:
  data=ZipFile(zipf).read('Gerber_'+layer); fn=base/(key+'-'+layer); fn.write_bytes(data)
  g=GerberFile.open(fn)
  assert all(p.polarity_dark for obj in g.objects for p in obj.to_primitives())
  geoms=[make_valid(Polygon(p.to_arc_poly().approximate_arcs(max_error=.001).outline)) for obj in g.objects for p in obj.to_primitives()]
  c=unary_union(geoms); raw[layer]=c
  (base/(key+'-'+layer+'-full.wkt')).write_text(c.wkt)
  pieces=list(c.geoms) if hasattr(c,'geoms') else[c]
  layers[layer]=[len(nodes)+n for n,p in enumerate(pieces)]; nodes.extend(pieces)
  counts[layer]=len(pieces)
 parent=list(range(len(nodes)))
 def root(i):
  while parent[i]!=i:i=parent[i]
  return i
 def hit(layer,p):return [n for n in layers[layer] if nodes[n].covers(Point(p))]
 def connect(ids):
  for i in ids[1:]:parent[root(i)]=root(ids[0])
 holes=[]
 for name in ['Drill_PTH_Through.DRL','Drill_PTH_Through_Via.DRL']:
  d=ZipFile(zipf).read(name).decode()
  for line in d.splitlines():
   m=re.fullmatch(r'X(-?[0-9.]+)Y(-?[0-9.]+)',line)
   if m:
    p=tuple(float(x) for x in m.groups()); holes.append(p)
    connect(hit('TopLayer.GTL',p)+hit('BottomLayer.GBL',p))
 results={}
 for name,p in pt.items():
  results[name]={l:hit(l,p) for l in layers}
  used_layers=list(layers) if name.startswith('J3.') else ['TopLayer.GTL']
  results[name]['connectedRoots']=sorted(set(root(n) for l in used_layers for n in hit(l,p)))
  results[name]['electricalPadLayers']=used_layers
 reports[key]={'layerPieces':counts,'drillCrossLayerPoints':len(holes),'points':results}
 for axis,points in {'vertical_spoke':[(41.00068,-3.5),(41.00068,-6.5)],'horizontal_spoke':[(39.8,-5.00126),(42.2,-5.00126)]}.items():
  reports[key][axis]={'lineFullyCovered':raw['TopLayer.GTL'].covers(LineString(points)), 'uncoveredLength':LineString(points).difference(raw['TopLayer.GTL']).length}
 print(key,json.dumps(reports[key],indent=2))
(base/'independent-connectivity.json').write_text(json.dumps(reports,indent=2))
