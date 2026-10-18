"""Build V2 draft components and evidence, refusing to call missing art complete."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image

from avatar_v2 import (CANVAS, CROP, KINDS, ORDER, ROOT, SINGLE_BUDGET, TOTAL_BUDGET,
    HAIR_COLORS, SKINS, clean_source, component_bytes, default_manifest, digest,
    legacy_hair, make_material, normalize_face, normalize_neckline, quantize,
    register_box, tint, write_json, largest_component, register_hair_interface, audit_hair_interface)


def geometry_audit(components, config, root):
    """Measure real uncovered pixels; do not infer aesthetic acceptance."""
    scalp_rows = config.get("scalpAuditRows", [67, 101])
    neck_region = config.get("neckSeamAuditBox", [122, 210, 198, 247])
    scalp = []
    seams = []
    interior = []
    output = root / "review" / "geometry"
    output.mkdir(parents=True, exist_ok=True)
    faces = sorted(n for k, n in components if k == "face")
    hairs = sorted(n for k, n in components if k == "hair")
    outfits = sorted(n for k, n in components if k == "outfit")
    for f in faces:
        fa = np.array(components[("face", f)][0].getchannel("A")) >= 128
        for h in hairs:
            ha = np.array(components[("hair", h)][0].getchannel("A")) >= 128
            outside = np.zeros((320, 320), dtype=bool)
            for y in range(*scalp_rows):
                head = np.flatnonzero(fa[y])
                hair = np.flatnonzero(ha[y])
                if not len(head):
                    continue
                if not len(hair):
                    outside[y, head] = True
                else:
                    outside[y, head[(head < hair[0]) | (head > hair[-1])]] = True
            count = int(outside.sum())
            if count:
                debug = np.zeros((320, 320, 4), dtype=np.uint8)
                debug[fa] = (120, 140, 155, 190)
                debug[ha] = (40, 150, 220, 200)
                debug[outside] = (255, 50, 70, 255)
                filename = f"scalp-h{h:02d}-f{f:02d}.png"
                Image.fromarray(debug).save(output / filename)
                scalp.append({"hair": h, "face": f, "outsideHairEnvelopePixels": count, "auditImage": "geometry/" + filename})
            matching = next((e for e in config["components"] if e["kind"] == "hair" and e["presetId"] == h), {})
            fit = matching.get("registration", {}).get("innerFit")
            if fit:
                audit, debug = audit_hair_interface(components[("hair", h)][0], components[("face", f)][0], fit)
                filename = f"hair-interior-h{h:02d}-f{f:02d}.png"
                debug.save(output / filename)
                interior.append({"hair": h, "face": f, "auditImage": "geometry/" + filename, **audit})
    if ("neck", 1) in components:
        na = np.array(components[("neck", 1)][0].getchannel("A")) >= 128
        for f in faces:
            fa = np.array(components[("face", f)][0].getchannel("A")) >= 128
            for o in outfits:
                oa = np.array(components[("outfit", o)][0].getchannel("A")) >= 128
                union = fa | na | oa
                x0, y0, x1, y1 = neck_region
                gap = np.zeros((320, 320), dtype=bool)
                gap[y0:y1, x0:x1] = ~union[y0:y1, x0:x1]
                # Rectangular seam ROI intentionally includes its boundary. These
                # are candidate gaps requiring inspection, not silently ignored.
                count = int(gap.sum())
                outside = largest_component(~union) > 0
                enclosed = gap & ~outside
                debug = np.zeros((320, 320, 4), dtype=np.uint8)
                debug[na] = (220, 170, 80, 255)
                debug[fa] = (80, 170, 220, 255)
                debug[oa] = (90, 170, 120, 255)
                debug[gap] = (255, 40, 90, 255)
                filename = f"seam-f{f:02d}-o{o:02d}.png"
                Image.fromarray(debug).save(output / filename)
                seams.append({"face": f, "outfit": o, "candidateGapPixels": count, "enclosedGapPixels": int(enclosed.sum()), "externalBoundaryPixels": int((gap & outside).sum()), "auditImage": "geometry/" + filename})
    return {"scalpRows": scalp_rows, "scalpHairFacePairsChecked": len(faces) * len(hairs), "scalpEnvelopeFailures": scalp, "hairInteriorAudits": interior, "neckSeamAuditBox": neck_region, "neckSeams": seams, "note": "Hair partings inside the outer envelope are allowed. Hair interior audits use the scalp/temple core excluding ears; red means gap, yellow means excessive overlap. Red seam pixels are uncovered within the fixed declared ROI; do not change the ROI to hide a failure. External boundary pixels remain visible in audit images and require review; enclosed holes fail the structural gate."}


def acceptance_fingerprint(config, records):
    return {"schema": 1, "registrationSha256": digest(json.dumps(config, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")), "components": {e["file"]: {"fileSha256": e["fileSha256"], "sourceSha256": e["sourceSha256"], "skinMaskSha256": e["skinMaterial"].get("maskSha256")} for e in sorted(records, key=lambda e: e["file"])}}


def acceptance_matches(candidate, acceptance):
    return acceptance.get("approved") is True and all(acceptance.get(key) == value for key, value in candidate.items())


def compile_assets(config: dict, root: Path = ROOT) -> dict:
    declarations = config.get("components", [])
    identities = [(e["kind"], e["presetId"]) for e in declarations]
    expected = {(k, n) for k in KINDS for n in range(1, 2 if k == "neck" else 11)}
    if len(identities) != len(set(identities)) or set(identities) != expected:
        raise ValueError("registration manifest must declare each of the 31 components exactly once")
    if config.get("schema") != 2:
        raise ValueError("registration manifest schema must be 2")
    components, registered, records, blocked, review = {}, {}, [], [], []
    settings = config.get("quantization", {})
    template_id = int(config.get("faceTemplate", 1))
    declarations = sorted(declarations, key=lambda e: (0 if (e["kind"], e["presetId"]) == ("face", template_id) else 1, KINDS[e["kind"]], e["presetId"]))
    for entry in declarations:
        kind, preset = entry["kind"], entry["presetId"]
        key = f"{kind[0]}{preset}"
        evidence = {"registration": entry.get("registration", {})}
        try:
            if entry.get("legacyHair"):
                if kind != "hair" or entry["legacyHair"] != f"H{preset:02d}":
                    raise ValueError("legacy reuse is permitted only for matching hair IDs")
                image, source = legacy_hair(preset)
                evidence["legacyReuse"] = True
            else:
                source = (root / entry["source"]).resolve()
                if not source.is_relative_to(root.resolve()):
                    raise ValueError("V2 source must be inside V2 root")
                if not source.exists():
                    blocked.append({"kind": kind, "presetId": preset, "reason": "missing source", "path": entry["source"]})
                    continue
                image, cleanup = clean_source(source, entry.get("alphaThreshold", 32))
                evidence["cleanup"] = cleanup
                registration = entry.get("registration", {})
                source_box = registration.get("sourceBox", cleanup["mainComponentBounds"])
                target_box = registration.get("targetBox")
                if not target_box:
                    raise ValueError("registration targetBox is required")
                image = register_box(image, source_box, target_box, registration.get("yAnchors"))
                evidence["sourceBox"] = list(source_box)
                evidence["targetBox"] = list(target_box)
                if kind == "face" and preset != template_id:
                    if ("face", template_id) not in registered:
                        raise ValueError("F01 template is unavailable")
                    image, face_audit = normalize_face(image, registered[("face", template_id)])
                    evidence["commonSilhouette"] = face_audit
                if kind == "outfit" and registration.get("method") == "neckline":
                    image, collar_audit = normalize_neckline(image, registration["neckline"])
                    evidence["neckline"] = collar_audit
                if kind == "hair" and registration.get("innerFit"):
                    image, fit_audit = register_hair_interface(image, registration["innerFit"])
                    evidence["innerFit"] = fit_audit
            registered[(kind, preset)] = image
            # Save the geometrically registered authored colours as well as compiled
            # neutral material, so material inference can be compared against art.
            target = root / "device" / "registered" / kind / f"{key}.png"
            target.parent.mkdir(parents=True, exist_ok=True)
            image.save(target)
            image, mask, material_audit = make_material(image, kind, entry.get("skinMaterial", {}), root)
            image, mask = quantize(image, mask, settings.get("rgbStep", 8), settings.get("alphaStep", 17))
            payload, record = component_bytes(kind, preset, image, mask)
            relative = Path(kind) / f"{key}.gavc"
            path = root / "runtime" / "components-v2" / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
            device = root / "device" / "layers" / kind / f"{key}.png"
            device.parent.mkdir(parents=True, exist_ok=True)
            image.save(device)
            mask_file = root / "device" / "masks" / kind / f"{key}.png"
            mask_file.parent.mkdir(parents=True, exist_ok=True)
            mask.save(mask_file)
            record.update({"file": relative.as_posix(), "name": entry.get("name", key), "source": str(source.relative_to(root)) if source.is_relative_to(root) else str(source.relative_to(root.parent)), "sourceSha256": digest(source.read_bytes()), "rgbaSha256": record["sampleSha256"], "sampleBytes": 5, "registrationEvidence": evidence, "skinMaterial": material_audit, "deviceRgba": device.relative_to(root).as_posix(), "deviceMask": mask_file.relative_to(root).as_posix()})
            if not entry.get("registration", {}).get("reviewed"):
                review.append(f"{kind}/{key}: registration/front-facing/neckline visual review")
            if not material_audit.get("reviewed"):
                review.append(f"{kind}/{key}: skin-mask and Ebony visual review")
            records.append(record)
            components[(kind, preset)] = (image, mask)
        except (ValueError, OSError) as error:
            blocked.append({"kind": kind, "presetId": preset, "reason": str(error)})

    total = sum(e["fileBytes"] for e in records)
    checks = {"eachComponentWithin128KiB": all(e["fileBytes"] <= SINGLE_BUDGET for e in records), "all31WithinBudget": len(records) == 31 and total <= TOTAL_BUDGET, "totalFileBytes": total, "totalBudgetBytes": TOTAL_BUDGET, "componentCount": len(records), "expectedComponentCount": 31, "complete": len(records) == 31 and not blocked}
    alpha_checks = []
    for (kind, preset), (image, mask) in components.items():
        if kind not in ("face", "neck", "hair"):
            continue
        colors = HAIR_COLORS if kind == "hair" else SKINS
        baseline = image.getchannel("A").tobytes()
        for name, color in colors.items():
            changed = tint(image, mask, color, kind)
            alpha_checks.append(changed.getchannel("A").tobytes() == baseline)
    checks["recolorAlphaInvariant"] = all(alpha_checks)
    checks["recolorVariantsChecked"] = len(alpha_checks)
    geometry = geometry_audit(components, config, root)
    checks["scalpEnvelopeFailurePairs"] = len(geometry["scalpEnvelopeFailures"])
    checks["neckSeamCandidateGapPairs"] = sum(e["candidateGapPixels"] > 0 for e in geometry["neckSeams"])
    checks["neckSeamEnclosedGapPairs"] = sum(e["enclosedGapPixels"] > 0 for e in geometry["neckSeams"])
    checks["hairInteriorFailurePairs"] = sum(e["failureRows"] > 0 for e in geometry["hairInteriorAudits"])
    if all(("face", n) in components for n in range(1, 11)):
        target_alpha = components[("face", template_id)][0].getchannel("A").tobytes()
        checks["allFacesShareExactAlpha"] = all(components[("face", n)][0].getchannel("A").tobytes() == target_alpha for n in range(1, 11))
    else:
        checks["allFacesShareExactAlpha"] = None
    if checks["complete"]:
        # Check actual layer unions for all geometry recipes. These are structural
        # checks, never substitutes for the three visual review matrices.
        count, seam_failures = 0, []
        for f in range(1, 11):
            for o in range(1, 11):
                for h in range(1, 11):
                    union = np.zeros((320, 320), dtype=bool)
                    for kind, n in (("neck", 1), ("face", f), ("outfit", o), ("hair", h)):
                        union |= np.array(components[(kind, n)][0].getchannel("A")) >= 128
                    # The central neck route must remain connected from jaw to shirt.
                    if not union[212:247, 157:163].all():
                        seam_failures.append([f, o, h])
                    count += 1
        checks["geometryRecipesChecked"] = count
        checks["centerNeckGapRecipes"] = seam_failures
    else:
        checks["geometryRecipesChecked"] = 0
        checks["centerNeckGapRecipes"] = None
    technical = checks["complete"] and checks["all31WithinBudget"] and checks["eachComponentWithin128KiB"] and checks["recolorAlphaInvariant"] and checks["allFacesShareExactAlpha"] and not checks["centerNeckGapRecipes"] and not checks["scalpEnvelopeFailurePairs"] and not checks["neckSeamEnclosedGapPairs"] and not checks["hairInteriorFailurePairs"]
    candidate = acceptance_fingerprint(config, records)
    acceptance_path = root / "acceptance.json"
    acceptance = json.loads(acceptance_path.read_text(encoding="utf-8")) if acceptance_path.exists() else {}
    accepted = checks["complete"] and acceptance_matches(candidate, acceptance)
    # Booleans are annotations; release approval binds the exact authored sources,
    # registration, masks and encoded samples instead of carrying over to new art.
    status = "blocked" if not checks["complete"] else "failed" if not technical else "passed" if accepted else "review_required"
    result = {"schema": 2, "format": "GAVC", "encoding": 2, "sampleBytes": 5, "headerBytes": 32, "artRelease": config.get("artRelease", "avatar-v2-draft"), "status": status, "canvas": {"width": 220, "height": 300}, "previewCropFromDevice": {"x": 50, "y": 10, "width": 220, "height": 300}, "compositionOrder": list(ORDER), "tintAlgorithm": "gridopoly-avatar-skin-mask-v2", "shadeChannel": "R; positive mask samples require R=G=B", "shadeReference": 178, "entryCount": len(records), "totalFileBytes": total, "checks": checks, "geometryAudit": geometry, "blocked": blocked, "pendingVisualReview": [] if accepted else review, "visualAcceptance": {"path": "acceptance.json", "matched": accepted, "reviewer": acceptance.get("reviewer") if accepted else None}, "quantization": settings, "entries": records}
    write_json(root / "runtime" / "avatar-components-v2.json", result)
    write_json(root / "runtime" / "components-v2" / "manifest.json", result)
    write_json(root / "review" / "build-report.json", result)
    write_json(root / "review" / "acceptance-candidate.json", {**candidate, "approved": False, "reviewer": "", "notes": "Fill only after visual review; copy to V2/acceptance.json. A source, registration, mask or encoded sample change invalidates acceptance."})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--init-template", action="store_true", help="Create tools/registration.example.json only; never overwrite a manifest")
    parser.add_argument("--require-reviewed", action="store_true")
    parser.add_argument("--review", action="store_true", help="Generate offline HTML and contact sheets for available components")
    args = parser.parse_args()
    root = args.root.resolve()
    if args.init_template:
        path = root / "tools" / "registration.example.json"
        if path.exists():
            raise SystemExit(f"Refusing to overwrite {path}")
        write_json(path, default_manifest())
        print(f"Created {path}; registration and material inference require visual review")
        return
    path = args.manifest or root / "tools" / "registration.example.json"
    config = json.loads(path.read_text(encoding="utf-8"))
    report = compile_assets(config, root)
    if args.review:
        from review_avatar_v2 import generate_review
        generate_review(root, report)
    print(json.dumps({"status": report["status"], "components": report["entryCount"], "bytes": report["totalFileBytes"], "blocked": report["blocked"], "checks": report["checks"], "pendingVisualReviews": len(report["pendingVisualReview"])}, ensure_ascii=False))
    if report["status"] in ("blocked", "failed"):
        sys.exit(2)
    if args.require_reviewed and report["status"] != "passed":
        sys.exit(3)


if __name__ == "__main__":
    main()
