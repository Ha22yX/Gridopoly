"""Copy known Gridopoly model assets into the project; verify bytes, never delete originals."""
from pathlib import Path
import hashlib,json,shutil,datetime

ROOT=Path(__file__).resolve().parents[2]
DOWNLOADS=Path('C:/Users/kicof/Downloads')
DIRECTORIES={
    'Gridopoly_BoardBase_Test_R0':'Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Test_R0',
    'Gridopoly_BoardBase_Test_R1_ONE_PIECE':'Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Test_R1_ONE_PIECE',
    'Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY':'Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Test_R2_PRINT_FRIENDLY',
    'Gridopoly_BoardBase_Full16_R1':'Mechanical/BoardBase/deliverables/Gridopoly_BoardBase_Full16_R1',
    'Gridopoly_Player_Stand_V2_R1':'Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R1',
    'Gridopoly_Player_Stand_V2_R2_FPC60':'Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R2_FPC60',
    'Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED':'Mechanical/PlayerConsoleStandV2/deliverables/Gridopoly_Player_Stand_V2_R3_EAR_REINFORCED',
    'Hi3D_30mm':'Mechanical/PlayerTokens/models/hi3d-30mm/deliverables/Hi3D_30mm',
    'Gridopoly_30mm_Remaining':'Mechanical/PlayerTokens/models/hi3d-remaining-30mm/deliverables/Gridopoly_30mm_Remaining',
}

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(8*1024*1024),b''):h.update(block)
    return h.hexdigest()

def files_to_copy():
    for name,destination in DIRECTORIES.items():
        source=DOWNLOADS/name
        assert source.is_dir(),source
        for p in sorted(source.rglob('*')):
            if p.is_file():yield p,ROOT/destination/p.relative_to(source)
        archive=DOWNLOADS/(name+'.zip')
        if archive.is_file():yield archive,(ROOT/destination).parent/archive.name
    for name in ['Hi3D.3mf','Hi3D(2).3mf','Hi3D(3).3mf','Hi3D(4).3mf','Hi3D(5).3mf']:
        yield DOWNLOADS/name,ROOT/'Mechanical/PlayerTokens/sources/hi3d-originals'/name
    name='Gridopoly_Player_Stand_R3_Assembly_Guide_CN.pdf'
    yield DOWNLOADS/name,ROOT/'Mechanical/PlayerConsoleStandV2/revisions/R3_EAR_REINFORCED/assembly_guide'/name
    photos=Path('C:/Users/kicof/.codex/codex-remote-attachments/01a0f687-eeae-7d61-bbbb-cbb5a98a7b33/890BC5E9-ACBC-4AC0-99E8-13FF07F7B8C9')
    for n in (1,2,3):
        name=f'{n}-Photo-{n}.jpg'
        yield photos/name,ROOT/'Mechanical/BoardBase/references/fit-photos-2026-10-15'/name

def main():
    plan=list(files_to_copy());entries=[]
    assert len({str(d) for _,d in plan})==len(plan),'Duplicate destination'
    report=ROOT/'Mechanical/model-assets-inventory.json'
    for i,(source,dest) in enumerate(plan,1):
        assert source.is_file(),source
        dest.resolve().relative_to(ROOT.resolve())
        expected=sha(source)
        if dest.exists():
            assert sha(dest)==expected,f'Existing target differs; preserving both: {dest}'
            action='already_identical'
        else:
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,dest);action='copied'
        actual=sha(dest)
        assert actual==expected and dest.stat().st_size==source.stat().st_size,dest
        entries.append({'source':str(source),'project_path':dest.relative_to(ROOT).as_posix(),
                        'bytes':dest.stat().st_size,'sha256':actual,'action':action})
        if i%10==0 or i==len(plan):print(f'VERIFIED {i}/{len(plan)} {dest.name}',flush=True)
    models=[p for p in (ROOT/'Mechanical').rglob('*') if p.is_file() and p.suffix.lower() in {'.blend','.blend1','.stl','.3mf','.step','.stp','.obj','.glb','.scad','.fcstd'}]
    inventory={'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'project_root':'Z:/Files/我的项目/Gridopoly','status':'ALL_COPIED_BYTES_VERIFIED',
               'source_files_preserved':True,'source_and_target_sha256_verified':True,
               'copied_file_count':len(entries),'total_bytes':sum(e['bytes'] for e in entries),
               'project_model_file_count':len(models),'entries':entries,
               'existing_models':[p.relative_to(ROOT).as_posix() for p in models],
               'notes':['Historical manifests retain their original paths as provenance.',
                        'Blender .blend1 files are retained for recovery, not designated printing releases.',
                        'No geometry, frozen ZIP, electrical design or physical-validation status was changed.']}
    report.write_text(json.dumps(inventory,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in inventory.items() if k not in ('entries','existing_models','notes')},ensure_ascii=True),flush=True)

if __name__=='__main__':main()
