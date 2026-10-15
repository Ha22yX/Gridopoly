"""R2 one-piece base versus PCB references. Run only in the R2 scene."""
import bpy
import bmesh
import json


def overlap(a,b):
    obj=a.copy()
    obj.data=a.data.copy()
    bpy.context.collection.objects.link(obj)
    bpy.context.view_layer.objects.active=obj
    mod=obj.modifiers.new('independent_overlap','BOOLEAN')
    mod.operation='INTERSECT'
    mod.solver='MANIFOLD'
    mod.object=b
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bm=bmesh.new()
    bm.from_mesh(obj.data)
    v=abs(bm.calc_volume(signed=True))
    bm.free()
    bpy.data.objects.remove(obj,do_unlink=True)
    return v


base=next(o for o in bpy.context.scene.objects if o.name.startswith('ONE_PIECE_BASE_R2_PRINT'))
pcbs=[o for o in bpy.context.scene.objects if o.name.endswith('_PCB_REFERENCE')]
assert len(pcbs)==3
checks=[{'pcb':pcb.name,'intersection_volume_mm3':overlap(base,pcb)} for pcb in pcbs]
assert max(c['intersection_volume_mm3'] for c in checks)<0.001, checks
bpy.ops.object.select_all(action='DESELECT')
base.select_set(True)
bpy.context.view_layer.objects.active=base
result={'revision':'R2_PRINT_FRIENDLY','checks':checks,
    'scope':'Fixed base versus three PCB laminate references including holes. Not a cable insertion, electronics bodies, screw tolerance or physical fit test.'}
print('ASSEMBLY_VERIFIED '+json.dumps(result))
