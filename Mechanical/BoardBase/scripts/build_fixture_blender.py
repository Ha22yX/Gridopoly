"""Blender/MCP geometry builder. CONFIG is supplied by prepare_fixture.py.

All lengths are millimetres. Printed parts and PCB/fastener references are
separate. This is a first-fit fixture, not a physically validated release.
"""
import bpy
import bmesh
import math
import json
from mathutils import Vector


def material(name, color):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    bs = m.node_tree.nodes.get('Principled BSDF')
    bs.inputs['Base Color'].default_value = (*color, 1)
    bs.inputs['Roughness'].default_value = 0.65
    return m


def box(name, lo, hi):
    bpy.ops.mesh.primitive_cube_add(size=1, location=tuple((a+b)/2 for a,b in zip(lo,hi)))
    o = bpy.context.object
    o.name = name
    o.dimensions = tuple(b-a for a,b in zip(lo,hi))
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return o


def cylinder(name, xy, radius, z0, z1, vertices=64, angle=0):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius,
        depth=z1-z0, location=(*xy,(z0+z1)/2), rotation=(0,0,angle))
    o=bpy.context.object
    o.name=name
    return o


def boolean(a,b,op='UNION'):
    bpy.context.view_layer.objects.active=a
    mod=a.modifiers.new('solid_'+op,'BOOLEAN')
    mod.operation=op
    mod.solver='MANIFOLD'
    mod.object=b
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(b, do_unlink=True)
    return a


def clean_mesh(obj):
    bm=bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.000001)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def export_stl(obj, path):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active=obj
    bpy.ops.wm.stl_export(filepath=path, export_selected_objects=True,
        apply_modifiers=True, use_scene_unit=False)


def mesh_report(obj):
    bm=bmesh.new()
    bm.from_mesh(obj.data)
    result={'vertices':len(bm.verts),'faces':len(bm.faces),
        'boundary_edges':sum(e.is_boundary for e in bm.edges),
        'non_manifold_edges':sum(not e.is_manifold for e in bm.edges),
        'volume_mm3':bm.calc_volume(signed=True)}
    remaining=set(bm.verts)
    count=0
    while remaining:
        stack=[remaining.pop()]
        count+=1
        while stack:
            for e in stack.pop().link_edges:
                for v in e.verts:
                    if v in remaining:
                        remaining.remove(v)
                        stack.append(v)
    result['connected_components']=count
    bm.free()
    return result


def extruded_polygon(name, pts, z0, z1):
    n=len(pts)
    vertices=[(x,y,z) for z in (z0,z1) for x,y in pts]
    faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    mesh=bpy.data.meshes.new(name)
    mesh.from_pydata(vertices,[],faces)
    mesh.update()
    o=bpy.data.objects.new(name,mesh)
    bpy.context.collection.objects.link(o)
    clean_mesh(o)
    return o


def camera_at(loc,target,scale):
    bpy.ops.object.camera_add(location=loc)
    c=bpy.context.object
    c.rotation_euler=(Vector(target)-c.location).to_track_quat('-Z','Y').to_euler()
    c.data.type='ORTHO'
    c.data.ortho_scale=scale
    bpy.context.scene.camera=c
    return c


def rounded_l_outline(vertices,radius):
    out=[]
    for i,p in enumerate(vertices):
        prev=vertices[i-1]; nxt=vertices[(i+1)%len(vertices)]
        va=Vector((p[0]-prev[0],p[1]-prev[1])).normalized()
        vb=Vector((nxt[0]-p[0],nxt[1]-p[1])).normalized()
        start=Vector(p)-va*radius
        center=start+vb*radius
        a=math.atan2(start.y-center.y,start.x-center.x)
        sign=1 if va.x*vb.y-va.y*vb.x>0 else -1
        for step in range(25):
            theta=a+sign*math.pi*0.5*step/24
            out.append((center.x+radius*math.cos(theta),center.y+radius*math.sin(theta)))
    return out


