"""Prepare a bounded Blender MCP job; does not alter the PCB or legacy models."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
OUT = Path('Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R1')
TEMP = Path('C:/Users/kicof/AppData/Local/Temp/gridopoly-player-stand-v2')
for p in (OUT, TEMP, ROOT/'outputs'):
    p.mkdir(parents=True, exist_ok=True)
interface = json.loads((ROOT/'interfaces/player-power-2026-10-15/mechanical-interface.json').read_text(encoding='utf-8'))
cfg = {
    'revision': 'V2 R1 - FIRST FIT',
    'out': OUT.as_posix(),
    'legacy_stl': (REPO/'Mechanical/PlayerConsoleStand/outputs/models/Gridopoly_Player_Console_Stand_CW47_5.stl').as_posix(),
    'legacy_fit': 'User confirmed printed CW47.5 fits on 2026-10-15',
    'board': interface,
    'feet_mm': 8.0, 'board_thickness_mm': 1.6,
    'spacer_mm': 8.0, 'tray_floor_mm': 2.0, 'battery_cavity_height_mm': 14.0,
    'battery_envelope_mm': [56.0,34.5,10.6],
    'battery_cavity_xy': [-33.0,-8.0,27.0,30.0],
    'battery_xy': [-3.0,11.0],
    'holes_xy': [[-35,-35],[-35,35],[35,-35],[35,35]],
    'printed_hole_mm': 3.6,
    'source_pcb_hole_mm': 3.4,
    'fastener': {'screw':'M3 x 35 mm', 'washer_thickness_mm':0.5,
                 'foot':'round OD6, M3 through-thread female, 8 mm long insulating spacer',
                 'thread_engagement_mm':4.9},
    'optional_raised_stack': {'spacer_mm':12, 'screw':'M3 x 40 mm', 'thread_engagement_mm':5.9},
    'openings': {
        'fpc':[-48,-27,-29,-12],
        'power':[29.5,6.2,48,20.2],
        'reset':[29.5,-20.2,48,-6.2],
        'battery_service':[-9,33,9,42]
    },
    'status': 'New assembly not yet sliced or physically fitted; cable length pending'
}
(ROOT/'design-parameters.json').write_text(json.dumps(cfg,indent=2,ensure_ascii=False),encoding='utf-8')
(OUT/'design-parameters.json').write_text(json.dumps(cfg,indent=2,ensure_ascii=False),encoding='utf-8')
code = 'CONFIG = '+repr(cfg)+'\n'+(ROOT/'scripts/build_model_blender.py').read_text(encoding='utf-8')
job={'calls':[{'name':'execute_blender_code','arguments':{'code':code,'user_prompt':'在Blender创建新版玩家屏幕模组底座，复用已试装CW47.5接口，底层PCB四孔固定，预留电池和所有接口，生成可打印模型并检查网格'}}]}
(TEMP/'build.json').write_text(json.dumps(job,ensure_ascii=False),encoding='utf-8')
print(TEMP/'build.json')
