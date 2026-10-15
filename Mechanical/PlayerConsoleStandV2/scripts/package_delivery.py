"""Package only verified delivery artifacts, excluding Blender backup copies."""
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=Path('C:/Users/kicof/Downloads/Gridopoly_Player_Stand_V2_R1')
shutil.copy2(ROOT/'README.md',OUT/'README.md')
for name in ('verification-report.json','assembly-verification.json'):
    (ROOT/'outputs').mkdir(exist_ok=True)
    shutil.copy2(OUT/name,ROOT/'outputs'/name)

names=['README.md','design-parameters.json','verification-report.json','assembly-verification.json',
       'Gridopoly_Player_Stand_V2_R1.blend',
       '01_Assembly.png','02_Open_Battery.png','03_Tray_Access.png','04_PCB_Access.png']
for stem in ('01_Upper_CW47_5','02_Battery_Tray','03_Spacer_8mm','03_Spacer_12mm'):
    names.extend([stem+'.stl',stem+'.3mf'])
manifest={'revision':'Player Stand V2 R1 first fit','legacy_interface_user_fit_passed':True,
          'new_assembly_printed_or_fitted':False,'fpc_actual_length_confirmed':False,
          'files':{n:hashlib.sha256((OUT/n).read_bytes()).hexdigest() for n in names}}
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
archive=OUT.with_suffix('.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for n in names+['manifest.json']:
        z.write(OUT/n,OUT.name+'/'+n)
    # Small interface records make source coordinate links reviewable offline.
    for n in ('README.md','mechanical-interface.json','mounting-holes.csv','interface-anchors.csv'):
        p=ROOT/'interfaces/player-power-2026-10-15'/n
        if p.exists(): z.write(p,OUT.name+'/interfaces/player-power-2026-10-15/'+n)
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert not any(n.endswith('.blend1') for n in z.namelist())
    for n,sha in manifest['files'].items():
        assert hashlib.sha256(z.read(OUT.name+'/'+n)).hexdigest()==sha
summary={'zip':str(archive),'bytes':archive.stat().st_size,
         'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
         'entry_count':len(z.namelist()),'crc_and_artifact_hashes_verified':True}
(ROOT/'outputs/delivery-manifest.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2))
