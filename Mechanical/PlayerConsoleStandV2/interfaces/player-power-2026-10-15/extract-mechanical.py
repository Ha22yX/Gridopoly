"""Read-only extraction of RC6-TP PCB mechanical facts; no EDA/PCB mutation."""
from pathlib import Path
import json, csv, math, zipfile, hashlib, re, sys

ROOT=Path('Z:/Files/我的项目/Gridopoly')
OUT=Path(__file__).parent
SOURCE=ROOT/'PCB Files/PlayerConsolePower/Testpoints-2026-10-15'
MM=.0254

def records(text):
    result=[]
    for line in text.splitlines():
        if '||' not in line: continue
        a,b=line.split('||',1)
        if not b.rstrip('|'):continue
        result.append((json.loads(a),json.loads(b.rstrip('|'))))
    return result

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def r(v):return round(v,6)
def bounds(points):return [r(min(x for x,y in points)),r(min(y for x,y in points)),r(max(x for x,y in points)),r(max(y for x,y in points))]
def linepoly(path):
    assert set(x for x in path if isinstance(x,str))<= {'L'}, path
    nums=[x for x in path if not isinstance(x,str)]
    return list(zip(nums[0::2],nums[1::2]))
def transformed(points,c):
    a=math.radians(c['rotation']);co,si=math.cos(a),math.sin(a)
    return [[r((c['x']+co*x-si*y)*MM),r(-(c['y']+si*x+co*y)*MM)] for x,y in points]
def segmentdistance(p,a,b):
    dx,dy=b[0]-a[0],b[1]-a[1];den=dx*dx+dy*dy
    t=max(0,min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/den)) if den else 0
    return math.hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy)
def pdist(p,poly):
    inside=False
    for a,b in zip(poly,poly[1:]+poly[:1]):
        if (a[1]>p[1])!=(b[1]>p[1]) and p[0] < (b[0]-a[0])*(p[1]-a[1])/(b[1]-a[1])+a[0]:inside=not inside
    return 0 if inside else min(segmentdistance(p,a,b) for a,b in zip(poly,poly[1:]+poly[:1]))
def paddistance(p,pad):
    x,y=pad['x']*MM,-pad['y']*MM
    # Conservative oriented bounding rectangle for rounded/elliptical pads too.
    shape=pad['pad']
    if shape[0]=='POLYGON':return pdist(p,[(xx*MM,-yy*MM) for xx,yy in linepoly(shape[1])])
    w,h=shape[1]*MM,shape[2]*MM
    a=math.radians(-pad.get('rotation',0));co,si=math.cos(a),math.sin(a)
    poly=[(x+co*dx-si*dy,y+si*dx+co*dy) for dx,dy in [(-w/2,-h/2),(w/2,-h/2),(w/2,h/2),(-w/2,h/2)]]
    return pdist(p,poly)

native=records((SOURCE/'PCB1-after.source.jsonl').read_text(encoding='utf8'))
snapshot=json.loads((SOURCE/'snapshot-after.json').read_text(encoding='utf8'))
with zipfile.ZipFile(SOURCE/'PlayerPower-R1-RC6-TP-20261015.epro2') as z:
    allrows=records(z.read('PlayerPower-R1-RC6-TP-20261015.epru').decode('utf8'))
docs={};current=None
for head,value in allrows:
    if head['type']=='DOCHEAD':current=value['uuid'];docs[current]=[]
    docs[current].append((head,value))
attrs={}
for head,value in native:
    if head['type']=='ATTR':attrs.setdefault(value.get('parentId'),{})[value['key']]=value['value']

