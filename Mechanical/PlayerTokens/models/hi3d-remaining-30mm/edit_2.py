# Executed through Blender MCP after import and measured alignment.
import bpy,bmesh,math
from mathutils import Vector,Quaternion
scene=bpy.data.scenes['Hi3D_2_30mm_EDIT']
bpy.context.window.scene=scene
obj=bpy.data.objects['Hi3D_2_Work']
bpy.context.view_layer.objects.active=obj
obj.select_set(True)
bm=bmesh.new()
bm.from_mesh(obj.data)
bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=0.000001,plane_co=(0,0,3.8949999999999996),plane_no=(0,0,1),clear_inner=True,clear_outer=False)
boundary=[e for e in bm.edges if e.is_boundary]
bmesh.ops.holes_fill(bm,edges=boundary,sides=0)
bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
bm.to_mesh(obj.data)
bm.free()
obj.data.update()
bpy.ops.mesh.primitive_cylinder_add(vertices=720,radius=15,depth=4.55,end_fill_type='NGON',location=(0,0,2.275))
base=bpy.context.object
base.name='Construction_2_D30'
bpy.context.view_layer.objects.active=obj
modifier=obj.modifiers.new(name='Exact_D30_base',type='BOOLEAN')
modifier.operation='UNION'
modifier.solver='EXACT'
modifier.object=base
bpy.ops.object.modifier_apply(modifier=modifier.name)
bpy.data.objects.remove(base,do_unlink=True)
obj.name='Beaver_30mm_Print'
obj['source_file']='Z:/Files/我的项目/Gridopoly/Mechanical/PlayerTokens/sources/hi3d-originals/Hi3D(2).3mf'
obj['base_diameter_mm']=30.0
obj['base_height_mm']=4.55
obj['uniform_scale']=0.41
obj.data.update()
for p in obj.data.polygons:p.use_smooth=p.center.z>4.56
for item in bpy.context.selected_objects:item.select_set(False)
obj.select_set(True)
bpy.context.view_layer.objects.active=obj
bpy.context.view_layer.update()
bpy.ops.wm.stl_export(filepath='Z:/Files/我的项目/Gridopoly/Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Beaver_30mm.stl',export_selected_objects=True,apply_modifiers=True,global_scale=1.0,use_scene_unit=False,ascii_format=False)
scene.render.filepath='Z:/Files/我的项目/Gridopoly/Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Beaver_30mm_preview.png'
bpy.ops.render.render(write_still=True)
cam=scene.camera
savedloc=cam.location.copy()
savedrot=cam.rotation_euler.copy()
savedscale=cam.data.ortho_scale
cam.location=(0,0,-80)
cam.rotation_euler=(Vector((0,0,0))-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.ortho_scale=35
scene.render.resolution_y=800
scene.render.filepath='Z:/Files/我的项目/Gridopoly/Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining/Beaver_30mm_bottom.png'
bpy.ops.render.render(write_still=True)
cam.location=savedloc
cam.rotation_euler=savedrot
cam.data.ortho_scale=savedscale
scene.render.resolution_y=900
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        view=area.spaces.active.region_3d
        view.view_location=Vector((0,0,18.274499035212312))
        view.view_distance=75
        view.view_rotation=Quaternion((0.8924,0.3696,0.099,0.2391))
        area.spaces.active.clip_end=10000
        area.spaces.active.shading.type='SOLID'
        area.spaces.active.shading.show_cavity=True
        area.spaces.active.overlay.show_floor=False
bottom=[v.co for v in obj.data.vertices if abs(v.co.z)<0.000001]
print({'name':obj.name,'dimensions_mm':list(obj.dimensions),'bottom_z':[min(v.z for v in bottom),max(v.z for v in bottom)],'bottom_radius':[min(math.hypot(v.x,v.y) for v in bottom),max(math.hypot(v.x,v.y) for v in bottom)]})
