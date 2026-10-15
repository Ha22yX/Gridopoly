"""Analytical FPC60 candidate, millimetres. Does not certify a real cable fit.

Only standard-library Python is required. Outputs stay beside this script.
The first bend START, rather than its virtual corner, is fixed at v=-20.5.
"""
from __future__ import annotations

import json
import math
from pathlib import Path


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def mul(a, k):
    return tuple(x * k for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def norm(a):
    return math.sqrt(dot(a, a))


def unit(a):
    return mul(a, 1 / norm(a))


def rotate(a, axis, angle):
    c, s = math.cos(angle), math.sin(angle)
    return add(add(mul(a, c), mul(cross(axis, a), s)), mul(axis, dot(axis, a)*(1-c)))


PARAMETERS = {
    "upper_base_z_mm": 15.6,
    "pcb_top_z_mm": 9.6,
    "legacy_interface_translation_xy_mm": [-25.0, -9.0],
    "legacy_screen_tilt_deg": 35.0,
    "legacy_pattern_clockwise_deg": 47.5,
    "assumed_source_local_pattern_v_mm": 0.0,
    "assumed_source_normal_offset_mm": 0.35,
    "first_bend_tangent_start_local_v_mm": -20.5,
    "minimum_cut_region_local_v_mm": -20.25,
    "centerline_fillet_radius_mm": 3.0,
    "j2_assumed_mouth_center_xyz_mm": [-40.4, -18.801, 10.9],
    "j2_external_straight_length_mm": 4.0,
    "assumed_insertion_length_mm": 4.0,
    "user_latest_cable_length_mm": 60.0,
    "ribbon_width_mm": 5.0,
    "ribbon_thickness_for_envelope_only_mm": 0.3,
    "maximum_sample_spacing_mm": 0.25,
    "maximum_arc_sample_angle_deg": 1.0,
}


def corner(a, b, c, radius):
    ti, to = unit(sub(b, a)), unit(sub(c, b))
    angle = math.acos(max(-1., min(1., dot(ti, to))))
    tangent = radius * math.tan(angle / 2)
    axis = unit(cross(ti, to))
    start, end = sub(b, mul(ti, tangent)), add(b, mul(to, tangent))
    center = add(start, mul(cross(axis, ti), radius))
    return {"virtual_corner": b, "start": start, "end": end,
            "center": center, "axis": axis, "incoming_tangent": ti,
            "outgoing_tangent": to, "angle": angle,
            "tangent_trim": tangent, "radius": radius}


def three_point_radius(a, b, c):
    ab, bc, ac = sub(b, a), sub(c, b), sub(c, a)
    twice_area = norm(cross(ab, ac))
    if twice_area < 1e-10:
        return None
    return norm(ab)*norm(bc)*norm(ac) / (2*twice_area)


def build():
    p = PARAMETERS
    angle = math.radians(p["legacy_screen_tilt_deg"])
    phi = math.radians(p["legacy_pattern_clockwise_deg"])
    width_axis_initial = (math.cos(phi), -math.sin(phi)*math.cos(angle), -math.sin(phi)*math.sin(angle))
    pattern_v_axis = (math.sin(phi), math.cos(phi)*math.cos(angle), math.cos(phi)*math.sin(angle))
    screen_normal = (0.0, -math.sin(angle), math.cos(angle))
    dx, dy = p["legacy_interface_translation_xy_mm"]
    center = (dx, 3 + dy, 28 + p["upper_base_z_mm"])
    source = add(center, mul(screen_normal, p["assumed_source_normal_offset_mm"]))
    source = add(source, mul(pattern_v_axis, p["assumed_source_local_pattern_v_mm"]))
    first_tangent = mul(pattern_v_axis, -1)
    mouth = tuple(p["j2_assumed_mouth_center_xyz_mm"])
    last_tangent = (1.0, 0.0, 0.0)
    tip = add(mouth, mul(last_tangent, p["assumed_insertion_length_mm"]))
    straight_before_bend = p["assumed_source_local_pattern_v_mm"] - p["first_bend_tangent_start_local_v_mm"]
    radius = p["centerline_fillet_radius_mm"]
    virtual1 = add(source, mul(first_tangent, straight_before_bend))
    virtual2 = sub(mouth, mul(last_tangent, 8))
    for _ in range(300):
        c1 = corner(source, virtual1, virtual2, radius)
        virtual1 = add(source, mul(first_tangent, straight_before_bend + c1["tangent_trim"]))
        c2 = corner(virtual1, virtual2, mouth, radius)
        virtual2 = sub(mouth, mul(last_tangent, p["j2_external_straight_length_mm"] + c2["tangent_trim"]))
    c1, c2 = corner(source, virtual1, virtual2, radius), corner(virtual1, virtual2, mouth, radius)
    first_line_length = norm(sub(c1["start"], source))
    middle_line_length = norm(sub(c2["start"], c1["end"]))
    final_line_length = norm(sub(tip, c2["end"]))
    assert abs(first_line_length - straight_before_bend) < 1e-9
    assert middle_line_length > 2.0
    assert abs(norm(sub(mouth, c2["end"])) - p["j2_external_straight_length_mm"]) < 1e-9

    records, pieces = [], []
    length = 0.0

    def line(name, a, b):
        nonlocal length
        distance = norm(sub(b, a))
        tangent = unit(sub(b, a))
        count = max(1, math.ceil(distance / p["maximum_sample_spacing_mm"]))
        pieces.append({"name": name, "type": "line", "start": a, "end": b,
                       "start_s_mm": length, "length_mm": distance, "radius_mm": None})
        for i in range(count + 1):
            if records and i == 0:
                continue
            t = i / count
            records.append({"s_mm": length + distance*t, "center_xyz_mm": add(a, mul(sub(b, a), t)),
                            "tangent_xyz": tangent, "piece": name})
        length += distance

    def arc(name, c):
        nonlocal length
        distance = c["radius"]*c["angle"]
        count = max(math.ceil(distance/p["maximum_sample_spacing_mm"]),
                    math.ceil(c["angle"]/math.radians(p["maximum_arc_sample_angle_deg"])))
        pieces.append({"name": name, "type": "circular_arc", "start": c["start"], "end": c["end"],
                       "center": c["center"], "axis": c["axis"], "angle_deg": math.degrees(c["angle"]),
                       "virtual_corner": c["virtual_corner"], "tangent_trim_mm": c["tangent_trim"],
                       "start_s_mm": length, "length_mm": distance, "radius_mm": c["radius"]})
        for i in range(1, count + 1):
            theta = c["angle"] * i / count
            position = add(c["center"], rotate(sub(c["start"], c["center"]), c["axis"], theta))
            tangent = rotate(c["incoming_tangent"], c["axis"], theta)
            records.append({"s_mm": length + c["radius"]*theta, "center_xyz_mm": position,
                            "tangent_xyz": tangent, "piece": name})
        length += distance

    line("old_groove_straight", source, c1["start"])
    arc("outside_ring_bend_R3", c1)
    descent_start_s = length
    line("free_descent_and_twist", c1["end"], c2["start"])
    descent_end_s = length
    arc("J2_approach_bend_R3", c2)
    line("J2_external_straight", c2["end"], mouth)
    line("J2_assumed_insertion", mouth, tip)

    widths = []
    width = width_axis_initial
    previous_tangent = first_tangent
    for rec in records:
        tangent = rec["tangent_xyz"]
        axis = cross(previous_tangent, tangent)
        sine = norm(axis)
        if sine > 1e-12:
            width = rotate(width, mul(axis, 1/sine), math.atan2(sine, dot(previous_tangent, tangent)))
        width = unit(sub(width, mul(tangent, dot(width, tangent))))
        widths.append(width)
        previous_tangent = tangent

    twist_s0, twist_s1 = descent_start_s + 1.0, descent_end_s - 1.0
    variants = {}
    for sign in (1, -1):
        target_width = (0., float(sign), 0.)
        twist = math.atan2(dot(last_tangent, cross(widths[-1], target_width)), dot(widths[-1], target_width))
        key = "end_normal_plus_Z" if sign == 1 else "end_normal_minus_Z"
        samples = []
        for rec, transported in zip(records, widths):
            phase = min(1., max(0., (rec["s_mm"] - twist_s0)/(twist_s1 - twist_s0)))
            smooth = phase*phase*(3-2*phase)
            w = rotate(transported, rec["tangent_xyz"], twist*smooth)
            normal = unit(cross(rec["tangent_xyz"], w))
            half_w = mul(w, p["ribbon_width_mm"]/2)
            samples.append({"s_mm": rec["s_mm"], "width_axis_xyz": w, "normal_xyz": normal,
                            "applied_twist_deg": math.degrees(twist*smooth),
                            "edge_negative_xyz_mm": sub(rec["center_xyz_mm"], half_w),
                            "edge_positive_xyz_mm": add(rec["center_xyz_mm"], half_w)})
        edge_minima = {}
        for edge in ("edge_negative_xyz_mm", "edge_positive_xyz_mm"):
            radii = [three_point_radius(a[edge], b[edge], c[edge]) for a, b, c in zip(samples, samples[1:], samples[2:])]
            edge_minima[edge] = min(r for r in radii if r is not None)
        after_departure = [(rec, sample) for rec, sample in zip(records, samples)
                           if rec["s_mm"] >= first_line_length - 1e-8]
        corner_v = []
        for rec, sample in after_departure:
            for edge in ("edge_negative_xyz_mm", "edge_positive_xyz_mm"):
                for thickness_sign in (-1, 1):
                    surface_point = add(sample[edge], mul(sample["normal_xyz"], thickness_sign*p["ribbon_thickness_for_envelope_only_mm"]/2))
                    corner_v.append(dot(sub(surface_point, center), pattern_v_axis))
        variants[key] = {
            "status": "GEOMETRIC_ENVELOPE_ONLY / CONTACT_FACE_AND_PIN_MAPPING_UNCONFIRMED",
            "end_width_axis_xyz": target_width,
            "end_normal_xyz": [0., 0., float(sign)],
            "distributed_twist_deg": math.degrees(twist),
            "twist_start_s_mm": twist_s0, "twist_end_s_mm": twist_s1,
            "twist_distribution": "smoothstep over straight descent only; no roll inside old groove or insertion",
            "sampled_minimum_edge_curve_circumradius_mm": edge_minima,
            "maximum_ribbon_surface_local_v_after_bend_start_mm": max(corner_v),
            "samples": samples,
        }

    center_radii = [three_point_radius(a["center_xyz_mm"], b["center_xyz_mm"], c["center_xyz_mm"])
                   for a, b, c in zip(records, records[1:], records[2:])]
    polyline = sum(norm(sub(b["center_xyz_mm"], a["center_xyz_mm"])) for a, b in zip(records, records[1:]))
    local_v = [dot(sub(rec["center_xyz_mm"], center), pattern_v_axis)
               for rec in records if rec["s_mm"] >= first_line_length - 1e-8]
    assert abs(max(local_v) + 20.5) < 1e-8
    assert all(norm(sub(v["samples"][-1]["width_axis_xyz"], v["end_width_axis_xyz"])) < 1e-9 for v in variants.values())
    assert all(v["maximum_ribbon_surface_local_v_after_bend_start_mm"] <= -20.25 for v in variants.values())
    return {
        "schema_version": 1,
        "status": "ANALYTICAL_CANDIDATE / NOT_PHYSICAL_FIT_PASS",
        "parameters": p,
        "coordinates": "PCB center XY, USB toward -Y, table Z=0; all lengths mm",
        "source_assumptions": [
            "User corrected the cable length to approximately 60 mm; replaces 65 mm.",
            "Screen exit is assumed at old pattern v=0,n=0.35; not independently surveyed.",
            "J2 mouth XYZ and 4 mm insertion are provisional; actual connector datum is unverified.",
            "R3 is a chosen centerline geometric radius, not a manufacturer-certified FPC limit.",
            "5 mm strip width is the requested routing envelope; 0.3 mm thickness is a modelling assumption.",
            "No contact face, conductor-edge identity or compatible J2 pin mapping is established here.",
            "Do not mirror J2 or its SCREEN_DM pin 1 mapping on the basis of either ribbon envelope variant.",
            "Parallel-transport ribbon is a collision envelope, not a developability, strain or crease simulation.",
        ],
        "datums": {"mating_center_xyz_mm": center, "assumed_source_xyz_mm": source,
                   "initial_width_axis_xyz": width_axis_initial, "pattern_v_axis_xyz": pattern_v_axis,
                   "first_bend_start_xyz_mm": c1["start"], "first_virtual_corner_xyz_mm": virtual1,
                   "j2_mouth_xyz_mm": mouth, "assumed_finger_tip_xyz_mm": tip},
        "lengths": {"analytic_centerline_total_mm": length,
                    "external_source_to_mouth_mm": length-p["assumed_insertion_length_mm"],
                    "assumed_inserted_length_mm": p["assumed_insertion_length_mm"],
                    "remaining_from_user_60mm_mm": p["user_latest_cable_length_mm"]-length,
                    "sampled_polyline_total_mm": polyline,
                    "polyline_chord_shortfall_mm": length-polyline,
                    "straight_descent_mm": middle_line_length},
        "curvature": {"analytic_minimum_centerline_radius_mm": radius,
                      "sampled_centerline_minimum_circumradius_mm": min(r for r in center_radii if r is not None),
                      "line_curvature": 0,
                      "joins": "C1 tangent-continuous; curvature steps 0 to 1/R at circular-arc joins",
                      "warning": "Minimum strip-edge radii are reported separately. R3 does not mean every ribbon edge has radius 3."},
        "interface_preservation": {"bend_start_local_v_mm": max(local_v),
                                   "centerline_min_local_v_after_bend_start_mm": min(local_v),
                                   "maximum_allowed_new_channel_local_v_mm": -20.25,
                                   "warning": "Apply a v<=-20.25 clipping region to new cutters; cutter caps/clearance margins must not reach old interface."},
        "pieces": pieces,
        "centerline": records,
        "ribbon_variants": variants,
    }


if __name__ == "__main__":
    result = build()
    destination = Path(__file__).with_name("fpc60-route.json")
    destination.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(destination), "lengths": result["lengths"],
                      "curvature": result["curvature"],
                      "twist_degrees": {k: v["distributed_twist_deg"] for k,v in result["ribbon_variants"].items()},
                      "edge_min_radii": {k: v["sampled_minimum_edge_curve_circumradius_mm"] for k,v in result["ribbon_variants"].items()}}, indent=2))