components=[]
for c in snapshot['components']:
    fp=attrs[c['id']]['Footprint'];rows=docs[fp]
    meta=next(v for h,v in rows if h['type']=='META')
    bodies=[transformed(linepoly(v['path']),c) for h,v in rows if v.get('layerId')==48 and 'path' in v]
    d={'ref':c['ref'],'side':'TOP' if c['layer']==1 else str(c['layer']), 'source_footprint_uuid':fp,'footprint_name':meta['title'],
       'anchor_mm':[r(c['x']*MM),r(-c['y']*MM)],'source_rotation_deg_ccw':c['rotation'],
       'body_polygons_xy_mm':bodies,'body_bbox_xy_mm':bounds(sum(bodies,[])) if bodies else None,
       'height_above_pcb_top_mm':None,'height_evidence':'No assembled height in source; individual data-sheet values below are separate.'}
    components.append(d)
byref={c['ref']:c for c in components}
holes=[]
for h,v in native:
    if h['type']=='PAD' and v.get('hole',{}).get('width',0)>100:
        p=[r(v['centerX']*MM),r(-v['centerY']*MM)]
        bodymin=min((pdist(p,poly),c['ref']) for c in components for poly in c['body_polygons_xy_mm'])
        padmin=min((paddistance(p,pad),c['ref'],pad['padNumber']) for c in snapshot['components'] for pad in c['pads'])
        trackmin=min(segmentdistance(p,(l['startX']*MM,-l['startY']*MM),(l['endX']*MM,-l['endY']*MM))-l['lineWidth']*MM/2 for l in snapshot['lines'])
        viamin=min(math.hypot(p[0]-v2['x']*MM,p[1]+v2['y']*MM)-v2['diameter']*MM/2 for v2 in snapshot['vias'])
        holes.append({'id':h['id'],'xy_mm':p,'drill_mm':r(v['hole']['width']*MM),'plated':v['plated'],
        'nominal_pad_diameter_mm':r(v['defaultPad']['width']*MM),'recommended_insulating_contact_outer_diameter_mm':6,
        'recommended_clearance_bore_mm':3.6,'designed_keepout_radius_mm':4,
        'nearest_body_distance_from_hole_center_mm':r(bodymin[0]),'nearest_body_ref':bodymin[1],
        'nearest_other_pad_bbox_distance_from_hole_center_mm':r(padmin[0]),'nearest_pad_ref':padmin[1],
        'nearest_trace_edge_distance_from_hole_center_mm':r(trackmin),'nearest_via_edge_distance_from_hole_center_mm':r(viamin),
        'contact_scope':'OD6 insulating annulus fits native R4 keepout and clears source component bodies/pads/traces/vias. Mounting pad has no assigned net and exposed metal; use insulating contact. Actual pour/mask geometry not separately recomputed here.'})
holes.sort(key=lambda h:(h['xy_mm'][1],h['xy_mm'][0]))
with zipfile.ZipFile(SOURCE/'current-Gerber.zip') as z:
    drill=z.read('Drill_PTH_Through.DRL').decode('ascii')
    tools={};tool=None;drills=[]
    for line in drill.splitlines():
        m=re.fullmatch(r'T(\d+)C([\d.]+)',line)
        if m:tools[int(m[1])]=float(m[2]);continue
        m=re.fullmatch(r'T(\d+)',line)
        if m:tool=int(m[1]);continue
        m=re.fullmatch(r'X(-?[\d.]+)Y(-?[\d.]+)',line)
        if m and tool:drills.append([float(m[1]),-float(m[2]),tools[tool]])
    for h in holes:
        matches=[d for d in drills if math.dist(h['xy_mm'],d[:2])<.00001 and abs(h['drill_mm']-d[2])<.00001]
        assert len(matches)==1,(h,matches)
        h['actual_gerber_plated_drill_mm']=matches[0][2]
outline=[{'id':h['id'],'path_mil':v['path'],'width_mil':v['width']} for h,v in native if h['type']=='POLY' and v.get('layerId')==11]
fixed=json.loads((ROOT/'PCB Files/PlayerConsolePower/Layout-2026-09-27-RC5/fixed-constraints.json').read_text(encoding='utf8'))
assert len(outline)==8
assert {h['id']:v['path'] for h,v in native if h['type']=='POLY' and v.get('layerId')==11}=={x['h']['id']:x['v']['path'] for x in fixed['frozenBoardOutline']}
assert len(holes)==4
assert all(h['nearest_body_distance_from_hole_center_mm']>3 and h['nearest_other_pad_bbox_distance_from_hole_center_mm']>3 and h['nearest_trace_edge_distance_from_hole_center_mm']>3 for h in holes)

