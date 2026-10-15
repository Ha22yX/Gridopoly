"""Prepare the four-quarter perimeter fixture from the physically fitted R2."""
from pathlib import Path
import copy
import hashlib
import json

ROOT = Path(__file__).resolve().parent
BASE = ROOT.parent
OUT = Path('C:/Users/kicof/Downloads/Gridopoly_BoardBase_Full16_R1')
JOB = Path('C:/Users/kicof/AppData/Local/Temp/gridopoly-board16')


def main():
    previous = json.loads((BASE/'fixture-config.json').read_text(encoding='utf-8'))
    cfg = {k:copy.deepcopy(previous[k]) for k in (
        'interface_sha256','pcb_bottom_mm','bottom_rib_mm','pcb_thickness_mm',
        'user_max_underside_mm','support_od_mm','support_hole_mm','pilot_hole_mm',
        'pilot_depth_mm','guide_depth_mm','pcb_screws','boards')}
    cfg.update(version='FULL16_R1', units='mm', printer='Bambu H2D', material='PLA',
        output_dir=OUT.as_posix(), quarter_count=4, bridge_count=8,
        center_local_mm=[-99,179], global_offset_mm=[278,0],
        quarter_angles_deg=[0,90,180,270], split_clearance_mm=0.3,
        main_frame_span_mm=366, outside_tabs_span_mm=406,
        central_nominal_opening_mm=190,
        status='Digital geometry checked separately; full-ring physical fit pending',
        baseline_user_validation='User confirms printed three-module corner fixture fits perfectly; photos supplied in this conversation.',
        full_ring_user_validation=False,
        long_long_gap_evidence='User approximate 15 mm main-body gap; not yet validated in this full-ring fixture',
        baseline_config_sha256=hashlib.sha256((BASE/'fixture-config.json').read_bytes()).hexdigest(),
        base_outline_corners=[[-67.35,-4],[84,-4],[84,210.35],[-4,210.35],[-4,84],[-67.35,84]],
        base_corner_radii_mm=[0,4,0,0,4,0],
        edge_ribs={'top_z_mm':6.5,'width_mm':2.4,'pcb_projection_clearance_mm':1.1},
        ear_top_z_mm=10, ear_pilot_depth_mm=8, ear_pilot_diameter_mm=2.5,
        joint_pin_diameter_mm=2.8, joint_pin_height_mm=0.8,
        bridge_width_mm=20, bridge_length_mm=36, bridge_thickness_mm=3,
        bridge_screw_hole_mm=3.6, bridge_pin_hole_mm=3.2,
        bridge_screw_offset_mm=10, bridge_pin_offset_mm=4,
        bridge_assembly_z_mm=10,
        ears=[
            {'name':'out_outer','lo':[80.5,192.5],'hi':[104,210.35],'radii':[2,2,0,0],'screw_xy':[92,200.5],'pin_xy':[92,206.5]},
            {'name':'out_inner','lo':[-24,192.5],'hi':[-0.5,210.35],'radii':[2,2,0,0],'screw_xy':[-12,200.5],'pin_xy':[-12,206.5]},
            {'name':'in_outer','lo':[-67.35,-24],'hi':[-49.5,-0.5],'radii':[0,2,2,0],'screw_xy':[-57.5,-12],'pin_xy':[-63.5,-12]},
            {'name':'in_inner','lo':[-67.35,80.5],'hi':[-49.5,104],'radii':[0,2,2,0],'screw_xy':[-57.5,92],'pin_xy':[-63.5,92]},
        ],
        bridges_canonical=[{'xy':[92,210.5],'rotation_deg':0},{'xy':[-12,210.5],'rotation_deg':0}],
        h2d_single_nozzle_build_mm=[325,320,325],
        h2d_source='https://cdn1.bambulab.com/documentation/h2d/en/H2D_Laser_Full_Combo_20250305.pdf',
        source_photos=[f'C:/Users/kicof/.codex/codex-remote-attachments/01a0f687-eeae-7d61-bbbb-cbb5a98a7b33/890BC5E9-ACBC-4AC0-99E8-13FF07F7B8C9/{n}-Photo-{n}.jpg' for n in (1,2,3)])
    fourth = copy.deepcopy(cfg['boards'][2])
    fourth['name']='Outgoing_Long_2'
    fourth['outline']=[[x,y+63] for x,y in fourth['outline']]
    for hole in fourth['holes']:
        hole['xy'][1]+=63
    cfg['boards'].append(fourth)
    cfg['pcb_screws']['qty']=32
    cfg['joint_screws']=dict(cfg['pcb_screws'],qty=16)
    cfg['washers_total']=48
    OUT.mkdir(parents=True,exist_ok=True)
    JOB.mkdir(parents=True,exist_ok=True)
    for dest in (ROOT,OUT):
        (dest/'config.json').write_text(json.dumps(cfg,ensure_ascii=False,indent=2),encoding='utf-8')
    helpers=(BASE/'scripts/build_fixture_blender.py').read_text(encoding='utf-8').split('def one_piece_base')[0]
    helpers=helpers.replace('dist=0.000001','dist=0.00001')
    code='CONFIG = '+repr(cfg)+'\n'+helpers+'\n'+(ROOT/'build.py').read_text(encoding='utf-8')
    assert len(code)<190000
    (OUT/'rebuild_in_blender.py').write_text(code,encoding='utf-8')
    job={'calls':[{'name':'execute_blender_code','arguments':{'code':code,'user_prompt':'使用已试装兼容的三模块底板，创建16模块中空环形底板，4块分别打印再稳固连接；保留用户现有Blender场景。'}}]}
    (JOB/'build-job.json').write_text(json.dumps(job,ensure_ascii=False),encoding='utf-8')
    print(json.dumps({'output':str(OUT),'job':str(JOB/'build-job.json'),'code_bytes':len(code)},ensure_ascii=False))


if __name__=='__main__':
    main()
