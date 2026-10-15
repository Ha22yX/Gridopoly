"""R3: repair only the screen mounting feet; preserve the proven mating face."""
bpy.ops.wm.stl_import(filepath=CONFIG['source_support'],use_scene_unit=False)
upper=bpy.context.object;upper.name='R3_02_SCREEN_SUPPORT_PRINT'
boolean(upper,extrusion('left_wide_foot',CONFIG['left_foot_polygon_xy'],0,4),'UNION')
boolean(upper,rounded_rect('right_wide_foot',-6,8,7,22,2,0,4),'UNION')
for xy in CONFIG['mount_holes_xy']:
    boolean(upper,cyl('original_M3_clearance',xy,1.8,-.1,4.1))
clean(upper)
# Remove coplanar Boolean slivers on the two foot planes before triangulation.
# This selection excludes the screen ring, pins and sloping mating face.
bm=bmesh.new();bm.from_mesh(upper.data)
for height in (0,4):
    selected=[v for v in bm.verts if abs(v.co.z-height)<0.00002]
    for vertex in selected:vertex.co.z=height
    bmesh.ops.remove_doubles(bm,verts=selected,dist=0.00001)
    edges=[e for e in bm.edges if all(abs(v.co.z-height)<0.000001 for v in e.verts)]
    bmesh.ops.dissolve_degenerate(bm,edges=edges,dist=0.0001)
    edges=[e for e in bm.edges if all(abs(v.co.z-height)<0.000001 for v in e.verts)]
    vertices=list(set(v for e in edges for v in e.verts))
    bmesh.ops.dissolve_limit(bm,angle_limit=0.00001,use_dissolve_boundaries=False,verts=vertices,edges=edges)
bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='BEAUTY')
bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
bm.to_mesh(upper.data);bm.free();upper.data.update()
assign(upper,plastic)
upper['revision']='V2 R3 EAR REINFORCED'
upper['mounting']='Original hole centres and 4mm foot thickness; print flat feet down'
upper['interface']='Imported R2 upper; all new geometry confined to bottom 4mm'
export(upper,'02_Screen_Support_CW47_5_R3.stl')
r=report(upper)
if r['non_manifold_edges'] or r['connected_components']!=1 or r['signed_volume_mm3']<=0:
    raise RuntimeError(json.dumps(r))
bpy.ops.object.camera_add(location=(-132,-108,85))
cam=bpy.context.object;cam.name='R3_REPAIR_CAMERA'
cam.rotation_euler=(Vector((-25,-3,14))-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.type='ORTHO';cam.data.ortho_scale=105;scene.camera=cam
for loc,energy,size in [((-80,-100,120),100000,90),((55,-25,90),80000,80),((-20,70,110),90000,85)]:
    bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object
    o.data.energy=energy;o.data.shape='DISK';o.data.size=size
    o.rotation_euler=(Vector((-25,0,10))-o.location).to_track_quat('-Z','Y').to_euler()
scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1400;scene.render.resolution_y=1100
scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG'
scene.view_settings.view_transform='AgX'
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_distance=120
        area.spaces.active.region_3d.view_location=(-25,-3,14)
        area.spaces.active.region_3d.view_rotation=cam.rotation_euler.to_quaternion()
        area.spaces.active.overlay.show_extras=False
        area.spaces.active.shading.color_type='MATERIAL'
bpy.ops.object.select_all(action='DESELECT');upper.select_set(True);bpy.context.view_layer.objects.active=upper
bpy.ops.wm.save_as_mainfile(filepath=CONFIG['out']+'/Screen_Support_R3_Working.blend',copy=True)
print('R3_SUPPORT_BUILD_OK '+json.dumps(r))