interfaces=[]
specs={
'J1':dict(function='USB-C charging/data',access_direction_xy=[0,1],datum='TOP mounted; opening faces +Y',design_service_bbox_xy_mm=[32,70,48,95],design_height_reserve_above_top_mm=5,height_evidence='Local manufacturer drawing j1-library-linked.png: shell face 2.57±0.05 mm; use 5 mm enclosure reserve, not measured assembled height.'),
'J2':dict(function='Screen FPC 10pin 0.5mm bottom-contact',access_direction_xy=[-1,0],datum='Insertion from -X; leave latch/finger room above',design_service_bbox_xy_mm=[-12,51,9,67],design_height_reserve_above_top_mm=6,height_evidence='Body height not independently confirmed here; 6 mm is design access reserve, not measured part height. Cable bend path requires actual free length.'),
'J3':dict(function='Battery JST PH 2pin top entry',access_direction_xyz=[0,0,1],datum='Pin1 left black/GND; pin2 right red/BAT_PACK',design_service_bbox_xy_mm=[33,0,47,13],design_height_reserve_above_top_mm=15,height_evidence='JST official PH drawing p1: mated top-entry height 8 mm; additional 7 mm chosen for wire bend/service. Unmating motion requires unobstructed access.'),
'SW1':dict(function='POWER',access_direction_xyz=[0,0,1],datum='Top-press switch, not side-actuated',design_service_bbox_xy_mm=[70,21.5,80,32],design_height_reserve_above_top_mm=10,height_evidence='Footprint name H1.5 is a library indication only. Finger/service envelope is design assumption.'),
'SW2':dict(function='RST',access_direction_xyz=[0,0,1],datum='Top-press switch, not side-actuated',design_service_bbox_xy_mm=[70,48,80,58.5],design_height_reserve_above_top_mm=10,height_evidence='Footprint name H1.5 is a library indication only. Finger/service envelope is design assumption.')}
for ref,spec in specs.items():interfaces.append({**byref[ref],**spec})
sourcefiles=['PCB1-after.source.jsonl','snapshot-after.json','PlayerPower-R1-RC6-TP-20261015.epro2','current-Gerber.zip']
data={
 'schema_version':1,'interface_status':'SOURCE_GEOMETRY_VERIFIED / AS_BUILT_Z_AND_FIT_PENDING',
 'board':{'name':'Player power board','revision':'PLAYER POWER R1 RC6-TP','pcb_uuid':'847ec1f4dfc5086a','source_date':'2026-10-15','width_mm':80,'height_mm':80,'corner_radius_mm':5,'thickness_nominal_mm':1.6,'thickness_status':'Design nominal; not physical measurement','copper_layers':2,'populated_top_components':68,'populated_bottom_components':0,'bare_testpoints':8},
 'coordinates':{'xy_view':'Top/component face, USB at bottom; left upper board outline origin; x right, y down','z':'PCB top surface z=0; positive up; PCB bottom nominal z=-1.6','native_conversion':'x_mm=x_mil*0.0254; y_mm=-y_mil*0.0254','bounding_boxes':'[xmin,ymin,xmax,ymax]'},
 'sources':[{'path':str((SOURCE/f).relative_to(ROOT)).replace('\\','/'),'sha256':sha(SOURCE/f)} for f in sourcefiles],
 'outline_native':outline,'mounting_holes':holes,'interfaces':interfaces,'components':components,
 'testpoint_row':{'y_mm':22.5,'x_mm':[8,16,24,32,40,48,56,64],'keep_accessible':'Do not fill through holes with plastic; top probing needs cover removal or access.'},
 'height_evidence':{'maximum_top_assembled_component_height_mm':None,'maximum_bottom_assembled_protrusion_mm':None,'j3_mated_height_above_pcb_top_mm':8,'j3_height_source':'ProductionReadiness-2026-10-14/JST-PH-official.pdf p1','j3_untrimmed_lead_length_from_seating_plane_mm':3.4,'j3_lead_evidence':'JST PH drawing p3; nominal leads imply ~1.8 mm below 1.6 mm PCB before solder; actual trimming and solder height unknown.','L1_height_mm':1.8,'L1_height_tolerance_mm':.2,'L1_source':'ProductionReadiness-2026-10-14/power-datasheets/mwsa.pdf MWSA0402S table','do_not_reuse':'8.1 mm underside height from tile/corner PCBs is unrelated to this PCB.'},
 'battery':{'purchased_model':'EEMB LP103454 1S 2000mAh','cell_datasheet_max_xyz_mm':[56,34.5,10.3],'source':'PCB Files/PlayerConsolePower/ProductionReadiness-2026-10-14/power-datasheets/eemb-lp103454.pdf p3','finished_pack_dimensions_confirmed':False,'note':'Cell dimensional data does not bind actual protected pack, tape, wire bend or swelling allowance. Do not substitute old 103450 52x34x10 dimensions.'},
 'mechanical_limits':['No live EDA manipulation or PCB modification.','No physical height measurements; service envelopes are provisional design reserves.','Insulating OD6 support contact only; keep screw/washer heads within OD6 until actual hardware clearance checked.','Do not claim entire assembled electronics passed from mechanical source checks.','Real repaired PCB may be older batch; user reports operation but exact as-built revision/height not tied to this archive.']}
