"""Independent FULL16_R1 STL QA and millimetre, single-object 3MF packaging.

Run only after Blender has exported both print parts. This script does not import
the model generator, connect to Blender, or change CAD/Git state. Expected sizes
and print orientation below are the separate review contract.
"""
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import math
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile


ROOT = Path(__file__).resolve().parent
OUT = Path('Z:/Files/我的项目/Gridopoly/Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Full16_R1')
REVISION = 'FULL16_R1'
NS = 'http://schemas.microsoft.com/3dmanufacturing/core/2015/02'
PARTS = (
    ('01_L_Quarter_PRINT_4', (171.35, 234.35, 16.8), 4),
    ('02_Splice_Bridge_PRINT_8', (20.0, 36.0, 3.0), 8),
)


def difference(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2],
            a[0]*b[1] - a[1]*b[0])


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def mesh_checks(vertices, faces, expected_size):
    edges, directed = Counter(), Counter()
    adjacency = defaultdict(set)
    duplicate_faces = Counter()
    volume = 0.0
    degenerate = 0
    down_faces = 0
    down_area = 0.0
    bed_area = 0.0
    for a, b, c in faces:
        pa, pb, pc = vertices[a], vertices[b], vertices[c]
        normal = cross(difference(pb, pa), difference(pc, pa))
        norm2 = dot(normal, normal)
        degenerate += norm2 < 1e-16
        length = math.sqrt(norm2)
        if length > 1e-8 and normal[2]/length < -math.sin(math.radians(0.01)):
            if max(pa[2], pb[2], pc[2]) > 1e-4:
                down_faces += 1
                down_area += length/2
            else:
                bed_area += length/2
        volume += dot(pa, cross(pb, pc))/6
        duplicate_faces[tuple(sorted((a, b, c)))] += 1
        for u, v in ((a, b), (b, c), (c, a)):
            edges[tuple(sorted((u, v)))] += 1
            directed[(u, v)] += 1
            adjacency[u].add(v)
            adjacency[v].add(u)
    unvisited = set(range(len(vertices)))
    components = 0
    while unvisited:
        components += 1
        stack = [unvisited.pop()]
        while stack:
            for neighbor in adjacency[stack.pop()]:
                if neighbor in unvisited:
                    unvisited.remove(neighbor)
                    stack.append(neighbor)
    boundary_edges = sum(count == 1 for count in edges.values())
    invalid_edges = sum(count != 2 for count in edges.values())
    winding_errors = sum(directed[(u, v)] != directed[(v, u)] for u, v in edges)
    duplicates = sum(count - 1 for count in duplicate_faces.values() if count > 1)
    bounds = [[min(p[i] for p in vertices), max(p[i] for p in vertices)] for i in range(3)]
    size = [hi - lo for lo, hi in bounds]
    checks = {
        'single_connected_component': components == 1,
        'every_edge_has_exactly_two_faces': invalid_edges == 0,
        'consistent_edge_winding': winding_errors == 0,
        'no_degenerate_triangles': degenerate == 0,
        'no_duplicate_triangles': duplicates == 0,
        'positive_outward_volume': volume > 0,
        'dimensions_match_within_0_001_mm': all(abs(a-b) < 0.001 for a, b in zip(size, expected_size)),
        'bottom_at_z_zero': abs(bounds[2][0]) < 1e-6,
        'nonzero_flat_bed_contact': bed_area > 0,
        'no_downward_surface_above_bed': down_faces == 0,
    }
    return {
        'revision': REVISION,
        'status': 'PASS' if all(checks.values()) else 'FAIL',
        'checks': checks,
        'vertices': len(vertices), 'triangles': len(faces), 'edges': len(edges),
        'connected_components': components,
        'boundary_edges': boundary_edges,
        'edges_not_shared_by_two_faces': invalid_edges,
        'inconsistent_edge_winding': winding_errors,
        'degenerate_triangles': degenerate,
        'duplicate_triangles': duplicates,
        'euler_characteristic': len(vertices) - len(edges) + len(faces),
        'signed_volume_mm3': volume,
        'bounds_mm': bounds,
        'dimensions_mm': size,
        'expected_dimensions_mm': list(expected_size),
        'flat_bed_contact_surface_area_mm2': bed_area,
        'downward_surface_triangles_above_bed': down_faces,
        'downward_surface_area_above_bed_mm2': down_area,
        'vertical_wall_angular_noise_tolerance_deg': 0.01,
    }


