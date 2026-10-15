from pathlib import Path
from tempfile import TemporaryDirectory
from collections import defaultdict
import json,zipfile,hashlib,warnings,re
from gerbonara import GerberFile,ExcellonFile
from shapely.geometry import Point,Polygon,GeometryCollection,box
from shapely.ops import unary_union
from shapely import make_valid
from PIL import Image,ImageDraw,ImageFont

BASE=Path(__file__).parent
ORDER=BASE/'Gridopoly_PCB1_20260927_204456.zip'
RC5=BASE.parent/'Layout-2026-09-27-RC5/NativeExports/PlayerPower-R1-RC5-Gerber.zip'

def shape(data,name):
    with TemporaryDirectory() as t:
        p=Path(t)/name;p.write_bytes(data)
        result=GeometryCollection();batch=[];polarity=None
        for obj in GerberFile.open(p).objects:
            for prim in obj.to_primitives():
                points=prim.to_arc_poly().approximate_arcs(max_error=.001).outline
                if len(points)<3:continue
                if batch and prim.polarity_dark!=polarity:
                    s=unary_union(batch);result=result.union(s) if polarity else result.difference(s);batch=[]
                batch.append(make_valid(Polygon(points)));polarity=prim.polarity_dark
        if batch:
            s=unary_union(batch);result=result.union(s) if polarity else result.difference(s)
        return result

def polys(s):
    return [p for p in (s.geoms if hasattr(s,'geoms') else [s]) if p.geom_type=='Polygon']

