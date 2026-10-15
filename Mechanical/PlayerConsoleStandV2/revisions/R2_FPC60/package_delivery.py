"""Package only the current modular R2, after independent checks pass."""
from pathlib import Path
import json,hashlib,shutil,zipfile
ROOT=Path(__file__).resolve().parent
OUT=Path('C:/Users/kicof/Downloads/Gridopoly_Player_Stand_V2_R2_FPC60')
parts=json.loads((OUT/'parts-verification.json').read_text(encoding='utf-8'))
assembly=json.loads((OUT/'assembly-verification.json').read_text(encoding='utf-8'))
assert parts['pass']
assert assembly['status']=='MODEL_CHECKS_PASS' and not assembly['collisions']
stems=['01_Connection_Plate_R2','02_Screen_Support_CW47_5_R2','03_Battery_Cradle_R2','04_Battery_Lid_R2','05_Spacer_6mm_R2']
files=[x+ext for x in stems for ext in ('.stl','.3mf')]
files+=['Gridopoly_Player_Stand_V2_R2_FPC60.blend','preview-assembly.png','preview-battery-exploded.png','preview-fpc-route.png','preview-print-parts.png','README.md','parameters.json','parts-verification.json','assembly-verification.json']
shutil.copy2(ROOT/'README.md',OUT/'README.md')
shutil.copy2(OUT/'assembly-verification.json',ROOT/'outputs/assembly-verification.json')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for part in parts['parts']:
    assert digest(OUT/part['file'])==part['sha256'],part['file']
manifest={'revision':'V2 R2 FPC60 MODULAR','date':'2026-10-15','status':'CAD/mesh/assembly-envelope checks passed; slicing and physical fit pending',
 'quantity':{stems[0]:1,stems[1]:1,stems[2]:1,stems[3]:1,stems[4]:4},
 'user_cable_length_mm':60,'battery_finished_pack_mm':[56,34.5,10.3],
 'files':[{'file':f,'bytes':(OUT/f).stat().st_size,'sha256':digest(OUT/f)} for f in files]}
(OUT/'delivery-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
zip_path=OUT.with_suffix('.zip')
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED) as z:
    for f in files+['delivery-manifest.json']:z.write(OUT/f,arcname=OUT.name+'/'+f)
with zipfile.ZipFile(zip_path) as z:
    assert z.testzip() is None
    assert len(z.namelist())==len(files)+1
manifest['archive']={'file':str(zip_path),'bytes':zip_path.stat().st_size,'sha256':digest(zip_path),'crc_checked':True}
(ROOT/'outputs/delivery-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(json.dumps({'archive':manifest['archive'],'files':len(files)+1,'solid_pairs':len(assembly['solid_pairs']),'fpc_pairs':len(assembly['fpc_envelope_pairs']),'wire_pairs':len(assembly['battery_wire_pairs'])},indent=2))