candidate=[10,10,70,48]
data['battery_tray_lowering_candidate']={'outer_xy_bbox_mm':candidate,'floor_underside_z_above_pcb_top_mm':4,'floor_thickness_mm':2,'status':'DESIGN_CANDIDATE_NOT_MEASURED','clear_of_tall_interface_bodies':['J1','J2','J3','SW1','SW2','L1'],'overlapped_body_refs':[c['ref'] for c in components if c['body_bbox_xy_mm'] and not(c['body_bbox_xy_mm'][2]<candidate[0] or c['body_bbox_xy_mm'][0]>candidate[2] or c['body_bbox_xy_mm'][3]<candidate[1] or c['body_bbox_xy_mm'][1]>candidate[3])],'conditions':['Actual component/solder maxima under tray must be <=3 mm to retain 1 mm gap.','Keep J3 local upward wiring clearance separate; do not extend tray north across J3.','This bounds the 60x38 tray footprint only, not confirmation that a nominal 60x38 internal cavity plus walls fits the same envelope.','Probe row lies under tray; allow disassembly to access testpoints.','Battery must not bear directly on electronics; retain insulating floor.']}
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'mechanical-interface.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
for name,rows,fields in [('mounting-holes.csv',holes,['id','xy_mm','drill_mm','plated','nominal_pad_diameter_mm','recommended_insulating_contact_outer_diameter_mm','recommended_clearance_bore_mm','nearest_body_distance_from_hole_center_mm','nearest_body_ref','nearest_other_pad_bbox_distance_from_hole_center_mm','nearest_pad_ref','actual_gerber_plated_drill_mm']),('interface-anchors.csv',interfaces,['ref','function','anchor_mm','body_bbox_xy_mm','source_rotation_deg_ccw','design_service_bbox_xy_mm','design_height_reserve_above_top_mm','datum'])]:
    with (OUT/name).open('w',encoding='utf-8-sig',newline='') as f:
        w=csv.DictWriter(f,fieldnames=fields,extrasaction='ignore');w.writeheader();w.writerows(rows)
print(json.dumps({'holes':holes,'interfaces':[{k:v for k,v in i.items() if k in ['ref','anchor_mm','body_bbox_xy_mm']} for i in interfaces]},ensure_ascii=False,indent=2))
