"""First-order resistance of the captured trace/via centerline paths, not a thermal qualification."""
from pathlib import Path
import hashlib
import heapq
import json
import math
from collections import defaultdict

B = Path(__file__).resolve().parent
f = B / 'before-live-audit.json'
live = json.loads(f.read_text(encoding='utf-8'))
MM = .0254
RHO = 1.724e-5  # ohm mm, nominal copper at 20 C
THICKNESS = .035  # nominal 1 oz, not a fabricator minimum
BARREL = .025  # assumed 25 um plating; must be confirmed by fabricator
BOARD = 1.6
def pad(ref, pin):
    c = next(c for c in live['components'] if c['ref'] == ref)
    return next(p for p in c['pads'] if p['padNumber'] == pin)
def xy(p):
    return (p['x'] * MM, p['y'] * MM)
def distance(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])

def solve(net, begin, end):
    lines = [l for l in live['lines'] if l['net'] == net]
    vias = [v for v in live['vias'] if v['net'] == net]
    points = []
    def point(layer, p):
        for i, (la, q) in enumerate(points):
            if layer == la and distance(p, q) < .003:
                return i
        points.append((layer, p))
        return len(points) - 1
    for l in lines:
        point(l['layer'], (l['startX'] * MM, l['startY'] * MM))
        point(l['layer'], (l['endX'] * MM, l['endY'] * MM))
    for v in vias:
        point(1, xy(v)); point(2, xy(v))
    startpad = pad(*begin); endpad = pad(*end)
    starts = [point(layer, xy(startpad)) for layer in ([1,2] if startpad['layer'] == 12 else [startpad['layer']])]
    ends = [point(layer, xy(endpad)) for layer in ([1,2] if endpad['layer'] == 12 else [endpad['layer']])]
    graph = defaultdict(list)
    def edge(a,b,res,desc):
        graph[a].append((b,res,desc)); graph[b].append((a,res,desc))
    for c in live['components']:
        for p in c['pads']:
            if p.get('net') != net or p['pad'][0] not in ('RECT','ROUND','ELLIPSE','OVAL'):
                continue
            px,py=xy(p); ang=math.radians(p['rotation']); w=p['pad'][1]*MM; h=p['pad'][2]*MM
            hits=[]
            for i,(layer,q) in enumerate(points):
                if p['layer'] != 12 and layer != p['layer']:continue
                dx,dy=q[0]-px,q[1]-py
                u=dx*math.cos(ang)+dy*math.sin(ang);v=-dx*math.sin(ang)+dy*math.cos(ang)
                inside=abs(u)<=w/2+.003 and abs(v)<=h/2+.003
                if p['pad'][0] in ('ROUND','ELLIPSE'):
                    inside=(u/(w/2+.003))**2+(v/(h/2+.003))**2<=1
                if inside:hits.append(i)
            for i in hits[1:]:edge(hits[0],i,0,{'type':'pad-contact','ref':c['ref'],'pin':p['padNumber']})
    for l in lines:
        a = (l['startX']*MM,l['startY']*MM); b = (l['endX']*MM,l['endY']*MM)
        dx,dy=b[0]-a[0],b[1]-a[1]; length=distance(a,b)
        if length < 1e-9:
            continue
        candidates=[]
        for i,(layer,p) in enumerate(points):
            if layer!=l['layer']:
                continue
            t=((p[0]-a[0])*dx+(p[1]-a[1])*dy)/(length*length)
            projection=(a[0]+t*dx,a[1]+t*dy)
            if -.003/length <= t <= 1+.003/length and distance(projection,p)<=l['lineWidth']*MM/2+.003:
                candidates.append((max(0,min(1,t)),i))
        candidates.sort()
        for (t,i),(u,j) in zip(candidates,candidates[1:]):
            length_mm=(u-t)*length; width=l['lineWidth']*MM
            edge(i,j,RHO*length_mm/(THICKNESS*width),{'type':'trace','id':l['primitiveId'],'lengthMm':length_mm,'widthMm':width,'layer':l['layer']})
    for v in vias:
        resistance=RHO*BOARD/(math.pi*v['holeDiameter']*MM*BARREL)
        edge(point(1,xy(v)),point(2,xy(v)),resistance,{'type':'via','id':v['primitiveId'],'holeMm':v['holeDiameter']*MM})
        for layer in (1,2):
            center=point(layer,xy(v))
            for i,(la,q) in enumerate(points):
                endpoint_width=max([l['lineWidth']*MM for l in lines if l['layer']==la and min(distance(q,(l['startX']*MM,l['startY']*MM)),distance(q,(l['endX']*MM,l['endY']*MM)))<.003] or [0])
                if la==layer and i!=center and distance(q,xy(v))<=v['diameter']*MM/2+endpoint_width/2+.003:
                    edge(center,i,0,{'type':'via-pad-contact','id':v['primitiveId']})
    best={s:0 for s in starts}; queue=[(0,s) for s in starts]; heapq.heapify(queue); previous={}
    found=None
    while queue:
        cost,i=heapq.heappop(queue)
        if cost>best[i]+1e-12:continue
        if i in ends:found=i;break
        for j,r,desc in graph[i]:
            if cost+r<best.get(j,float('inf')):
                best[j]=cost+r;previous[j]=(i,desc);heapq.heappush(queue,(cost+r,j))
    if found is None:
        return {'net':net,'from':begin,'to':end,'pathFound':False}
    path=[];i=found
    while i in previous:
        i,desc=previous[i];path.append(desc)
    path.reverse()
    return {'net':net,'from':'.'.join(begin),'to':'.'.join(end),'pathFound':True,'nominalResistanceOhm20C':best[found], 'traceLengthMm':sum(p.get('lengthMm',0) for p in path),'viaCount':sum(p['type']=='via' for p in path),'path':path}

routes=[solve('BAT_PACK',('J3','1'),('U1','3')),solve('SYS_RAW',('U1','10'),('L1','1')),solve('5V_SCREEN',('U2','6'),('J2','10')),solve('USB_VBUS',('J1','A4B9'),('U1','13'))]
for r,current in zip(routes,[1.0,1.0,.5,.9]):
    if r['pathFound']:
        resistance=r['nominalResistanceOhm20C']
        r['exampleCurrentA']=current
        r['exampleDropMv']=resistance*current*1000
        r['exampleCopperLossW']=current*current*resistance
report={'inputSha256':hashlib.sha256(f.read_bytes()).hexdigest(),'assumptions':{'copperResistivityOhmMm20C':RHO,'traceCopperMm':THICKNESS,'viaBarrelMm':BARREL,'boardMm':BOARD},'limitations':['A first-order path estimate, not a field/thermal solver. Connections across trace width and pads are idealized to zero transverse resistance; pad/parallel copper, etch tolerance and temperature are not solved.', 'Excludes battery wire/protection resistance, connector contacts, ground return, BQ24074 pass FETs and transient inductance.', 'Example currents are calculation cases, not proven connector or board load ratings. J2 catalog rating is 0.5 A/contact.'], 'routes':routes}
(B/'power-path-resistance.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps([{k:v for k,v in r.items() if k!='path'}for r in routes],indent=2))
