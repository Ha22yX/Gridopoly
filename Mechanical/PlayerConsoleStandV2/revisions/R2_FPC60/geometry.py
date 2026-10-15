"""R2 geometry appended after shared R1 primitive helpers. Dimensions in mm."""
from mathutils import Matrix

dx,dy=CONFIG['screen_translation_xy']
deck_z=CONFIG['deck_z']; pcb_top=CONFIG['pcb_top']
bpy.ops.wm.stl_import(filepath=CONFIG['legacy_stl'],use_scene_unit=False)
upper=bpy.context.object;upper.name='R2_02_SCREEN_SUPPORT_PRINT'
# Keep the complete legacy sloping interface above the original four-mm base.
boolean(upper,box('remove_old_base',(-100,-100,-1),(100,100,4)))
upper.location.x+=dx;upper.location.y+=dy
plate=rounded_rect('R2_01_CONNECTION_PLATE_PRINT',-69,-47,47,64,5,0,4)

# Open side chute. The complete legacy ring (radius 20) remains on v > -20.25.
ang=math.radians(35);cw=math.radians(47.5)
u=Vector((math.cos(cw),-math.sin(cw)*math.cos(ang),-math.sin(cw)*math.sin(ang)))
v=Vector((math.sin(cw),math.cos(cw)*math.cos(ang),math.cos(cw)*math.sin(ang)))
n=Vector((0,-math.sin(ang),math.cos(ang)))
origin=Vector((dx,3+dy,28))
uv=[(-4.5,-20.25),(-6,-38),(-6,-90),(22,-90),(22,-38),(4.5,-20.25)]
verts=[tuple(origin+u*x+v*y+n*z) for z in (-90,12) for x,y in uv]
count=len(uv)
faces=[tuple(reversed(range(count))),tuple(range(count,2*count))]+[(i,(i+1)%count,(i+1)%count+count,i+count) for i in range(count)]
m=bpy.data.meshes.new('open_chute');m.from_pydata(verts,[],faces);m.update()
c=bpy.data.objects.new('open_chute',m);scene.collection.objects.link(c);clean(c)
c2=c.copy();c2.data=c.data.copy();scene.collection.objects.link(c2)
boolean(upper,c)
boolean(plate,c2)
boolean(plate,cyl('reinforced_front_left_mount',(-35,-35),6,0,4),'UNION')

for xy in CONFIG['holes_xy']:boolean(plate,cyl('main_M3',xy,1.8,-1,7))
for cy in (13.208,-13.208):boolean(plate,box('button_access',(29.5,cy-7,-1),(48,cy+7,9)))
boolean(plate,box('J3_service',(-9,31,-1),(9,39.5,4.1)))
boolean(plate,box('strap_front_slot',(-6,40.5,-1),(6,43,5)))

# The proven screen support is a separate, flat-footed print, attached by two ears.
screen_mounts=[(-57,-18),(7,15)]
for xy in screen_mounts:
    boolean(upper,cyl('screen_mount_ear',xy,5,4,8),'UNION')
boolean(upper,box('left_ear_bridge',(-57,-23,4),(-51,-13,8)),'UNION')
boolean(upper,box('right_ear_bridge',(1,10,4),(7,20,8)),'UNION')
for xy in screen_mounts:
    boolean(upper,cyl('screen_ear_hole',xy,1.8,3,9))
    boolean(plate,cyl('plate_screen_hole',xy,1.8,-1,5))
clean(upper);assign(upper,plastic)
upper.location.z-=4
export(upper,'02_Screen_Support_CW47_5_R2.stl')
upper.location.z+=4

# Separate upright cradle: 2mm floor, open top, external fixing ears.
cradle=rounded_rect('R2_03_BATTERY_CRADLE_PRINT',-32,44,32,61,2,0,39)
boolean(cradle,rounded_rect('battery_void',-30,46,30,59,0.8,2,40))
boolean(cradle,box('open_left_wire_end',(-33,43.5,6),(-25,59.3,40)))
battery_mounts=[(-37,52.5),(37,52.5)]
for xy in battery_mounts:
    boolean(cradle,cyl('cradle_ear',xy,5,0,3),'UNION')
boolean(cradle,box('left_cradle_bridge',(-37,47.5,0),(-31,57.5,3)),'UNION')
boolean(cradle,box('right_cradle_bridge',(31,47.5,0),(37,57.5,3)),'UNION')
for xy in battery_mounts:
    boolean(cradle,cyl('cradle_ear_hole',xy,1.8,-1,4))
    boolean(plate,cyl('plate_cradle_hole',xy,1.8,-1,5))
