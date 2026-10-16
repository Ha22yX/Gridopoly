"""Inventory all mechanical publication files without changing CAD, Git, or frozen ZIPs.

Run from any directory. Publication/upload is performed separately by the project
coordinator. Manifest hashes cover file bytes, not Git LFS pointer text.
"""
from pathlib import Path
from collections import Counter, defaultdict
from datetime import datetime, timezone
import hashlib
import json
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
MECH = ROOT / 'Mechanical'
GENERATED = {
    'Mechanical/public-model-inventory.json',
    'Mechanical/publish-paths.txt',
    'Mechanical/publish-ignored-paths.txt',
    'Mechanical/PUBLICATION.md',
}
MODEL_EXTENSIONS = {'.stl', '.3mf', '.blend'}


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def excluded_reason(path):
    if '__pycache__' in path.parts or path.suffix.lower() in {'.pyc', '.pyo'}:
        return 'runtime_cache'
    if path.name == '.gitattributes':
        return 'coordinator_managed_git_attributes_not_in_this_staging_list'
    if path.suffix.lower() == '.blend1':
        return 'automatic_recovery_backup; corresponding final Blender design is included'
    if path.name.endswith('_Working.blend'):
        return 'intermediate_checkpoint; corresponding final Blender design is included'
    return None


def purpose(path):
    relative = path.relative_to(ROOT).as_posix()
    if '/sources/hi3d-originals/' in relative:
        return 'original_input', 'original_not_ready_to_print'
    if path.suffix.lower() in MODEL_EXTENSIONS:
        role = 'editable_design' if path.suffix.lower() == '.blend' else 'print_model'
        if 'Gridopoly_BoardBase_Full16_R1/' in relative:
            return role, 'current_full16_candidate_physical_fit_pending'
        if 'Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY/' in relative:
            return role, 'fitted_three_module_reference'
        if 'Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED/' in relative:
            return role, 'current_player_R3_candidate_physical_fit_pending'
        if 'PlayerTokens/models/' in relative:
            return role, 'current_30mm_tokens_print_validation_pending'
        if 'CW47_5' in path.name and 'PlayerConsoleStand/outputs/models/' in relative:
            return role, 'fitted_screen_interface_reference'
        return role, 'historical_revision_not_default_print_choice'
    if path.suffix.lower() == '.zip':
        return 'frozen_bundle', 'historical_frozen_bytes_preserved'
    if path.suffix.lower() in {'.png', '.jpg', '.svg'}:
        return 'reference_or_preview', 'supporting_material'
    if path.suffix.lower() == '.pdf':
        return 'assembly_guide' if 'Assembly_Guide' in path.name else 'reference_document', 'supporting_material'
    if path.suffix.lower() == '.py':
        return 'source_or_validation_script', 'supporting_material'
    if path.suffix.lower() in {'.md', '.txt'}:
        return 'documentation', 'supporting_material'
    return 'parameters_or_evidence', 'supporting_material'


