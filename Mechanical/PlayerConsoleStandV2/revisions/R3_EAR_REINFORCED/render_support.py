"""Render the actual exported support before/after; no illustrative geometry."""
import bpy,math
from pathlib import Path
from mathutils import Vector
OUT=Path('Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED')
OLD=Path('Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R2_FPC60')
scene=bpy.context.scene
keep=set(scene.objects)
for s in list(bpy.data.scenes):
    if s!=scene:bpy.data.scenes.remove(s)
for o in list(bpy.data.objects):
    if o not in keep:bpy.data.objects.remove(o,do_unlink=True)
new=next(o for o in scene.objects if o.name.startswith('R3_02'))
bpy.ops.wm.stl_import(filepath=str(OLD/'02_Screen_Support_CW47_5_R2.stl'),use_scene_unit=False)
old=bpy.context.object;old.name='R2_BEFORE_COMPARISON_ONLY'
old.data.materials.append(new.data.materials[0])
scene.render.resolution_x=1400;scene.render.resolution_y=1100;scene.render.resolution_percentage=100
def shot(name,position,target,scale,show_old=False):
    new.hide_render=show_old;old.hide_render=not show_old
    scene.camera.location=position
    scene.camera.rotation_euler=(Vector(target)-scene.camera.location).to_track_quat('-Z','Y').to_euler()
    scene.camera.data.ortho_scale=scale
    scene.render.filepath=str(OUT/name);bpy.ops.render.render(write_still=True)
shot('preview-support-R3.png',(-132,-108,85),(-25,-3,14),102)
shot('preview-left-foot-R2-before.png',(-108,-86,60),(-54,-9,5),39,True)
shot('preview-left-foot-R3-after.png',(-108,-86,60),(-54,-9,5),39)
shot('preview-support-underside-R3.png',(-100,-110,-95),(-25,-3,8),98)
print('R3_RENDER_COMPLETE')
