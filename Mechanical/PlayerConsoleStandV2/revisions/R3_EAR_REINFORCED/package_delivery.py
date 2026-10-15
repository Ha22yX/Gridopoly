"""Package only verified final R3 assets, preserving unchanged R2 geometry."""
from pathlib import Path
import json,hashlib,shutil,zipfile
HERE=Path(__file__).resolve().parent
OUT=Path('C:/Users/kicof/Downloads/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED')
REPORTS=HERE/'outputs';REPORTS.mkdir(exist_ok=True)
parts=json.loads((OUT/'parts-verification.json').read_text(encoding='utf-8'))
assembly=json.loads((OUT/'assembly-verification.json').read_text(encoding='utf-8'))
roots=json.loads((REPORTS/'ear-root-verification.json').read_text(encoding='utf-8'))
assert parts['pass']
assert assembly['status']=='MODEL_CHECKS_PASS' and not assembly['collisions']
assert roots['geometry_checks_pass']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert roots['sources']['R3']['sha256']==digest(OUT/'02_Screen_Support_CW47_5_R3.stl')
for part in parts['parts']:assert digest(OUT/part['file'])==part['sha256'],part['file']
stems=['01_Connection_Plate_R2','02_Screen_Support_CW47_5_R3','03_Battery_Cradle_R2','04_Battery_Lid_R2','05_Spacer_6mm_R2']
files=[stem+ext for stem in stems for ext in ('.stl','.3mf')]
files+=['Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED.blend','preview-assembly.png','preview-support-R3.png',
        'preview-left-foot-R2-before.png','preview-left-foot-R3-after.png','preview-support-underside-R3.png',
        'README.md','Assembly_Instructions_R2.md','parameters.json','parts-verification.json',
        'assembly-verification.json','ear-root-verification.json']
shutil.copy2(HERE/'README.md',OUT/'README.md')
instructions=(HERE.parent/'R2_FPC60/README.md').read_text(encoding='utf-8')
instructions=instructions.replace('../R3_EAR_REINFORCED/README.md','README.md')
(OUT/'Assembly_Instructions_R2.md').write_text(instructions,encoding='utf-8')
config=json.loads((HERE/'parameters.json').read_text(encoding='utf-8'))
config['status']='MODEL_CHECKS_PASS_PHYSICAL_FIT_PENDING'
(HERE/'parameters.json').write_text(json.dumps(config,indent=2),encoding='utf-8')
shutil.copy2(HERE/'parameters.json',OUT/'parameters.json')
shutil.copy2(REPORTS/'ear-root-verification.json',OUT/'ear-root-verification.json')
shutil.copy2(OUT/'assembly-verification.json',REPORTS/'assembly-verification.json')
manifest={'revision':'V2 R3 EAR REINFORCED','date':'2026-10-15','replace_only':stems[1],
 'status':'STL, interface, root-section and assembly-envelope checks passed; slicing, print and physical fit pending',
 'quantities':{stem:(4 if stem.startswith('05') else 1) for stem in stems},
 'files':[{'file':f,'bytes':(OUT/f).stat().st_size,'sha256':digest(OUT/f)} for f in files]}
(OUT/'delivery-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
zip_path=OUT.with_suffix('.zip')
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED) as z:
    for f in files+['delivery-manifest.json']:z.write(OUT/f,arcname=OUT.name+'/'+f)
with zipfile.ZipFile(zip_path) as z:
    assert z.testzip() is None
    assert len(z.namelist())==len(files)+1
manifest['archive']={'file':str(zip_path),'bytes':zip_path.stat().st_size,'sha256':digest(zip_path),'crc_checked':True}
(REPORTS/'delivery-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(json.dumps({'archive':manifest['archive'],'files':len(files)+1},indent=2))
