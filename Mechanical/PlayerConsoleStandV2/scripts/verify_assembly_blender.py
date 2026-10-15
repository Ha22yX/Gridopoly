"""Run in background Blender against the exported copy, never the live source scene."""
import bpy
import bmesh
import json
from pathlib import Path
from mathutils import Vector

OUT=Path('Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R1')
scene=bpy.context.scene
assert scene.name.startswith('Player Stand V2 R1')
keep=set(scene.objects)
# This output copy should contain only this player module, not the unrelated
# chessboard scenes which remain intact in the live Blender session.
for s in list(bpy.data.scenes):
    if s!=scene: bpy.data.scenes.remove(s)
for o in list(bpy.data.objects):
    if o not in keep: bpy.data.objects.remove(o,do_unlink=True)

def bounds(o):
    pts=[o.matrix_world@Vector(p) for p in o.bound_box]
    return [[min(p[i] for p in pts),max(p[i] for p in pts)] for i in range(3)]

def candidate(a,b):
    aa,bb=bounds(a),bounds(b)
    return all(min(aa[i][1],bb[i][1])-max(aa[i][0],bb[i][0])>0.0001 for i in range(3))

def intersect_volume(a,b):
    c=a.copy(); c.data=a.data.copy(); scene.collection.objects.link(c)
    bpy.context.view_layer.objects.active=c
    mod=c.modifiers.new('check intersection','BOOLEAN'); mod.operation='INTERSECT'; mod.solver='MANIFOLD'; mod.object=b
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bm=bmesh.new(); bm.from_mesh(c.data)
    volume=abs(bm.calc_volume(signed=True)); bm.free()
    bpy.data.objects.remove(c,do_unlink=True)
    return volume

parts=[o for o in scene.objects if o.name.startswith(('01_UPPER','02_BATTERY','03_SPACER_8MM'))]
reference=[o for o in scene.objects if o.name.startswith(('PCB_','BATTERY_LP103454','J3_PLUG','M3x35','NYLON_FEMALE'))]
checks=[]
for i,a in enumerate(parts):
    for b in parts[i+1:]+reference:
        vol=intersect_volume(a,b) if candidate(a,b) else 0.0
        checks.append({'a':a.name,'b':b.name,'intersection_volume_mm3':vol})
fail=[c for c in checks if c['intersection_volume_mm3']>0.001]
assert not fail,fail

spacers=[o for o in parts if o.name.startswith('03_SPACER')]
assert len(spacers)==4
for o in spacers:
    bb=bounds(o)
    assert abs(bb[2][0]-9.6)<0.001 and abs(bb[2][1]-17.6)<0.001,bb

allbounds=[bounds(o) for o in scene.objects if o.type=='MESH' and not o.hide_render]
union=[[min(b[i][0] for b in allbounds),max(b[i][1] for b in allbounds)] for i in range(3)]
result={
 'status':'PASS for modelled envelopes; actual cable and component heights still require fitting',
 'scene_count':len(bpy.data.scenes), 'collision_pairs_checked':len(checks),
 'overlap_tolerance_mm3':0.001,'collisions':fail,
 'assembled_bounds_including_screen_reference_mm':union,
 'assembled_size_including_screen_reference_mm':[b-a for a,b in union],
 'pcb_to_tray_gap_mm':8.0, 'spacer_count':4,
 'battery_floor_padding_space_mm':0.5,'battery_lid_clearance_mm':2.9,
 'default_screw_engagement_mm':4.9,'optional_12mm_spacer_screw':'M3 x 40; 5.9 mm engagement',
 'limits':['Component reference heights are illustrative, not measured; XY source geometry verified.',
           'Screen cylinder is an envelope; original pins enter manufacturer holes omitted from this reference.',
           'Cable guide is not full-width FPC geometry; physical cable length and bend fit pending.',
           'No slicing, printing, thermal/load testing or physical assembly done in this verification.'],
 'pairs':checks}
(OUT/'assembly-verification.json').write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Gridopoly_Player_Stand_V2_R1.blend'))
print(json.dumps({k:v for k,v in result.items() if k!='pairs'},ensure_ascii=False))
