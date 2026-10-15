"""Background Blender: inspect only the saved R2 copy, not the live scene."""
import bpy,bmesh,json,math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
OUT=Path('Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R2_FPC60')
scene=bpy.context.scene
assert scene.name.startswith('Player Stand V2 R2')
keep=set(scene.objects)
for s in list(bpy.data.scenes):
    if s!=scene:bpy.data.scenes.remove(s)
for o in list(bpy.data.objects):
    if o not in keep:bpy.data.objects.remove(o,do_unlink=True)

def bounds(o):
    ps=[o.matrix_world@Vector(p) for p in o.bound_box]
    return [[min(p[i] for p in ps),max(p[i] for p in ps)] for i in range(3)]
def candidate(a,b):
    aa,bb=bounds(a),bounds(b)
    return all(min(aa[i][1],bb[i][1])-max(aa[i][0],bb[i][0])>0.0001 for i in range(3))
def vol(a,b):
    c=a.copy();c.data=a.data.copy();scene.collection.objects.link(c)
    c.hide_set(False);bpy.context.view_layer.objects.active=c
    m=c.modifiers.new('intersection','BOOLEAN');m.operation='INTERSECT';m.solver='MANIFOLD';m.object=b
    bpy.ops.object.modifier_apply(modifier=m.name)
    bm=bmesh.new();bm.from_mesh(c.data);value=abs(bm.calc_volume(signed=True));bm.free()
    bpy.data.objects.remove(c,do_unlink=True);return value
parts=[o for o in scene.objects if o.name.startswith(('R2_01_','R2_02_','R2_03_','R2_04_','R2_05_'))]
refs=[o for o in scene.objects if o.name.startswith(('R2_PCB_','R2_COMPONENT_','R2_BATTERY_REFERENCE','R2_J3_PLUG','R2_M3x18','R2_FOOT_','R2_SCREEN_BODY','R2_SCREEN_GLASS','R2_SOFT_STRAP_','R2_EXTRA_SCREW_','R2_EXTRA_NUT_'))]
pairs=[]
for i,a in enumerate(parts):
    for b in parts[i+1:]+refs:
        value=vol(a,b) if candidate(a,b) else 0
        pairs.append({'a':a.name,'b':b.name,'overlap_mm3':value})
fpc_checks=[]
for a in [o for o in refs if o.name.startswith(('R2_EXTRA_SCREW_','R2_EXTRA_NUT_'))]:
    for b in [o for o in refs if o.name.startswith(('R2_PCB_','R2_COMPONENT_'))]:
        value=vol(a,b) if candidate(a,b) else 0
        pairs.append({'a':a.name,'b':b.name,'overlap_mm3':value})
for a in [o for o in scene.objects if o.name.startswith('R2_FPC_')]:
    for b in parts+[o for o in refs if not o.name.startswith(('R2_COMPONENT_J2','R2_SCREEN_'))]:
        value=vol(a,b) if candidate(a,b) else 0
        fpc_checks.append({'a':a.name,'b':b.name,'overlap_mm3':value})

# Convert the illustrative wire in the copied file only, to check its finite diameter.
wire=next(o for o in scene.objects if o.name.startswith('R2_BATTERY_WIRE_'))
bpy.ops.object.select_all(action='DESELECT');wire.select_set(True);bpy.context.view_layer.objects.active=wire
bpy.ops.object.convert(target='MESH')
bm=bmesh.new();bm.from_mesh(wire.data)
bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00001)
bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
assert all(e.is_manifold for e in bm.edges),'Converted wire is not a closed volume'
bm.to_mesh(wire.data);bm.free();wire.data.update()
wire_checks=[]
for b in parts+[o for o in refs if not o.name.startswith(('R2_BATTERY_REFERENCE','R2_J3_PLUG','R2_COMPONENT_J3'))]:
    value=vol(wire,b) if candidate(wire,b) else 0
    wire_checks.append({'a':wire.name,'b':b.name,'overlap_mm3':value})

spacers=[o for o in parts if o.name.startswith('R2_05_')]
spacer_ok=len(spacers)==4 and all(abs(bounds(o)[2][0]-9.6)<.001 and abs(bounds(o)[2][1]-15.6)<.001 for o in spacers)
bad=[p for p in pairs+fpc_checks+wire_checks if p['overlap_mm3']>.001]
allbb=[bounds(o) for o in scene.objects if o.type=='MESH' and not o.hide_render]
bb=[[min(b[i][0] for b in allbb),max(b[i][1] for b in allbb)] for i in range(3)]
r={'revision':'V2 R2 FPC60 MODULAR','status':'MODEL_CHECKS_PASS' if not bad and spacer_ok else 'MODEL_CHECKS_FAIL',
   'spacers_count':len(spacers),'spacer_positions_pass':spacer_ok,'assembled_bounds_mm':bb,
   'assembled_dimensions_mm':[b-a for a,b in bb],'collision_tolerance_mm3':.001,'collisions':bad,
   'solid_pairs':pairs,'fpc_envelope_pairs':fpc_checks,'battery_wire_pairs':wire_checks,
   'limitations':['FPC outlet, insertion, contact side, strain and bend limits are unmeasured; both ribbon directions are conditional.',
   'FPC and wire envelopes exclude their intended connector insertion; screen boss holes omitted from simplified screen reference.',
   'Component heights are illustrative, not measured. Actual PCB clearance remains to be fitted.',
   'No physical print, slicing, stability or thermal testing has been completed.']}
(OUT/'assembly-verification.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in r.items() if k not in ('solid_pairs','fpc_envelope_pairs','battery_wire_pairs')},indent=2))
scene.render.resolution_percentage=65
scene.render.filepath=str(OUT/'preview-assembly.png');bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Gridopoly_Player_Stand_V2_R2_FPC60.blend'))
