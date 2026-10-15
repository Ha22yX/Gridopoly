"""Read-only R3 STL QA, frozen R2 part identity, and geometry-only 3MF delivery.

Run after final R3 STL notification. Writes only this revision's reports and
the new download folder's 3MF/report files. Never changes STL, Blender or R2.
"""
from __future__ import annotations

from pathlib import Path
import hashlib
import importlib.util
import json
import shutil
import xml.etree.ElementTree as ET
import zipfile


HERE = Path(__file__).resolve().parent
R2_SCRIPT = HERE.parent / "R2_FPC60/verify_parts.py"
R2_OUT = Path("Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R2_FPC60")
OUT = Path("Z:/Files/我的项目/Gridopoly/Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED")
INTERFACE_PART = "02_Screen_Support_CW47_5_R3.stl"
EXPECTED = {
    "01_Connection_Plate_R2.stl": (116., 111., 4.),
    INTERFACE_PART: (74., 50., 41.864471435546875),
    "03_Battery_Cradle_R2.stl": (84., 17., 39.),
    "04_Battery_Lid_R2.stl": (69., 22., 4.),
    "05_Spacer_6mm_R2.stl": (6., 6., 6.),
}
UNCHANGED = tuple(name for name in EXPECTED if name != INTERFACE_PART)
NS = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"

spec = importlib.util.spec_from_file_location("r2_independent_stl_qa", R2_SCRIPT)
r2 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r2)
# In-memory configuration only; imported source files remain untouched.
r2.EXPECTED = EXPECTED
r2.qa.EXPECTED = EXPECTED
r2.INTERFACE_PART = INTERFACE_PART


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check3mf(vertices, faces, path):
    origin = [min(p[i] for p in vertices) for i in range(3)]
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None, "3MF ZIP checksum failure"
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
        metadata = {m.attrib.get("name"): m.text for m in model.findall("{"+NS+"}metadata")}
    return {
        "file": path.name, "single_print_object": True, "unit": "millimeter",
        "zip_crc_pass": True, "triangle_indices_unchanged": True,
        "vertices_equal_after_translation": True, "translation_from_stl_mm": [-x for x in origin],
        "metadata": metadata, "geometry_only": True, "slicer_project": False,
        "printer_settings_included": False, "sha256": sha(path),
    }


def make_new_support_3mf(vertices, faces, path):
    ET.register_namespace("", NS)
    def el(parent, tag, attributes=None):
        return ET.SubElement(parent, "{"+NS+"}"+tag, attributes or {})
    model = ET.Element("{"+NS+"}model", {"unit": "millimeter", "{http://www.w3.org/XML/1998/namespace}lang": "en-US"})
    el(model, "metadata", {"name": "Title"}).text = path.stem
    el(model, "metadata", {"name": "Description"}).text = (
        "Gridopoly Player Stand V2 R3 EAR REINFORCED FIRST FIT. GEOMETRY ONLY: "
        "one printable screen support; no printer, material, support, toolpath or process settings. "
        "Reinforced mounting ears, print strength, installed clearances and physical fit remain unverified.")
    resources = el(model, "resources")
    obj = el(resources, "object", {"id": "1", "type": "model", "name": path.stem})
    mesh = el(obj, "mesh")
    verts = el(mesh, "vertices")
    origin = [min(p[i] for p in vertices) for i in range(3)]
    for p in vertices:
        el(verts, "vertex", {axis: format(p[i]-origin[i], ".12g") for i, axis in enumerate("xyz")})
    triangles = el(mesh, "triangles")
    for face in faces:
        el(triangles, "triangle", {f"v{i+1}": str(face[i]) for i in range(3)})
    el(el(model, "build"), "item", {"objectid": "1"})
    content_types = '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>'
    relationships = '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel" Target="/3D/3dmodel.model"/></Relationships>'
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types)
        z.writestr("_rels/.rels", relationships)
        z.writestr("3D/3dmodel.model", ET.tostring(model, encoding="utf-8", xml_declaration=True))
    report = check3mf(vertices, faces, path)
    report["metadata_revision"] = "V2 R3 EAR REINFORCED FIRST FIT"
    report["newly_generated"] = True
    return report