clean(cradle);assign(cradle,plastic);export(cradle,'03_Battery_Cradle_R2.stl')
clean(plate);assign(plate,plastic);export(plate,'01_Connection_Plate_R2.stl')
upper['screen_mating_geometry']='CW47.5 original, rigid XY translation only; new chute outside v=-20.25'
upper['battery']='Upright, 56 X / 10.3 Y / 34.5 Z, no compression'

# Lid printed flat with the open side up; invert only when assembling.
lid=rounded_rect('R2_04_BATTERY_LID_PRINT',-34.5,-11,34.5,11,2,0,4)
boolean(lid,rounded_rect('lid_void',-32.5,-9,32.5,9,0.8,2,5))
clean(lid);assign(lid,plastic);export(lid,'04_Battery_Lid_R2.stl')
lid['assembly']='Open side down, over cradle; retain with 10mm wide 180-200mm long soft reusable strap through dedicated front slot and around rear edge'

spacer=cyl('R2_05_SPACER_6MM_PRINT',(0,0),3,0,6)
boolean(spacer,cyl('spacer_bore',(0,0),1.8,-1,7));clean(spacer);assign(spacer,plastic)
export(spacer,'05_Spacer_6mm_R2.stl')
for obj in (plate,upper,cradle,lid,spacer):
    r=report(obj)
    if r['non_manifold_edges'] or r['connected_components']!=1:raise RuntimeError(json.dumps(r))

upper.location.z+=deck_z
plate.location.z+=deck_z
cradle.location.z+=deck_z+4
# Import object's matrix contains the original STL translation. Its mesh is in
# translated-space local coordinates after Boolean operations; do not reset XY.
lid.rotation_euler.x=math.pi;lid.location=(0,52.5,deck_z+45)
spacer.location=(*CONFIG['holes_xy'][0],pcb_top+3)
for xy in CONFIG['holes_xy'][1:]:
    o=spacer.copy();o.data=spacer.data.copy();scene.collection.objects.link(o);o.location=(*xy,pcb_top+3)

pcb=rounded_rect('R2_PCB_REFERENCE',-40,-40,40,40,5,8,9.6)
for xy in CONFIG['holes_xy']:boolean(pcb,cyl('PCB_hole',xy,1.7,7,11))
assign(pcb,pcbmat)
for part in CONFIG['board']['components']:
    bb=part.get('body_bbox_xy_mm')
    if not bb:continue
    ref=part['ref'];h=6 if ref=='J3' else (2.7 if ref=='J1' else (2 if ref in ('J2','L1') else 1.5))
    o=box('R2_COMPONENT_'+ref,(bb[0]-40,40-bb[3],pcb_top),(bb[2]-40,40-bb[1],pcb_top+h))
    assign(o,silver if ref.startswith('J') else black);o['height_status']='illustrative, not physical measurement'
plug=box('R2_J3_PLUG',(-3.1,32.1,14.1),(3.1,37,17.6));assign(plug,white)
battery=rounded_rect('R2_BATTERY_REFERENCE',-28,47.35,28,57.65,0.5,deck_z+6.5,deck_z+41)
assign(battery,battmat)
for i,xy in enumerate(CONFIG['holes_xy'],1):
    foot=cyl('R2_FOOT_'+str(i),xy,3,0,8);boolean(foot,cyl('foot_thread',xy,1.5,-1,9));assign(foot,white)
    wash=cyl('R2_WASHER_'+str(i),xy,3.3,deck_z+4,deck_z+4.5);boolean(wash,cyl('wash_bore',xy,1.6,deck_z+3.9,deck_z+4.6));assign(wash,silver)
    screw=cyl('R2_M3x18_'+str(i),xy,1.5,deck_z+4.5-18,deck_z+4.5)
    boolean(screw,cyl('head',xy,2.8,deck_z+4.45,deck_z+6.8),'UNION');assign(screw,silver)

for group,xy_list,thickness,length in [('SCREEN',screen_mounts,4,12),('CRADLE',battery_mounts,3,12)]:
    for i,xy in enumerate(xy_list):
        top=deck_z+4+thickness
        wash=cyl('R2_EXTRA_WASHER_'+group+str(i),xy,3.3,top,top+.5)
        boolean(wash,cyl('wash_hole',xy,1.6,top-.1,top+.6));assign(wash,silver)
        screw=cyl('R2_EXTRA_SCREW_'+group+str(i),xy,1.5,top+.5-length,top+.5)
        boolean(screw,cyl('head',xy,2.8,top+.45,top+2.8),'UNION');assign(screw,silver)
        nut=cyl('R2_EXTRA_NUT_'+group+str(i),xy,5.5/math.sqrt(3),deck_z-2.4,deck_z,n=6)
        boolean(nut,cyl('nut_hole',xy,1.5,deck_z-2.5,deck_z+.1));assign(nut,silver)

