"""Render assembly and fitting diagrams from the verified saved copy."""
import bpy
from mathutils import Vector
from pathlib import Path
OUT=Path('C:/Users/kicof/Downloads/Gridopoly_Player_Stand_V2_R2_FPC60')
s=bpy.context.scene
assert s.name.startswith('Player Stand V2 R2')
s.render.resolution_percentage=100
cam=s.camera
def shot(name,pos,target,scale):
    cam.location=pos;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler()
    cam.data.ortho_scale=scale;s.render.filepath=str(OUT/name);bpy.ops.render.render(write_still=True)
shot('preview-assembly.png',(150,-190,150),(-8,8,43),175)
orig={o:(o.hide_render,o.location.copy()) for o in s.objects}
for o in s.objects:
    if o.name.startswith(('R2_SCREEN_','R2_SOFT_STRAP_')):o.hide_render=True
    if o.name.startswith('R2_04_'):o.location.z+=35
    if o.name.startswith('R2_BATTERY_REFERENCE'):o.location.z+=16
shot('preview-battery-exploded.png',(-140,180,170),(-8,25,52),180)
for o,(hide,loc) in orig.items():o.hide_render=hide;o.location=loc
for o in s.objects:
    if o.name.startswith('R2_SCREEN_'):o.hide_render=True
shot('preview-fpc-route.png',(-160,-155,145),(-28,-13,32),115)
for o,(hide,loc) in orig.items():o.hide_render=hide;o.location=loc
for o in s.objects:
    if o.type=='MESH' and not o.name.startswith(('R2_01_','R2_02_','R2_03_','R2_04_','R2_05_')):o.hide_render=True
    if o.name.startswith('R2_02_'):o.location.z+=20
    if o.name.startswith('R2_03_'):o.location.z+=32
    if o.name.startswith('R2_04_'):o.location.z+=55
shot('preview-print-parts.png',(155,-190,170),(-5,8,60),205)
for o,(hide,loc) in orig.items():o.hide_render=hide;o.location=loc
cam.location=(150,-190,150);cam.rotation_euler=(Vector((-8,8,43))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=175
s.render.filepath=str(OUT/'preview-assembly.png')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Gridopoly_Player_Stand_V2_R2_FPC60.blend'))
