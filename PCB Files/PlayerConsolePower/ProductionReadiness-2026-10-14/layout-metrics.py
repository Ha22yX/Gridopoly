from pathlib import Path
import contextlib,io,json,runpy,zipfile,tempfile,math,collections
from gerbonara import GerberFile
from shapely.geometry import Point,Polygon,GeometryCollection,LineString,box
from shapely.ops import unary_union,nearest_points
from shapely import make_valid
from shapely.affinity import rotate
from PIL import Image,ImageDraw,ImageFont
BASE=Path(__file__).parent
with contextlib.redirect_stdout(io.StringIO()):
 g=runpy.run_path(str(BASE/'verify-current-copper-layout.py'))
live=g['LIVE'];polys=g['polygons'];layers=g['layers'];root=g['root'];by_net=g['by_net']
comps={c['ref']:c for c in live['components']}
net_shapes={l:{n:unary_union([polys[i] for i in ids if root(i) in roots]) for n,roots in by_net.items()} for l,ids in layers.items()}
def parts(shape):return list(shape.geoms) if hasattr(shape,'geoms') else [shape]
def gerber_shape(z,name):
 with tempfile.TemporaryDirectory() as t:
  f=Path(t)/name;f.write_bytes(z.read(name));out=GeometryCollection();batch=[];polarity=None
  for obj in GerberFile.open(f).objects:
   for prim in obj.to_primitives():
    try:outline=prim.to_arc_poly().approximate_arcs(max_error=.001).outline
    except ZeroDivisionError:continue
    if len(outline)<3:continue
    if batch and prim.polarity_dark!=polarity:
     s=unary_union(batch);out=out.union(s) if polarity else out.difference(s);batch=[]
    polarity=prim.polarity_dark;batch.append(make_valid(Polygon(outline)))
  if batch:
   s=unary_union(batch);out=out.union(s) if polarity else out.difference(s)
  return out
z=zipfile.ZipFile(BASE/'current-Gerber.zip');paste=gerber_shape(z,'Gerber_TopPasteMaskLayer.GTP');mask=gerber_shape(z,'Gerber_TopSolderMaskLayer.GTS');bmask=gerber_shape(z,'Gerber_BottomSolderMaskLayer.GBS');silk=gerber_shape(z,'Gerber_TopSilkscreenLayer.GTO')
report={'limits':['Geometric analysis only; no impedance solver or fabricated-board measurements.','USB trace totals include connector branch stubs and do not equal a single end-to-end signal path.'], 'paste':{},'usb':{},'copperClearances':[],'thermalVias':{},'manufacturing':{}}
bridges=[]
for sh in parts(paste):
 ns=[n for n,s in net_shapes[1].items() if sh.intersection(s).area>1e-7]
 if len(ns)>1:bridges.append({'nets':ns,'bounds':list(sh.bounds)})
report['paste']['bridges']=bridges
for ref in ['U1','U10','U2','U5','U7']:
 c=comps[ref];x,y=c['x']*.0254,c['y']*.0254;reg=box(x-2,y-2,x+2,y+2)
 if ref=='U1':reg=box(x-.86,y-.86,x+.86,y+.86)
 if ref=='U10':reg=box(x-.61,y-.41,x+.61,y+.41)
 ps=parts(paste.intersection(reg));vs=[]
 for v in live['vias']:
  p=Point(v['x']*.0254,v['y']*.0254)
  if reg.contains(p):
   hole=p.buffer(v['holeDiameter']*.0254/2,quad_segs=32)
   vs.append({'net':v['net'],'xyMm':list(p.coords[0]),'drillMm':v['holeDiameter']*.0254,'topMaskOpenHoleFraction':mask.intersection(hole).area/hole.area,'bottomMaskOpenHoleFraction':bmask.intersection(hole).area/hole.area,'pasteOverHoleFraction':paste.intersection(hole).area/hole.area})
 report['paste'][ref]={'areaMm2':sum(s.area for s in ps),'islands':[{'boundsMm':list(s.bounds),'areaMm2':s.area} for s in ps if s.area>1e-6]}
 report['thermalVias'][ref]=vs
for n in ['USB_HOST_DP','USB_HOST_DM','SCREEN_DP','SCREEN_DM']:
 tr=[t for t in live['lines'] if t['net']==n];lens=[];holes=[];norefl=[]
 for t in tr:
  ln=LineString([(t['startX']*.0254,t['startY']*.0254),(t['endX']*.0254,t['endY']*.0254)])
  other=3-t['layer'];nr=ln.difference(net_shapes[other]['GND'].buffer(.003))
  lens.append(ln.length)
  if nr.length>.01:norefl.append({'id':t['primitiveId'],'layer':t['layer'],'lengthMm':nr.length,'coordinates':list(ln.coords)})
 report['usb'][n]={'totalTraceLengthMm':sum(lens),'widthsMm':sorted(set(round(t['lineWidth']*.0254,5) for t in tr)),'vias':sum(v['net']==n for v in live['vias']),'noGroundDirectlyOppositeLengthMm':sum(i['lengthMm'] for i in norefl),'noGroundSegments':norefl}
