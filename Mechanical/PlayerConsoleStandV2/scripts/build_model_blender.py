"""Millimetre geometry. CONFIG supplied by prepare_model.py. Blender MCP safe mode."""
import bpy
import bmesh
import math
import json
from mathutils import Vector

scene=bpy.data.scenes.new('Player Stand V2 R1 - FIRST FIT')
bpy.context.window.scene=scene
scene.unit_settings.system='METRIC'
scene.unit_settings.scale_length=0.001
scene.unit_settings.length_unit='MILLIMETERS'
scene.world=bpy.data.worlds.new('Player Stand Studio')
scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(0.7,0.75,0.8,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=0.45

def mat(name,col,metal=0):
    m=bpy.data.materials.new(name)
    m.diffuse_color=(*col,1)
    m.use_nodes=True
    s=m.node_tree.nodes.get('Principled BSDF')
    s.inputs['Base Color'].default_value=(*col,1)
    s.inputs['Roughness'].default_value=0.55
    s.inputs['Metallic'].default_value=metal
    return m

plastic=mat('Single colour PLA - blue grey',(0.28,0.40,0.50))
pcbmat=mat('PCB reference green',(0.03,0.22,0.14))
black=mat('Module reference black',(0.025,0.03,0.04))
glass=mat('Display reference',(0.025,0.20,0.25))
silver=mat('Hardware reference',(0.52,0.56,0.60),0.5)
battmat=mat('Battery reference silver',(0.70,0.72,0.74),0.35)
amber=mat('FPC route reference - not a fitted cable',(0.90,0.38,0.05))
white=mat('Insulating feet reference',(0.80,0.83,0.85))

def assign(o,m):
    o.data.materials.clear()
    o.data.materials.append(m)

def clean(o):
    bm=bmesh.new(); bm.from_mesh(o.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=0.000001)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(o.data); bm.free(); o.data.update()

def box(name,lo,hi):
    bpy.ops.mesh.primitive_cube_add(size=1,location=tuple((a+b)/2 for a,b in zip(lo,hi)))
    o=bpy.context.object; o.name=name; o.dimensions=tuple(b-a for a,b in zip(lo,hi))
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    return o

def cyl(name,xy,r,z0,z1,n=64):
    bpy.ops.mesh.primitive_cylinder_add(vertices=n,radius=r,depth=z1-z0,location=(*xy,(z0+z1)/2))
    o=bpy.context.object; o.name=name
    return o

def boolean(a,b,op='DIFFERENCE'):
    bpy.context.view_layer.objects.active=a
    md=a.modifiers.new('Geometry '+op,'BOOLEAN'); md.solver='MANIFOLD'; md.operation=op; md.object=b
    bpy.ops.object.modifier_apply(modifier=md.name)
    bpy.data.objects.remove(b,do_unlink=True)
    return a

def extrusion(name,pts,z0,z1):
    n=len(pts)
    verts=[(x,y,z) for z in (z0,z1) for x,y in pts]
    faces=[tuple(reversed(range(n))),tuple(range(n,n*2))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    mesh=bpy.data.meshes.new(name); mesh.from_pydata(verts,[],faces); mesh.update()
    o=bpy.data.objects.new(name,mesh); scene.collection.objects.link(o); clean(o)
    return o

def rounded_rect(name,x0,y0,x1,y1,r,z0,z1):
    pts=[]
    for cx,cy,a in [(x1-r,y1-r,0),(x0+r,y1-r,90),(x0+r,y0+r,180),(x1-r,y0+r,270)]:
        for i in range(17):
            t=math.radians(a+90*i/16)
            pts.append((cx+r*math.cos(t),cy+r*math.sin(t)))
    return extrusion(name,pts,z0,z1)

def report(o):
    bm=bmesh.new(); bm.from_mesh(o.data)
    result={'vertices':len(bm.verts),'faces':len(bm.faces),'non_manifold_edges':sum(not e.is_manifold for e in bm.edges),'signed_volume_mm3':bm.calc_volume(signed=True)}
    remaining=set(bm.verts); count=0
    while remaining:
        q=[remaining.pop()]; count+=1
        while q:
            for e in q.pop().link_edges:
                for v in e.verts:
                    if v in remaining: remaining.remove(v); q.append(v)
    result['connected_components']=count
    bm.free()
    return result

def export(o,filename):
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active=o
    bpy.ops.wm.stl_export(filepath=CONFIG['out']+'/'+filename,export_selected_objects=True,use_scene_unit=False,ascii_format=False)

def cut_openings(o,tray=False):
    for key,rect in CONFIG['openings'].items():
        x0,y0,x1,y1=rect
        if tray and key=='battery_service': y0=30.0
        boolean(o,box(key+'_clearance',(x0,y0,-1),(x1,y1,55)))

# Preserve the proven screen interface exactly; only the peripheral base is cut.
bpy.ops.wm.stl_import(filepath=CONFIG['legacy_stl'],use_scene_unit=False)
upper=bpy.context.object; upper.name='01_UPPER_CW47_5_PRINT'
for xy in CONFIG['holes_xy']:
    boolean(upper,cyl('M3_clearance',xy,1.8,-1,8))
cut_openings(upper)
clean(upper); assign(upper,plastic)
upper['legacy_interface']='CW47.5 user test-fit passed; ring, pins and face unchanged'
upper['retention']='Original gravity seating; not a positive screen carry lock'
export(upper,'01_Upper_CW47_5.stl')

# Separate open-top tray: planar print bed, vertical walls, no hidden roof.
tray=rounded_rect('02_BATTERY_TRAY_PRINT',-47,-41,47,41,5,0,2)
outer=rounded_rect('battery_walls',-35,-10,29,32,2,1.9,16)
inner=rounded_rect('battery_cavity',-33,-8,27,30,1,2,17)
boolean(outer,inner)
boolean(tray,outer,'UNION')
for xy in CONFIG['holes_xy']:
    boolean(tray,cyl('corner_column',xy,4,1.9,16),'UNION')
    boolean(tray,cyl('M3_through',xy,1.8,-1,17))
cut_openings(tray,True)
clean(tray); assign(tray,plastic)
tray['battery_cavity_mm']='60 x 38 x 14; loose battery fit, not compression clamp'
export(tray,'02_Battery_Tray.stl')

spacers=[]
for h in (8,12):
    o=cyl('03_SPACER_'+str(h)+'MM_PRINT',(0,0),3,0,h)
    boolean(o,cyl('M3_through',(0,0),1.8,-1,h+1)); clean(o); assign(o,plastic)
    export(o,'03_Spacer_'+str(h)+'mm.stl')
    spacers.append(o)

reports={o.name:report(o) for o in [upper,tray]+spacers}
for r in reports.values():
    if r['non_manifold_edges'] or r['connected_components']!=1 or r['signed_volume_mm3']<=0:
        raise RuntimeError('Print mesh failed: '+json.dumps(r))

# Assembly coordinates: table Z=0, USB toward -Y, PCB bottom Z=8.
pcb_bottom=CONFIG['feet_mm']; pcb_top=pcb_bottom+CONFIG['board_thickness_mm']
tray_z=pcb_top+CONFIG['spacer_mm']; upper_z=tray_z+16
upper.location.z+=upper_z; tray.location.z+=tray_z
spacers[1].hide_render=True; spacers[1].hide_set(True)
spacers[0].location=(*CONFIG['holes_xy'][0],pcb_top+CONFIG['spacer_mm']/2)
for xy in CONFIG['holes_xy'][1:]:
    o=spacers[0].copy(); o.data=spacers[0].data.copy(); scene.collection.objects.link(o); o.location=(*xy,pcb_top+CONFIG['spacer_mm']/2)

pcb=rounded_rect('PCB_REFERENCE_80x80',-40,-40,40,40,5,pcb_bottom,pcb_top)
for xy in CONFIG['holes_xy']: boolean(pcb,cyl('PCB_hole',xy,1.7,pcb_bottom-1,pcb_top+1))
assign(pcb,pcbmat)
component_refs=[]
for item in CONFIG['board']['components']:
    bb=item.get('body_bbox_xy_mm')
    if not bb: continue
    ref=item.get('designator',item.get('reference',item.get('ref','part')))
    h=6 if ref=='J3' else (2.7 if ref=='J1' else (2 if ref in ('J2','L1') else 1.5))
    o=box('PCB_'+ref+'_ENVELOPE',(bb[0]-40,40-bb[3],pcb_top),(bb[2]-40,40-bb[1],pcb_top+h))
    assign(o,silver if ref.startswith('J') else black)
    o['height_status']='Illustrative reserve, not as-built height validation'
    component_refs.append(o)

# Top-entry battery plug and wire volume; freely accessible at the back.
plug=box('J3_PLUG_REFERENCE',(-3.1,32.1,pcb_top+4.5),(3.1,37,pcb_top+8)); assign(plug,white)
battery=rounded_rect('BATTERY_LP103454_REFERENCE',-31,-6.25,25,28.25,1.5,tray_z+2.5,tray_z+13.1)
assign(battery,battmat)
battery['source']='EEMB Amazon finished pack envelope 56 x 34.5 x 10.6 mm, not measured'

hardware=[]
for i,xy in enumerate(CONFIG['holes_xy'],1):
    foot=cyl('NYLON_FEMALE_FOOT_'+str(i),xy,3,0,8)
    boolean(foot,cyl('thread_reference',xy,1.5,-1,9)); assign(foot,white); hardware.append(foot)
    washer=cyl('M3_WASHER_'+str(i),xy,3.3,upper_z+4,upper_z+4.5)
    boolean(washer,cyl('washer_bore',xy,1.6,upper_z+3.9,upper_z+4.6)); assign(washer,silver); hardware.append(washer)
    screw=cyl('M3x35_SCREW_'+str(i),xy,1.5,upper_z+4.5-35,upper_z+4.5)
    boolean(screw,cyl('pan_head',xy,2.8,upper_z+4.45,upper_z+6.8),'UNION'); assign(screw,silver); hardware.append(screw)

angle=math.radians(35); normal=Vector((0,-math.sin(angle),math.cos(angle)))
center=Vector((0,3,28+upper_z))
def screen_cylinder(name,r,depth,distance,m):
    bpy.ops.mesh.primitive_cylinder_add(vertices=128,radius=r,depth=depth,location=center+normal*distance,rotation=(angle,0,0))
    o=bpy.context.object; o.name=name; assign(o,m); return o
screenparts=[screen_cylinder('SCREEN_REAR_BOSS_REFERENCE',17,7.6,3.8,black),
    screen_cylinder('SCREEN_MODULE_REFERENCE',40.065,17.9,7.6+17.9/2,black),
    screen_cylinder('SCREEN_GLASS_REFERENCE',27.2,0.6,7.6+17.9+0.3,glass)]

def wire(name,pts,r,m):
    c=bpy.data.curves.new(name,'CURVE'); c.dimensions='3D'; c.bevel_depth=r; c.bevel_resolution=3
    s=c.splines.new('POLY'); s.points.add(len(pts)-1)
    for p,co in zip(s.points,pts): p.co=(*co,1)
    o=bpy.data.objects.new(name,c); scene.collection.objects.link(o); assign(o,m); return o

# A route guide only: deliberately not exported as a printable or certified cable.
path=[(-1.47,1.89,upper_z+28.8),(-20.64,-12.50,upper_z+17.8),(-28.5,-16,upper_z+17),
      (-34,-18.8,upper_z+11),(-43,-18.8,upper_z+2),(-44,-18.8,pcb_top+5),(-42,-18.8,pcb_top+2),(-40.4,-18.8,pcb_top+1.3)]
fpc=wire('FPC_ROUTE_GUIDE_LENGTH_UNVERIFIED',path,0.6,amber)
route_length=sum((Vector(b)-Vector(a)).length for a,b in zip(path,path[1:]))
fpc['polyline_length_mm']=route_length; fpc['warning']='Guide only; actual lead length, pin mapping and bend radius must be fitted'
red=mat('Battery wire red',(0.7,0.025,0.025))
bwire=wire('BATTERY_WIRE_ROUTE_REFERENCE',[(-31,20,tray_z+7),(-32,28.5,tray_z+7),(-7,28.5,tray_z+7),(-7,33,tray_z+7),(0,35,tray_z+2),(0,35,pcb_top+8)],0.6,red)

scene['design_status']=CONFIG['status']
scene['pcb_source']=CONFIG['board']['board']['revision']
scene['assembly_z_mm']=json.dumps({'pcb_bottom':pcb_bottom,'pcb_top':pcb_top,'tray_bottom':tray_z,'upper_bottom':upper_z})
scene['fpc_route_polyline_length_mm']=route_length

def camera(loc,target,scale):
    bpy.ops.object.camera_add(location=loc); c=bpy.context.object; c.name='Player_Stand_View'
    c.rotation_euler=(Vector(target)-c.location).to_track_quat('-Z','Y').to_euler()
    c.data.type='ORTHO'; c.data.ortho_scale=scale; scene.camera=c; return c

cam=camera((150,-190,160),(0,0,45),155)
for loc,energy,size in [((40,-90,180),1100,130),((-100,-30,90),700,100),((0,120,150),900,100)]:
    bpy.ops.object.light_add(type='AREA',location=loc); o=bpy.context.object; o.data.energy=energy*100; o.data.shape='DISK'; o.data.size=size
    o.rotation_euler=(Vector((0,0,35))-o.location).to_track_quat('-Z','Y').to_euler()
scene.render.engine='BLENDER_EEVEE'; scene.render.resolution_x=1600; scene.render.resolution_y=1400; scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'; scene.render.film_transparent=False
scene.view_settings.view_transform='AgX'
for a in bpy.context.screen.areas:
    if a.type=='VIEW_3D':
        a.spaces.active.region_3d.view_distance=180
        a.spaces.active.region_3d.view_location=(0,0,43)
        a.spaces.active.region_3d.view_rotation=cam.rotation_euler.to_quaternion()
        a.spaces.active.overlay.show_extras=False
        a.spaces.active.clip_end=5000
        a.spaces.active.shading.color_type='MATERIAL'
bpy.ops.object.select_all(action='DESELECT'); upper.select_set(True); bpy.context.view_layer.objects.active=upper
bpy.ops.wm.save_as_mainfile(filepath=CONFIG['out']+'/Gridopoly_Player_Stand_V2_R1.blend',copy=True)
print('BUILD_REPORT '+json.dumps({'meshes':reports,'stack':{'pcb_bottom':pcb_bottom,'pcb_top':pcb_top,'tray_bottom':tray_z,'upper_bottom':upper_z},'fpc_route_guide_length_mm':route_length,'output':CONFIG['out']}))