def main():
    records, excluded = [], []
    files = sorted(p for p in MECH.rglob('*') if p.is_file())
    for path in files:
        relative = path.relative_to(ROOT).as_posix()
        if relative in GENERATED:
            continue
        reason = excluded_reason(path)
        if reason:
            record = {'path': relative, 'bytes': path.stat().st_size, 'reason': reason}
            if path.suffix.lower() in {'.blend', '.blend1'}:
                record['sha256'] = digest(path)
            excluded.append(record)
            continue
        role, status = purpose(path)
        records.append({'path': relative, 'bytes': path.stat().st_size,
                        'sha256': digest(path), 'role': role, 'revision_status': status})
    # The Docs entry changed as part of this publication; the root README is
    # deliberately left to the coordinating task.
    docs = ROOT / 'Docs/mechanical/README.md'
    records.append({'path': docs.relative_to(ROOT).as_posix(), 'bytes': docs.stat().st_size,
                    'sha256': digest(docs), 'role': 'documentation', 'revision_status': 'supporting_material'})
    records.sort(key=lambda row: row['path'])
    models = [r for r in records if Path(r['path']).suffix.lower() in MODEL_EXTENSIONS]
    print_files = [r for r in models if Path(r['path']).suffix.lower() in {'.stl', '.3mf'}]
    actual_print_files = {p.relative_to(ROOT).as_posix() for p in files if p.suffix.lower() in {'.stl', '.3mf'}}
    assert {r['path'] for r in print_files} == actual_print_files, 'A printable/source model is missing'
    by_hash = defaultdict(list)
    for record in records:
        if record['role'] in {'print_model', 'editable_design', 'original_input', 'frozen_bundle'}:
            by_hash[record['sha256']].append(record['path'])
    duplicates = [{'sha256': sha, 'paths': paths} for sha, paths in by_hash.items() if len(paths) > 1]
    archives = []
    for record in records:
        if record['role'] != 'frozen_bundle':
            continue
        with zipfile.ZipFile(ROOT / record['path']) as archive:
            bad = archive.testzip()
            assert bad is None, (record['path'], bad)
            archives.append({'path': record['path'], 'entries': len(archive.namelist()), 'crc': 'PASS',
                             'backups_inside_frozen_bundle': [n for n in archive.namelist() if n.endswith(('.blend1', '_Working.blend'))]})
    old = json.loads((MECH / 'model-assets-inventory.json').read_text(encoding='utf-8'))
    current = {r['path']: r.get('sha256') for r in records + excluded}
    for item in old['entries']:
        assert current[item['project_path']] == item['sha256'], ('Frozen archive bytes changed', item['project_path'])
    paths = sorted([r['path'] for r in records] + list(GENERATED))
    # Read ignore rules only. --no-index also reports ignored formal outputs
    # already tracked by a coordinator, without consulting or changing index.
    check = subprocess.run(['git', '-c', 'safe.directory=*', 'check-ignore', '--no-index', '-z', '--stdin'],
                           cwd=ROOT, input=('\0'.join(paths) + '\0').encode('utf-8'), capture_output=True)
    if check.returncode not in (0, 1):
        raise RuntimeError(check.stderr.decode('utf-8', errors='replace'))
    ignored = sorted(x for x in check.stdout.decode('utf-8').split('\0') if x)
    summary = {'payload_files': len(records), 'payload_bytes': sum(r['bytes'] for r in records),
               'model_files': len(models), 'stl_files': sum(r['path'].endswith('.stl') for r in print_files),
               'three_mf_files': sum(r['path'].endswith('.3mf') for r in print_files),
               'original_input_files': sum(r['role'] == 'original_input' for r in records),
               'editable_blend_files': sum(r['role'] == 'editable_design' for r in records),
               'frozen_zip_files': len(archives), 'ignored_formal_files': len(ignored),
               'staging_paths': len(paths), 'archive_baseline_files_sha256_unchanged': len(old['entries'])}
    inventory = {'schema_version': 1, 'generated_utc': datetime.now(timezone.utc).isoformat(),
                 'publication_status': 'PREPARED_PENDING_COORDINATOR_UPLOAD_AND_REMOTE_VERIFICATION',
                 'summary': summary, 'files': records, 'same_bytes_groups': duplicates,
                 'excluded_files': excluded, 'zip_crc_checks': archives,
                 'generated_metadata_files': sorted(GENERATED),
                 'ignored_formal_paths': ignored,
                 'limits': ['No CAD rebuild, geometry edits, repackaging, license changes or Git writes.',
                            'Byte and CRC verification does not constitute physical print/assembly validation.',
                            'Generated metadata excludes its own hashes to avoid recursive checksums.',
                            'Root README, LFS configuration, commits, upload and remote verification belong to the coordinator.']}
    (MECH / 'public-model-inventory.json').write_text(json.dumps(inventory, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (MECH / 'publish-paths.txt').write_text('\n'.join(paths) + '\n', encoding='utf-8')
    (MECH / 'publish-ignored-paths.txt').write_text('\n'.join(ignored) + '\n', encoding='utf-8')
    lines = ['# 3D 模型完整公开清单', '', '更新：2026-10-16。[当前优先打印与下载方法](README.md)。', '',
             f"本清单覆盖 {summary['stl_files']} 个 STL、{summary['three_mf_files']} 个 3MF（其中 {summary['original_input_files']} 个为原始输入），以及 {summary['editable_blend_files']} 个正式 Blender 工程。",
             '当前版、历史版和原始输入分别标注；不通过重新建模或重新打包删减历史内容。此页是发布范围清单，远端上传状态由项目主会话核验。', '',
             '[逐文件大小及 SHA256](public-model-inventory.json) · [完整待提交路径](publish-paths.txt) · [被忽略但需纳入的正式文件](publish-ignored-paths.txt)', '',
             '## 模型文件', '', '| 文件 | MiB | 用途 / 版本状态 |', '| --- | ---: | --- |']
    for row in models:
        rel = row['path'].removeprefix('Mechanical/')
        lines.append(f"| [{rel}]({rel.replace('(', '%28').replace(')', '%29')}?raw=true) | {row['bytes'] / 1048576:.3f} | {row['role']} / {row['revision_status']} |")
    lines += ['', '## 冻结包与配套资料', '', '下列 ZIP 保持原始字节，其内容可能与单独模型重复；已检查压缩包 CRC。', '', '| 包 | MiB |', '| --- | ---: |']
    for row in records:
        if row['role'] == 'frozen_bundle':
            rel = row['path'].removeprefix('Mechanical/')
            lines.append(f"| [{rel}]({rel}?raw=true) | {row['bytes'] / 1048576:.3f} |")
    lines += ['', 'PDF、预览、参数、重建脚本、原始接口证据和检查报告也在机器清单内。', '',
              '## 保留在本地的恢复材料', '',
              '6 个 `.blend1` 自动备份、2 个 `_Working.blend` 中间检查点和 Python 缓存不进入本次模型发布；对应正式 Blender 工程及全部 STL/3MF 已列入。忽略规则或 Git 属性由主会话管理。', '',
              '## 验证范围', '',
              f"对比上一轮归档，{len(old['entries'])} 个文件的 SHA256 全部未变，所有现存 STL/3MF 都出现在清单。",
              '尚未上传/下载验收的状态不等于公开失败；主会话负责提交、LFS 传输和从 GitHub 取回后的字节核对。模型公开不增加新的实物打印、配合或承载证据。', '']
    (MECH / 'PUBLICATION.md').write_text('\n'.join(lines), encoding='utf-8')
    print(json.dumps(summary, ensure_ascii=False, indent=2), flush=True)


if __name__ == '__main__':
    main()
