"""Independent STL section audit of the R2/R3 mounting ears (millimetres).

The original wedge wall is X=-52 (left) / X=2 (right). A section of the
delivered part just OUTSIDE that wall identifies material supplied by the
ear. Intersect it with the original CW47.5 core just INSIDE the wall, after
the documented rigid translation and FPC chute subtraction. This avoids
counting all of the wedge, or the ear bridge occupying the chute, as a root.
No Blender, Boolean engine, source-design mesh connectivity or Git is used.
Area is integrated from independently intersected STL scanlines, and a
second wall offset checks convergence. This is a geometry audit, not FEA.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[3]
DELIVERABLES = REPO / 'Mechanical/PlayerConsoleStandV2/deliverables'


def load_stl(path):
    raw = path.read_bytes()
    count = int.from_bytes(raw[80:84], 'little')
    if len(raw) != 84 + count * 50:
        raise ValueError(f'Not a strict binary STL: {path}')
    dtype = np.dtype([('normal', '<f4', (3,)), ('v', '<f4', (3, 3)), ('attr', '<u2')])
    tri = np.frombuffer(raw, dtype=dtype, count=count, offset=84)['v'].astype(float)
    return tri, {'path': str(path), 'sha256': hashlib.sha256(raw).hexdigest(),
                 'triangles': count, 'bounds_mm': [tri.min(axis=(0, 1)).tolist(), tri.max(axis=(0, 1)).tolist()]}


def section(tri, axis, value):
    """Intersect a non-coplanar plane with STL triangles; return 2-D segments."""
    axes = [i for i in range(3) if i != axis]
    tri = tri[(tri[:, :, axis].min(1) < value) & (tri[:, :, axis].max(1) > value)]
    result = []
    for t in tri:
        pts = []
        for i, j in ((0, 1), (1, 2), (2, 0)):
            a, b = t[i], t[j]
            da, db = a[axis] - value, b[axis] - value
            if da * db < 0:
                p = a + (b - a) * (-da / (db - da))
                if not any(np.linalg.norm(p[axes] - q) < 1e-8 for q in pts):
                    pts.append(p[axes])
        if len(pts) == 2 and np.linalg.norm(pts[0] - pts[1]) > 1e-9:
            result.append(pts)
    return np.array(result)


def intervals(seg, ordinate):
    """Filled intervals on coordinate 0 of a closed section at coordinate 1."""
    a, b = seg[:, 0], seg[:, 1]
    selected = ((a[:, 1] <= ordinate) & (ordinate < b[:, 1])) | ((b[:, 1] <= ordinate) & (ordinate < a[:, 1]))
    aa, bb = a[selected], b[selected]
    hits = np.sort(aa[:, 0] + (bb[:, 0] - aa[:, 0]) * (ordinate - aa[:, 1]) / (bb[:, 1] - aa[:, 1]))
    if len(hits) % 2:
        raise ValueError('Odd scanline intersections: open or numerically degenerate section')
    return [(float(hits[i]), float(hits[i + 1])) for i in range(0, len(hits), 2)]


def intersect(a, b):
    return [(max(x0, y0), min(x1, y1)) for x0, x1 in a for y0, y1 in b if min(x1, y1) > max(x0, y0)]


def subtract(a, cut):
    if cut is None:
        return a
    lo, hi = cut
    out = []
    for start, end in a:
        if lo > start:
            out.append((start, min(end, lo)))
        if hi < end:
            out.append((max(start, hi), end))
    return [(s, e) for s, e in out if e > s]


ANG, CW = math.radians(35), math.radians(47.5)
U = np.array([math.cos(CW), -math.sin(CW) * math.cos(ANG), -math.sin(CW) * math.sin(ANG)])
V = np.array([math.sin(CW), math.cos(CW) * math.cos(ANG), math.cos(CW) * math.sin(ANG)])
N = np.array([0, -math.sin(ANG), math.cos(ANG)])
ORIGIN = np.array([-25, -6, 28])
CHUTE = [(-4.5, -20.25), (-6, -38), (-6, -90), (22, -90), (22, -38), (4.5, -20.25)]


def cutter_interval(x, z_print):
    """Convex chute intersection along Y, including its normal limits."""
    p = np.array([x, 0, z_print + 4]) - ORIGIN
    u0, v0, n0 = U @ p, V @ p, N @ p
    # Each half-plane is aa*Y + bb >= 0.
    inequalities = []
    for i, (ux, vy) in enumerate(CHUTE):
        ux2, vy2 = CHUTE[(i + 1) % len(CHUTE)]
        du, dv = ux2 - ux, vy2 - vy
        inequalities.append((du * V[1] - dv * U[1], du * (v0 - vy) - dv * (u0 - ux)))
    inequalities.extend([(N[1], n0 + 90), (-N[1], 12 - n0)])
    lo, hi = -float('inf'), float('inf')
    for aa, bb in inequalities:
        if abs(aa) < 1e-12:
            if bb < 0:
                return None
        elif aa > 0:
            lo = max(lo, -bb / aa)
        else:
            hi = min(hi, -bb / aa)
    return (lo, hi) if hi > lo else None


def root_audit(part, legacy, side, epsilon, steps):
    wall, outside, window = (-52., -1., (-23., 2.)) if side == 'left' else (2., 1., (8., 22.))
    ear = section(part, 0, wall + outside * epsilon)
    core = section(legacy, 0, wall + 25 - outside * epsilon).copy()
    core[:, :, 0] -= 9  # Original Y translated -9.
    core[:, :, 1] -= 4  # Print floor after separating the old base.

    def width(z):
        ea = intersect(intervals(ear, z), [window])
        co = subtract(intervals(core, z), cutter_interval(wall - outside * epsilon, z))
        return sum(end - start for start, end in intersect(ea, co))

    # Midpoint integration avoids horizontal faces at exact Z=0/4. For linear
    # boundaries it is exact except at transition strips; repeat resolution.
    heights = (np.arange(steps) + .5) * (4. / steps)
    widths = np.array([width(float(z)) for z in heights])
    active = heights[widths > 1e-7]
    probes = [0.00001, .5, 1., 2., 3., 3.75, 3.9, 3.99999]
    return {
        'wall_x_mm': wall, 'one_sided_offset_mm': epsilon, 'root_y_window_mm': list(window),
        'print_z_window_mm': [0, 4], 'area_mm2': float(widths.sum() * 4 / steps),
        'minimum_width_over_full_4mm_thickness_mm': float(widths.min()),
        'maximum_root_width_mm': float(widths.max()),
        'connected_z_extent_approx_mm': None if not len(active) else [float(active.min() - 2 / steps), float(active.max() + 2 / steps)],
        'width_probes_mm': [{'print_z_mm': z, 'root_y_width_mm': width(z)} for z in probes],
        'integration_step_mm': 4 / steps,
    }


def hole_audit(part, xy):
    result = []
    x, y = xy
    # Offset from common triangulation vertex heights, so the strict segment
    # intersection never has to infer whether a coplanar vertex is a crossing.
    for z in (.250017, 2.000017, 3.750017):
        seg = section(part, 2, z)

        def solid(px, py):
            return any(a < px < b for a, b in intervals(seg, py))

        radii = []
        for theta in np.arange(32) * (2 * math.pi / 32) + .017:
            lo, hi = 0., 3.
            if solid(x, y) or not solid(x + hi * math.cos(theta), y + hi * math.sin(theta)):
                raise ValueError(f'Hole obstructed or lacks surrounding material at {xy}, z={z}')
            for _ in range(28):
                mid = (lo + hi) / 2
                if solid(x + mid * math.cos(theta), y + mid * math.sin(theta)):
                    hi = mid
                else:
                    lo = mid
            radii.append((lo + hi) / 2)
        result.append({'z_mm': z, 'minimum_diameter_mm': 2 * min(radii), 'maximum_diameter_mm': 2 * max(radii)})
    return {'specified_center_xy_mm': list(xy), 'radial_center_check': '32 radial rays at each of three through-height sections',
            'sections': result, 'pass': all(3.59 < s['minimum_diameter_mm'] <= s['maximum_diameter_mm'] < 3.61 for s in result)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--old', type=Path, default=DELIVERABLES/'Gridopoly_Player_Stand_V2_R2_FPC60/02_Screen_Support_CW47_5_R2.stl')
    parser.add_argument('--new', type=Path, default=DELIVERABLES/'Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED/02_Screen_Support_CW47_5_R3.stl')
    parser.add_argument('--legacy', type=Path, default=REPO/'Mechanical/PlayerConsoleStand/outputs/models/Gridopoly_Player_Console_Stand_CW47_5.stl')
    parser.add_argument('--report', type=Path, default=ROOT/'outputs/ear-root-verification.json')
    parser.add_argument('--steps', type=int, default=12000)
    args = parser.parse_args()
    old, old_meta = load_stl(args.old)
    new, new_meta = load_stl(args.new)
    legacy, legacy_meta = load_stl(args.legacy)
    result = {
        'status': 'INDEPENDENT_GEOMETRIC_SECTION_AUDIT_NOT_LOAD_TEST',
        'method': __doc__, 'sources': {'R2': old_meta, 'R3': new_meta, 'legacy_core': legacy_meta},
        'coordinate_system': 'Delivered print STL coordinates, PCB-centered XY, support print floor Z=0; assembled floor Z=19.6 mm',
        'limitations': ['Section area is not a strength rating or physical load validation.',
                       'Core/ear separation uses documented original wedge walls and chute geometry; total wedge area is excluded.',
                       'STL source coordinates are float32; one-sided wall offsets and integration errors are reported.',
                       'Hole check validates the requested centers and bore passage; it does not certify screw-tool or FPC clearance.'],
        'roots': {}, 'holes': {},
    }
    for side in ('left', 'right'):
        r2 = root_audit(old, legacy, side, .0001, args.steps)
        r3 = root_audit(new, legacy, side, .0001, args.steps)
        convergence = root_audit(new, legacy, side, .00002, args.steps * 2)
        result['roots'][side] = {'R2': r2, 'R3': r3, 'R3_finer_check': convergence,
                                 'new_to_old_area_ratio': r3['area_mm2'] / r2['area_mm2'],
                                 'convergence_area_difference_mm2': abs(r3['area_mm2'] - convergence['area_mm2'])}
    for xy in [(-57, -18), (7, 15)]:
        result['holes'][str(xy)] = hole_audit(new, xy)
    result['geometry_checks_pass'] = (
        result['roots']['left']['R3']['minimum_width_over_full_4mm_thickness_mm'] > 10 and
        result['roots']['right']['R3']['minimum_width_over_full_4mm_thickness_mm'] > 13 and
        all(h['pass'] for h in result['holes'].values()))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps({'report': str(args.report), 'pass': result['geometry_checks_pass'],
                      'roots': {k: {'old_area_mm2': v['R2']['area_mm2'], 'new_area_mm2': v['R3']['area_mm2'],
                                    'new_minimum_width_mm': v['R3']['minimum_width_over_full_4mm_thickness_mm'],
                                    'ratio': v['new_to_old_area_ratio'], 'convergence_mm2': v['convergence_area_difference_mm2']}
                                for k, v in result['roots'].items()},
                      'holes_pass': all(h['pass'] for h in result['holes'].values())}, ensure_ascii=False))
    if not result['geometry_checks_pass']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
