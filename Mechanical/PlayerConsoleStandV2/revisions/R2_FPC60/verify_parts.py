"""Independent R2 binary-STL QA and individual geometry-only 3MF packaging.

Does not invoke Blender, modify STL, alter geometry.py or edit legacy sources.
Run only after the main modelling task confirms the final STL exports are ready.
"""
from __future__ import annotations

from pathlib import Path
import hashlib
import importlib.util
import json
import math
import xml.etree.ElementTree as ET
import zipfile

import numpy as np


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
OUT = Path("C:/Users/kicof/Downloads/Gridopoly_Player_Stand_V2_R2_FPC60")
LEGACY = ROOT / "Mechanical/PlayerConsoleStand/outputs/models/Gridopoly_Player_Console_Stand_CW47_5.stl"
EXPECTED = {
    "01_Connection_Plate_R2.stl": (116., 111., 4.),
    "02_Screen_Support_CW47_5_R2.stl": (74., 50., 41.864471435546875),
    "03_Battery_Cradle_R2.stl": (84., 17., 39.),
    "04_Battery_Lid_R2.stl": (69., 22., 4.),
    "05_Spacer_6mm_R2.stl": (6., 6., 6.),
}
INTERFACE_PART = "02_Screen_Support_CW47_5_R2.stl"
DX, DY, DZ = -25., -9., -4.
REGION_RADIUS = 20.
REGION_SIDES = 1024
NS = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"

spec = importlib.util.spec_from_file_location(
    "player_stand_independent_stl_qa", HERE.parents[1] / "scripts/verify_and_package.py")
qa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qa)
qa.EXPECTED = EXPECTED


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def legacy_coordinates(vertices):
    # New export is in print coordinates, with translated mating features.
    result = np.asarray(vertices, dtype=float).copy()
    result[:, 0] -= DX
    result[:, 1] -= DY
    result[:, 2] -= DZ
    return result


def interface_basis(vertices):
    angle = math.radians(35.)
    points = np.asarray(vertices, dtype=float) - np.array([0., 3., 28.])
    # x/v are the untwisted mating-face coordinates. A circle is invariant
    # to the additional CW47.5 in-plane rotation, so no rotation is needed.
    return np.column_stack((points[:, 0],
                            points[:, 1]*math.cos(angle)+points[:, 2]*math.sin(angle),
                            -points[:, 1]*math.sin(angle)+points[:, 2]*math.cos(angle)))


def clip_halfspace(poly, coefficients, boundary):
    """Keep dot(point, coefficients) <= boundary; no artificial cap faces."""
    if not poly:
        return []
    output = []
    for a, b in zip(poly, poly[1:]+poly[:1]):
        da, db = float(np.dot(a, coefficients)-boundary), float(np.dot(b, coefficients)-boundary)
        ina, inb = da <= 1e-10, db <= 1e-10
        if ina:
            output.append(a)
        if ina != inb:
            output.append(a+(b-a)*(da/(da-db)))
    return output


