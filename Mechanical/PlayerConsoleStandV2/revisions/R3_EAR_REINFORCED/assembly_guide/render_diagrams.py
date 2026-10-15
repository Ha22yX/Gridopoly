"""Illustration-only rendering of the verified R3 assembly; never saves the blend."""
import bpy, json, math
from pathlib import Path
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

OUT=Path('Z:/Files/我的项目/Gridopoly/tmp/pdfs/player-stand-r3/images')
OUT.mkdir(parents=True,exist_ok=True)
s=bpy.context.scene
assert s.name.startswith('Player Stand V2 R3')
W,H=1800,1500
s.render.resolution_x=W;s.render.resolution_y=H;s.render.resolution_percentage=100
s.render.engine='BLENDER_EEVEE'
s.render.film_transparent=True
s.render.image_settings.file_format='PNG';s.render.image_settings.color_mode='RGBA'
s.view_settings.view_transform='AgX'
s.view_settings.look='AgX - Medium High Contrast'
s.view_settings.exposure=-0.35
s.world.use_nodes=True
s.world.node_tree.nodes['Background'].inputs['Color'].default_value=(1,1,1,1)
s.world.node_tree.nodes['Background'].inputs['Strength'].default_value=0.6
cam=s.camera
cam.data.type='ORTHO'
objects=list(s.objects)
base={o:(o.location.copy(),o.hide_render) for o in objects}

def material(name,col,metal=0):
    m=bpy.data.materials.new('GUIDE '+name);m.diffuse_color=(*col,1);m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*col,1)
    bs.inputs['Metallic'].default_value=metal;bs.inputs['Roughness'].default_value=.52
    return m
colors={
    'plate':material('connection plate',(.34,.58,.76)),
    'support':material('support',(.045,.48,.52)),
    'cradle':material('cradle',(.25,.58,.72)),
    'lid':material('lid',(.43,.69,.81)),
    'spacer':material('spacer',(.95,.62,.16)),
    'pcb':material('PCB',(.06,.30,.19)),
    'screw':material('fasteners',(.55,.60,.67),.45),
    'foot':material('nylon feet',(.75,.80,.85)),
    'battery':material('battery',(.93,.73,.22)),
    'fpc':material('FPC',(.93,.27,.035)),
}
for o in objects:
    key=None
    if o.name.startswith('R2_01_'):key='plate'
    elif o.name.startswith('R3_02_'):key='support'
    elif o.name.startswith('R2_03_'):key='cradle'
    elif o.name.startswith('R2_04_'):key='lid'
    elif o.name.startswith('R2_05_'):key='spacer'
    elif o.name.startswith('R2_PCB_'):key='pcb'
    elif o.name.startswith(('R2_WASHER_','R2_M3x18_','R2_EXTRA_')):key='screw'
    elif o.name.startswith('R2_FOOT_'):key='foot'
    elif o.name.startswith('R2_BATTERY_REFERENCE'):key='battery'
    elif o.name.startswith('R2_FPC_'):key='fpc'
    if key:
        o.data=o.data.copy();o.data.materials.clear();o.data.materials.append(colors[key])
    if o.type=='LIGHT':o.data.energy*=1.0

def reset():
    for o,(p,h) in base.items():o.location=p.copy();o.hide_render=h
    bpy.context.view_layer.update()

def choose(prefixes):
    for o in objects:
        if o.type in ('MESH','CURVE'):o.hide_render=not o.name.startswith(tuple(prefixes))
    for o in objects:
        if o.name.startswith('R2_FPC_') and 'minus_Z' not in o.name:o.hide_render=True

def move(prefix,dz):
    for o in objects:
        if o.name.startswith(prefix):o.location.z+=dz

def obj(prefix):return next(o for o in objects if o.name.startswith(prefix))

def center(prefix):
    o=obj(prefix);bb=[o.matrix_world@Vector(v) for v in o.bound_box]
    return sum(bb,Vector())/8

