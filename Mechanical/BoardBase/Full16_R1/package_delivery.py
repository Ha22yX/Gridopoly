"""Package only reviewed print parts, previews, documentation and evidence."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile

ROOT=Path(__file__).resolve().parent
OUT=Path('C:/Users/kicof/Downloads/Gridopoly_BoardBase_Full16_R1')
ZIP=OUT.with_suffix('.zip')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    for name in ('README.md','config.json','layout-audit.json','assembly-verification.json','parts-verification.json'):
        shutil.copyfile(ROOT/name,OUT/name)
    assert json.loads((OUT/'parts-verification.json').read_text())['status']=='PASS'
    assert json.loads((OUT/'layout-audit.json').read_text())['status']=='PASS'
    ar=json.loads((OUT/'assembly-verification.json').read_text())
    assert all(not row['overlaps'] for row in ar['checks'])
    names=['01_L_Quarter_PRINT_4.stl','01_L_Quarter_PRINT_4.3mf',
        '02_Splice_Bridge_PRINT_8.stl','02_Splice_Bridge_PRINT_8.3mf',
        'Gridopoly_Full16_R1.blend','rebuild_in_blender.py',
        '01_full_frame.png','02_with_16_pcbs_reference.png','03_top_layout.png',
        '04_single_quarter_print_4.png','05_joint_detail.png',
        'README.md','config.json','layout-audit.json','assembly-verification.json','parts-verification.json']
    manifest={'revision':'FULL16_R1','status':'Digital geometry passed; full-ring print, fit and mechanical strength pending',
        'print_quantities':{'01_L_Quarter_PRINT_4':4,'02_Splice_Bridge_PRINT_8':8},
        'source_interface_sha256':json.loads((ROOT/'config.json').read_text())['interface_sha256'],
        'files':{name:{'bytes':(OUT/name).stat().st_size,'sha256':sha(OUT/name)} for name in names}}
    for dest in (ROOT,OUT):
        (dest/'delivery-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    with zipfile.ZipFile(ZIP,'w',zipfile.ZIP_DEFLATED) as z:
        for name in names+['delivery-manifest.json']:
            z.write(OUT/name,name)
    with zipfile.ZipFile(ZIP) as z:
        assert z.testzip() is None
        assert set(z.namelist())==set(names+['delivery-manifest.json'])
        for name in names:
            assert hashlib.sha256(z.read(name)).hexdigest()==manifest['files'][name]['sha256']
    package={'zip_path':str(ZIP),'zip_sha256':sha(ZIP),'zip_bytes':ZIP.stat().st_size,
        'zip_crc_verified':True,'all_manifest_file_hashes_verified':True,'manifest_file_count':len(names),
        'includes_backup_blends':False,'source_files_uncommitted_by_this_task':True}
    (ROOT/'delivery-package.json').write_text(json.dumps(package,indent=2),encoding='utf-8')
    print(json.dumps(package))


if __name__=='__main__': main()
