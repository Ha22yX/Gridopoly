"""Background only: open R2 read-only, replace upper, save a distinct R3 assembly."""
import bpy,json
from pathlib import Path
from mathutils import Vector
HERE=Path(__file__).resolve().parent
OUT=Path('Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED')
scene=bpy.context.scene
assert scene.name.startswith('Player Stand V2 R2')
old=next(o for o in scene.objects if o.name.startswith('R2_02_'))
material=old.data.materials[0] if len(old.data.materials) else None
bpy.data.objects.remove(old,do_unlink=True)
bpy.ops.wm.stl_import(filepath=str(OUT/'02_Screen_Support_CW47_5_R3.stl'),use_scene_unit=False)
upper=bpy.context.object;upper.name='R3_02_SCREEN_SUPPORT_PRINT';upper.location.z=19.6
if material:upper.data.materials.append(material)
upper['revision']='R3 reinforced flat mounting feet; original holes and screen mating surface retained'
scene.name='Player Stand V2 R3 - REINFORCED FEET';scene['revision']='V2 R3 EAR REINFORCED'
bpy.context.view_layer.update()
# Run the established assembly checks on the replaced support and retained R2 references.
code=(HERE.parent/'R2_FPC60/verify_assembly.py').read_text(encoding='utf-8')
code=code.replace('Gridopoly_Player_Stand_V2_R2_FPC60','Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED')
code=code.replace("scene.name.startswith('Player Stand V2 R2')","scene.name.startswith('Player Stand V2 R3')")
code=code.replace("'R2_01_','R2_02_',","'R2_01_','R3_02_',")
code=code.replace("'V2 R2 FPC60 MODULAR'","'V2 R3 EAR REINFORCED'")
exec(compile(code,'R3_assembly_verification','exec'))
# Check a 7mm screwdriver access cylinder above each screen fixing foot.
tool_checks=[]
for i,xy in enumerate([(-57,-18),(7,15)]):
    bpy.ops.mesh.primitive_cylinder_add(vertices=96,radius=3.5,depth=50,location=(*xy,48.61))
    tool=bpy.context.object;tool.name='R3_TOOL_CLEARANCE_CHECK'
    value=vol(tool,upper) if candidate(tool,upper) else 0
    tool_checks.append({'hole_xy_mm':xy,'tool_diameter_mm':7,'start_world_z_mm':23.61,'overlap_mm3':value})
    bpy.data.objects.remove(tool,do_unlink=True)
r['screwdriver_access']=tool_checks
r['parts_count']=len(parts)
if any(c['overlap_mm3']>.001 for c in tool_checks):r['status']='MODEL_CHECKS_FAIL'
(OUT/'assembly-verification.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
print('R3_FINAL_ASSEMBLY '+json.dumps({'status':r['status'],'solid_pairs':len(pairs),'fpc_pairs':len(fpc_checks),'wire_pairs':len(wire_checks),'tool_checks':tool_checks,'collisions':bad}))
assert r['status']=='MODEL_CHECKS_PASS'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED.blend'))
