"""Run in background Blender against the generated copy, never the live scene."""
import bpy
import bmesh
import json
import math
from pathlib import Path
from mathutils import Vector

OUT=Path('Z:/Files/我的项目/Gridopoly/Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Full16_R1')
ROOT=Path(__file__).resolve().parent
scene=bpy.context.scene
assert scene.name.startswith('Gridopoly Full16 R1')
bpy.context.window.scene=scene
for old in list(bpy.data.scenes):
    if old!=scene: bpy.data.scenes.remove(old)
cfg=json.loads(scene['config_json'])
# Booleans can leave an empty material slot; normalize presentation only.
for o in scene.objects:
    if o.type=='MESH':
        valid=[m for m in o.data.materials if m]
        if valid:
            chosen=valid[-1]
            o.data.materials.clear(); o.data.materials.append(chosen)
            for p in o.data.polygons: p.material_index=0
quarters=[bpy.data.objects[n] for n in json.loads(scene['quarter_objects_json'])]
bridges=[bpy.data.objects[n] for n in json.loads(scene['bridge_objects_json'])]
pcbs=[bpy.data.objects[n] for n in json.loads(scene['reference_pcb_objects_json'])]
hardware=[bpy.data.objects[n] for n in json.loads(scene['reference_fasteners_json'])]
master=next(o for o in scene.objects if o.get('kind')=='PRINT_PART_BRIDGE' and o.hide_render)


def bounds(o):
    vv=[o.matrix_world @ Vector(v) for v in o.bound_box]
    return [[min(v[i] for v in vv),max(v[i] for v in vv)] for i in range(3)]


def bbox_touch(a,b):
    aa,bb=bounds(a),bounds(b)
    return all(min(aa[i][1],bb[i][1])>=max(aa[i][0],bb[i][0])-1e-5 for i in range(3))


def intersection_volume(a,b):
    if not bbox_touch(a,b): return 0.0,False
    o=a.copy(); o.data=a.data.copy(); scene.collection.objects.link(o)
    o.hide_viewport=False; o.hide_set(False)
    bpy.context.view_layer.objects.active=o
    mod=o.modifiers.new('Independent_assembly_intersection','BOOLEAN')
    mod.operation='INTERSECT'; mod.solver='MANIFOLD'; mod.object=b
    # Hidden render flags do not change Boolean input geometry.
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bm=bmesh.new(); bm.from_mesh(o.data)
    vol=abs(bm.calc_volume(signed=True))*abs(o.matrix_world.determinant())
    bm.free(); bpy.data.objects.remove(o,do_unlink=True)
    return vol,True


bpy.context.view_layer.update()
checks=[]
for category,pairs in [
    ('quarter_to_quarter',[(a,b) for i,a in enumerate(quarters) for b in quarters[i+1:]]),
    ('bridge_to_quarter',[(a,b) for a in bridges for b in quarters]),
    ('pcb_to_plastic',[(a,b) for a in pcbs for b in quarters+bridges]),
    ('bridge_to_bridge',[(a,b) for i,a in enumerate(bridges) for b in bridges[i+1:]])]:
    maxvol=0; tested=0; overlaps=[]
    for a,b in pairs:
        vol,performed=intersection_volume(a,b)
        tested+=int(performed); maxvol=max(maxvol,vol)
        if vol>1e-4: overlaps.append({'a':a.name,'b':b.name,'volume_mm3':vol})
    checks.append({'category':category,'pair_count':len(pairs),'boolean_pairs':tested,
        'maximum_intersection_mm3':maxvol,'overlaps':overlaps})
assert not any(x['overlaps'] for x in checks),checks
report={
    'revision':'FULL16_R1','checks':checks,
    'method':'Separate background Blender reopens saved assembly; world-space bounds prune, MANIFOLD solid Boolean intersection for touching boxes.',
    'quarter_count':len(quarters),'bridge_count':len(bridges),'reference_pcb_count':len(pcbs),
    'quarter_world_bounds_mm':[bounds(o) for o in quarters],
    'nominal_underside_component_to_plate_mm':4.4,
    'rib_top_mm':6.5,'underside_component_lowest_z_mm':7.9,
    'actual_component_3d_geometry_included':False,
    'limits':'Checks PCB substrate with mounting holes and plastic parts. Exact connector bodies, cable plugs, real print shrink, screw thread forces and structural loads are not simulated.',
    'physical_full16_fit_verified':False,'actually_sliced':False,
}
for p in (ROOT,OUT): (p/'assembly-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('ASSEMBLY_QA '+json.dumps(checks))


def visible(o,value):
    o.hide_viewport=False
    o.hide_set(not value); o.hide_render=not value


def camera(loc,target,scale):
    c=scene.camera
    c.location=loc; c.rotation_euler=(Vector(target)-c.location).to_track_quat('-Z','Y').to_euler()
    c.data.ortho_scale=scale; c.data.clip_end=10000


def render(name):
    scene.render.filepath=(OUT/name).as_posix()
    bpy.ops.render.render(write_still=True)


def text_label(name,body,xy,z,size,mat):
    curve=bpy.data.curves.new(name,'FONT'); curve.body=body; curve.size=size
    curve.align_x='CENTER'; curve.align_y='CENTER'
    ob=bpy.data.objects.new(name,curve); scene.collection.objects.link(ob)
    ob.location=(*xy,z); ob.data.materials.append(mat)
    ob['kind']='ANNOTATION_DO_NOT_PRINT'
    return ob


floor=bpy.data.objects[scene['render_floor_object']]; visible(floor,True)
visible(master,False)
for o in quarters+bridges: visible(o,True)
for o in pcbs+hardware: visible(o,False)
camera((360,-400,690),(-99,179,3),610)
render('01_full_frame.png')

labels=[]
mat=bpy.data.materials.new('Reference label white'); mat.diffuse_color=(0.9,0.95,1,1)
for qi,angle in enumerate(cfg['quarter_angles_deg']):
    for bi,b in enumerate(cfg['boards']):
        bnd=bounds(pcbs[qi*4+bi])
        xy=[sum(bnd[j])/2 for j in (0,1)]
        labels.append(text_label('Reference_board_label',('CORNER' if bi==0 else 'LONG')+'\nQ'+str(qi+1),xy,17.65,5,mat))
for o in pcbs+hardware: visible(o,True)
render('02_with_16_pcbs_reference.png')
camera((-99,179,800),(-99,179,0),490)
render('03_top_layout.png')

for o in labels+pcbs+hardware+bridges+quarters[1:]: visible(o,False)
camera((240,-240,330),(15,96,4),315)
render('04_single_quarter_print_4.png')

# Joint close-up: hide boards, show joining hardware only.
for o in quarters+bridges: visible(o,True)
for o in hardware:
    visible(o,o.name.startswith('Joint_'))
camera((165,135,105),(92,210.5,7),76)
render('05_joint_detail.png')

# Save a clean single-scene deliverable with plastic parts as the default view.
for o in hardware+pcbs+labels: visible(o,False)
for o in quarters+bridges: visible(o,True)
visible(floor,False)
camera((360,-400,690),(-99,179,3),610)
bpy.ops.object.select_all(action='DESELECT')
for o in quarters+bridges: o.select_set(True)
bpy.context.view_layer.objects.active=quarters[0]
for o in list(bpy.data.objects):
    if not o.users_scene: bpy.data.objects.remove(o,do_unlink=True)
bpy.ops.wm.save_as_mainfile(filepath=(OUT/'Gridopoly_Full16_R1.blend').as_posix())
print('FULL16_REVIEW_AND_RENDERS_COMPLETE')
