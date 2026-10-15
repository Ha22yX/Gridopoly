"""Independent, read-only planar audit of config.json; writes only layout-audit.json.

This checks nominal geometry, not printed strength, physical connector insertion,
or the final Blender/STL meshes. All distances are millimetres.
"""
from pathlib import Path
import ast
import csv
import hashlib
import json
import math

ROOT = Path(__file__).resolve().parent
BASE = ROOT.parent
EPS = 1e-5
# Frozen arc endpoints carry approximately 0.00005 mm source rounding.
# Keep exact coordinate-copy checks tight; use 1 micrometre for nominal bounds.
NOMINAL_EPS = 0.001


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bbox(poly):
    return [min(p[0] for p in poly), min(p[1] for p in poly),
            max(p[0] for p in poly), max(p[1] for p in poly)]


def rectangle(lo, hi):
    return [(lo[0], lo[1]), (hi[0], lo[1]), (hi[0], hi[1]), (lo[0], hi[1])]


def cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def on_segment(p, a, b):
    return abs(cross(a,b,p)) < 1e-9 and all(min(a[i],b[i])-1e-9 <= p[i] <= max(a[i],b[i])+1e-9 for i in (0,1))


def segment_intersects(a,b,c,d):
    q = [cross(a,b,c),cross(a,b,d),cross(c,d,a),cross(c,d,b)]
    if q[0]*q[1] < 0 and q[2]*q[3] < 0:
        return True
    return any((on_segment(c,a,b),on_segment(d,a,b),on_segment(a,c,d),on_segment(b,c,d)))


def point_segment_distance(p,a,b):
    den = math.dist(a,b)**2
    if den == 0:
        return math.dist(p,a)
    t = max(0,min(1,sum((p[i]-a[i])*(b[i]-a[i]) for i in (0,1))/den))
    return math.dist(p, [a[i]+t*(b[i]-a[i]) for i in (0,1)])


def inside(p, poly):
    hit = False
    for a,b in zip(poly,poly[1:]+poly[:1]):
        if on_segment(p,a,b):
            return True
        if (a[1]>p[1]) != (b[1]>p[1]) and p[0] < (b[0]-a[0])*(p[1]-a[1])/(b[1]-a[1])+a[0]:
            hit = not hit
    return hit


def polygon_distance(a,b):
    if inside(a[0],b) or inside(b[0],a):
        return 0.0
    best = float('inf')
    for p,q in zip(a,a[1:]+a[:1]):
        for r,s in zip(b,b[1:]+b[:1]):
            if segment_intersects(p,q,r,s):
                return 0.0
            best = min(best,point_segment_distance(p,r,s),point_segment_distance(q,r,s),
                       point_segment_distance(r,p,q),point_segment_distance(s,p,q))
    return best