def read_stl(path, expected_size):
    raw = path.read_bytes()
    if len(raw) < 84:
        raise ValueError('Binary STL is shorter than its header')
    triangles, = struct.unpack_from('<I', raw, 80)
    if triangles == 0 or len(raw) != 84 + triangles*50:
        raise ValueError('Binary STL has invalid triangle count or byte length')
    vertices, faces, ids = [], [], {}
    for index in range(triangles):
        record = struct.unpack_from('<12fH', raw, 84 + index*50)
        face = []
        for vertex in range(3):
            point = tuple(record[3 + 3*vertex:6 + 3*vertex])
            if not all(math.isfinite(value) for value in point):
                raise ValueError('Nonfinite STL vertex')
            if point not in ids:
                ids[point] = len(vertices)
                vertices.append(point)
            face.append(ids[point])
        faces.append(tuple(face))
    report = mesh_checks(vertices, faces, expected_size)
    report['method'] = 'Independent binary STL parsing; exact-coordinate vertex welding; calculated face normals'
    report['stl_sha256'] = hashlib.sha256(raw).hexdigest()
    return vertices, faces, report


def make_and_verify_3mf(vertices, faces, path, expected_size, copies):
    ET.register_namespace('', NS)

    def child(parent, name, attributes=None):
        return ET.SubElement(parent, '{' + NS + '}' + name, attributes or {})

    model = ET.Element('{' + NS + '}model', {
        'unit': 'millimeter', '{http://www.w3.org/XML/1998/namespace}lang': 'en-US'})
    child(model, 'metadata', {'name': 'Title'}).text = path.stem
    child(model, 'metadata', {'name': 'Description'}).text = (
        f'{REVISION}. One printable object; print {copies} copies. '
        'No printer, material or slicing settings. Full-ring physical first fit pending.')
    resources = child(model, 'resources')
    obj = child(resources, 'object', {'id': '1', 'type': 'model', 'name': path.stem})
    mesh = child(obj, 'mesh')
    vv, tt = child(mesh, 'vertices'), child(mesh, 'triangles')
    origin = [min(p[i] for p in vertices) for i in range(3)]
    for point in vertices:
        child(vv, 'vertex', {axis: format(point[i] - origin[i], '.12g') for i, axis in enumerate('xyz')})
    for face in faces:
        child(tt, 'triangle', {f'v{i+1}': str(vertex) for i, vertex in enumerate(face)})
    child(child(model, 'build'), 'item', {'objectid': '1'})
    types = ('<?xml version="1.0" encoding="UTF-8"?>'
        '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
        '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
        '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>'
        '</Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel" '
        'Target="/3D/3dmodel.model"/></Relationships>')
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as package:
        package.writestr('[Content_Types].xml', types)
        package.writestr('_rels/.rels', rels)
        package.writestr('3D/3dmodel.model', ET.tostring(model, encoding='utf-8', xml_declaration=True))
    with zipfile.ZipFile(path) as package:
        assert package.testzip() is None, '3MF failed ZIP CRC check'
        parsed = ET.fromstring(package.read('3D/3dmodel.model'))
        assert parsed.attrib['unit'] == 'millimeter'
        assert len(parsed.findall('.//{' + NS + '}object')) == 1
        assert len(parsed.findall('.//{' + NS + '}item')) == 1
        assert parsed.find('{' + NS + '}build/{' + NS + '}item').attrib == {'objectid': '1'}
        vrows = parsed.findall('.//{' + NS + '}vertex')
        frows = parsed.findall('.//{' + NS + '}triangle')
        decoded_vertices = [tuple(float(row.attrib[axis]) for axis in 'xyz') for row in vrows]
        decoded_faces = [tuple(int(row.attrib[f'v{i+1}']) for i in range(3)) for row in frows]
        assert len(decoded_vertices) == len(vertices) and decoded_faces == faces
        for original, decoded in zip(vertices, decoded_vertices):
            assert all(abs(original[i] - origin[i] - decoded[i]) < 1e-8 for i in range(3))
        decoded_report = mesh_checks(decoded_vertices, decoded_faces, expected_size)
        assert decoded_report['status'] == 'PASS', decoded_report['checks']
        assert REVISION in package.read('3D/3dmodel.model').decode('utf-8')
    return {
        'revision': REVISION, 'status': 'PASS', 'object_count': 1, 'build_item_count': 1,
        'units': 'millimeter', 'zip_crc_ok': True,
        'triangles_equal_to_stl': True,
        'vertices_match_stl_after_translation_within_1e_8_mm': True,
        'vertex_translation_from_stl_mm': [-value for value in origin],
        'independent_readback_mesh_checks': decoded_report,
        'recommended_print_copies': copies,
        'printer_settings_included': False, 'opened_in_slicer': False,
        'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def main():
    report = {
        'revision': REVISION,
        'checked_at_utc': datetime.now(timezone.utc).isoformat(),
        'status': 'PASS', 'print_direction': '+Z; flat base on print bed',
        'parts': {},
        'scope_limit': (
            'Independent exported mesh topology, dimensions and downward-face checks, '
            'plus 3MF container/geometry readback. Does not prove absence of self-intersection, '
            'assembly collision, real slicing/toolpaths, physical fit, PLA screw strength or structural loads. '
            'The prior three-module physical fit does not validate the full 16-module assembly.'),
        'expected_assembly_parameters_mm': {
            'base_plate_thickness': 3.5, 'ear_top_z': 10, 'assembled_bridge_z_range': [10, 13],
            'pcb_bottom_z': 16, 'pcb_thickness': 1.6, 'max_underside_projection': 8.1,
            'nominal_underside_clearance_to_base': 4.4,
            'ear_pilot_diameter': 2.5, 'ear_pilot_depth': 8, 'ear_pilot_bottom_z': 2,
            'ear_guide_diameter': 3.6, 'ear_guide_depth': 1.2,
            'bridge_screw_clearance_diameter': 3.6, 'bridge_screw_y_centers': [-10, 10],
            'locator_pin_diameter': 2.8, 'locator_y_centers': [-4, 4],
            'bridge_locator_clearance_diameter': 3.2,
            'plastic_join_screw_nominal_diameter': 3, 'plastic_join_screw_length': 10,
            'join_washer_thickness': 1, 'nominal_join_screw_insertion': 6,
            'nominal_join_screw_tip_to_pilot_floor': 2,
        },
        'assembly_parameters_are_design_contract_not_stl_feature_measurement': True,
        'assembly_quantities': {'l_quarters': 4, 'bridge_plates': 8, 'join_screws': 16},
    }
    for stem, expected_size, copies in PARTS:
        try:
            vertices, faces, part = read_stl(OUT / (stem + '.stl'), expected_size)
            part['recommended_print_copies'] = copies
            report['parts'][stem] = part
            if part['status'] == 'PASS':
                part['3mf'] = make_and_verify_3mf(vertices, faces, OUT / (stem + '.3mf'), expected_size, copies)
            else:
                report['status'] = 'FAIL'
        except Exception as error:
            report['status'] = 'FAIL'
            report['parts'].setdefault(stem, {})['error'] = f'{type(error).__name__}: {error}'
    ROOT.joinpath('parts-verification.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    if OUT.is_dir():
        OUT.joinpath('parts-verification.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