def main():
    missing = [str(OUT/name) for name in EXPECTED if not (OUT/name).is_file()]
    assert not missing, "Final STL exports missing: "+", ".join(missing)
    reference_report_path = R2_OUT/"parts-verification.json"
    reference_report = json.loads(reference_report_path.read_text(encoding="utf-8"))
    reference_parts = {part["file"]: part for part in reference_report["parts"]}
    result = {
        "revision": "Player Stand V2 R3 EAR REINFORCED", "parts": [],
        "source": "Independent final exported-STL QA; imported R2 algorithms with no R2 source edits",
        "reused_algorithm_script": str(R2_SCRIPT), "reused_algorithm_script_sha256": sha(R2_SCRIPT),
        "frozen_r2_report": str(reference_report_path), "frozen_r2_report_sha256": sha(reference_report_path),
        "unchanged_r2_part_identity": [],
        "limitations": [
            "No actual H2D/PLA slicing, layer preview, printing, load/ear fatigue or physical assembly test.",
            "New ear roots, screw clearances and FPC assembly interference are evaluated separately by the main modelling task.",
            "Retained CW47.5 surfaces are verified geometrically, not re-certified for support-free printing.",
            "Cable contact face/pin order, true installed length and allowable bend strain remain physical checks.",
            "3MFs are single geometry objects with no print settings; four copies of the retained6mm spacer are required.",
            "Edge topology, signed volume and sampled interface equivalence are not a full self-intersection or structural-strength proof.",
        ],
    }
    for name in UNCHANGED:
        old_hash, new_hash = sha(R2_OUT/name), sha(OUT/name)
        frozen_hash = reference_parts[name]["sha256"]
        result["unchanged_r2_part_identity"].append({
            "file": name, "r2_stl_sha256": old_hash, "r3_copy_sha256": new_hash,
            "frozen_r2_report_sha256": frozen_hash,
            "binary_equal": old_hash == new_hash,
            "source_matches_frozen_r2_report": old_hash == frozen_hash,
            "pass": old_hash == new_hash == frozen_hash,
        })
    geometry = {}
    for name in EXPECTED:
        vertices, faces, normals = r2.qa.parse(OUT/name)
        part = r2.qa.verify(OUT/name, vertices, faces, normals)
        part["overhang_geometry"] = r2.overhang_geometry(vertices, faces)
        part["checks"].pop("no_downward_faces_above_bed", None)
        if name == INTERFACE_PART:
            part["overhang_policy"] = "Report all screen-support overhang geometry; retained sloping interface and new ear roots require separate layer/support review."
        else:
            part["checks"]["no_overhang_steeper_than_45deg_above_bed"] = part["overhang_geometry"]["faces_steeper_than_45deg_above_bed"] == 0
            part["overhang_policy"] = "Unchanged R2 part; report geometry and retain45deg geometric screening, not a no-support printing guarantee."
        part["pass"] = all(part["checks"].values())
        geometry[name] = (vertices, faces)
        result["parts"].append(part)
    result["legacy_screen_interface_comparison"] = r2.interface_comparison(*geometry[INTERFACE_PART])
    result["pass"] = (all(p["pass"] for p in result["parts"])
                      and all(p["pass"] for p in result["unchanged_r2_part_identity"])
                      and result["legacy_screen_interface_comparison"]["pass"])
    if result["pass"]:
        for part in result["parts"]:
            name = part["file"]
            destination = OUT/Path(name).with_suffix(".3mf")
            if name == INTERFACE_PART:
                part["3mf"] = make_new_support_3mf(*geometry[name], destination)
            else:
                source = R2_OUT/destination.name
                # Retain the exact old3MF bytes and truthful R2 metadata.
                assert sha(source) == reference_parts[name]["3mf"]["sha256"]
                if not destination.is_file() or sha(destination) != sha(source):
                    shutil.copyfile(source, destination)
                part["3mf"] = check3mf(*geometry[name], destination)
                part["3mf"].update({"retained_r2_binary_copy": True,
                                    "r2_source_sha256": sha(source), "metadata_revision": "Retained R2 original"})
    report_path = HERE/"outputs/parts-verification.json"
    report_path.parent.mkdir(exist_ok=True)
    text = json.dumps(result, ensure_ascii=False, indent=2)+"\n"
    report_path.write_text(text, encoding="utf-8")
    (OUT/"parts-verification.json").write_text(text, encoding="utf-8")
    print(json.dumps({"pass": result["pass"],
                      "parts": [{k: v for k, v in p.items() if k in
                                 ("file", "pass", "dimensions_mm", "connected_components", "non_manifold_or_boundary_edges",
                                  "inconsistent_edge_winding", "degenerate_triangles", "signed_volume_mm3")}
                                for p in result["parts"]],
                      "unchanged_r2_parts": result["unchanged_r2_part_identity"],
                      "legacy": result["legacy_screen_interface_comparison"]}, ensure_ascii=False, indent=2))
    assert result["pass"], "R3 STL/frozen-part/interface QA failed; do not release new3MF."


if __name__ == "__main__":
    main()
