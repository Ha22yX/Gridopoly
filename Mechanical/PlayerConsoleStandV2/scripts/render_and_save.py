"""Blender MCP script. Renders current new scene without altering old scenes."""
import bpy
import json
from mathutils import Vector
scene=bpy.context.scene
out='Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R1/'
cam=scene.camera
def render(name,loc,target,scale):
    cam.location=loc
    cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler()
    cam.data.ortho_scale=scale
    scene.render.filepath=out+name+'.png'
    bpy.ops.render.render(write_still=True)
render('01_Assembly',(145,-180,150),(0,0,50),160)
upper=next(o for o in scene.objects if o.name.startswith('01_UPPER'))
tray=next(o for o in scene.objects if o.name.startswith('02_BATTERY'))
module=[o for o in scene.objects if o.name.startswith('SCREEN_')]
fpc=next(o for o in scene.objects if o.name.startswith('FPC_ROUTE'))
fpc.hide_render=True
for o in module+[upper]: o.location.z+=45
render('02_Open_Battery',(140,-170,180),(0,0,68),210)
for o in module+[upper]: o.location.z-=45
for o in module+[upper]: o.hide_render=True
render('03_Tray_Access',(-125,-175,160),(0,0,22),130)
tray.hide_render=True
for o in scene.objects:
    if o.name.startswith('BATTERY_'): o.hide_render=True
render('04_PCB_Access',(0,-1,200),(0,0,8),110)
tray.hide_render=False
for o in scene.objects:
    if o.name.startswith('BATTERY_'): o.hide_render=False
for o in module+[upper]: o.hide_render=False
fpc.hide_render=False
cam.location=(145,-180,150)
cam.rotation_euler=(Vector((0,0,50))-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.ortho_scale=160
for a in bpy.context.screen.areas:
    if a.type=='VIEW_3D':
        a.spaces.active.region_3d.view_distance=180
        a.spaces.active.region_3d.view_location=(0,0,43)
        a.spaces.active.region_3d.view_rotation=cam.rotation_euler.to_quaternion()
bpy.ops.wm.save_as_mainfile(filepath=out+'Gridopoly_Player_Stand_V2_R1.blend',copy=True)
print('RENDERS_SAVED '+out)
