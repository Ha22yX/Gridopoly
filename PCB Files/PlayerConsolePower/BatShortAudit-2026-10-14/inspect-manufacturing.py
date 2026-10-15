from pathlib import Path
import json, zipfile, tempfile, runpy, contextlib, io
from collections import defaultdict
from gerbonara import GerberFile
from shapely.geometry import Polygon, GeometryCollection, Point, box
from shapely.ops import unary_union, nearest_points
from shapely import make_valid
from PIL import Image, ImageDraw, ImageFont

BASE=Path(__file__).parent
with contextlib.redirect_stdout(io.StringIO()):
    g=runpy.run_path(str(BASE/'verify-current-copper.py'))
polys=g['polygons']; layer_ids=g['layers']; root=g['root']; by_net=g['by_net']; live=g['LIVE']
comp={c['ref']:c for c in live['components']}
net_shapes={layer:{net:unary_union([polys[i] for i in ids if root(i) in roots]) for net,roots in by_net.items()} for layer,ids in layer_ids.items()}

def gerber_shape(z,name):
    with tempfile.TemporaryDirectory() as t:
        f=Path(t)/name;f.write_bytes(z.read(name)); out=GeometryCollection();batch=[];polarity=None
        for obj in GerberFile.open(f).objects:
            for prim in obj.to_primitives():
                outline=prim.to_arc_poly().approximate_arcs(max_error=.001).outline
                if len(outline)<3:continue
                if batch and prim.polarity_dark!=polarity:
                    shape=unary_union(batch);out=out.union(shape) if polarity else out.difference(shape);batch=[]
                polarity=prim.polarity_dark;batch.append(make_valid(Polygon(outline)))
        if batch:
            shape=unary_union(batch);out=out.union(shape) if polarity else out.difference(shape)
        return out

def parts(shape):
    return list(shape.geoms) if hasattr(shape,'geoms') else [shape]

z=zipfile.ZipFile(BASE/'current-Gerber.zip')
paste=gerber_shape(z,'Gerber_TopPasteMaskLayer.GTP')
mask=gerber_shape(z,'Gerber_TopSolderMaskLayer.GTS')
paste_bridges=[]
for shape in parts(paste):
    nets=[n for n,s in net_shapes[1].items() if s.intersection(shape).area>1e-7]
    if len(nets)>1:paste_bridges.append({'nets':nets,'boundsMm':list(shape.bounds)})

report={'copperBatGndClearanceMm':{},'pasteBridgeCandidates':paste_bridges,'parts':{},'limitations':['Digital current export only; physical solder and actual order package are not inspected.','Mask openings can be merged by fab process; this does not assert solder quality.']}
for layer in [1,2]:
    bat=net_shapes[layer]['BAT_PACK'];ground=net_shapes[layer]['GND']
    pair=nearest_points(bat,ground)
    report['copperBatGndClearanceMm'][str(layer)]={'distance':bat.distance(ground),'points':[list(p.coords[0]) for p in pair]}
for ref in ['U1','U10','C2','C19','J3']:
    c=comp[ref];x,y=c['x']*.0254,c['y']*.0254
    region=box(x-2.1,y-2.1,x+2.1,y+2.1)
    report['parts'][ref]={'centerTopViewMm':[x,-y],'pasteAreaWithin4p2mmSquare':paste.intersection(region).area,'maskAreaWithin4p2mmSquare':mask.intersection(region).area}

oldzip=BASE.parent/'Layout-2026-09-27-RC5/Manufacturing-RC5/PlayerPower-R1-RC5-Gerber.zip'
oldpaste=gerber_shape(zipfile.ZipFile(oldzip),'Gerber_TopPasteMaskLayer.GTP')
u1=comp['U1'];x,y=u1['x']*.0254,u1['y']*.0254
region=box(x-.86,y-.86,x+.86,y+.86)
report['u1CentralPasteAreaMm2']={'current':paste.intersection(region).area,'frozenRc5':oldpaste.intersection(region).area}

W,H=1500,1180
img=Image.new('RGB',(W,H),'#f8fafc');d=ImageDraw.Draw(img)
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',18);small=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',15);title=ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf',25)
d.text((25,15),'PLAYER POWER - BAT short diagnostic locations',font=title,fill='#122436')
d.text((25,52),'Current CAD, not a photograph. Top view throughout; USB-C is at the bottom.',font=font,fill='#334155')
d.text((25,80),'Red = BAT_PACK     Green = GND     Gold outline = paste opening     Grey = other copper',font=font,fill='#334155')
panels=[('J3 battery connector',(35,-10,45,0),(25,120,700,580)),('U1 charger / C2 battery capacitor',(48,-68,59,-59),(775,120,1475,580)),('U10 fuel gauge / C19',(56,-52,62,-44),(25,650,700,1120)),('Whole board - top view',(0,-80,80,0),(775,650,1475,1120))]
for label,bounds,rect in panels:
    l,t,r,b=rect;d.rounded_rectangle(rect,12,fill='white',outline='#b9c4d0',width=2);d.text((l+14,t+10),label,font=font,fill='#122436')
    xmin,ymin,xmax,ymax=bounds;scale=min((r-l-40)/(xmax-xmin),(b-t-80)/(ymax-ymin));ox=(l+r)/2-(xmin+xmax)*scale/2;oy=(t+b+30)/2+(ymin+ymax)*scale/2
    def point(x,y):return (ox+x*scale,oy-y*scale)
    clip=box(*bounds)
    def render(s,fill,outline=None):
        for p in parts(s.intersection(clip)):
            if p.geom_type!='Polygon':continue
            exterior=[point(x,y) for x,y in p.exterior.coords]
            if fill:d.polygon(exterior,fill=fill)
            if outline:d.line(exterior,fill=outline,width=2)
            for ring in p.interiors:
                coords=[point(x,y) for x,y in ring.coords]
                if fill:d.polygon(coords,fill='white')
                if outline:d.line(coords,fill=outline,width=2)
    render(unary_union([polys[i] for i in layer_ids[1]]),'#dee3e9')
    render(net_shapes[1]['GND'],'#b8d8bf')
    render(net_shapes[1]['BAT_PACK'],'#ea7a75')
    if 'Whole' not in label:render(paste,None,'#ae7400')
    else:
        for trace in live['lines']:
            if trace['net']=='BAT_PACK':d.line([point(trace['startX']*.0254,trace['startY']*.0254),point(trace['endX']*.0254,trace['endY']*.0254)],fill='#e43d30',width=3)
    for ref in ['J3','U1','C2','U10','C19']:
        c=comp[ref];x,y=c['x']*.0254,c['y']*.0254
        if not (xmin<x<xmax and ymin<y<ymax):continue
        px,py=point(x,y);d.text((px+5,py-25),ref,font=font,fill='#102a43')
        if 'Whole' not in label:
            for pad in c['pads']:
                if pad['net'] in ['BAT_PACK','GND']:
                    px,py=point(pad['x']*.0254,pad['y']*.0254)
                    label=pad['padNumber']+(' +' if pad['net']=='BAT_PACK' else ' -')
                    d.text((px-10,py-8),label,font=small,fill='#101820')
d.text((25,1145),'All power disconnected. Do not connect the battery or USB while a persistent near-zero resistance remains.',font=font,fill='#8b1e16')
img.save(BASE/'bat-short-locations.png')
(BASE/'manufacturing-detail-review.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report,indent=2))