def protected_surface(vertices, faces):
    """Clip to n>=0 and an inscribed 1024-gon of radius20 in mating plane.

    Strictly remains within the requested circular region. Also sample exact
    original vertices at the radius20 boundary, to cover the narrow polygon
    approximation band omitted from clipped-area calculations.
    """
    original = np.asarray(vertices, dtype=float)
    uvn = interface_basis(original)
    # Float32 STL export gives the nominal n=0 mating face micrometre-scale
    # signed noise. Snap only for clipping to avoid arbitrary half-face loss.
    uvn[np.abs(uvn[:, 2]) < .00002, 2] = 0.
    cos_a, sin_a = math.cos(math.radians(35.)), math.sin(math.radians(35.))
    inradius = REGION_RADIUS*math.cos(math.pi/REGION_SIDES)
    normals = [(math.cos((2*i+1)*math.pi/REGION_SIDES), math.sin((2*i+1)*math.pi/REGION_SIDES), 0.)
               for i in range(REGION_SIDES)]
    protected = []
    for face in faces:
        tri = uvn[list(face)]
        if np.max(tri[:, 2]) < -1e-8:
            continue
        # Axis-aligned rectangle's minimum radial distance safely rejects
        # remote cradle/deck triangles without discarding crossing triangles.
        lo, hi = tri[:, :2].min(axis=0), tri[:, :2].max(axis=0)
        nearest = np.maximum(lo, np.minimum(hi, np.zeros(2)))
        if np.linalg.norm(nearest) > REGION_RADIUS + 1e-8:
            continue
        poly = clip_halfspace([p for p in tri], np.array([0., 0., -1.]), 0.)
        if not poly:
            continue
        if max(np.linalg.norm(p[:2]) for p in poly) > inradius:
            for normal in normals:
                poly = clip_halfspace(poly, np.array(normal), inradius)
                if not poly:
                    break
        for i in range(1, len(poly)-1):
            triangle = np.array([poly[0], poly[i], poly[i+1]])
            if np.linalg.norm(np.cross(triangle[1]-triangle[0], triangle[2]-triangle[0])) <= 1e-10:
                continue
            # Convert back to the common legacy XYZ coordinate system.
            xyz = np.column_stack((triangle[:, 0],
                                   3+triangle[:, 1]*cos_a-triangle[:, 2]*sin_a,
                                   28+triangle[:, 1]*sin_a+triangle[:, 2]*cos_a))
            protected.append(xyz)
    on_region = ((uvn[:, 0]**2+uvn[:, 1]**2 <= (REGION_RADIUS+1e-6)**2)
                 & (uvn[:, 2] >= -1e-6))
    return np.asarray(protected), original[on_region], uvn


def surface_samples(tris, extra_vertices):
    sets = [tris.reshape(-1, 3), tris.mean(axis=1), extra_vertices]
    for a, b in ((0, 1), (1, 2), (2, 0)):
        for t in (.25, .5, .75):
            sets.append((1-t)*tris[:, a]+t*tris[:, b])
    return np.unique(np.round(np.concatenate(sets), 8), axis=0)


def area(tris):
    return float(np.linalg.norm(np.cross(tris[:, 1]-tris[:, 0], tris[:, 2]-tris[:, 0]), axis=1).sum()/2)


def bounded_surface_distance(samples, triangles, tolerance=.0002):
    """Exact nearest distances below tolerance using expanded triangle AABBs.

    Any triangle capable of producing a distance <tolerance must have its
    axis-aligned box within tolerance of the point. Only if no candidate
    proves that bound do we fall back to the full independent evaluator.
    """
    low = triangles.min(axis=1)-tolerance
    high = triangles.max(axis=1)+tolerance
    output = []
    for start in range(0, len(samples), 128):
        points = samples[start:start+128]
        mask = np.all(points[:, None, :] >= low[None, :, :], axis=2)
        mask &= np.all(points[:, None, :] <= high[None, :, :], axis=2)
        point_ids, triangle_ids = np.nonzero(mask)
        best = np.full(len(points), np.inf)
        if len(point_ids):
            p = points[point_ids]
            a, b, c = (triangles[triangle_ids, i] for i in range(3))
            ab, ac, pa = b-a, c-a, p-a
            n = np.cross(ab, ac)
            nn = np.einsum('ij,ij->i', n, n)
            pn = np.einsum('ij,ij->i', pa, n)
            projected = pa-pn[:, None]/nn[:, None]*n
            d00 = np.einsum('ij,ij->i', ab, ab)
            d01 = np.einsum('ij,ij->i', ab, ac)
            d11 = np.einsum('ij,ij->i', ac, ac)
            d20 = np.einsum('ij,ij->i', projected, ab)
            d21 = np.einsum('ij,ij->i', projected, ac)
            denominator = d00*d11-d01*d01
            v = (d11*d20-d01*d21)/denominator
            w = (d00*d21-d01*d20)/denominator
            inside = (v>=-1e-9)&(w>=-1e-9)&(v+w<=1+1e-9)
            distance2 = np.where(inside, pn*pn/nn, np.inf)
            for x, y in ((a, b), (b, c), (c, a)):
                d = y-x
                dd = np.einsum('ij,ij->i', d, d)
                t = np.clip(np.einsum('ij,ij->i', p-x, d)/dd, 0, 1)
                delta = p-x-t[:, None]*d
                distance2 = np.minimum(distance2, np.einsum('ij,ij->i', delta, delta))
            np.minimum.at(best, point_ids, np.sqrt(np.maximum(0, distance2)))
        unresolved = best >= tolerance
        if np.any(unresolved):
            best[unresolved] = qa.surface_distance(points[unresolved], triangles)
        output.extend(best)
    return np.asarray(output)


