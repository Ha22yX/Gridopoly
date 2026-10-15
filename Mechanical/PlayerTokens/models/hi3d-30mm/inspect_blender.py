import bpy, re, zipfile, json, time
import numpy as np
from pathlib import Path
from mathutils import Vector

SRC=Path(r'Z:/Files/我的项目/Gridopoly/Mechanical/PlayerTokens/sources/hi3d-originals/Hi3D.3mf')
OUT=Path(r'Z:/Files/我的项目/Gridopoly/Mechanical/PlayerTokens/models/hi3d-30mm/working/original-inspection')
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
with zipfile.ZipFile(SRC) as z:
    xml=z.read('3D/Objects/object-1.model')
print('READ_XML',len(xml),flush=True)
v=np.asarray(re.findall(rb'<vertex x="([^"]+)" y="([^"]+)" z="([^"]+)"',xml),dtype=np.float32)
f=np.asarray(re.findall(rb'<triangle v1="(\d+)" v2="(\d+)" v3="(\d+)"',xml),dtype=np.int32)
del xml
print('MESH',v.shape,f.shape,flush=True)
np.savez(OUT/'source_mesh.npz',v=v,f=f)
mesh=bpy.data.meshes.new('Hi3D_original_mesh')
mesh.vertices.add(len(v)); mesh.vertices.foreach_set('co',v.ravel())
mesh.loops.add(f.size); mesh.loops.foreach_set('vertex_index',f.ravel())
mesh.polygons.add(len(f)); mesh.polygons.foreach_set('loop_start',np.arange(len(f),dtype=np.int32)*3); mesh.polygons.foreach_set('loop_total',np.full(len(f),3,dtype=np.int32))
mesh.update()
obj=bpy.data.objects.new('Hi3D_original',mesh); bpy.context.collection.objects.link(obj)
bpy.context.view_layer.objects.active=obj; obj.select_set(True)
lo=v.min(axis=0); hi=v.max(axis=0)
print('BOUNDS',lo,hi,hi-lo,flush=True)
obj.location=(-float((lo[0]+hi[0])/2),-float((lo[1]+hi[1])/2),-float(lo[2]))
bpy.ops.object.transform_apply(location=True,rotation=False,scale=False)
scene=bpy.context.scene; scene.unit_settings.system='METRIC'; scene.unit_settings.scale_length=0.001; scene.unit_settings.length_unit='MILLIMETERS'
mat=bpy.data.materials.new('Single_gray'); mat.diffuse_color=(0.55,0.55,0.55,1); obj.data.materials.append(mat)
for p in mesh.polygons:p.use_smooth=True
scene.render.engine='BLENDER_WORKBENCH'; scene.render.resolution_x=850; scene.render.resolution_y=850; scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('Inspection_World')
scene.display.shading.light='STUDIO'; scene.display.shading.color_type='MATERIAL'; scene.display.shading.show_shadows=True; scene.display.shading.show_cavity=True; scene.display.shading.cavity_type='BOTH'; scene.display.shading.background_type='WORLD'; scene.world.color=(0.8,0.8,0.8)
camdata=bpy.data.cameras.new('Inspection_camera'); cam=bpy.data.objects.new('Inspection_camera',camdata); scene.collection.objects.link(cam); scene.camera=cam; camdata.type='ORTHO'; camdata.ortho_scale=float(max(hi-lo)*1.2)
center=Vector((0,0,float((hi[2]-lo[2])*0.47)))
size=float(max(hi-lo))
for name,vec in [('front',(0,-2,0.65)),('back',(0,2,0.65)),('side',(2,0,0.5)),('bottom',(0,0,-2))]:
    cam.location=center+Vector(vec)*size; cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler(); scene.render.filepath=str(OUT/(name+'.png')); bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'original_import.blend'))
print('INSPECT_COMPLETE',flush=True)
