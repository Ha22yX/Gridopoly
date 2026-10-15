"""Appended to the inherited geometry helpers; CONFIG is supplied by prepare.py."""

def rounded_corners(vertices,radii):
    out=[]
    for i,p in enumerate(vertices):
        r=radii[i]
        if not r:
            out.append(p)
            continue
        va=(Vector(p)-Vector(vertices[i-1])).normalized()
        vb=(Vector(vertices[(i+1)%len(vertices)])-Vector(p)).normalized()
        start=Vector(p)-va*r
        center=start+vb*r
        a=math.atan2(start.y-center.y,start.x-center.x)
        sign=1 if va.x*vb.y-va.y*vb.x>0 else -1
        for k in range(17):
            t=a+sign*math.pi/2*k/16
            out.append([center.x+r*math.cos(t),center.y+r*math.sin(t)])
    return out


def rigid_xy(xy,angle):
    cx,cy=CONFIG['center_local_mm']
    a=math.radians(angle)
    x,y=xy[0]-cx,xy[1]-cy
    return [cx+x*math.cos(a)-y*math.sin(a),cy+x*math.sin(a)+y*math.cos(a)]


def final_print_cleanup(obj):
    # Resolve tiny Boolean slivers before export, not by relaxing STL QA.
    planes=[[-67.35,-49.5,-24,-4,-3.5,-1.1,-0.5,78,80.5,81.1,83.5,84,104],
            [-24,-4,-3.5,-1.1,-0.5,2,81.1,83.5,84,90,104,192.5,210.35],
            [0,2,3.49,3.5,5,6,6.5,8.8,9.95,10,10.8,14.8,15.8,16,16.6,16.8]]
    bm=bmesh.new(); bm.from_mesh(obj.data)
    for v in bm.verts:
        for axis in range(3):
            nearest=min((abs(p-v.co[axis]),p) for p in planes[axis])
            p=nearest[1]
            if abs(p-v.co[axis])<0.00002: v.co[axis]=p
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=0.00001)
    bmesh.ops.dissolve_degenerate(bm,dist=0.0001,edges=list(bm.edges))
    bmesh.ops.dissolve_limit(bm,angle_limit=0.00001,use_dissolve_boundaries=False,verts=list(bm.verts),edges=list(bm.edges))
    bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='BEAUTY')
    bmesh.ops.dissolve_degenerate(bm,dist=0.0001,edges=list(bm.edges))
    bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='BEAUTY')
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(obj.data); bm.free(); obj.data.update()


def add_posts(obj,cfg):
    for board in cfg['boards']:
        passive_i=0
        for h in board['holes']:
            xy=h['xy']; height=cfg['pcb_bottom_mm']
            if not h['active']:
                passive_i+=1
                if passive_i==2: height-=0.2
            boolean(obj,cylinder('post',xy,3,3.49,height))
            bpy.ops.mesh.primitive_cone_add(vertices=64,radius1=4,radius2=3,depth=1.5,location=(*xy,4.25))
            boolean(obj,bpy.context.object)
            if h['active']:
                boolean(obj,cylinder('pilot',xy,cfg['pilot_hole_mm']/2,height-cfg['pilot_depth_mm'],height+0.1),'DIFFERENCE')
                boolean(obj,cylinder('guide',xy,cfg['support_hole_mm']/2,height-cfg['guide_depth_mm'],height+0.1),'DIFFERENCE')
            else:
                boolean(obj,cylinder('locator',xy,1.4,height-0.05,height+0.8))