def interface_comparison(new_vertices, new_faces):
    ov, of, _ = qa.parse(LEGACY)
    nv = legacy_coordinates(new_vertices)
    old_clipped, old_boundary, _ = protected_surface(ov, of)
    new_clipped, new_boundary, _ = protected_surface(nv, new_faces)
    assert len(old_clipped) and len(new_clipped), "Protected mating region must not be empty"
    old_samples = surface_samples(old_clipped, old_boundary)
    new_samples = surface_samples(new_clipped, new_boundary)
    # Use the full opposite mesh for distance so exact radius20 boundary
    # vertices are not falsely compared to an inscribed-polygon boundary.
    old_triangles = np.asarray(ov)[np.asarray(of)]
    new_triangles = nv[np.asarray(new_faces)]
    od = bounded_surface_distance(old_samples, new_triangles)
    nd = bounded_surface_distance(new_samples, old_triangles)
    maximum = max(float(od.max()), float(nd.max()))
    difference = abs(area(old_clipped)-area(new_clipped))
    return {
        "source": str(LEGACY), "source_sha256": sha(LEGACY),
        "comparison_part": INTERFACE_PART,
        "new_inverse_xyz_translation_mm": [-DX, -DY, -DZ],
        "region": "Legacy mating-plane radius<=20mm and normal distance n>=0; not the entire old z>4 region",
        "mating_center_legacy_xyz_mm": [0, 3, 28],
        "plane_tilt_deg": 35,
        "area_clip": {"shape": "inscribed regular 1024-gon of circumradius20, intersected with n>=0",
                      "maximum_radial_inset_mm": REGION_RADIUS*(1-math.cos(math.pi/REGION_SIDES)),
                      "nominal_plane_float_noise_snap_mm": .00002,
                      "no_added_cap_surfaces": True,
                      "boundary_coverage": "Exact original radius20-region vertices additionally sampled, including excluded polygon slivers"},
        "method": "Bidirectional point-to-triangle distances at clipped vertices, centroids, quarter/mid/three-quarter edge points and exact original protected-region vertices; compare clipped surface areas. Expanded triangle AABBs accelerate exact sub-tolerance candidates; unresolved points use the full evaluator. Planar retriangulation permitted.",
        "legacy_protected_original_vertices": len(old_boundary),
        "new_protected_original_vertices": len(new_boundary),
        "legacy_clipped_triangles": len(old_clipped), "new_clipped_triangles": len(new_clipped),
        "legacy_clipped_surface_area_mm2": area(old_clipped),
        "new_clipped_surface_area_mm2": area(new_clipped),
        "surface_area_difference_mm2": difference,
        "sample_count_old_to_new": len(old_samples), "sample_count_new_to_old": len(new_samples),
        "maximum_old_to_new_sampled_distance_mm": float(od.max()),
        "maximum_new_to_old_sampled_distance_mm": float(nd.max()),
        "maximum_sampled_surface_distance_mm": maximum,
        "distance_tolerance_mm": .0002, "surface_area_tolerance_mm2": .02,
        "pass": maximum < .0002 and difference < .02,
        "scope": "Strong sampled geometric equivalence within the protected interface; not a formal exact-surface proof or new physical fit acceptance.",
    }


