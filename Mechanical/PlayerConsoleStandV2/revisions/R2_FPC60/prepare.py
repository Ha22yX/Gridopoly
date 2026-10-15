"""R2 preserves the archived R1 package. Prepare Blender MCP job only."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parent
PROJECT=ROOT.parents[3]
V2=ROOT.parents[1]
OUT=Path('Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R2_FPC60')
TEMP=Path('C:/Users/kicof/AppData/Local/Temp/gridopoly-player-stand-r2')
for p in (OUT,TEMP,ROOT/'outputs'):p.mkdir(parents=True,exist_ok=True)
route=json.loads((ROOT/'route/fpc60-route.json').read_text(encoding='utf-8'))
cfg={
 'revision':'V2 R2 FPC60 MODULAR - CONDITIONAL FIRST FIT', 'out':OUT.as_posix(),
 'legacy_stl':(PROJECT/'Mechanical/PlayerConsoleStand/outputs/models/Gridopoly_Player_Console_Stand_CW47_5.stl').as_posix(),
 'board':json.loads((V2/'interfaces/player-power-2026-10-15/mechanical-interface.json').read_text(encoding='utf-8')),
 'fpc_user_length_mm':60,'fpc_length_evidence':'User corrected 65 to approximately 60 mm',
 'battery_pack_mm':[56,34.5,10.3], 'battery_orientation':'56 X, 10.3 Y, 34.5 Z; wire end left',
 'deck_bounds_xy':[-69,-47,47,64],'deck_thickness':4,
 'screen_translation_xy':[-25,-9], 'pcb_bottom':8, 'pcb_top':9.6, 'spacer':6, 'deck_z':15.6,
 'holes_xy':[[-35,-35],[-35,35],[35,-35],[35,35]],
 'battery_cavity_xy':[-30,46,30,59], 'battery_cavity_z_local':[2,39],
 'battery_cradle_floor':2,'cradle_assembly_z':19.6,'screen_mounts':[[-57,-18],[7,15]],
 'battery_cradle_mounts':[[-37,52.5],[37,52.5]],
 'module_screws':'4 x M3 x 12mm with 0.5mm washers and ordinary M3 nuts (2.4mm thick), no printed nut pockets',
 'strap':'10mm wide, 180-200mm reusable soft strap, through front slot and around rear edge',
 'main_screw':'M3 x 18 mm', 'washer_thickness':0.5, 'thread_engagement':5.9,
 'foot':'OD6 x 8 mm M3 through-thread insulating foot, four',
 'route':route,
 'status':'60 mm routing model is conditional on outlet/contact/insertion assumptions; actual FPC fit pending'
}
(ROOT/'parameters.json').write_text(json.dumps(cfg,indent=2,ensure_ascii=False),encoding='utf-8')
(OUT/'parameters.json').write_text(json.dumps(cfg,indent=2,ensure_ascii=False),encoding='utf-8')
helpers=(V2/'scripts/build_model_blender.py').read_text(encoding='utf-8').split('def cut_openings')[0]
helpers=helpers.replace('Player Stand V2 R1 - FIRST FIT','Player Stand V2 R2 FPC60 - FIRST FIT')
helpers=helpers.replace('dist=0.000001','dist=0.00001')
render_cfg=dict(cfg)
render_cfg['board']={'components':[{k:p[k] for k in ('ref','body_bbox_xy_mm')} for p in cfg['board']['components'] if p.get('body_bbox_xy_mm')]}
indices=list(range(0,len(route['centerline']),2))
if indices[-1]!=len(route['centerline'])-1:indices.append(len(route['centerline'])-1)
render_cfg['route']={'centerline':[{'center_xyz_mm':route['centerline'][i]['center_xyz_mm']} for i in indices],
 'ribbon_variants':{key:{'samples':[{k:v['samples'][i][k] for k in ('width_axis_xyz','normal_xyz')} for i in indices]} for key,v in route['ribbon_variants'].items()}}
code='CONFIG='+repr(render_cfg)+'\n'+helpers+(ROOT/'geometry.py').read_text(encoding='utf-8')
job={'calls':[{'name':'execute_blender_code','arguments':{'code':code,'user_prompt':'按用户修正的60mm软排线和10.3mm电池厚度修改玩家底座，保留已验证屏幕接口，PCB仍用四孔固定，创建降低高度的后置电池仓版本'}}]}
(TEMP/'build.json').write_text(json.dumps(job,ensure_ascii=False),encoding='utf-8')
print(TEMP/'build.json')