normal=Vector((0,-math.sin(ang),math.cos(ang)))
center=Vector((dx,3+dy,28+deck_z))
def disk(name,r,depth,d):
    bpy.ops.mesh.primitive_cylinder_add(vertices=128,radius=r,depth=depth,location=center+normal*d,rotation=(ang,0,0))
    o=bpy.context.object;o.name=name;assign(o,glass if 'GLASS' in name else black);return o
disk('R2_SCREEN_BOSS',17,7.6,3.8);disk('R2_SCREEN_BODY',40.065,17.9,16.55);disk('R2_SCREEN_GLASS',27.2,0.6,25.8)

def cable(name,pts,r,material):
    cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';cu.bevel_depth=r;cu.bevel_resolution=3;cu.use_fill_caps=True
    s=cu.splines.new('POLY');s.points.add(len(pts)-1)
    for p,co in zip(s.points,pts):p.co=(*co,1)
    o=bpy.data.objects.new(name,cu);scene.collection.objects.link(o);assign(o,material);return o

# ROUTE_KEYS is filled by the route script; it is a design envelope only.
route=CONFIG['route']
pts=[s['center_xyz_mm'] for s in route['centerline']]
for key,variant in route['ribbon_variants'].items():
    verts=[]
    for p,s in zip(pts,variant['samples']):
        w=Vector(s['width_axis_xyz']);nn=Vector(s['normal_xyz'])
        for sw,sn in [(-1,-1),(1,-1),(1,1),(-1,1)]:
            verts.append(tuple(Vector(p)+sw*2.5*w+sn*0.15*nn))
    faces=[(3,2,1,0),tuple(4*(len(pts)-1)+j for j in range(4))]
    for i in range(len(pts)-1):
        for j in range(4):faces.append((4*i+j,4*i+(j+1)%4,4*(i+1)+(j+1)%4,4*(i+1)+j))
    m=bpy.data.meshes.new('FPC_width5_thickness_assumed0.3');m.from_pydata(verts,[],faces);m.update()
    fpc=bpy.data.objects.new('R2_FPC_'+key+'_UNCONFIRMED',m);scene.collection.objects.link(fpc);clean(fpc);assign(fpc,amber)
    fpc.hide_render=key!='end_normal_minus_Z';fpc.hide_set(key!='end_normal_minus_Z')
    fpc['status']='Both contact-face alternatives are conditional; showing one does not approve pin mapping or actual fit'
red=mat('R2 Battery red wire',(0.7,0.025,0.025))
cable('R2_BATTERY_WIRE_REFERENCE',[(-28,49,32),(-30.7,45,29),(0,35,23.6),(0,35,17.6)],0.6,red)

# Purchased soft strap reference, not a printable part; no battery compression.
strap=rounded_rect('R2_SOFT_STRAP_REFERENCE',40.7,14.8,64.8,61.4,1,-5,5)
boolean(strap,rounded_rect('strap_inner',41.3,15.4,64.2,60.8,0.4,-6,6))
for vert in strap.data.vertices:
    x,y,z=vert.co;vert.co=(z,x,y)
clean(strap);assign(strap,black)
strap['status']='Schematic 10mm wide soft strap, 180-200mm adjustable stock length; hook/loop overlap not modelled'

scene['revision']=CONFIG['revision'];scene['fpc_user_length_mm']=60
scene['route_status']=CONFIG['status']
scene['battery_lid_retention']='10mm wide 180-200mm reusable soft strap through dedicated front slot and around rear edge; do not compress pack'
bpy.ops.object.camera_add(location=(150,-190,150));cam=bpy.context.object;cam.name='R2_CAMERA'
cam.rotation_euler=(Vector((-8,8,43))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=175;scene.camera=cam
for loc,energy,size in [((40,-90,180),110000,130),((-100,-30,90),70000,100),((0,120,150),90000,100)]:
    bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=energy;o.data.shape='DISK';o.data.size=size
    o.rotation_euler=(Vector((-8,8,35))-o.location).to_track_quat('-Z','Y').to_euler()
scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1600;scene.render.resolution_y=1400;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_distance=180;area.spaces.active.region_3d.view_location=(-8,8,40)
        area.spaces.active.region_3d.view_rotation=cam.rotation_euler.to_quaternion();area.spaces.active.overlay.show_extras=False
        area.spaces.active.shading.color_type='MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=CONFIG['out']+'/Gridopoly_Player_Stand_V2_R2_FPC60.blend',copy=True)
print('R2_BUILD_OK '+json.dumps({o.name:{**report(o),'dimensions_mm':list(o.dimensions)} for o in (plate,upper,cradle,lid,spacer)}))
