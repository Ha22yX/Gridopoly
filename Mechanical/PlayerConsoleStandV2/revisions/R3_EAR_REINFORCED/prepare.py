"""Keep R2 files immutable and prepare the live Blender MCP repair job."""
import hashlib,json,shutil
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
OLD=Path('Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R2_FPC60')
OUT=Path('Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED')
OUT.mkdir(parents=True,exist_ok=True)
unchanged=['01_Connection_Plate_R2','03_Battery_Cradle_R2','04_Battery_Lid_R2','05_Spacer_6mm_R2']
for name in unchanged:
    for suffix in ('.stl','.3mf'):shutil.copy2(OLD/(name+suffix),OUT/(name+suffix))
source=OLD/'02_Screen_Support_CW47_5_R2.stl'
config={'revision':'V2 R3 EAR REINFORCED','out':OUT.as_posix(),'source_support':source.as_posix(),
        'source_support_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'left_foot_polygon_xy':[[-62,-18],[-50,-18],[-50,-8],[-44,-8],[-44,2],[-56,2],[-62,-4]],
        'right_foot_bounds_xy':[-6,8,7,22],'foot_thickness_mm':4,
        'mount_holes_xy':[[-57,-18],[7,15]],'mount_hole_diameter_mm':3.6,
        'assembly_support_z_mm':19.6,'unchanged_parts':unchanged,
        'status':'GEOMETRY_REPAIR_PENDING_VALIDATION'}
(HERE/'parameters.json').write_text(json.dumps(config,indent=2),encoding='utf-8')
helper=(ROOT/'Mechanical/PlayerConsoleStandV2/scripts/build_model_blender.py').read_text(encoding='utf-8').split('def cut_openings')[0]
helper=helper.replace('Player Stand V2 R1 - FIRST FIT','Player Stand V2 R3 - REINFORCED FEET')
code='CONFIG='+repr(config)+'\n'+helper+(HERE/'geometry.py').read_text(encoding='utf-8')
job={'calls':[{'name':'execute_blender_code','arguments':{'code':code,'user_prompt':'Fix the narrow player screen mounting ear, retaining screen geometry, holes, FPC slot and assembly height.'}}]}
tmp=Path('C:/Users/kicof/AppData/Local/Temp/gridopoly-player-stand-r3');tmp.mkdir(exist_ok=True)
(tmp/'build-job.json').write_text(json.dumps(job),encoding='utf-8')
print(json.dumps({'job':str(tmp/'build-job.json'),'output':str(OUT)}))