def one_piece_base(cfg):
    pts=rounded_l_outline(cfg['base_outline_corners'],cfg['base_corner_radius_mm'])
    obj=extruded_polygon('ONE_PIECE_BASE_R2_PRINT',pts,0,cfg['bottom_rib_mm'])
    for board in cfg['boards']:
        passive_i=0
        for h in board['holes']:
            xy=h['xy']; height=cfg['pcb_bottom_mm']
            if not h['active']:
                passive_i+=1
                if passive_i==2:
                    height-=0.2
            boolean(obj,cylinder('post',xy,3,3.49,height))
            bpy.ops.mesh.primitive_cone_add(vertices=64,radius1=4,radius2=3,
                depth=1.5,location=(*xy,4.25))
            boolean(obj,bpy.context.object)
            if h['active']:
                boolean(obj,cylinder('pilot_blind',xy,cfg['pilot_hole_mm']/2,height-cfg['pilot_depth_mm'],height+0.1),'DIFFERENCE')
                boolean(obj,cylinder('top_guide',xy,cfg['support_hole_mm']/2,height-cfg['guide_depth_mm'],height+0.1),'DIFFERENCE')
            else:
                boolean(obj,cylinder('loose_locator',xy,1.4,height-0.05,height+0.8))
    clean_mesh(obj)
    obj['kind']='PRINTABLE_ONE_PIECE_TEST_BASE'
    obj['physical_status']='Not yet printed or test fitted'
    return obj


def screw_fit_coupon(cfg):
    # The missing lower-left corner identifies the 2.4 mm end without text.
    obj=extruded_polygon('SCREW_FIT_COUPON_R2_PRINT',[(3,0),(34,0),(34,14),(0,14),(0,3)],0,3.5)
    for x,diam in zip((6,17,28),cfg['coupon_pilot_diameters_mm']):
        xy=(x,7)
        boolean(obj,cylinder('coupon_post',xy,3,3.49,16))
        bpy.ops.mesh.primitive_cone_add(vertices=64,radius1=4,radius2=3,depth=1.5,location=(*xy,4.25))
        boolean(obj,bpy.context.object)
        boolean(obj,cylinder('coupon_pilot',xy,diam/2,6,16.1),'DIFFERENCE')
        boolean(obj,cylinder('coupon_guide',xy,1.8,14.8,16.1),'DIFFERENCE')
    clean_mesh(obj)
    obj['kind']='OPTIONAL_SCREW_FIT_SAMPLE_NOT_A_MODULE_BASE'
    return obj