def main():
    cfg = read(ROOT/'config.json')
    old = read(BASE/'fixture-config.json')
    interface_path = BASE/'interfaces/pcb-interface-2026-10-15/mechanical-interface.json'
    frozen = read(interface_path)
    ctr, offset = cfg['center_local_mm'], cfg['global_offset_mm']
    checks = []
    def check(name,passed,**evidence):
        checks.append(dict(name=name,passed=bool(passed),**evidence))
    def rotate(p,deg,shift=True):
        c,s = round(math.cos(math.radians(deg))), round(math.sin(math.radians(deg)))
        x,y = p[0]-ctr[0], p[1]-ctr[1]
        return [ctr[0]+c*x-s*y+(offset[0] if shift else 0),
                ctr[1]+s*x+c*y+(offset[1] if shift else 0)]
    def native(p,name):
        x,y = p
        if name == 'Corner': return [x,80-y]
        if name == 'Incoming_Long': return [x-63,80-y]
        if name == 'Outgoing_Long': return [y,89+x]
        if name == 'Outgoing_Long_2': return [y,152+x]
        raise ValueError(name)
    check('frozen_interface_hash',digest(interface_path)==cfg['interface_sha256'],sha256=digest(interface_path))
    check('baseline_config_hash',digest(BASE/'fixture-config.json')==cfg['baseline_config_sha256'],sha256=digest(BASE/'fixture-config.json'))
    old_errors=[]
    for b in old['boards']:
        cur=next(x for x in cfg['boards'] if x['name']==b['name'])
        old_errors += [math.dist(a,d) for a,d in zip(b['outline'],cur['outline'])]
        old_errors += [math.dist(a['xy'],d['xy']) for a,d in zip(b['holes'],cur['holes'])]
        check('baseline_hole_roles_'+b['name'],[(h['id'],h['active']) for h in b['holes']]==[(h['id'],h['active']) for h in cur['holes']])
    check('three_test_boards_preserved',max(old_errors)<EPS,max_coordinate_error_mm=max(old_errors))
    out1=next(b for b in cfg['boards'] if b['name']=='Outgoing_Long')
    out2=next(b for b in cfg['boards'] if b['name']=='Outgoing_Long_2')
    shift_errors=[math.dist([a[0],a[1]+63],b) for a,b in zip(out1['outline'],out2['outline'])]
    shift_errors += [math.dist([a['xy'][0],a['xy'][1]+63],b['xy']) for a,b in zip(out1['holes'],out2['holes'])]
    check('added_long_is_translation_63mm',max(shift_errors)<EPS,max_coordinate_error_mm=max(shift_errors))
    boards=[]
    native_errors=[]
    for deg in cfg['quarter_angles_deg']:
        for b in cfg['boards']:
            row=dict(name=f"Q{deg}_{b['name']}",kind=b['kind'],angle_deg=deg,canonical_name=b['name'],
                     outline=[rotate(p,deg) for p in b['outline']],
                     holes=[dict(id=h['id'],xy=rotate(h['xy'],deg),active=h['active']) for h in b['holes']])
            row['bounds']=bbox(row['outline'])
            boards.append(row)
            src={h['id']:h for h in frozen['boards'][b['kind']]['mounting_holes']}
            native_errors += [math.dist(h['xy'],native(src[h['id']]['center_mm'],b['name'])) for h in b['holes']]
    check('source_hole_transforms',max(native_errors)<EPS,max_coordinate_error_mm=max(native_errors))
    check('four_proper_quarter_rotations',cfg['quarter_angles_deg']==[0,90,180,270],rotation_determinants=[1,1,1,1],
          note='Native XY-down is first converted to XY-up; no PCB mirror or backside flip is applied.')
    holes=[h for b in boards for h in b['holes']]
    counts=dict(total=len(boards),corner=sum(b['kind']=='corner' for b in boards),long=sum(b['kind']=='long' for b in boards),
                holes=len(holes),active=sum(h['active'] for h in holes),passive=sum(not h['active'] for h in holes))
    check('board_and_hole_counts',counts==dict(total=16,corner=4,long=12,holes=64,active=32,passive=32),**counts)
    spans=bbox([p for b in boards for p in b['outline']])
    check('bare_pcb_span_358mm',max(abs(a-b) for a,b in zip(spans,[0,0,358,358]))<NOMINAL_EPS,bounds_mm=spans,nominal_tolerance_mm=NOMINAL_EPS)
    sides=[]
    for side,axis,boundindex,target in [('bottom',0,1,0),('right',1,2,358),('top',0,3,358),('left',1,0,0)]:
        bs=sorted([b for b in boards if abs(b['bounds'][boundindex]-target)<NOMINAL_EPS],key=lambda b:b['bounds'][axis])
        gaps=[b['bounds'][axis]-a['bounds'][axis+2] for a,b in zip(bs,bs[1:])]
        body_gaps=[g+3*(a['kind']=='long')+3*(b['kind']=='long') for g,a,b in zip(gaps,bs,bs[1:])]
        sides.append(dict(side=side,boards=[b['name'] for b in bs],envelope_gaps_mm=gaps,main_body_gaps_mm=body_gaps))
        check('side_'+side,len(bs)==5 and max(abs(g-9) for g in gaps)<NOMINAL_EPS and max(abs(g-v) for g,v in zip(body_gaps,[12,15,15,12]))<NOMINAL_EPS,nominal_tolerance_mm=NOMINAL_EPS,**sides[-1])
    with (BASE/'interfaces/pcb-interface-2026-10-15/connector-anchors.csv').open(encoding='utf-8-sig',newline='') as f:
        anchors={(row['board'],row['ref']):[float(row['anchor_x_mm']),float(row['anchor_y_mm'])] for row in csv.DictReader(f)}
    by_name={b['name']:b for b in boards}
    ring=[]
    for deg in (0,90,180,270):
        ring += [by_name[f'Q{deg}_Corner'],by_name[f'Q{deg}_Outgoing_Long'],by_name[f'Q{deg}_Outgoing_Long_2'],by_name[f'Q{(deg+90)%360}_Incoming_Long']]
    connector_rows=[]
    for a,b in zip(ring,ring[1:]+ring[:1]):
        ca=[(a['bounds'][i]+a['bounds'][i+2])/2 for i in (0,1)]
        cb=[(b['bounds'][i]+b['bounds'][i+2])/2 for i in (0,1)]
        dx,dy=cb[0]-ca[0],cb[1]-ca[1]
        axis=[1 if dx>0 else -1,0] if abs(dx)>abs(dy) else [0,1 if dy>0 else -1]
        for num in (1,3):
            pa=rotate(native(anchors[(a['kind'],f'JOUT{num}')],a['canonical_name']),a['angle_deg'])
            pb=rotate(native(anchors[(b['kind'],f'JIN{num}')],b['canonical_name']),b['angle_deg'])
            dv=[pb[i]-pa[i] for i in (0,1)]
            connector_rows.append(dict(from_board=a['name'],to_board=b['name'],group=num,
                lateral_error_mm=abs(dv[0]*axis[1]-dv[1]*axis[0]),
                axial_anchor_distance_mm=sum(dv[i]*axis[i] for i in (0,1))))
    check('all_sixteen_connector_pairs_tangentially_aligned',max(r['lateral_error_mm'] for r in connector_rows)<EPS,
          group_pairs=len(connector_rows),max_lateral_error_mm=max(r['lateral_error_mm'] for r in connector_rows),
          axial_anchor_distance_range_mm=[min(r['axial_anchor_distance_mm'] for r in connector_rows),max(r['axial_anchor_distance_mm'] for r in connector_rows)],
          limitation='Anchor alignment is not insertion-depth, electrical polarity or full plug-envelope validation.')
    ears=[]
    bridges=[]
    ribs=[]
    rib_rectangles=[([-67.35,-3.5],[78,-1.1]),([81.1,2],[83.5,210.35]),([-3.5,90],[-1.1,210.35]),([-67.35,81.1],[-10,83.5])]
    tree=ast.parse((ROOT/'build.py').read_text(encoding='utf-8-sig'))
    fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='make_quarter')
    rib_loop=next(n for n in fn.body if isinstance(n,ast.For) and isinstance(n.target,ast.Tuple) and [i.id for i in n.target.elts]==['lo','hi'])
    actual_ribs=ast.literal_eval(rib_loop.iter)
    check('rib_rectangles_match_actual_builder',len(actual_ribs)==4 and all(list(lo[:2])==expected[0] and list(hi[:2])==expected[1] and lo[2]==3.49 and hi[2]==6.5 for (lo,hi),expected in zip(actual_ribs,rib_rectangles)),
          actual_rib_boxes_mm=actual_ribs,builder_sha256=digest(ROOT/'build.py'))
    for deg in cfg['quarter_angles_deg']:
        for e in cfg['ears']:
            ears.append(dict(name=f"Q{deg}_{e['name']}",poly=[rotate(p,deg) for p in rectangle(e['lo'],e['hi'])],
                             screw=rotate(e['screw_xy'],deg),pin=rotate(e['pin_xy'],deg)))
        for i,b in enumerate(cfg['bridges_canonical']):
            x,y=b['xy']; w,l=cfg['bridge_width_mm'],cfg['bridge_length_mm']
            if b['rotation_deg']!=0:
                raise ValueError('Audit currently expects canonical bridge long axis Y')
            bridges.append(dict(name=f'Q{deg}_B{i}',poly=[rotate(p,deg) for p in rectangle([x-w/2,y-l/2],[x+w/2,y+l/2])],
                                screws=[rotate([x,y+s*cfg['bridge_screw_offset_mm']],deg) for s in (-1,1)],
                                pins=[rotate([x,y+s*cfg['bridge_pin_offset_mm']],deg) for s in (-1,1)]))
        for i,(lo,hi) in enumerate(rib_rectangles):
            ribs.append(dict(name=f'Q{deg}_rib{i}',poly=[rotate(p,deg) for p in rectangle(lo,hi)]))
    used_screws=[];used_pins=[];matches=[]
    for b in bridges:
        for label,pts,key,used in [('screw',b['screws'],'screw',used_screws),('pin',b['pins'],'pin',used_pins)]:
            for p in pts:
                distances=[math.dist(p,e[key]) for e in ears]
                j=min(range(len(ears)),key=lambda j:distances[j])
                match=dict(bridge=b['name'],feature=label,point_mm=p,ear=ears[j]['name'],center_error_mm=distances[j])
                matches.append(match); used.append(j)
    max_join_error=max(m['center_error_mm'] for m in matches)
    check('eight_bridges_sixteen_ears_one_to_one',len(bridges)==8 and len(ears)==16 and len(set(used_screws))==16 and len(set(used_pins))==16 and max_join_error<EPS,
          bridge_count=len(bridges),ear_count=len(ears),matched_screw_count=len(used_screws),matched_pin_count=len(used_pins),max_feature_center_error_mm=max_join_error)
    clearance_results={}
    for group,objects in [('ears',ears),('bridges',bridges),('ribs',ribs)]:
        tested=[]
        for ob in objects:
            for b in boards:
                tested.append(dict(feature=ob['name'],board=b['name'],distance_mm=polygon_distance(ob['poly'],b['outline'])))
        closest=min(tested,key=lambda x:x['distance_mm'])
        clearance_results[group]=dict(pair_count=len(tested),minimum=closest,zero_clearance_count=sum(t['distance_mm']<=EPS for t in tested))
        check(group+'_outside_all_pcb_projections',closest['distance_mm']>EPS,**clearance_results[group])
    basepoly=cfg['base_outline_corners']
    base_rotated=[[rotate(p,deg) for p in basepoly] for deg in cfg['quarter_angles_deg']]
    base_span=bbox([p for poly in base_rotated for p in poly])
    full_span=bbox([p for e in ears for p in e['poly']]+[p for poly in base_rotated for p in poly])
    check('frame_and_tab_spans',abs(base_span[2]-base_span[0]-366)<EPS and abs(full_span[2]-full_span[0]-406)<EPS,
          frame_bounds_mm=base_span,overall_with_tabs_bounds_mm=full_span)
    seam_gaps=[]
    for i in range(4):
        seam_gaps.append(polygon_distance(base_rotated[i],base_rotated[(i+1)%4]))
    check('quarter_split_clearance',max(abs(v-cfg['split_clearance_mm']) for v in seam_gaps)<EPS,base_polygon_adjacent_distances_mm=seam_gaps)
    native_bbox=bbox(basepoly+[p for e in cfg['ears'] for p in rectangle(e['lo'],e['hi'])])
    size=[native_bbox[2]-native_bbox[0],native_bbox[3]-native_bbox[1]]
    check('single_print_xy_fits_configured_bed',size[0]<cfg['h2d_single_nozzle_build_mm'][0] and size[1]<cfg['h2d_single_nozzle_build_mm'][1],
          part_bbox_mm=native_bbox,part_size_xy_mm=size,bed_xy_mm=cfg['h2d_single_nozzle_build_mm'][:2])
    fastener_chain=dict(pcb_screw_entry_mm=cfg['pcb_screws']['under_head_length_mm']-cfg['pcb_thickness_mm']-cfg['pcb_screws']['washer_thickness_mm'],
                        bridge_screw_entry_mm=cfg['joint_screws']['under_head_length_mm']-cfg['bridge_thickness_mm']-cfg['joint_screws']['washer_thickness_mm'])
    fastener_chain['pcb_blind_floor_clearance_mm']=cfg['pilot_depth_mm']-fastener_chain['pcb_screw_entry_mm']
    fastener_chain['ear_blind_floor_clearance_mm']=cfg['ear_pilot_depth_mm']-fastener_chain['bridge_screw_entry_mm']
    check('nominal_screw_lengths_do_not_bottom',min(fastener_chain.values())>0,**fastener_chain)
    check('unchanged_pcb_height_and_clearance',cfg['pcb_bottom_mm']==old['pcb_bottom_mm'] and cfg['bottom_rib_mm']==old['bottom_rib_mm'],
          pcb_bottom_z_mm=cfg['pcb_bottom_mm'],base_top_z_mm=cfg['bottom_rib_mm'],bottom_component_assumed_z_mm=cfg['pcb_bottom_mm']-cfg['user_max_underside_mm'],
          nominal_component_to_flat_base_clearance_mm=cfg['pcb_bottom_mm']-cfg['user_max_underside_mm']-cfg['bottom_rib_mm'])
    report=dict(status='PASS' if all(c['passed'] for c in checks) else 'FAIL',config_sha256=digest(ROOT/'config.json'),
                audit_script_sha256=digest(Path(__file__)),method='Independent stdlib coordinate, polygon-segment distance, transform and fastener dimension-chain audit',
                checks=checks,boards=[{k:v for k,v in b.items() if k!='outline'} for b in boards],bridge_feature_matches=matches,connector_anchor_comparisons=connector_rows,
                assumptions_and_limits=[
                    'R2 three-module physical fit accepted from user report and photos; this script does not remeasure photographs.',
                    'Full sixteen-module fit, long-long mating, four-part joint strength and actual PLA screw holding remain untested.',
                    'Ear/bridge/rib PCB clearance uses conservative unrounded rectangular projections and frozen sampled PCB outlines, not component or plug envelopes.',
                    'Rib rectangles are independently audited from root design specification because config contains only rib top/width/clearance scalars.',
                    'No final Blender/STL mesh verification is performed here; separate mesh and Z collision reports remain required.',
                    'Outer tab span 406 mm includes tabs, while PCB span is 358 mm and plain perimeter frame span is 366 mm.',
                    'PCB below-board protrusion is the user approximate 8.1 mm bound, not component-by-component measured Z.'
                ])
    (ROOT/'layout-audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(status=report['status'],checks=len(checks),failed=[c for c in checks if not c['passed']],
                          clearances=clearance_results,size_xy_mm=size,max_bridge_error_mm=max_join_error),ensure_ascii=False))
    if report['status']!='PASS':
        raise SystemExit(1)


if __name__=='__main__':
    main()