for l,ns in net_shapes.items():
 names=list(ns)
 for j,a in enumerate(names):
  if ns[a].is_empty:continue
  for b in names[j+1:]:
   if ns[b].is_empty:continue
   d=ns[a].distance(ns[b])
   if d<.155:report['copperClearances'].append({'layer':l,'nets':[a,b],'gapMm':d,'points':[list(p.coords[0]) for p in nearest_points(ns[a],ns[b])]})
outline=box(5,-75,75,-5).buffer(5,quad_segs=256)
for l in [1,2]:
 cu=unary_union([polys[i] for i in layers[l]])
 report['manufacturing']['layer'+str(l)]={'copperAreaMm2':cu.area,'copperOutsideBoardMm2':cu.difference(outline).area,'minCopperToEdgeMm':cu.distance(outline.boundary)}
report['manufacturing']['silkIntersectMaskAreaMm2']=silk.intersection(mask).area
report['manufacturing']['traceWidthsMm']=sorted(set(round(t['lineWidth']*.0254,5) for t in live['lines']))
report['manufacturing']['viaDrillsMm']=sorted(set(round(v['holeDiameter']*.0254,5) for v in live['vias']))
report['manufacturing']['viaRingsMm']=sorted(set(round((v['diameter']-v['holeDiameter'])*.0254/2,5) for v in live['vias']))
report['manufacturing']['totalBoardLayers']=2
(BASE/'layout-metrics.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in report.items() if k!='usb'},indent=2))
print('USB_SUMMARY',json.dumps({n:{k:v for k,v in m.items() if k!='noGroundSegments'} for n,m in report['usb'].items()},indent=2))
panels=[('Boost top / L1 U2 C7-C10 / orange=FB',(44,-60.5,63,-47),1),('Charger top / U1 C1 C2 C3 / purple=paste',(43,-69.5,60,-60),1),('USB top routing / blue=other-side route',(0,-78.5,46,-50),1),('USB bottom ground / red=top-route projection',(0,-78.5,46,-50),2)]
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',18);small=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',14)
im=Image.new('RGB',(1600,1300),'white');d=ImageDraw.Draw(im)
colors={'GND':'#b8d4b8','USB_HOST_DP':'#c52d27','USB_HOST_DM':'#e98344','SCREEN_DP':'#c52d27','SCREEN_DM':'#e98344','BOOST_SW':'#cf496f','BOOST_FB':'#ed9904','SYS_RAW':'#52a6cf','5V_SCREEN':'#6970e0','BAT_PACK':'#e04545'}
for idx,(title,bounds,layer) in enumerate(panels):
 l=(idx%2)*800+10;t=(idx//2)*650+10;r=l+780;b=t+630;d.rectangle((l,t,r,b),outline='grey',width=2);d.text((l+10,t+10),title,font=font,fill='black')
 xmin,ymin,xmax,ymax=bounds;sc=min(750/(xmax-xmin),565/(ymax-ymin));ox=(l+r)/2-(xmin+xmax)*sc/2;oy=(t+b+40)/2+(ymin+ymax)*sc/2
 def pt(x,y):return (ox+x*sc,oy-y*sc)
 def render(s,col):
  for p in parts(s.intersection(box(*bounds))):
   if p.geom_type!='Polygon' or p.is_empty:continue
   d.polygon([pt(x,y) for x,y in p.exterior.coords],fill=col)
   for ring in p.interiors:d.polygon([pt(x,y) for x,y in ring.coords],fill='white')
 for net,s in net_shapes[layer].items():render(s,colors.get(net,'#b9bdc2'))
 for tr in live['lines']:
  if tr['net'] not in ['USB_HOST_DP','USB_HOST_DM','SCREEN_DP','SCREEN_DM'] or tr['layer']==layer:continue
  ln=LineString([(tr['startX']*.0254,tr['startY']*.0254),(tr['endX']*.0254,tr['endY']*.0254)]).intersection(box(*bounds))
  if ln.geom_type=='LineString' and not ln.is_empty:d.line([pt(x,y) for x,y in ln.coords],fill='#3157aa' if layer==1 else '#d82317',width=2)
 if idx<2:
  for p in parts(paste.intersection(box(*bounds))):
   if p.geom_type=='Polygon':d.line([pt(x,y) for x,y in p.exterior.coords],fill='#8c248c',width=2)
 for c in live['components']:
  x,y=c['x']*.0254,c['y']*.0254
  if xmin<x<xmax and ymin<y<ymax:
   px,py=pt(x,y);d.text((px+3,py-18),c['ref'],font=small,fill='black')
 for v in live['vias']:
  x,y=v['x']*.0254,v['y']*.0254
  if xmin<x<xmax and ymin<y<ymax:
   px,py=pt(x,y);rr=v['holeDiameter']*.0254/2*sc;d.ellipse((px-rr,py-rr,px+rr,py+rr),fill='#333333')
im.save(BASE/'layout-review-copper.png')