result={}
def shot(name,pos,target,scale,points):
    cam.location=pos;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=scale
    bpy.context.view_layer.update()
    s.render.filepath=str(OUT/name);bpy.ops.render.render(write_still=True)
    output=[]
    for key,p in points.items():
        q=world_to_camera_view(s,cam,Vector(p))
        output.append({'label':key,'pixel':[round(q.x*W,1),round((1-q.y)*H,1)],'xyz_mm':list(p)})
    result[name]={'width':W,'height':H,'points':output}
    (OUT/'diagram-points.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')

# Cover: physically assembled configuration; only colours and lighting changed.
reset()
shot('00_assembled.png',(160,-205,155),(-9,10,39),180,{
    '屏幕':(-25,-18,61),'R3屏幕支架':(-12,-18,34),'电池仓':(30,52,44),'连接板':(42,-30,18),'PCB':(37,-36,9),'尼龙底脚':(35,-35,4)})

# Upper mounting step: all hardware shown in its actual XY hole position.
reset();choose(['R2_01_','R3_02_','R2_03_','R2_EXTRA_'])
move('R3_02_',24);move('R2_03_',24)
move('R2_EXTRA_WASHER_',36);move('R2_EXTRA_SCREW_',51);move('R2_EXTRA_NUT_',-8)
shot('01_upper_mounts.png',(-170,-210,205),(-10,12,50),205,{
    '02 R3屏幕支架':(-24,-10,69),'03 空电池仓':(23,54,69),'01 连接板':(35,-29,17),
    'M3×12 螺丝（共4颗）':(-57,-18,76.4),'平垫片（共4片）':(-57,-18,59.9),
    'M3螺母（连接板下，共4个）':(-57,-18,6.4),'加固螺丝耳':(-57,-18,45.6),
    '支架螺丝孔':(7,15,17.6),'电池仓螺丝孔':(37,52.5,17.6)})

# Main stack: 4 repeated bolts preserve XY; layer spacing is schematic only.
reset();choose(['R2_01_','R2_05_','R2_PCB_','R2_COMPONENT_','R2_FOOT_','R2_WASHER_','R2_M3x18_'])
move('R2_PCB_',14);move('R2_COMPONENT_',14)
move('R2_05_',29);move('R2_01_',48);move('R2_WASHER_',55);move('R2_M3x18_',80)
shot('02_pcb_stack.png',(170,-240,165),(-9,8,47),230,{
    'M3×18 螺丝，4颗':(35,-35,101),'平垫片，4片':(35,-35,74.8),
    '01 连接板':(39,-30,65.6),'6 mm打印隔柱，4个':(35,-35,41.6),
    'PCB（元件朝上）':(25,-30,23.6),'8 mm内螺纹尼龙底脚，4个':(35,-35,4)})

# FPC remains at the verified route envelope; connectors are simplified references.
reset();choose(['R2_01_','R3_02_','R2_PCB_','R2_COMPONENT_','R2_05_','R2_FPC_','R2_FOOT_'])
shot('03_fpc_route.png',(-180,-160,150),(-31,-13,28),112,{
    '屏幕背面定位接口':(-25,-6,43.6),'排线槽口':(-43,-18,38),
    'FPC 路径包络':(-45,-18.6,22.5),'J2 排线插座':tuple(center('R2_COMPONENT_J2')),
    '01 连接板':(-57,-35,17.6),'R3加宽底脚':(-54,-5,21.6)})

# Battery presentation: pack/lid lifted only to explain fit; lead route remains installed reference.
reset();choose(['R2_01_','R2_03_','R2_04_','R2_BATTERY_REFERENCE','R2_BATTERY_WIRE_REFERENCE','R2_J3_PLUG','R2_COMPONENT_J3','R2_PCB_','R2_05_','R2_FOOT_'])
move('R2_BATTERY_REFERENCE',39);move('R2_04_',55)
shot('04_battery.png',(-170,-210,175),(-4,33,57),210,{
    '04 仓盖（内凹面朝下）':(28,51,114.6),'LP103454电池':(2,51,79),
    '03 电池仓':(27,50,42),'仓体侧面出线缺口':(-30,47,43),
    '导线走向参考':(-24,43,27),'J3 电池插头':(0,34.5,16),
    '绑带穿槽':(0,41.8,17.6),'电池仓底部薄垫位置':(0,51,22)})
print('GUIDE_RENDERS_DONE '+str(OUT))