def analyze(archive):
    z=zipfile.ZipFile(archive)
    copper={side:shape(z.read(name),name) for side,name in [('T','Gerber_TopLayer.GTL'),('B','Gerber_BottomLayer.GBL')]}
    nodes=[];layers={}
    for side,c in copper.items():
        layers[side]=[]
        for p in polys(c):layers[side].append(len(nodes));nodes.append(p)
    parent=list(range(len(nodes)))
    def root(i):
        while i!=parent[i]:parent[i]=parent[parent[i]];i=parent[i]
        return i
    def join(ids):
        if ids:
            r=root(ids[0])
            for i in ids[1:]:parent[root(i)]=r
    def locate(side,x,y):
        return [i for i in layers[side] if nodes[i].distance(Point(x,y))<.003]
    holes=[]
    with TemporaryDirectory() as t:
        for name in ['Drill_PTH_Through.DRL']:
            p=Path(t)/name;p.write_bytes(z.read(name))
            with warnings.catch_warnings():
                warnings.simplefilter('ignore');drills=ExcellonFile.open(p)
            for h in drills.objects:
                if hasattr(h,'x'):
                    x,y=h.x,h.y;points=[(x,y)];slot=None
                elif all(hasattr(h,k) for k in ['x1','x2','y1','y2']):
                    x,y=(h.x1+h.x2)/2,(h.y1+h.y2)/2
                    points=[(h.x1,h.y1),(x,y),(h.x2,h.y2)];slot=[h.x1,h.y1,h.x2,h.y2]
                else:raise ValueError('Unsupported plated drill primitive')
                ids=list(set(i for px,py in points for side in ['T','B'] for i in locate(side,px,py)))
                join(ids);holes.append({'x':x,'y':y,'diameter':h.aperture.diameter,'nodes':ids,'slot':slot})
    probe=json.loads(z.read('FlyingProbeTesting.json'))
    assert probe['lengthUnit']=='mil'
    records=[dict(zip(probe['pins']['fields'],r)) for r in probe['pins']['rows']]
    # Named component pins only. PADxxx entries are duplicate CAM records, not extra devices.
    records={p['PIN_NAME']:p for p in records if not p['PIN_NAME'].startswith('PAD')}
    terminals=[];missing=[]
    for name,p in records.items():
        x,y=p['PIN_X']*.0254,p['PIN_Y']*.0254
        sides=['T','B'] if p['PIN_TYPE']=='DIP' else [p['LAYER']]
        ids=[]
        for side in sides:ids+=locate(side,x,y)
        if not ids:missing.append(name)
        terminals.append({'name':name,'net':p['NET_NAME'] or '', 'nodes':ids,'xyMm':[x,y]})
    groups=defaultdict(list);by_net=defaultdict(set)
    for t in terminals:
        for r in set(root(i) for i in t['nodes']):
            groups[r].append(t)
            if t['net']:by_net[t['net']].add(r)
    shorts=[];nc=[]
    for r,ts in groups.items():
        nets=sorted(set(t['net'] for t in ts if t['net']))
        if len(nets)>1:shorts.append({'nets':nets,'pins':[t['name'] for t in ts]})
        if nets and any(not t['net'] for t in ts):nc.append({'nets':nets,'nc':[t['name'] for t in ts if not t['net']]})
    opens=[{'net':n,'islands':len(rs)} for n,rs in by_net.items() if len(rs)>1]
    j3=[t for t in terminals if t['name'].startswith('J3_')]
    detail=[]
    for t in j3:
        detail.append({**t,'sameLayerNets':{side:sorted(set(u['net'] for u in terminals if any(i in u['nodes'] for i in locate(side,*t['xyMm'])) and u['net'])) for side in ['T','B']}})
    return {'copper':copper,'nodes':nodes,'layers':layers,'holes':holes,'probe':probe,'terminals':terminals,'by_net':by_net,'root':root,'report':{'zip':archive.name,'zipSha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'uniqueComponentPins':len(terminals),'platedDrills':len(holes),'rawNetNames':len(by_net),'humanReadableNetNames':sum(not re.fullmatch(r'NET_\d+',n) for n in by_net),'automaticNetNames':sum(bool(re.fullmatch(r'NET_\d+',n)) for n in by_net),'shorts':shorts,'opens':opens,'ncConnections':nc,'missingPins':missing,'j3':detail,'passed':not any([shorts,opens,nc,missing])}}

order=analyze(ORDER);fixed=analyze(RC5)
diff={}
for side in ['T','B']:
    extra=order['copper'][side].difference(fixed['copper'][side])
    missing=fixed['copper'][side].difference(order['copper'][side])
    diff[side]={'orderOnlyAreaMm2':extra.area,'rc5OnlyAreaMm2':missing.area,'orderOnlyBounds':[list(p.bounds) for p in polys(extra) if p.area>.001],'rc5OnlyBounds':[list(p.bounds) for p in polys(missing) if p.area>.001]}

report={'method':'Copper boolean geometry from each ZIP; connectivity joined only at that ZIP actual plated drills; expected nets and coordinates from that ZIP FlyingProbeTesting.json. 1um arc approximation, 3um point identification; no geometric gap healing.', 'limitations':['File evidence only, does not inspect physical assembly or prove there are no other electrical/thermal issues.','FlyingProbeTesting.json presence does not prove the factory performed a fly-probe test.','Photos of the actual board still required before defining manual rework.'], 'order':order['report'],'rc5':fixed['report'],'geometryDifference':diff}
(BASE/'order-package-audit.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf8')

W,H=1480,1190
im=Image.new('RGB',(W,H),'#f3f5f8');d=ImageDraw.Draw(im)
fontpath='C:/Windows/Fonts/msyh.ttc'
font=ImageFont.truetype(fontpath,23);small=ImageFont.truetype(fontpath,19);title=ImageFont.truetype(fontpath,31)
d.text((25,15),'打样文件已确认：J3.2（BAT+）接入顶层地铜',font=title,fill='#a31919')
d.text((25,65),'同一坐标视角：元件面朝上、USB-C 朝下；底层图未镜像，不是背面操作图。',font=font,fill='#334155')
d.text((25,100),'黄色：铜箔   深色：无铜/孔   红圈：错误接地连接',font=small,fill='#334155')
panels=[('打样包 顶层 20:44:56',order,'T',(25,145,725,610)),('RC5 修复后 顶层 20:57:30',fixed,'T',(755,145,1455,610)),('打样包 底层（透视）',order,'B',(25,640,725,1100)),('RC5 修复后 底层（透视）',fixed,'B',(755,640,1455,1100))]
for label,obj,side,rect in panels:
    l,t,r,b=rect;d.rounded_rectangle(rect,12,fill='#102237',outline='#8e9bab',width=2)
    d.text((l+15,t+10),label,font=font,fill='white')
    bounds=(36.8,-7.2,43.2,-2.8);xmin,ymin,xmax,ymax=bounds
    scale=min((r-l-40)/(xmax-xmin),(b-t-100)/(ymax-ymin));ox=(l+r)/2-(xmin+xmax)*scale/2;oy=(t+b+20)/2+(ymin+ymax)*scale/2
    def pt(x,y):return ox+x*scale,oy-y*scale
    for p in sorted(polys(obj['copper'][side].intersection(box(*bounds))),key=lambda p:-p.area):
        d.polygon([pt(x,y) for x,y in p.exterior.coords],fill='#d7b15b')
        for ring in p.interiors:d.polygon([pt(x,y) for x,y in ring.coords],fill='#102237')
    for h in obj['holes']:
        if xmin<h['x']<xmax and ymin<h['y']<ymax:
            px,py=pt(h['x'],h['y']);rr=h['diameter']*scale/2;d.ellipse((px-rr,py-rr,px+rr,py+rr),fill='#102237')
    for x,txt in [(38.99916,'1：GND'),(41.00068,'2：BAT+')]:
        px,py=pt(x,-5.00126)
        d.text((px-42,py+51),txt,font=small,fill='white')
    if obj is order and side=='T':
        px,py=pt(41.00068,-5.00126);rr=1.12*scale;d.ellipse((px-rr,py-rr,px+rr,py+rr),outline='#ff4141',width=5)
        d.text((l+20,b-37),'BAT+ 焊盘仍通过地铜连接条接地',font=small,fill='#ff9090')
    else:d.text((l+20,b-37),'请勿直接按此透视图进行割线',font=small,fill='#becbd9')
d.text((25,1135),'结论：这份打样 Gerber 自身有短路。当前工程的 DRC 0 不适用于它；保持电池和 USB 断开。',font=font,fill='#a31919')
im.save(BASE/'order-vs-rc5-j3.png')
print(json.dumps({'order':{k:v for k,v in order['report'].items() if k not in ['shorts','j3']},'shortedNets':[s['nets'] for s in order['report']['shorts']],'rc5Passed':fixed['report']['passed'],'differences':diff},ensure_ascii=False,indent=2))
