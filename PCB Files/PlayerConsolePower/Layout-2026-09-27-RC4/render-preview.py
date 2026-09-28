from pathlib import Path
from tempfile import TemporaryDirectory
import zipfile
import pymupdf
from gerbonara import GerberFile, ExcellonFile

B=Path(__file__).resolve().parent
with TemporaryDirectory(prefix='gridopoly-rc4-preview-')as tmp:
    p=Path(tmp)
    with zipfile.ZipFile(B/'Manufacturing-RC4/PlayerPower-R1-RC4-Gerber.zip')as z:
        for name in z.namelist():(p/name).write_bytes(z.read(name))
    for side,suffix,bg,cu in [('top','GTL','#123d32','#386756'),('bottom','GBL','#152c49','#294866')]:
        s=['<svg xmlns="http://www.w3.org/2000/svg" width="1800" height="1800" viewBox="-2 -2 84 84">','<rect x="-2" y="-2" width="84" height="84" fill="#e9edef"/>',f'<defs><clipPath id="board"><rect width="80" height="80" rx="5"/></clipPath></defs><rect width="80" height="80" rx="5" fill="{bg}"/>','<g clip-path="url(#board)"><g transform="scale(1 -1)">']
        for name,fg,bg2 in [(f'Gerber_{side.title()}Layer.{suffix}',cu,bg),(f'Gerber_{side.title()}SolderMaskLayer.'+('GTS'if side=='top'else'GBS'),'#deb86a',bg),(f'Gerber_{side.title()}SilkscreenLayer.'+('GTO'if side=='top'else'GBO'),'#f7faf4',bg)]:
            s.extend(str(x)for x in GerberFile.open(p/name).svg_objects(fg=fg,bg=bg2))
        for name in ['Drill_PTH_Through.DRL','Drill_NPTH_Through.DRL']:
            s.extend(str(x)for x in ExcellonFile.open(p/name).svg_objects(fg='#101820',bg=bg))
        s.extend(['</g></g><rect width="80" height="80" rx="5" fill="none" stroke="#0c1e1b" stroke-width=".15"/>','</svg>'])
        out=B/f'pcb-{side}-fabrication-preview.svg';out.write_text('\n'.join(s),encoding='utf-8')
        doc=pymupdf.open(out);pdf=pymupdf.open('pdf',doc.convert_to_pdf());pdf[0].get_pixmap().save(B/f'pcb-{side}-fabrication-preview.png')
        print(out.name)
