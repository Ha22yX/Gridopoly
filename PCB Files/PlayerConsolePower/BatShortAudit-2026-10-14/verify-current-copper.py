"""Audit exported two-layer copper against captured component pads and vias.

Requires gerbonara 1.6.3 / shapely 2.1.2. Read-only inputs; report written here.
This is geometric continuity, not impedance, plating quality or an electrical test.
"""
from pathlib import Path
from tempfile import TemporaryDirectory
from collections import defaultdict
import hashlib
import json
import zipfile
from gerbonara import GerberFile
from shapely.geometry import Point, Polygon, GeometryCollection, LineString
from shapely.ops import unary_union
from shapely import make_valid

BASE = Path(__file__).resolve().parent
LIVE = json.loads((BASE / 'current-snapshot.json').read_text(encoding='utf-8'))
ZIP = BASE / 'current-Gerber.zip'
polygons = []
layers = {}
input_hashes = {}
with TemporaryDirectory(prefix='gridopoly-copper-audit-') as tmp:
    for layer, name in [(1, 'Gerber_TopLayer.GTL'), (2, 'Gerber_BottomLayer.GBL')]:
        data = zipfile.ZipFile(ZIP).read(name)
        input_hashes[name] = hashlib.sha256(data).hexdigest()
        file = Path(tmp) / name
        file.write_bytes(data)
        copper = GeometryCollection()
        pending = []
        polarity = None
        for obj in GerberFile.open(file).objects:
            for prim in obj.to_primitives():
                outline = prim.to_arc_poly().approximate_arcs(max_error=.001).outline
                if len(outline) < 3:
                    continue
                if pending and polarity != prim.polarity_dark:
                    shape = unary_union(pending)
                    copper = copper.union(shape) if polarity else copper.difference(shape)
                    pending = []
                polarity = prim.polarity_dark
                pending.append(make_valid(Polygon(outline)))
        if pending:
            shape = unary_union(pending)
            copper = copper.union(shape) if polarity else copper.difference(shape)
        parts = list(copper.geoms) if hasattr(copper, 'geoms') else [copper]
        layers[layer] = []
        for poly in parts:
            if poly.geom_type == 'Polygon':
                layers[layer].append(len(polygons))
                polygons.append(poly)
        print('Copper layer', layer, 'polygons', len(layers[layer]), flush=True)

parent = list(range(len(polygons)))
def root(i):
    while i != parent[i]:
        parent[i] = parent[parent[i]]
        i = parent[i]
    return i
def union(ids):
    if ids:
        a = root(ids[0])
        for i in ids[1:]:
            parent[root(i)] = a
def locate(layer, x, y):
    point = Point(x * .0254, y * .0254)
    # 3 um allowance only for point-on-copper identification, never for joining polygons.
    return [i for i in layers[layer] if polygons[i].distance(point) <= .003]

terminals = []
missing = []
ambiguous = []
for comp in LIVE['components']:
    for pad in comp['pads']:
        # The API includes unused tiny hole metadata on several TOP-only USB pads.
        # Only MULTI layer pads are through pads; never bridge layers from hole metadata alone.
        sides = [1, 2] if pad['layer'] == 12 and pad.get('metallization') else [pad['layer']]
        ids = []
        for side in sides:
            hits = locate(side, pad['x'], pad['y'])
            if not hits:
                missing.append({'ref': comp['ref'], 'pin': pad['padNumber'], 'layer': side})
            if len(hits) > 1:
                ambiguous.append({'ref': comp['ref'], 'pin': pad['padNumber'], 'layer': side})
            ids += hits
        union(ids)
        terminals.append({'label': comp['ref'] + '.' + pad['padNumber'], 'net': pad.get('net') or '', 'nodes': ids})

for via in LIVE['vias']:
    ids = []
    for side in [1, 2]:
        hits = locate(side, via['x'], via['y'])
        if not hits:
            missing.append({'via': via['primitiveId'], 'layer': side})
        if len(hits) > 1:
            ambiguous.append({'via': via['primitiveId'], 'layer': side})
        ids += hits
    union(ids)
    terminals.append({'label': 'via:' + via['primitiveId'], 'net': via['net'], 'nodes': ids})

islands = defaultdict(list)
by_net = defaultdict(set)
per_polygon = defaultdict(list)
for terminal in terminals:
    for node in terminal['nodes']:
        per_polygon[node].append({'label': terminal['label'], 'net': terminal['net']})
    for r in set(root(i) for i in terminal['nodes']):
        islands[r].append(terminal)
        if terminal['net']:
            by_net[terminal['net']].add(r)
shorts = []
nc_connections = []
for island, ts in islands.items():
    nets = sorted(set(t['net'] for t in ts if t['net']))
    if len(nets) > 1:
        shorts.append({'nets': nets, 'terminals': [t['label'] for t in ts]})
    if nets and any(not t['net'] for t in ts):
        nc_connections.append({'nets': nets, 'nc': [t['label'] for t in ts if not t['net']]})
opens = [{'net': net, 'islands': len(rs), 'groups': [[t['label'] for t in islands[r]] for r in rs]} for net, rs in by_net.items() if len(rs) > 1]

# Check every trace centerline is represented by copper of its intended electrical island.
trace_failures = []
for trace in LIVE['lines']:
    rs = by_net.get(trace['net'], set())
    copper = unary_union([polygons[i] for i in layers[trace['layer']] if root(i) in rs])
    line = LineString([(trace['startX'] * .0254, trace['startY'] * .0254), (trace['endX'] * .0254, trace['endY'] * .0254)])
    missing_length = line.difference(copper.buffer(.003)).length
    if missing_length > .003:
        trace_failures.append({'id': trace['primitiveId'], 'net': trace['net'], 'uncoveredMm': missing_length})

report = {
    'snapshotAt': LIVE['capturedAt'],
    'method': 'Exported Gerber polarity booleans, arcs approximated to 1 um; connectivity only through actual copper and captured plated vias/component holes. Point lookup tolerance 3 um; no tolerance joining separate polygons.',
    'limitations': ['Assumes all captured through vias/PTH component pads are manufactured and plated correctly.', 'Does not validate impedance, signal integrity, thermal performance or real connector contact wiring.', 'Only declared component pads, vias and routed traces are assigned electrical nets; anonymous copper islands are not functional nets.'],
    'counts': {'componentPads': sum(len(c['pads']) for c in LIVE['components']), 'vias': len(LIVE['vias']), 'traces': len(LIVE['lines']), 'electricalNets': len(by_net), 'copperPolygons': len(polygons)},
    'shorts': shorts, 'opens': opens, 'unexpectedNcConnections': nc_connections,
    'mixedNetPolygons': [{'layer': next(layer for layer, ids in layers.items() if i in ids), 'boundsMm': list(polygons[i].bounds), 'terminals': ts} for i, ts in per_polygon.items() if len(set(t['net'] for t in ts if t['net'])) > 1],
    'missingTerminalCopper': missing, 'ambiguousTerminalCopper': ambiguous, 'traceCopperFailures': trace_failures,
    'passed': not any([shorts, opens, nc_connections, missing, ambiguous, trace_failures]),
    'sha256': {**input_hashes, 'current-Gerber.zip': hashlib.sha256(ZIP.read_bytes()).hexdigest(), 'current-snapshot.json': hashlib.sha256((BASE/'current-snapshot.json').read_bytes()).hexdigest()}
}
(BASE / 'copper-connectivity-review.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({k: v for k, v in report.items() if k not in ['sha256', 'method', 'limitations']}, indent=2))
if not report['passed']:
    raise SystemExit(1)
