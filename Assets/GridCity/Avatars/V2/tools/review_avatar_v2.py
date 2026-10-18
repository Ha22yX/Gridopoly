"""Generate an offline, exact-compositor review page and compact QA matrices."""
from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from avatar_v2 import ROOT, CROP, FACE_NAMES, OUTFIT_NAMES, HAIR_NAMES, SKINS, HAIR_COLORS, compose, digest, decode_component


def load_components(root, report):
    result = {}
    for e in report["entries"]:
        decoded, _ = decode_component((root / "runtime/components-v2" / e["file"]).read_bytes())
        canvas = np.zeros((320, 320, 5), dtype=np.uint8)
        canvas[10:310, 50:270] = decoded
        result[(e["kind"], e["presetId"])] = (Image.fromarray(canvas[:, :, :4]), Image.fromarray(canvas[:, :, 4]))
    return result


def contact_sheet(root, components, rows, cols, filename, title, recipes):
    cell_w, cell_h = 142, 168
    sheet = Image.new("RGB", (cols * cell_w, rows * cell_h + 50), "#101a29")
    draw = ImageDraw.Draw(sheet)
    font_path = Path("C:/Windows/Fonts/segoeui.ttf")
    font = ImageFont.truetype(str(font_path), 13) if font_path.exists() else ImageFont.load_default()
    title_font = ImageFont.truetype(str(font_path), 20) if font_path.exists() else font
    draw.text((14, 14), title, fill="#bfeaff", font=title_font)
    rendered = 0
    for index, (label, recipe) in enumerate(recipes):
        x, y = (index % cols) * cell_w, (index // cols) * cell_h + 50
        try:
            image = compose(components, **recipe).resize((128, 128), Image.Resampling.BILINEAR)
            sheet.paste(image, (x + 7, y + 4), image)
            rendered += 1
        except KeyError:
            draw.rectangle((x + 7, y + 4, x + 135, y + 132), outline="#5b3740")
            draw.text((x + 30, y + 60), "MISSING ART", fill="#dd8898", font=font)
        draw.text((x + 8, y + 137), label, fill="#dae7f0", font=font)
    path = root / "review" / filename
    sheet.save(path)
    return {"file": filename, "rendered": rendered, "expected": len(recipes)}


def generate_review(root: Path, report: dict):
    components = load_components(root, report)
    outfits = sorted(n for kind, n in components if kind == "outfit")
    faces = sorted(n for kind, n in components if kind == "face")
    base_outfit = 1 if 1 in outfits else outfits[0] if outfits else 1
    base_face = 1 if 1 in faces else faces[0] if faces else 1
    (root / "review").mkdir(parents=True, exist_ok=True)
    for skin in ("warm", "ebony"):
        try:
            compose(components, hair=8, face=base_face, outfit=base_outfit, skin=skin).save(root / "review" / f"first-h08-f{base_face:02d}-o{base_outfit:02d}-{skin}.png")
        except KeyError:
            pass
    sheets = []
    sheets.append(contact_sheet(root, components, 10, 10, "hair-face-matrix.png", f"100 hair / face joins | O{base_outfit:02d} | warm | missing != passed", [(f"H{h:02d} / F{f:02d}", {"hair": h, "face": f, "outfit": base_outfit}) for h in range(1, 11) for f in range(1, 11)]))
    sheets.append(contact_sheet(root, components, 10, 10, "face-outfit-matrix.png", "100 face / shared neck / outfit joins | H01 | warm", [(f"F{f:02d} / O{o:02d}", {"hair": 1, "face": f, "outfit": o}) for f in range(1, 11) for o in range(1, 11)]))
    sheets.append(contact_sheet(root, components, 8, 10, "skin-face-matrix.png", f"8 skin materials x 10 faces | H01 / O{base_outfit:02d}", [(f"{s} / F{f:02d}", {"hair": 1, "face": f, "outfit": base_outfit, "skin": s}) for s in SKINS for f in range(1, 11)]))
    embedded = []
    for entry in report["entries"]:
        embedded.append({"kind": entry["kind"], "presetId": entry["presetId"], "name": entry["name"], "gavc": base64.b64encode((root / "runtime" / "components-v2" / entry["file"]).read_bytes()).decode(), "evidence": entry["registrationEvidence"], "skinMaterial": entry["skinMaterial"]})
    golden = None
    try:
        image = compose(components, hair=8, face=base_face, outfit=base_outfit, skin="ebony")
        golden = {"recipe": {"hair": 8, "face": base_face, "outfit": base_outfit, "neck": 1, "skin": "ebony", "hairColor": "graphite"}, "rgba320Sha256": digest(image.tobytes()), "rgba220x300Sha256": digest(image.crop(CROP).tobytes())}
        (root / "review" / "golden-composite.json").write_text(json.dumps(golden, indent=2) + "\n", encoding="utf-8")
    except KeyError:
        pass
    data = {"entries": embedded, "status": report["status"], "checks": report["checks"], "blocked": report["blocked"], "pendingVisualReview": report["pendingVisualReview"], "skins": SKINS, "hairColors": HAIR_COLORS, "baseOutfit": base_outfit, "baseFace": base_face, "golden": golden}
    html = PAGE.replace("__DATA__", json.dumps(data, ensure_ascii=False).replace("</", "<\\/"))
    (root / "review" / "index.html").write_text(html, encoding="utf-8")
    (root / "review" / "review-matrices.json").write_text(json.dumps(sheets, indent=2) + "\n", encoding="utf-8")


PAGE = r'''<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gridopoly · Avatar V2 组合评审</title>
<style>
:root{font:15px/1.55 system-ui,sans-serif;color:#dfebf7;background:#0a111b;color-scheme:dark}*{box-sizing:border-box}body{margin:0}main{max-width:1200px;margin:auto;padding:28px}h1{font-size:28px;margin:0 0 8px}h2{font-size:19px}p{color:#9fb2c7;margin:8px 0}.bar{display:flex;gap:10px;flex-wrap:wrap}.badge{background:#20324b;border:1px solid #3d536e;border-radius:99px;padding:4px 12px}.layout{display:grid;grid-template-columns:minmax(300px,400px) 1fr;gap:24px;margin-top:22px}.panel{border:1px solid #283b50;border-radius:15px;background:#111c2b;padding:20px}label{display:block;margin:12px 0 5px;color:#aabed4}select,button{font:inherit;padding:8px 12px;background:#1d3045;color:#eef7ff;border:1px solid #3e5772;border-radius:8px}select{width:100%}button{cursor:pointer}.previews{display:flex;align-items:center;gap:24px;flex-wrap:wrap;justify-content:center}.unit{text-align:center}.unit p{font-size:12px}canvas{display:block;background:#152237;image-rendering:auto}.circle{border-radius:50%;outline:1px solid #325169}.primary{outline:1px solid #325169}.big{width:220px;height:300px}.maskrow{display:flex;gap:12px;flex-wrap:wrap}.maskrow canvas{width:160px;height:160px;border:1px solid #324257}.note{padding:12px 14px;background:#282131;color:#e4c4df;border-left:3px solid #bd83b7;border-radius:5px}.links{display:flex;gap:12px;flex-wrap:wrap}a{color:#7cd5ed}pre{font-size:12px;white-space:pre-wrap;overflow-wrap:anywhere;color:#a7c1d7;max-height:400px;overflow:auto}details{margin-top:20px}.hidden{display:none}@media(max-width:800px){.layout{grid-template-columns:1fr}}
</style><main><h1>Avatar V2 · 组合评审</h1><p>统一头部轮廓、独立共用脖子、正面衣服和显式肤色材质。这里运行与设备一致的整数染色及四层合成。</p><div class="bar"><span id="state" class="badge"></span><span id="count" class="badge"></span><span class="badge">neck → face → outfit → hair</span></div>
<div class="layout"><section class="panel"><h2>自由搭配</h2><label>Hair style</label><select id="hair"></select><label>Face</label><select id="face"></select><label>Outfit · front view</label><select id="outfit"></select><label>Skin tone</label><select id="skin"></select><label>Hair color</label><select id="hairColor"></select><div class="bar" style="margin-top:16px"><button id="random">随机组合</button><button id="ebony">检查 Ebony 边缘</button></div><p>未生成的选项不替换成其他素材；缺失项会直接报出。1,000 个结构组合检查不等于全部视觉验收。</p></section>
<section class="panel"><div class="previews"><div class="unit"><canvas id="screen" width="220" height="300" class="primary big"></canvas><p>组件未缩放裁剪 · 220 × 300 原始像素</p></div><div class="unit"><canvas id="editor" width="224" height="270" class="primary"></canvas><p>编辑窗口模拟 · 224 × 270<br>zoom 230/256 · offset (−6,−12)</p></div><div class="unit"><canvas id="avatar128" width="128" height="128" class="circle"></canvas><p>确认头像 · 128 × 128</p><canvas id="avatar24" width="24" height="24" class="circle" style="margin:auto"></canvas><p>列表 · 24 × 24</p></div></div><p id="recipe"></p><p class="note" id="warning">正在载入组件与材质…</p><p id="goldenCheck"></p><p>整数合成直接解码 GAVC，无 PNG 半透明回读误差。缩放展示由浏览器完成，编辑窗口用于比例检查，不冒充 LVGL 实机截图。</p></section></div>
<section class="panel" style="margin-top:24px"><h2>肤色语义与接缝</h2><p>白色 mask 表示随肤色染色，黑色保留眼白、虹膜和深色轮廓。自动推断需要人工核对；不能把“像素一致”当作“画面自然”。</p><div class="maskrow"><div><canvas id="faceMask" width="320" height="320"></canvas><p>Face skin mask</p></div><div><canvas id="neckMask" width="320" height="320"></canvas><p>Shared neck mask</p></div><div><canvas id="skinOnly" width="320" height="320"></canvas><p>Face + neck without clothes / hair</p></div><div><canvas id="full" width="320" height="320"></canvas><p>320 × 320 registration</p></div></div></section>
<section class="panel" style="margin-top:24px"><h2>完整检查表</h2><div class="links"><a href="hair-face-matrix.png">100 种头发 × 脸型</a><a href="face-outfit-matrix.png">100 种脸型 × 衣服</a><a href="skin-face-matrix.png">80 种脸型 × 肤色</a><a href="build-report.json">构建与缺失报告</a></div><details><summary>当前组合的登记与材质依据</summary><pre id="evidence"></pre></details><details><summary>验收状态与未完成项</summary><pre id="report"></pre></details></section></main>
<script>
const D=__DATA__,items=new Map(),$=id=>document.getElementById(id),round=(n,d)=>Math.floor((n+Math.floor(d/2))/d);
const crcTable=Uint32Array.from({length:256},(_,n)=>{for(let k=0;k<8;k++)n=(n&1)?(0xedb88320^(n>>>1)):(n>>>1);return n>>>0});
function crc32(raw){let c=0xffffffff;for(let b of raw)c=crcTable[(c^b)&255]^(c>>>8);return(c^0xffffffff)>>>0}
function decodeGavc(base64){let s=atob(base64),b=Uint8Array.from(s,c=>c.charCodeAt(0)),v=new DataView(b.buffer);if(b.length<32||String.fromCharCode(...b.subarray(0,4))!=='GAVC'||b[4]!==2||b[7]!==2||v.getUint16(8,true)!==220||v.getUint16(10,true)!==300)throw Error('Invalid GAVC v2 header');let x=v.getUint16(12,true),y=v.getUint16(14,true),w=v.getUint16(16,true),h=v.getUint16(18,true),len=v.getUint32(20,true);if(!w||!h||x+w>220||y+h>300||len!==w*h*5||v.getUint32(24,true)!==b.length-32)throw Error('Invalid GAVC dimensions');let raw=new Uint8Array(len),p=32,d=0;while(p<b.length){if(p+2>b.length)throw Error('Truncated GAVC token');let t=v.getUint16(p,true),n=t&32767;p+=2;if(!n||d+n*5>len)throw Error('Invalid GAVC run');let size=t&32768?5:n*5;if(p+size>b.length)throw Error('Truncated GAVC samples');if(t&32768){for(let k=0;k<n;k++)raw.set(b.subarray(p,p+5),d+k*5)}else raw.set(b.subarray(p,p+size),d);p+=size;d+=n*5}if(d!==len||crc32(raw)!==v.getUint32(28,true))throw Error('GAVC CRC mismatch');let rgba=new Uint8ClampedArray(320*320*4),mask=new Uint8Array(320*320);for(let row=0;row<h;row++)for(let col=0;col<w;col++){let src=(row*w+col)*5,dst=(y+10+row)*320+x+50+col;rgba.set(raw.subarray(src,src+4),dst*4);mask[dst]=raw[src+4]}return{rgba,mask}}
function tint(e,target){let out=new Uint8ClampedArray(e.rgba);for(let i=0;i<out.length;i+=4){if(e.kind==='face'||e.kind==='neck'){let m=e.mask[i/4],shade=out[i];for(let c=0;c<3;c++){let v=Math.min(255,round(target[c]*shade,178));out[i+c]=round(v*m+out[i+c]*(255-m),255)}}else if(e.kind==='hair'){let w=54*out[i]+183*out[i+1]+19*out[i+2];if(out[i+3]&&w>=6528)for(let c=0;c<3;c++)out[i+c]=Math.min(255,round(target[c]*(7*65280+21*w),1305600))}}return out}
function over(dst,src){for(let i=0;i<dst.length;i+=4){let sa=src[i+3];if(!sa)continue;if(sa===255){dst.set(src.subarray(i,i+4),i);continue}let da=dst[i+3],iv=255-sa,oa=sa+round(da*iv,255);for(let c=0;c<3;c++)dst[i+c]=Math.min(255,round(src[i+c]*sa+round(dst[i+c]*da*iv,255),oa));dst[i+3]=oa}return dst}
function put(id,pixels){$(id).getContext('2d').putImageData(new ImageData(pixels,320,320),0,0)}
function maskRgba(mask){let out=new Uint8ClampedArray(320*320*4);for(let i=0;i<mask.length;i++)out.set([mask[i],mask[i],mask[i],255],i*4);return out}
function composite(ids,skin,hairColor){let dst=new Uint8ClampedArray(320*320*4);for(let kind of ['neck','face','outfit','hair']){let e=items.get(kind+'/'+ids[kind]);if(!e)throw Error('缺少 '+kind+'/'+ids[kind]);over(dst,tint(e,kind==='hair'?D.hairColors[hairColor]:D.skins[skin]))}return dst}
function cropRgba(full){let out=new Uint8ClampedArray(220*300*4);for(let y=0;y<300;y++)out.set(full.subarray(((y+10)*320+50)*4,((y+10)*320+270)*4),y*220*4);return out}
function render(){try{let ids={face:+$('face').value,hair:+$('hair').value,outfit:+$('outfit').value,neck:1},dst=composite(ids,$('skin').value,$('hairColor').value),skinOnly=new Uint8ClampedArray(dst.length),ev=[];for(let kind of ['neck','face','outfit','hair']){let e=items.get(kind+'/'+ids[kind]);if(kind==='face'||kind==='neck'){over(skinOnly,tint(e,D.skins[$('skin').value]));put(kind+'Mask',maskRgba(e.mask))}ev.push({kind,preset:ids[kind],registration:e.evidence,skinMaterial:e.skinMaterial})}put('full',dst);put('skinOnly',skinOnly);$('screen').getContext('2d').putImageData(new ImageData(cropRgba(dst),220,300),0,0);let editor=$('editor').getContext('2d');editor.clearRect(0,0,224,270);editor.drawImage($('screen'),-6,-12,220*230/256,300*230/256);for(let id of ['avatar128','avatar24']){let c=$(id),ctx=c.getContext('2d');ctx.clearRect(0,0,c.width,c.height);ctx.drawImage($('full'),0,0,c.width,c.height)}$('recipe').textContent=`H${ids.hair} / F${ids.face} / O${ids.outfit} / ${$('skin').value} / ${$('hairColor').value}`;$('warning').textContent='视觉待核对：头发是否完整覆盖额顶，脖子与衣领是否连续，Ebony 是否留有浅色边缘；衣服是否正对前方。';$('evidence').textContent=JSON.stringify(ev,null,2)}catch(e){$('warning').textContent=e.message}}
async function verifyGolden(){if(!D.golden)return;if(!globalThis.crypto?.subtle){$('goldenCheck').textContent='此浏览器上下文未提供 SHA-256；未执行合成 hash 检查。';return}let r=D.golden.recipe,raw=composite(r,r.skin,r.hairColor),hash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',cropRgba(raw)))).map(b=>b.toString(16).padStart(2,'0')).join('');let pass=hash===D.golden.rgba220x300Sha256;$('goldenCheck').textContent=pass?'PASS · JS GAVC 解码 / 整数合成与 Python 220 × 300 参考 SHA-256 一致':'FAIL · JS / Python 参考像素不一致';if(!pass)throw Error('Golden RGBA mismatch')}
window.avatarV2Review=Object.freeze({renderPixels:recipe=>composite({...recipe,neck:1},recipe.skin||'warm',recipe.hairColor||'graphite'),renderCrop:recipe=>cropRgba(composite({...recipe,neck:1},recipe.skin||'warm',recipe.hairColor||'graphite')),report:D,decodeGavc});
async function start(){for(let kind of ['hair','face','outfit'])for(let e of D.entries.filter(e=>e.kind===kind).sort((a,b)=>a.presetId-b.presetId)){let o=document.createElement('option');o.value=e.presetId;o.textContent=`${kind[0].toUpperCase()}${String(e.presetId).padStart(2,'0')} · ${e.name}`;$(kind).append(o)}for(let [id,colors] of [['skin',D.skins],['hairColor',D.hairColors]])for(let key of Object.keys(colors)){let o=document.createElement('option');o.value=o.textContent=key;$(id).append(o)}$('skin').value='warm';$('hairColor').value='graphite';$('face').value=D.baseFace;$('outfit').value=D.baseOutfit;$('hair').value=8;for(let e of D.entries)items.set(e.kind+'/'+e.presetId,{...e,...decodeGavc(e.gavc)});for(let id of ['hair','face','outfit','skin','hairColor'])$(id).onchange=render;$('random').onclick=()=>{for(let id of ['hair','face','outfit','skin','hairColor'])$(id).selectedIndex=Math.floor(Math.random()*$(id).options.length);render()};$('ebony').onclick=()=>{$('skin').value='ebony';render()};$('state').textContent=D.status;$('count').textContent=D.entries.length+' / 31 components';$('report').textContent=JSON.stringify({status:D.status,checks:D.checks,blocked:D.blocked,pendingVisualReview:D.pendingVisualReview},null,2);render();await verifyGolden()}start().catch(e=>$('warning').textContent=e.message);
</script></html>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    report = json.loads((args.root / "runtime" / "avatar-components-v2.json").read_text(encoding="utf-8"))
    generate_review(args.root, report)
    print(f"Review: {args.root / 'review' / 'index.html'} ({report['status']}, {report['entryCount']}/31)")


if __name__ == "__main__":
    main()
