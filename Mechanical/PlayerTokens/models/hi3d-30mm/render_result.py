import bpy
from mathutils import Vector
from pathlib import Path

root=Path(r'Z:/Files/我的项目/Gridopoly/Mechanical/PlayerTokens/models/hi3d-30mm/deliverables/Hi3D_30mm')
obj=bpy.data.objects['Fox_30mm_Print']
scene=bpy.data.scenes['Hi3D_30mm_EDIT']
bpy.context.window.scene=scene
for other in scene.objects:
    if other!=obj:other.hide_render=True
obj.hide_render=False
scene.world=bpy.data.worlds.new('Preview_white')
scene.world.color=(0.8,0.8,0.8)
scene.render.engine='BLENDER_WORKBENCH'
scene.render.resolution_x=1000
scene.render.resolution_y=1100
scene.render.resolution_percentage=100
scene.display.shading.light='STUDIO'
scene.display.shading.color_type='MATERIAL'
scene.display.shading.show_cavity=True
scene.display.shading.cavity_type='BOTH'
scene.display.shading.show_shadows=True
scene.display.shading.background_type='WORLD'
camdata=bpy.data.cameras.new('Result_preview')
cam=bpy.data.objects.new('Result_preview',camdata)
scene.collection.objects.link(cam)
scene.camera=cam
camdata.type='ORTHO'
camdata.ortho_scale=57
cam.location=(13,-100,58)
target=Vector((0,0,23))
cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
scene.render.filepath=str(root/'Hi3D_30mm_preview.png')
bpy.ops.render.render(write_still=True)
cam.location=(0,0,-80)
target=Vector((0,0,0))
cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
camdata.ortho_scale=35
scene.render.resolution_y=1000
scene.render.filepath=str(root/'Hi3D_30mm_bottom.png')
bpy.ops.render.render(write_still=True)
