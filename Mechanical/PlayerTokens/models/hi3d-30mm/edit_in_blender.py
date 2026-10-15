# Executed through the configured Blender MCP in the live Blender 5.2.2 session.
# Requires a mesh named Hi3D_Fox_Work imported from the source geometry (millimeters).
import bpy, bmesh
from mathutils import Matrix, Vector, Quaternion
obj=bpy.data.objects['Hi3D_Fox_Work']
original=obj.copy()
original.data=obj.data.copy()
original.name='REFERENCE_Original_Hi3D'
bpy.context.scene.collection.objects.link(original)
original.hide_render=True
original.hide_set(True)
original.hide_select=True
rotation=Matrix(((0.9999989395088579,-0.0002919482802057639,0.0014267961877380347),(0.0,0.9797009628929175,0.20046451882238484),(-0.0014563588704913676,-0.20046430623153833,0.9796999239287244)))
transform=Matrix.Scale(0.49,4) @ Matrix.Translation(Vector((-0.60,-0.23,35.08851416257475))) @ rotation.to_4x4()
obj.data.transform(transform)
obj.data.update()
bpy.context.view_layer.objects.active=obj
obj.select_set(True)
bm=bmesh.new()
bm.from_mesh(obj.data)
bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=0.000001,plane_co=(0,0,3.43),plane_no=(0,0,1),clear_inner=True,clear_outer=False)
boundary=[e for e in bm.edges if e.is_boundary]
bmesh.ops.holes_fill(bm,edges=boundary,sides=0)
bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
bm.to_mesh(obj.data)
bm.free()
obj.data.update()
bpy.ops.mesh.primitive_cylinder_add(vertices=720,radius=15,depth=4.09,end_fill_type='NGON',location=(0,0,2.045))
base=bpy.context.object
base.name='Construction_D30mm_Base'
bpy.context.view_layer.objects.active=obj
modifier=obj.modifiers.new(name='Union_exact_D30mm_base',type='BOOLEAN')
modifier.operation='UNION'
modifier.solver='EXACT'
modifier.object=base
bpy.ops.object.modifier_apply(modifier=modifier.name)
bpy.data.objects.remove(base,do_unlink=True)
obj.name='Fox_30mm_Print'
obj['base_diameter_mm']=30.0
obj['base_height_mm']=4.09
obj['uniform_scale_from_Hi3D']=0.49
obj['source_file']='Z:/Files/我的项目/Gridopoly/Mechanical/PlayerTokens/sources/hi3d-originals/Hi3D.3mf'
for polygon in obj.data.polygons:
    polygon.use_smooth=polygon.center.z>4.1
for item in bpy.context.selected_objects:
    item.select_set(False)
obj.select_set(True)
bpy.context.view_layer.objects.active=obj
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        region=area.spaces.active.region_3d
        region.view_location=Vector((0,0,23))
        region.view_distance=83
        region.view_rotation=Quaternion((0.8924,0.3696,0.099,0.2391))
        area.spaces.active.clip_end=10000
        area.spaces.active.shading.type='SOLID'
        area.spaces.active.shading.light='STUDIO'
        area.spaces.active.shading.show_cavity=True
        area.spaces.active.overlay.show_floor=False
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath='Z:/Files/我的项目/Gridopoly/Mechanical/PlayerTokens/models/hi3d-30mm/deliverables/Hi3D_30mm/Hi3D_30mm.blend')
bpy.ops.wm.stl_export(filepath='Z:/Files/我的项目/Gridopoly/Mechanical/PlayerTokens/models/hi3d-30mm/deliverables/Hi3D_30mm/Hi3D_30mm.stl',export_selected_objects=True,apply_modifiers=True,global_scale=1.0,use_scene_unit=False,ascii_format=False)
print({'object':obj.name,'vertices':len(obj.data.vertices),'faces':len(obj.data.polygons),'dimensions_mm':list(obj.dimensions),'bottom_z':min(v.co.z for v in obj.data.vertices)})
