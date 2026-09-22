from pathlib import Path
import json,math
p=Path('Docs/hardware/interconnect-audit-2026-09-21')
d=json.loads((p/'evidence.json').read_text(encoding='utf-8'))
M=.0254
def rot(v,a):
 c=round(math.cos(math.radians(a)));s=round(math.sin(math.radians(a)));return [c*v[0]-s*v[1],s*v[0]+c*v[1]]
def add(a,b):return [a[0]+b[0],a[1]+b[1]]
def sub(a,b):return [a[0]-b[0],a[1]-b[1]]
def center(b,ref):
 v=next(c['body'] for c in d[b]['connectors'] if c['attrs']['Designator']==ref);return [v['x']*M,v['y']*M]
def port(b,side):return add(center(b,'J'+side+'1'),rot([50*M,0],next(c['body']['angle'] for c in d[b]['connectors'] if c['attrs']['Designator']=='J'+side+'1')))
# Pin 4: male local +50 mil; female local +50 mil as well.
# All outlines/centres come from live source. Mating row spacing remains an assumption.
seq=(['corner']+['long']*7)*4
q=0;t=[0,0];placed=[];rowgap=14.5
for b in seq:
 placed.append({'board':b,'angle':q,'translation_mm':t})
 nxtq=(q+(90 if b=='corner' else 0))%360
 endpoint=add(add(rot(port(b,'OUT'),q),t),rot([rowgap,0],nxtq))
 t=sub(endpoint,rot(port('long','IN'),nxtq));q=nxtq
result={'scenario':'4 corners + 28 straight modules; 7 per side','assumed_pad_row_spacing_mm':rowgap,'closure_residual_mm':math.hypot(*t),'closure_angle_deg':q,'placements':placed,'long_output_padrow_normal_offset_mm':(1925-1920)*M,'connector_group_center_spacing_mm':530*M,'pin_pitch_mm':100*M,'note':'2D nominal geometry only; spacing is illustrative, not a released assembly dimension.'}
errs=[]
for prev,nxt,ang in [('long','long',0),('long','corner',0),('corner','long',90),('corner','corner',90)]:
 a={x['pin']:x for x in d[prev]['pins_api']['JOUT1']};b={x['pin']:x for x in d[nxt]['pins_api']['JIN1']}
 base_a=[a['4']['x'],a['4']['y']];base_b=rot([b['4']['x'],b['4']['y']],ang);shift=sub(base_a,base_b);axis=0 if ang==90 else 1
 for k in a:
  bp=add(rot([b[k]['x'],b[k]['y']],ang),shift)
  err=abs([a[k]['x'],a[k]['y']][axis]-bp[axis])*M;errs.append(err);assert err<.005
  assert a[k]['net']==b[k]['net'] or (a[k]['net']=='ORDER_OUT' and b[k]['net']=='ORDER_IN')
 ga=sorted([x['x'],x['y']][axis] for x in d[prev]['pins_api']['JOUT3'])
 gb=sorted(add(rot([x['x'],x['y']],ang),shift)[axis] for x in d[nxt]['pins_api']['JIN3'])
 for aa,bb in zip(ga,gb):errs.append(abs(aa-bb)*M);assert abs(aa-bb)*M<.005
result['tangential_pin_checks']=len(errs)
result['max_tangential_error_mm']=max(errs)
(p/'closure-model.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
# Model figures omit rounded corners. No footprint, PCB or library modification.
shapes=[];pts=[]
for i,a in enumerate(placed):
 b=a['board'];q=a['angle'];t=a['translation_mm']
 if b=='corner':rects=[[-114.6798,-3152.8798,3034.9265,-3.2735]]
 else:rects=[[3.4304,-3152.8798,1893.1942,-3.2735],[-114.6798,-2089.8877,3.4304,-1066.2536],[1893.1942,-2089.8896,2011.3044,-1066.2656]]
 for x0,y0,x1,y1 in rects:
  v=[add(rot([x*M,y*M],q),t) for x,y in [(x0,y0),(x1,y0),(x1,y1),(x0,y1)]];pts+=v
  shapes.append('<polygon points="'+ ' '.join(f'{x:.3f},{-y:.3f}' for x,y in v)+'" fill="'+('#cfe2ff' if b=='corner' else '#e3eee7')+'" stroke="#34465b" stroke-width="0.6"/>')
 mid=center(b,'JIN1');mid=[(1460.12335 if b=='corner' else 948.3123)*M,-1578.07665*M];mid=add(rot(mid,q),t)
 shapes.append(f'<text x="{mid[0]:.3f}" y="{-mid[1]:.3f}" text-anchor="middle" dominant-baseline="middle" font-size="8" fill="#24364a">{i+1}{"C" if b=="corner" else "L"}</text>')
 for side,color in [('IN','#ef8d32'),('OUT','#306bd8')]:
  point=add(rot(port(b,side),q),t);shapes.append(f'<circle cx="{point[0]:.3f}" cy="{-point[1]:.3f}" r="2.2" fill="{color}"/>')
x0=min(v[0] for v in pts)-20;x1=max(v[0] for v in pts)+20;y0=-max(v[1] for v in pts)-35;y1=-min(v[1] for v in pts)+20
cx=(x0+x1)/2
svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="1000" viewBox="{x0} {y0} {x1-x0} {y1-y0}" font-family="Arial,sans-serif"><rect x="{x0}" y="{y0}" width="{x1-x0}" height="{y1-y0}" fill="white"/><text x="{cx}" y="{y0+12}" text-anchor="middle" font-size="10">32-module nominal assembly: 4 C + 28 L</text>'+''.join(shapes)+f'<text x="{cx}" y="{(y0+y1)/2}" text-anchor="middle" font-size="10">Orange: IN / male; Blue: OUT / female</text><text x="{cx}" y="{(y0+y1)/2+16}" text-anchor="middle" font-size="8">Illustrative mating spacing. RS-485 loop requires a data break.</text></svg>'
(p/'32-module-layout.svg').write_text(svg,encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='placements'},ensure_ascii=False,indent=2))