def build_fixture(cfg):
    # Preserve the user's previous scene, including any unsaved changes.
    scene=bpy.data.scenes.new('Gridopoly One Piece Corner Base R2 - FIRST FIT')
    bpy.context.window.scene=scene
    scene.world=bpy.data.worlds.new('R2_World')
    scene.unit_settings.system='METRIC'
    scene.unit_settings.scale_length=0.001
    scene.unit_settings.length_unit='MILLIMETERS'
    plastic=material('PLA - one print colour',(0.22,0.29,0.40))
    green=material('PCB reference',(0.03,0.20,0.12))
    copper=material('Hardware reference',(0.23,0.26,0.29))
    single=one_piece_base(cfg)
    single.data.materials.clear()
    single.data.materials.append(plastic)
    print_objs=[single]
    refs=[]
    reports={single.name:mesh_report(single)}
    if reports[single.name]['non_manifold_edges'] or reports[single.name]['connected_components']!=1:
        raise ValueError('Invalid one-piece mesh: '+json.dumps(reports))
    export_stl(single,cfg['output_dir']+'/ONE_PIECE_Corner_Test_R2.stl')
    coupon=screw_fit_coupon(cfg)
    reports[coupon.name]=mesh_report(coupon)
    if reports[coupon.name]['non_manifold_edges'] or reports[coupon.name]['connected_components']!=1:
        raise ValueError('Invalid coupon: '+json.dumps(reports))
    export_stl(coupon,cfg['output_dir']+'/Screw_Fit_Coupon_R2.stl')
    coupon.hide_render=True
    coupon.hide_viewport=True
    for b in cfg['boards']:
        pcb=extruded_polygon('R2_'+b['name']+'_PCB_REFERENCE',b['outline'],cfg['pcb_bottom_mm'],cfg['pcb_bottom_mm']+1.6)
        for h in b['holes']:
            boolean(pcb,cylinder('pcb_hole',h['xy'],1.75,cfg['pcb_bottom_mm']-0.1,cfg['pcb_bottom_mm']+1.7),'DIFFERENCE')
        pcb.data.materials.clear()
        pcb.data.materials.append(green)
        pcb['kind']='REFERENCE_ONLY_DO_NOT_PRINT'
        refs.append(pcb)
        for h in b['holes']:
            if h['active']:
                head=cylinder('Plastic_screw_3x10_head_REFERENCE',h['xy'],2.8,cfg['pcb_bottom_mm']+cfg['pcb_thickness_mm']+cfg['pcb_screws']['washer_thickness_mm'],cfg['pcb_bottom_mm']+cfg['pcb_thickness_mm']+cfg['pcb_screws']['washer_thickness_mm']+1.8)
                head.data.materials.append(copper)
                refs.append(head)
    scene['config_json']=json.dumps(cfg,ensure_ascii=False)
    scene['mesh_report_json']=json.dumps(reports)
    scene['physical_validation']='NOT YET PRINTED OR ASSEMBLED'
    scene.render.engine='BLENDER_EEVEE'
    scene.render.resolution_x=1500
    scene.render.resolution_y=1300
    scene.render.resolution_percentage=100
    scene.world.color=(0.3,0.3,0.3)
    scene.world.use_nodes=True
    scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(0.8,0.8,0.8,1)
    scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value=0.35
    scene.view_settings.view_transform='AgX'
    scene.view_settings.look='AgX - Medium High Contrast'
    scene.view_settings.exposure=0.0
    floor=box('RENDER_FLOOR',(-200,-100,-1),(200,250,-0.2))
    floor.data.materials.append(material('Backdrop',(0.93,0.94,0.96)))
    refs.append(floor)
    for loc,power,size in [((0,-40,220),1200,180),((-120,140,150),850,130),((160,160,140),1000,140)]:
        bpy.ops.object.light_add(type='AREA',location=loc)
        light=bpy.context.object
        light.data.energy=power*100
        light.data.shape='DISK'
        light.data.size=size
        light.rotation_euler=(Vector((10,65,0))-light.location).to_track_quat('-Z','Y').to_euler()
    cam=camera_at((200,-190,220),(8,65,8),205)
    bpy.context.view_layer.update()
    inverse=cam.matrix_world.inverted()
    projected=[inverse @ (o.matrix_world @ Vector(p)) for o in print_objs+refs if o!=floor for p in o.bound_box]
    low=[min(p[i] for p in projected) for i in (0,1)]
    high=[max(p[i] for p in projected) for i in (0,1)]
    cam.location+=cam.rotation_euler.to_quaternion() @ Vector(((low[0]+high[0])/2,(low[1]+high[1])/2,0))
    cam.data.ortho_scale=max(high[0]-low[0],(high[1]-low[1])*1500/1300)*1.12
    scene.render.filepath=cfg['output_dir']+'/fixture_assembled.png'
    bpy.ops.render.render(write_still=True)
    for obj in refs:
        if obj!=floor:
            obj.hide_render=True
    scene.render.filepath=cfg['output_dir']+'/fixture_frame.png'
    bpy.ops.render.render(write_still=True)
    # Actual half-section of one mounting post, for inspecting the open bore.
    old_cam=scene.camera
    first_xy=next(h['xy'] for h in cfg['boards'][0]['holes'] if h['active'])
    x,y=first_xy
    section=single.copy()
    section.data=single.data.copy()
    scene.collection.objects.link(section)
    section.name='R2_SECTION_REFERENCE_DO_NOT_PRINT'
    boolean(section,box('section_clip',(x-4,y,0),(x+4,y+4,17)),'INTERSECT')
    single.hide_render=True
    section.data.materials.clear()
    section.data.materials.append(plastic)
    detail_cam=camera_at((x+14,y-45,27),(x,y+1,8),22)
    scene.render.resolution_x=1100
    scene.render.resolution_y=1100
    scene.render.filepath=cfg['output_dir']+'/mounting_post_section.png'
    bpy.ops.render.render(write_still=True)
    section.hide_render=True
    section.hide_viewport=True
    detail_cam.hide_viewport=True
    single.hide_render=False
    scene.camera=old_cam
    scene.render.resolution_x=1500
    scene.render.resolution_y=1300
    floor.hide_viewport=True
    # Open the new scene showing only printable base to avoid confusing PCB references with printing geometry.
    for obj in refs:
        if obj!=floor:
            obj.hide_render=False
            obj.hide_viewport=True
    bpy.ops.object.select_all(action='DESELECT')
    for obj in print_objs:
        obj.select_set(True)
    bpy.context.view_layer.objects.active=print_objs[0]
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_distance=220
                area.spaces.active.region_3d.view_location=(8,65,7)
                area.spaces.active.region_3d.view_rotation=cam.rotation_euler.to_quaternion()
                area.spaces.active.clip_end=2000
                area.spaces.active.shading.color_type='MATERIAL'
    bpy.ops.wm.save_as_mainfile(filepath=cfg['output_dir']+'/Gridopoly_OnePiece_Corner_Test_R2.blend')
    print('MESH_REPORT '+json.dumps(reports))
    print('OUTPUT '+cfg['output_dir'])


build_fixture(CONFIG)