def make3mf(vertices, faces, path):
    ET.register_namespace("", NS)
    def element(parent, tag, attributes=None):
        return ET.SubElement(parent, "{"+NS+"}"+tag, attributes or {})
    model = ET.Element("{"+NS+"}model", {"unit": "millimeter", "{http://www.w3.org/XML/1998/namespace}lang": "en-US"})
    element(model, "metadata", {"name": "Title"}).text = path.stem
    element(model, "metadata", {"name": "Description"}).text = (
        "Gridopoly Player Stand V2 R2 FPC60 FIRST FIT. GEOMETRY ONLY: one printable part; "
        "no printer, material, support, toolpath or process settings. New assembly and 60mm cable fit remain unverified.")
    resources = element(model, "resources")
    obj = element(resources, "object", {"id": "1", "type": "model", "name": path.stem})
    mesh = element(obj, "mesh")
    verts = element(mesh, "vertices")
    origin = [min(p[i] for p in vertices) for i in range(3)]
    for p in vertices:
        element(verts, "vertex", {axis: format(p[i]-origin[i], ".12g") for i, axis in enumerate("xyz")})
    triangles = element(mesh, "triangles")
    for face in faces:
        element(triangles, "triangle", {f"v{i+1}": str(face[i]) for i in range(3)})
    element(element(model, "build"), "item", {"objectid": "1"})
    content_types = '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>'
    relationships = '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel" Target="/3D/3dmodel.model"/></Relationships>'
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types)
        z.writestr("_rels/.rels", relationships)
        z.writestr("3D/3dmodel.model", ET.tostring(model, encoding="utf-8", xml_declaration=True))
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        model = ET.fromstring(z.read("3D/3dmodel.model"))
        assert model.attrib["unit"] == "millimeter"
        assert len(model.findall(".//{"+NS+"}object")) == 1
        assert len(model.findall(".//{"+NS+"}item")) == 1
        vr, fr = model.findall(".//{"+NS+"}vertex"), model.findall(".//{"+NS+"}triangle")
        assert len(vr) == len(vertices) and len(fr) == len(faces)
        for p, row in zip(vertices, vr):
            assert all(abs(float(row.attrib[axis])-(p[i]-origin[i])) < 1e-8 for i, axis in enumerate("xyz"))
        for face, row in zip(faces, fr):
            assert tuple(int(row.attrib[f"v{i+1}"]) for i in range(3)) == face
    return {"file": path.name, "single_print_object": True, "unit": "millimeter",
            "zip_crc_pass": True, "triangle_indices_unchanged": True,
            "vertices_equal_after_translation": True, "translation_from_stl_mm": [-x for x in origin],
            "metadata_revision": "V2 R2 FPC60 FIRST FIT", "geometry_only": True,
            "slicer_project": False, "printer_settings_included": False, "sha256": sha(path)}


def overhang_geometry(vertices, faces):
    triangles = np.asarray(vertices)[np.asarray(faces)]
    normals = np.cross(triangles[:, 1]-triangles[:, 0], triangles[:, 2]-triangles[:, 0])
    lengths = np.linalg.norm(normals, axis=1)
    valid = lengths > 1e-8
    unit_z = np.zeros(len(faces))
    unit_z[valid] = normals[valid, 2]/lengths[valid]
    above_bed = triangles[:, :, 2].max(axis=1) > 1e-4
    down = valid & above_bed & (unit_z < -math.sin(math.radians(.01)))
    steep = valid & above_bed & (unit_z < -math.sin(math.radians(45.01)))
    horizontal = valid & above_bed & (unit_z < -math.cos(math.radians(.01)))
    return {
        "classification": "Geometric screening only, not an actual sliced-support verdict",
        "angle_definition": "Outward overhang angle from a vertical wall: asin(-unit_normal_z); horizontal downward roof=90deg",
        "downward_faces_above_bed": int(down.sum()),
        "maximum_outward_overhang_angle_from_vertical_deg": float(np.degrees(np.arcsin(np.clip(-unit_z[down], 0, 1))).max()) if np.any(down) else 0.,
        "faces_steeper_than_45deg_above_bed": int(steep.sum()),
        "steep_overhang_area_mm2": float(lengths[steep].sum()/2),
        "horizontal_downward_roof_faces_above_bed": int(horizontal.sum()),
        "horizontal_downward_roof_area_mm2": float(lengths[horizontal].sum()/2),
    }


