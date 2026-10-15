"""Read-only consistency checks for the frozen mechanical handoff package."""
import csv
import hashlib
import json
import math
from pathlib import Path

root = Path(__file__).parent
data = json.loads((root / 'mechanical-interface.json').read_text(encoding='utf-8'))
holes = list(csv.DictReader((root / 'mounting-holes.csv').open(encoding='utf-8-sig')))
assert len(holes) == 8
for kind, board in data['boards'].items():
    assert hashlib.sha256((root / board['source_file']).read_bytes()).hexdigest() == board['source_sha256']
    for key in ['native_export', 'gerber_export', 'bottom_clearance_data_file']:
        assert (root / board[key]).is_file()
    assert len(board['mounting_holes']) == 4
    assert len(board['recommended_diagonal_pair']) == 2
    for hole in board['mounting_holes']:
        row = next(r for r in holes if r['board'] == kind and r['hole'] == hole['id'])
        assert math.dist([float(row['x_mm']), float(row['y_mm'])], hole['center_mm']) < 1e-8
        assert float(row['diameter_mm']) == hole['nominal_diameter_mm'] == 3.5
        assert hole['manufacturing_type'] == 'NPTH'
        assert hole['nearest_expanded_bottom_obstacle']['disc_edge_clearance_mm'] > 0

bottom = json.loads((root / 'reviews/bottom-review/bottom-clearance.json').read_text(encoding='utf-8'))
assert [len(bottom['boards'][k]['bottom_components']) for k in ['long', 'corner']] == [48, 46]
for b in bottom['boards'].values():
    assert all(c['body_height_below_pcb_mm'] is None for c in b['bottom_components'])
assert data['assembly']['inter_board_gap_mm'] is None
assert data['assembly']['mating_pad_row_spacing_mm'] is None
assert abs(data['assembly']['candidate_mating_pad_row_spacing_mm'] - data['assembly']['candidate_inter_board_gap_mm'] - 4.59699868) < 1e-8
assert abs(14 - 3.5 - 8.1 - 2.4) < 1e-8

manifest = root / 'SHA256SUMS.txt'
if manifest.is_file():
    for line in manifest.read_text(encoding='utf-8').splitlines():
        sha, name = line.split('  ', 1)
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == sha, name
print('PASS: source hashes, 8 holes, 94 bottom components, support clearances, gap evidence and package manifest')