def make_quarter(cfg):
    pts=rounded_corners(cfg['base_outline_corners'],cfg['base_corner_radii_mm'])
    obj=extruded_polygon('01_L_Quarter_PRINT_4',pts,0,3.5)
    # Ribs remain entirely outside PCB projection and below the lowest component.
    # Stop short of curved corners; ears provide the seam-end reinforcement.
    for lo,hi in [((-67.35,-3.5,3.49),(78,-1.1,6.5)),
                  ((81.1,2,3.49),(83.5,210.35,6.5)),
                  ((-3.5,90,3.49),(-1.1,210.35,6.5)),
                  ((-67.35,81.1,3.49),(-10,83.5,6.5))]:
        boolean(obj,box('edge_stiffener',lo,hi))
    add_posts(obj,cfg)
    for e in cfg['ears']:
        x0,y0=e['lo']; x1,y1=e['hi']
        p=rounded_corners([(x0,y0),(x1,y0),(x1,y1),(x0,y1)],e['radii'])
        boolean(obj,extruded_polygon('seam_ear',p,0,10))
        boolean(obj,cylinder('ear_pilot',e['screw_xy'],1.25,2,10.1),'DIFFERENCE')
        boolean(obj,cylinder('ear_guide',e['screw_xy'],1.8,8.8,10.1),'DIFFERENCE')
        boolean(obj,cylinder('bridge_locator',e['pin_xy'],1.4,9.95,10.8))
    final_print_cleanup(obj)
    obj['kind']='PRINT_PART_QUARTER'; obj['print_quantity']=4
    return obj


def make_bridge():
    pts=rounded_corners([(-10,-18),(10,-18),(10,18),(-10,18)],[2]*4)
    obj=extruded_polygon('02_Splice_Bridge_PRINT_8',pts,0,3)
    for y in (-10,10): boolean(obj,cylinder('screw_clearance',(0,y),1.8,-0.1,3.1),'DIFFERENCE')
    for y in (-4,4): boolean(obj,cylinder('pin_clearance',(0,y),1.6,-0.1,3.1),'DIFFERENCE')
    clean_mesh(obj)
    obj['kind']='PRINT_PART_BRIDGE'; obj['print_quantity']=8
    return obj


def duplicate_rotated(obj,name,angle):
    o=obj.copy(); o.data=obj.data.copy(); o.name=name
    bpy.context.collection.objects.link(o)
    o.rotation_euler.z=math.radians(angle)
    o.location=(*rigid_xy((0,0),angle),0)
    return o


