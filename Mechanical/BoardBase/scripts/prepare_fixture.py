"""Build a reproducible Blender MCP job from the PCB team's frozen interface."""
from pathlib import Path
import hashlib
import json
import math

ROOT = Path(__file__).resolve().parents[1]
INTERFACE = ROOT / 'interfaces/pcb-interface-2026-10-15/mechanical-interface.json'
OUT = Path('Z:/Files/我的项目/Gridopoly/Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY')
JOB_DIR = Path('C:/Users/kicof/AppData/Local/Temp/gridopoly-board-base')


def ordered_outline(segments):
    todo = list(segments)
    points = []
    current = todo[0]['start_mm']
    origin = current
    while todo:
        candidates = [(math.dist(current, s[key]), i, key == 'end_mm')
                      for i,s in enumerate(todo) for key in ('start_mm','end_mm')]
        dist, i, reverse = min(candidates)
        if dist > 0.002:
            raise ValueError(f'Outline not joined: {current}, gap {dist}')
        s = todo.pop(i)
        start = s['end_mm'] if reverse else s['start_mm']
        end = s['start_mm'] if reverse else s['end_mm']
        if not points:
            points.append(start)
        if s['type'] == 'arc':
            c = s['center_mm']
            a = math.atan2(start[1]-c[1], start[0]-c[0])
            sweep = math.radians(s['sweep_deg_in_xy_down']) * (-1 if reverse else 1)
            for j in range(1,25):
                t = a + sweep*j/24
                points.append([c[0]+s['radius_mm']*math.cos(t),c[1]+s['radius_mm']*math.sin(t)])
        else:
            points.append(end)
        current = end
    if math.dist(current, origin) > 0.002:
        raise ValueError('Outline not closed')
    return points[:-1]


def main():
    data = json.loads(INTERFACE.read_text(encoding='utf-8-sig'))
    gap = 9.0
    config = {
        'version':'TEST_R2_PRINT_FRIENDLY', 'status':'unprinted_first_fit',
        'printability_correction':'Flat uninterrupted bed face; top-opening screw pilot holes. No underside nut-pocket ceilings.',
        'user_design_correction':'One continuous base with fixed module locations. No per-module bases or sliding joints.',
        'units':'mm','printer':'Bambu H2D','material':'PLA',
        'interface_file':str(INTERFACE),
        'interface_sha256':hashlib.sha256(INTERFACE.read_bytes()).hexdigest(),
        'gap_mm':gap,
        'gap_evidence':'Conditional inference from user approx12/15mm main-body gaps. Fixed first-fit test must verify.',
        'pcb_bottom_mm':16.0,
        'bottom_rib_mm':3.5,
        'pcb_thickness_mm':1.6,
        'user_max_underside_mm':8.1,
        'component_to_rib_nominal_clearance_mm':4.4,
        'support_od_mm':6.0,'support_hole_mm':3.6,
        'pilot_hole_mm':2.5,'pilot_depth_mm':10.0,'guide_depth_mm':1.2,
        'coupon_pilot_diameters_mm':[2.4,2.5,2.6],
        'slide_range_mm':None,
        'pcb_screws':{'type':'thread forming screw specifically for plastics; not M3 machine screw',
                      'nominal_diameter_mm':3.0,'under_head_length_mm':10.0,
                      'head_od_max_mm':6.0,'qty':6,'washer_id_mm':3.2,'washer_od_max_mm':6.0,'washer_thickness_mm':1.0},
        'frame_screws':{'qty':0},
        'nuts':{'qty':0},
        'output_dir':str(OUT).replace('\\','/'),
        'boards':[],
        'future_ring':{'corner_count':4,'long_count':12,'long_per_side':3,
                       'candidate_bare_pcb_span_mm':160+54*3+4*gap,
                       'candidate_base_span_with_4mm_rim_mm':160+54*3+4*gap+8,
                       'required_structure':'one continuous base; fixed module positions',
                       'print_strategy':'whole design exceeds H2D flat-bed area; physical production method pending after test'},
    }
    for name, kind in [('Corner','corner'),('Incoming_Long','long'),('Outgoing_Long','long')]:
        source=data['boards'][kind]
        def transform(p):
            x,y=p
            if name=='Corner': return [x,80-y]
            if name=='Incoming_Long': return [x-54-gap,80-y]
            return [y,80+gap+x]
        config['boards'].append({
            'name':name,'kind':kind,
            'pcb_uuid':source['pcb_uuid'],
            'outline':[transform(p) for p in ordered_outline(source['outline_segments'])],
            'holes':[{'id':h['id'],'xy':transform(h['center_mm']),
                      'active':h['recommended_screw_location']} for h in source['mounting_holes']],
        })
    config['joints']=[]
    config['base_outline_corners']=[[-58-gap,-4],[84,-4],[84,138+gap],[-4,138+gap],[-4,84],[-58-gap,84]]
    config['base_corner_radius_mm']=4
    OUT.mkdir(parents=True,exist_ok=True)
    JOB_DIR.mkdir(parents=True,exist_ok=True)
    (ROOT/'fixture-config.json').write_text(json.dumps(config,ensure_ascii=False,indent=2),encoding='utf-8')
    (OUT/'fixture-config.json').write_text(json.dumps(config,ensure_ascii=False,indent=2),encoding='utf-8')
    builder=(ROOT/'scripts/build_fixture_blender.py').read_text(encoding='utf-8')
    code='CONFIG = '+repr(config)+'\n'+builder
    job={'calls':[{'name':'execute_blender_code','arguments':{
        'code':code,'user_prompt':'这个底部的悬空不好做3D打印吧，你要考虑是3D打印，有重力影响。保持整体底板，每个模块两个对角螺丝，可以使用自攻螺丝。'}}]}
    (JOB_DIR/'build-job.json').write_text(json.dumps(job,ensure_ascii=False),encoding='utf-8')
    (OUT/'rebuild_in_blender.py').write_text(code,encoding='utf-8')
    print(json.dumps({'job':str(JOB_DIR/'build-job.json'),'output':str(OUT),'holes':
                     {b['name']:b['holes'] for b in config['boards']}},ensure_ascii=False))


if __name__=='__main__':
    main()
