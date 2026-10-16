"""Package AI-generated masters; only resize/format conversion and review layout.
No painting, synthesis, cropping or changes to original master bytes."""
from __future__ import annotations
import argparse
import hashlib
import json
import shutil
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT.parent / "StreetV3"
LABELS = ["Undeveloped", "1 house", "2 houses", "3 houses", "4 houses", "Hotel"]
COLORS = dict(A="#C97852",B="#63C6E8",C="#D970AD",D="#EE9B47",E="#E85D63",F="#E7C64B",G="#54BB78",H="#5F80D8")
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def rgb565(image):
    result = bytearray()
    for r,g,b in image.convert("RGB").get_flattened_data():
        value = ((r&248)<<8)|((g&252)<<3)|(b>>3)
        result.extend((value&255,value>>8))
    return bytes(result)
def font(size,bold=False):
    candidates = [Path("C:/Windows/Fonts")/("arialbd.ttf" if bold else "arial.ttf"),
                  Path("/usr/share/fonts/truetype/dejavu")/("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf")]
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default(size=size)
def sheets(properties,records,size=160):
    indexed = {(r["key"],r["level"]):r for r in records}
    pitch = size + 32
    row_height = size + 42
    image_kind = "tile_png" if size == 160 else "player_png"
    for group,color in COLORS.items():
        members = [p for p in properties if p["group"]==group]
        sheet = Image.new("RGB",(40+6*pitch,98+row_height*len(members)),"#101619")
        draw = ImageDraw.Draw(sheet)
        draw.text((24,17),f"GRIDOPOLY / DISTRICT {group} / {size}px",font=font(24,True),fill=color)
        for level,label in enumerate(LABELS):
            draw.text((24+pitch*level,59),label,font=font(15),fill="#D2DFE1")
        for row,p in enumerate(members):
            y = 91+row*row_height
            for level in range(6):
                x = 24+level*pitch
                record = indexed.get((p["key"],level))
                if record:
                    with Image.open(ROOT/record["files"][image_kind]["path"]) as im:
                        sheet.paste(im,(x,y))
                else:
                    draw.rectangle((x,y,x+size-1,y+size-1),outline="#53616A")
                    draw.text((x+10,y+70),"PENDING",font=font(14),fill="#A9B5BA")
            draw.text((24,y+size+6),p["name"],font=font(16,True),fill=color)
        suffix = "" if size == 160 else "-128"
        sheet.save(ROOT/"review"/f"district-{group.lower()}{suffix}.png",optimize=True)
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--allow-partial",action="store_true")
    args=parser.parse_args()
    plan=json.loads((ROOT/"generation-plan.json").read_text(encoding="utf-8"))
    generated=json.loads((ROOT/"generation-records.json").read_text(encoding="utf-8"))
    selected={(r["key"],r["level"]):r for r in generated}
    properties=plan["properties"]
    base_catalog=json.loads((BASE/"manifests"/"grid-city-street-assets-v3.json").read_text(encoding="utf-8"))
    tile_ids={p["key"]:p["tile_id"] for p in base_catalog["assets"] if p["kind"]=="property"}
    if {p["key"] for p in properties}!=set(tile_ids):
        raise SystemExit("Development catalog must cover exactly all buildable StreetV3 properties")
    for name in ("source","device","player","rgb565-tile","rgb565-player","review","manifests"):
        (ROOT/name).mkdir(parents=True,exist_ok=True)
    expected={(p["key"],l) for p in properties for l in range(1,6)}
    unexpected=set(selected)-expected
    if unexpected:
        raise SystemExit(f"Unexpected generated states: {sorted(unexpected)}")
    missing=sorted(expected-set(selected))
    if missing and not args.allow_partial:
        raise SystemExit(f"Missing {len(missing)}/110 new states; refusing complete manifest")
    records=[]
    for p in properties:
        for level in range(6):
            if level>0 and (p["key"],level) not in selected:
                continue
            src=BASE/"source"/f'{p["key"]}.png' if level==0 else Path(selected[(p["key"],level)]["path"])
            if not src.is_absolute():
                src=ROOT/src
            master=ROOT/"source"/f'{p["key"]}-l{level}.png'
            if src.resolve()!=master.resolve() and (not master.exists() or sha(src)!=sha(master)):
                shutil.copyfile(src,master)
            with Image.open(master) as original:
                original.load()
                if original.width!=original.height or original.width<1024:
                    raise ValueError(f"Unexpected master size: {master} {original.size}")
                im=original.convert("RGB")
                dimensions=list(original.size)
            tile=im.resize((160,160),Image.Resampling.LANCZOS)
            player=im.resize((128,128),Image.Resampling.LANCZOS)
            outputs=dict(master=master,tile_png=ROOT/"device"/master.name,player_png=ROOT/"player"/master.name,
                         tile_rgb565=ROOT/"rgb565-tile"/f"{master.stem}.rgb565",
                         player_rgb565=ROOT/"rgb565-player"/f"{master.stem}.rgb565")
            tile.save(outputs["tile_png"],optimize=True)
            player.save(outputs["player_png"],optimize=True)
            outputs["tile_rgb565"].write_bytes(rgb565(tile))
            outputs["player_rgb565"].write_bytes(rgb565(player))
            records.append(dict(key=p["key"],tile_id=tile_ids[p["key"]],name=p["name"],group=p["group"],level=level,
                                house_count=level if level<5 else 0,hotel_count=int(level==5),
                                source_dimensions=dimensions,
                                files={kind:dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,sha256=sha(path))
                                       for kind,path in outputs.items()}))
    manifest=dict(schema_version=1,catalog_id=plan["catalog_id"],
                  status="complete-assets-not-deployed" if not missing else "partial-generation",
                  property_count=len(properties),expected_state_count=132,state_count=len(records),
                  new_state_count=len(selected),missing=[dict(key=k,level=l) for k,l in missing],
                  formats=dict(tile_png=[160,160],player_png=[128,128],tile_rgb565="160x160 RGB565 LE; 51200 bytes",
                               player_rgb565="128x128 RGB565 LE; 32768 bytes"),
                  runtime_integrated=False,assets=records)
    (ROOT/"manifests"/"development-v1.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    (ROOT/"review"/"catalog.js").write_text("window.GRIDOPOLY_ART="+json.dumps(manifest,ensure_ascii=False)+";\n",encoding="utf-8")
    sheets(properties,records)
    sheets(properties,records,128)
    print(json.dumps(dict(states=len(records),new=len(selected),missing=len(missing))))
if __name__=="__main__":
    main()