def build_full_ring(cfg):
    scene=bpy.data.scenes.new('Gridopoly Full16 R1 - 4 quarters - fit pending')
    bpy.context.window.scene=scene
    scene.world=bpy.data.worlds.new('Full16_R1_World')
    scene.unit_settings.system='METRIC'; scene.unit_settings.scale_length=0.001
    scene.unit_settings.length_unit='MILLIMETERS'
    plastics=[material('PLA Quarter '+str(i+1),c) for i,c in enumerate([(0.20,0.55,0.62),(0.29,0.61,0.66),(0.20,0.55,0.62),(0.29,0.61,0.66)])]
    bridge_mat=material('PLA bridge - preview contrast',(0.16,0.28,0.34))
    pcb_mat=material('PCB reference black',(0.035,0.05,0.065))
    metal_mat=material('Screw reference',(0.26,0.29,0.30))
    q=make_quarter(cfg)
    q.data.materials.append(plastics[0])
    q.name='Q1_L_MAIN_PRINT'
    reports={'quarter':mesh_report(q)}
    export_stl(q,cfg['output_dir']+'/01_L_Quarter_PRINT_4.stl')
    bridge=make_bridge()
    bridge.data.materials.append(bridge_mat)
    reports['bridge']=mesh_report(bridge)
    export_stl(bridge,cfg['output_dir']+'/02_Splice_Bridge_PRINT_8.stl')
    bridge.hide_viewport=True; bridge.hide_render=True
    quarter_objs=[q]
    for i,angle in enumerate((90,180,270),2):
        o=duplicate_rotated(q,'Q'+str(i)+'_L_MAIN_PRINT',angle)
        o.data.materials.clear(); o.data.materials.append(plastics[i-1])
        quarter_objs.append(o)
    bridge_objs=[]; pcb_objs=[]; refs=[]
    for qi,angle in enumerate(cfg['quarter_angles_deg']):
        for bi,b in enumerate(cfg['bridges_canonical']):
            o=bridge.copy(); o.data=bridge.data.copy(); bpy.context.collection.objects.link(o)
            o.name='Joint_'+str(qi+1)+'_'+('OUTER' if bi==0 else 'INNER')+'_BRIDGE'
            o.hide_viewport=False; o.hide_render=False
            o.location=(*rigid_xy(b['xy'],angle),10)
            o.rotation_euler.z=math.radians(angle)
            bridge_objs.append(o)
        for board in cfg['boards']:
            pts=[rigid_xy(p,angle) for p in board['outline']]
            pcb=extruded_polygon('Q'+str(qi+1)+'_'+board['name']+'_PCB_REFERENCE',pts,16,17.6)
            for h in board['holes']:
                xy=rigid_xy(h['xy'],angle)
                boolean(pcb,cylinder('pcb_mount_hole',xy,1.75,15.9,17.7),'DIFFERENCE')
                if h['active']:
                    washer=cylinder('PCB_washer_REFERENCE',xy,3,17.6,18.6)
                    boolean(washer,cylinder('washer_hole',xy,1.6,17.5,18.7),'DIFFERENCE')
                    head=cylinder('Plastic_3x10_head_REFERENCE',xy,2.8,18.6,20.4)
                    for ob in (washer,head): ob.data.materials.append(metal_mat); refs.append(ob)
            pcb.data.materials.append(pcb_mat)
            pcb['kind']='REFERENCE_DO_NOT_PRINT'
            pcb_objs.append(pcb)
        for e in cfg['ears']:
            xy=rigid_xy(e['screw_xy'],angle)
            washer=cylinder('Joint_washer_REFERENCE',xy,3,13,14)
            boolean(washer,cylinder('washer_hole',xy,1.6,12.9,14.1),'DIFFERENCE')
            head=cylinder('Joint_3x10_head_REFERENCE',xy,2.8,14,15.8)
            for ob in (washer,head): ob.data.materials.append(metal_mat); refs.append(ob)
    for o in refs: o['kind']='REFERENCE_DO_NOT_PRINT'
    scene['config_json']=json.dumps(cfg,ensure_ascii=False)
    scene['mesh_report_json']=json.dumps(reports)
    scene['physical_status']='Previous 3-module corner fit confirmed by user; this full ring not printed or assembled.'
    scene['reference_pcb_objects_json']=json.dumps([o.name for o in pcb_objs])
    scene['reference_fasteners_json']=json.dumps([o.name for o in refs])
    scene['quarter_objects_json']=json.dumps([o.name for o in quarter_objs])
    scene['bridge_objects_json']=json.dumps([o.name for o in bridge_objs])
    for o in pcb_objs+refs: o.hide_set(True); o.hide_render=True
    scene.render.engine='BLENDER_EEVEE'
    scene.render.resolution_x=1600; scene.render.resolution_y=1450; scene.render.resolution_percentage=100
    scene.world.use_nodes=True
    scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(0.8,0.85,0.9,1)
    scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value=0.5
    scene.view_settings.view_transform='AgX'
    scene.view_settings.look='AgX - Medium High Contrast'
    floor=box('RENDER_FLOOR',(-500,-150,-1),(350,550,-0.3))
    scene['render_floor_object']=floor.name
    floor.data.materials.append(material('Backdrop',(0.91,0.93,0.94)))
    floor.hide_set(True)
    for loc,power,size in [((-100,-50,420),600000,400),((-350,250,280),450000,300),((220,320,300),500000,300)]:
        bpy.ops.object.light_add(type='AREA',location=loc)
        light=bpy.context.object; light.data.energy=power; light.data.shape='DISK'; light.data.size=size
        light.rotation_euler=(Vector((-99,179,0))-light.location).to_track_quat('-Z','Y').to_euler()
    camera_at((400,-420,650),(-99,179,4),620)
    for o in scene.objects:
        if o.type=='MESH':
            valid=[m for m in o.data.materials if m]
            if valid:
                chosen=valid[-1]
                o.data.materials.clear(); o.data.materials.append(chosen)
                for p in o.data.polygons: p.material_index=0
    if bpy.context.screen:
        for area in bpy.context.screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_location=Vector((-99,179,0))
                area.spaces.active.region_3d.view_distance=650
                area.spaces.active.clip_end=10000
                area.spaces.active.shading.color_type='MATERIAL'
    bpy.ops.object.select_all(action='DESELECT')
    q.select_set(True); bpy.context.view_layer.objects.active=q
    bpy.ops.wm.save_as_mainfile(filepath=cfg['output_dir']+'/Gridopoly_Full16_R1.blend',copy=True)
    print('FULL16_BUILD_RESULT '+json.dumps(reports))


build_full_ring(CONFIG)
