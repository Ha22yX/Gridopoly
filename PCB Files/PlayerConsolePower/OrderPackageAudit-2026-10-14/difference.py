from pathlib import Path
import sys,json
sys.path.insert(0,r'C:/Users/kicof/AppData/Local/Temp/gridopoly-player-short-20261014/deps')
from shapely import from_wkt
from shapely.geometry import box,Point
from PIL import Image,ImageDraw,ImageFont
p=Path(r'C:/Users/kicof/AppData/Local/Temp/gridopoly-order-independent-20261014')
report={}
for layer in ['TopLayer.GTL','BottomLayer.GBL']:
 a=from_wkt((p/('order-'+layer+'-full.wkt')).read_text());b=from_wkt((p/('rc5-'+layer+'-full.wkt')).read_text())
 added=a.difference(b);removed=b.difference(a);delta=a.symmetric_difference(b)
 pieces=list(delta.geoms)if hasattr(delta,'geoms')else[delta]
 large=[x for x in pieces if x.area>.001]
 report[layer]={'deltaAreaMm2':delta.area,'bounds':delta.bounds,'majorDifferences': [{'area':x.area,'bounds':x.bounds}for x in large], 'deltaOutsideJ3Mm2':delta.difference(box(37,-7,43,-3)).area}
print(json.dumps(report,indent=2));(p/'geometry-difference.json').write_text(json.dumps(report,indent=2))
# Pure Gerber geometry, showing pads and thermal reliefs without inference about a physical board.
im=Image.new('RGB',(1600,820),'white');d=ImageDraw.Draw(im);f=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',25);small=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',20)
roi=(37,-7.5,43,-2.5);s=110
for i,key in enumerate(['order','rc5']):
 ox=30+i*800;oy=90
 d.text((ox,20),('ORDER 2026-09-27 20:44:56' if key=='order'else'FROZEN RC5 - after copper rebuild'),font=f,fill='black')
 g=from_wkt((p/(key+'-TopLayer.GTL-full.wkt')).read_text()).intersection(box(*roi))
 def xy(coords):return[(ox+(x-roi[0])*s,oy+(roi[3]-y)*s)for x,y in coords]
 parts=list(g.geoms) if hasattr(g,'geoms')else[g]
 for q in sorted(parts,key=lambda x:x.area,reverse=True):
  if q.geom_type!='Polygon':continue
  d.polygon(xy(q.exterior.coords),fill='#bd751e')
  for h in q.interiors:d.polygon(xy(h.coords),fill='white')
 for label,x in [('J3.1 GND',38.99916),('J3.2 BAT+',41.00068)]:
  y=-5.00126;cx,cy=xy([(x,y)])[0];r=.4*s
  d.ellipse((cx-r,cy-r,cx+r,cy+r),fill='#333333')
  d.text((cx-64,oy+575),label,font=small,fill='black')
 d.text((ox,oy+620),'Top copper, component side. Drill diameter: 0.8 mm.',font=small,fill='black')
 if key=='order':d.text((ox,oy+655),'BAT+ pad has three unwanted copper spokes to GND.',font=small,fill='#be0000')
 else:d.text((ox,oy+655),'BAT+ has clearance from GND copper.',font=small,fill='#004f00')
im.save(p/'independent-j3-comparison.png')
