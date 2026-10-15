import bpy,sys
from pathlib import Path
args=sys.argv[sys.argv.index('--')+1:]
index,name=args
root=Path(r'Z:/Files/我的项目/Gridopoly/Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining')
scene=bpy.data.scenes['Hi3D_'+index+'_30mm_EDIT']
bpy.context.window.scene=scene
keep=set(scene.objects)
for other in list(bpy.data.scenes):
    if other!=scene:bpy.data.scenes.remove(other)
for obj in list(bpy.data.objects):
    if obj not in keep:bpy.data.objects.remove(obj,do_unlink=True)
bpy.data.orphans_purge(do_recursive=True)
obj=bpy.data.objects[name+'_30mm_Print']
bpy.context.view_layer.objects.active=obj
obj.select_set(True)
assert scene.unit_settings.system=='METRIC'
assert abs(scene.unit_settings.scale_length-0.001)<1e-9
bpy.ops.wm.save_as_mainfile(filepath=str(root/(name+'_30mm.blend')))
print('SAVED_INDIVIDUAL',name,len(scene.objects),list(obj.dimensions))
