"""Independently verify the exported STL and package a single-object mm 3MF."""
from pathlib import Path
from collections import defaultdict, Counter
import hashlib
import json
import math
import struct
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = Path('Z:/Files/我的项目/Gridopoly/Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY')
NS = 'http://schemas.microsoft.com/3dmanufacturing/core/2015/02'


def sub(a, b):
    return tuple(x-y for x,y in zip(a,b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def dot(a, b):
    return sum(x*y for x,y in zip(a,b))


def parse_and_verify_stl(path, expected_size=(151,151,16.8)):
    raw = path.read_bytes()
    n, = struct.unpack_from('<I', raw, 80)
    assert len(raw) == 84 + n*50, 'Invalid binary STL length'
    vertices, faces, ids = [], [], {}
    for i in range(n):
        values = struct.unpack_from('<12fH', raw, 84+i*50)
        face = []
        for j in range(3):
            p = tuple(values[3+3*j:6+3*j])
            assert all(math.isfinite(v) for v in p), 'Non-finite coordinate'
            if p not in ids:
                ids[p] = len(vertices)
                vertices.append(p)
            face.append(ids[p])
        faces.append(tuple(face))
    edges, directed = Counter(), Counter()
    adjacency = defaultdict(set)
    degenerate, volume = 0, 0.0
    unsupported_downward_faces,unsupported_downward_area=0,0.0
    for a,b,c in faces:
        pa,pb,pc = vertices[a],vertices[b],vertices[c]
        normal = cross(sub(pb,pa),sub(pc,pa))
        degenerate += dot(normal,normal) < 1e-16
        length=math.sqrt(dot(normal,normal))
        # Float32 STL and Boolean splits produce minute tilts on vertical walls.
        # Ignore only 0.01 degrees from vertical, not actual overhang slopes.
        if length>1e-8 and normal[2]/length < -math.sin(math.radians(0.01)) and max(pa[2],pb[2],pc[2])>1e-4:
            unsupported_downward_faces+=1
            unsupported_downward_area+=length/2
        volume += dot(pa,cross(pb,pc))/6
        for u,v in ((a,b),(b,c),(c,a)):
            edges[tuple(sorted((u,v)))] += 1
            directed[(u,v)] += 1
            adjacency[u].add(v)
            adjacency[v].add(u)
    unvisited = set(range(len(vertices)))
    components = 0
    while unvisited:
        components += 1
        stack = [unvisited.pop()]
        while stack:
            for i in adjacency[stack.pop()]:
                if i in unvisited:
                    unvisited.remove(i)
                    stack.append(i)
    bad_edges = sum(n != 2 for n in edges.values())
    winding_errors = sum(directed[(u,v)] != directed[(v,u)] for u,v in edges)
    bounds = [[min(p[i] for p in vertices),max(p[i] for p in vertices)] for i in range(3)]
    size = [b-a for a,b in bounds]
    assert components == 1 and bad_edges == 0 and winding_errors == 0 and degenerate == 0
    assert volume > 0
    assert all(abs(a-b)<0.001 for a,b in zip(size,expected_size)), size
    assert abs(bounds[2][0]) < 1e-6
    assert unsupported_downward_faces==0, (unsupported_downward_faces,unsupported_downward_area)
    report = {
        'revision':'R2_PRINT_FRIENDLY','method':'Independent binary STL parsing; exact-coordinate vertex welding',
        'vertices':len(vertices),'triangles':len(faces),'edges':len(edges),
        'connected_components':components,'edges_not_shared_by_two_faces':bad_edges,
        'inconsistent_edge_winding':winding_errors,'degenerate_triangles':degenerate,
        'signed_volume_mm3':volume,'bounds_mm':bounds,'dimensions_mm':size,
        'print_direction':'+Z, flat bottom on bed',
        'downward_surface_triangles_above_bed':unsupported_downward_faces,
        'downward_surface_area_above_bed_mm2':unsupported_downward_area,
        'vertical_wall_angular_noise_tolerance_deg':0.01,
        'stl_sha256':hashlib.sha256(raw).hexdigest(),
        'scope_limit':'Topology, size and geometric overhang verification only; not physical fitting, structural load, full collision, actual layer toolpaths or slicer validation.'
    }
    return vertices,faces,report


def make_3mf(vertices, faces, path):
    ET.register_namespace('',NS)
    def elem(parent, tag, attributes=None):
        return ET.SubElement(parent,'{'+NS+'}'+tag,attributes or {})
    model = ET.Element('{'+NS+'}model', {'unit':'millimeter','{http://www.w3.org/XML/1998/namespace}lang':'en-US'})
    elem(model,'metadata',{'name':'Title'}).text = path.stem
    elem(model,'metadata',{'name':'Description'}).text = 'R2 printable geometry only. Physical first fit pending.'
    res = elem(model,'resources')
    obj = elem(res,'object',{'id':'1','type':'model','name':path.stem})
    mesh = elem(obj,'mesh')
    vv = elem(mesh,'vertices')
    origin = [min(p[i] for p in vertices) for i in range(3)]
    for p in vertices:
        elem(vv,'vertex',{axis:format(p[i]-origin[i],'.12g') for i,axis in enumerate('xyz')})
    tt = elem(mesh,'triangles')
    for face in faces:
        elem(tt,'triangle',{f'v{i+1}':str(v) for i,v in enumerate(face)})
    build = elem(model,'build')
    elem(build,'item',{'objectid':'1'})
    types = '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>'
    rels = '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel" Target="/3D/3dmodel.model"/></Relationships>'
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml',types)
        z.writestr('_rels/.rels',rels)
        z.writestr('3D/3dmodel.model',ET.tostring(model,encoding='utf-8',xml_declaration=True))
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        parsed = ET.fromstring(z.read('3D/3dmodel.model'))
        assert parsed.attrib['unit'] == 'millimeter'
        objs = parsed.findall('.//{'+NS+'}object')
        assert len(objs) == 1
        vrows = parsed.findall('.//{'+NS+'}vertex')
        frows = parsed.findall('.//{'+NS+'}triangle')
        assert len(vrows)==len(vertices) and len(frows)==len(faces)
        for expected,row in zip(vertices,vrows):
            assert all(abs(expected[i]-origin[i]-float(row.attrib[axis]))<1e-8 for i,axis in enumerate('xyz'))
        for expected,row in zip(faces,frows):
            assert expected == tuple(int(row.attrib[f'v{i+1}']) for i in range(3))
    return {'object_count':1,'units':'millimeter','zip_crc_ok':True,
            'triangles_equal_to_stl':True,'vertex_translation_from_stl_mm':[-v for v in origin],
            'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'printer_settings_included':False,'opened_in_slicer':False}


def main():
    config=json.loads((OUT/'fixture-config.json').read_text(encoding='utf-8'))
    vertices,faces,report=parse_and_verify_stl(OUT/'ONE_PIECE_Corner_Test_R2.stl')
    report['3mf']=make_3mf(vertices,faces,OUT/'ONE_PIECE_Corner_Test_R2.3mf')
    cv,cf,coupon_report=parse_and_verify_stl(OUT/'Screw_Fit_Coupon_R2.stl',(34,14,16))
    coupon_report['3mf']=make_3mf(cv,cf,OUT/'Screw_Fit_Coupon_R2.3mf')
    report['optional_screw_fit_coupon']=coupon_report
    report['geometry_parameters']={
        'plate_thickness_mm':config['bottom_rib_mm'],
        'pcb_bottom_above_table_mm':config['pcb_bottom_mm'],
        'nominal_component_clearance_over_plate_mm':config['pcb_bottom_mm']-8.1-config['bottom_rib_mm'],
        'mounting_screws':6,'integral_posts':12,'fixed_gap_g_mm':9,
        'pilot_diameter_candidate_mm':config['pilot_hole_mm'],
        'pilot_bottom_z_mm':config['pcb_bottom_mm']-config['pilot_depth_mm'],
        'top_guide_diameter_mm':config['support_hole_mm'],
        'top_guide_depth_mm':config['guide_depth_mm'],
        'screw_insertion_into_post_mm':config['pcb_screws']['under_head_length_mm']-config['pcb_thickness_mm']-config['pcb_screws']['washer_thickness_mm'],
        'screw_tip_to_blind_floor_clearance_mm':config['pilot_depth_mm']-(config['pcb_screws']['under_head_length_mm']-config['pcb_thickness_mm']-config['pcb_screws']['washer_thickness_mm']),
        'hole_positions_source_sha256':config['interface_sha256'],
        'physical_print_and_fit_verified':False}
    for target in (ROOT,OUT):
        (target/'verification-report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False))


if __name__=='__main__':
    main()