def main():
    missing = [str(OUT/name) for name in EXPECTED if not (OUT/name).is_file()]
    assert not missing, "Final STL exports missing: "+", ".join(missing)
    result = {
        "revision": "Player Stand V2 R2 FPC60", "performed_at_date": "2026-10-15",
        "source": "Independent verification of exported binary STL; does not invoke Blender or modify STL",
        "parts": [],
        "limitations": [
            "No H2D/PLA slicing, layer inspection, actual printing or assembled fit performed here.",
            "No cable contact-face/pin-order acceptance, real bend-strain acceptance or true 60mm installed slack measurement.",
            "Separate screen support downward faces are reported, not rejected en masse; inherited tilted interface still requires slicing/support review.",
            "3MFs contain one printable geometric part each; no printer, material or support settings. Four spacer copies are required.",
            "Mesh edge topology/volume and interface samples do not constitute a complete self-intersection or structural load proof.",
        ],
    }
    geometry = {}
    for name in EXPECTED:
        vertices, faces, normals = qa.parse(OUT/name)
        part = qa.verify(OUT/name, vertices, faces, normals)
        part["overhang_geometry"] = overhang_geometry(vertices, faces)
        part["checks"].pop("no_downward_faces_above_bed", None)
        if name == INTERFACE_PART:
            part["overhang_policy"] = "Separate screen support retains tilted legacy interfaces. All downward faces are reported; slicing/support and new-channel printability remain separate checks."
        else:
            part["checks"]["no_overhang_steeper_than_45deg_above_bed"] = part["overhang_geometry"]["faces_steeper_than_45deg_above_bed"] == 0
            part["overhang_policy"] = "Separate plate/cradle/lid/spacer: report downward surfaces, allow walls no more than45deg outward from vertical, and flag steeper/horizontal overhead regions. This is a geometric screen, not a no-support printing guarantee."
        part["pass"] = all(part["checks"].values())
        geometry[name] = (vertices, faces)
        result["parts"].append(part)
    old_report = HERE/"outputs/parts-verification.json"
    previous = json.loads(old_report.read_text(encoding="utf-8")) if old_report.is_file() else {}
    previous_interface = previous.get("legacy_screen_interface_comparison", {})
    previous_part = next((p for p in previous.get("parts", []) if p.get("file") == INTERFACE_PART), {})
    if (previous_interface.get("pass") and previous_interface.get("comparison_part") == INTERFACE_PART
            and previous_interface.get("new_inverse_xyz_translation_mm") == [-DX, -DY, -DZ]
            and previous_interface.get("source_sha256") == sha(LEGACY)
            and previous_part.get("sha256") == sha(OUT/INTERFACE_PART)):
        result["legacy_screen_interface_comparison"] = previous_interface
        result["legacy_screen_interface_comparison"]["unchanged_mesh_evidence_reused"] = {
            "previous_report_sha256": sha(old_report),
            "comparison_stl_sha256": sha(OUT/INTERFACE_PART),
            "reason": "Exact same exported interface STL and legacy source; only independent flat-part overhang classification or unrelated parts changed."
        }
    else:
        result["legacy_screen_interface_comparison"] = interface_comparison(*geometry[INTERFACE_PART])
    result["pass"] = all(p["pass"] for p in result["parts"]) and result["legacy_screen_interface_comparison"]["pass"]
    if result["pass"]:
        for part in result["parts"]:
            part["3mf"] = make3mf(*geometry[part["file"]], OUT/Path(part["file"]).with_suffix(".3mf"))
    output_report = HERE/"outputs/parts-verification.json"
    output_report.parent.mkdir(exist_ok=True)
    text = json.dumps(result, ensure_ascii=False, indent=2)+"\n"
    output_report.write_text(text, encoding="utf-8")
    (OUT/"parts-verification.json").write_text(text, encoding="utf-8")
    print(json.dumps({"pass": result["pass"],
                      "parts": [{k: v for k, v in p.items() if k in
                                 ("file", "pass", "dimensions_mm", "connected_components", "non_manifold_or_boundary_edges",
                                  "inconsistent_edge_winding", "degenerate_triangles", "signed_volume_mm3", "downward_triangles_above_bed")}
                                for p in result["parts"]],
                      "legacy": result["legacy_screen_interface_comparison"]}, ensure_ascii=False, indent=2))
    assert result["pass"], "Exported-STL or protected-interface QA failed; do not release fresh 3MF."


if __name__ == "__main__":
    main()
