"""Chinese illustrated assembly manual from the frozen R3 CAD assembly."""
from pathlib import Path
import json, math
from PIL import Image
from reportlab.pdfgen import canvas
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import Paragraph, Table, TableStyle
from reportlab.lib.styles import ParagraphStyle
from pypdf import PdfReader

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
IMAGES=HERE/'assets'
OUT=ROOT/'output/pdf';OUT.mkdir(parents=True,exist_ok=True)
PDF=OUT/'Gridopoly_Player_Stand_R3_Assembly_Guide_CN.pdf'
pdfmetrics.registerFont(TTFont('CN','C:/Windows/Fonts/msyh.ttc',subfontIndex=0))
pdfmetrics.registerFont(TTFont('CNB','C:/Windows/Fonts/msyhbd.ttc',subfontIndex=0))
pdfmetrics.registerFontFamily('CN',normal='CN',bold='CNB',italic='CN',boldItalic='CNB')
W,H=A4;M=36;CW=W-2*M
INK=colors.HexColor('#16313D');TEAL=colors.HexColor('#087F8C');GRAY=colors.HexColor('#526773')
LIGHT=colors.HexColor('#F1F6F7');LINE=colors.HexColor('#D6E3E7');AMBER=colors.HexColor('#94621D')
c=canvas.Canvas(str(PDF),pagesize=A4)
c.setTitle('Gridopoly 玩家屏幕底座 R3 图文装配说明')
c.setAuthor('Gridopoly 3D建模开发')
c.setSubject('R3安装耳加固版；零件、紧固件、分解图及分步安装')
LAYOUT=[]
POINTS=json.loads((IMAGES/'diagram-points.json').read_text(encoding='utf-8'))

def para(text,x,top,width,size=11,leading=None,bold=False,color=INK):
    style=ParagraphStyle('p',fontName='CNB' if bold else 'CN',fontSize=size,leading=leading or size*1.6,
                         textColor=color,wordWrap='CJK',spaceAfter=0)
    p=Paragraph(text,style);_,h=p.wrap(width,800)
    p.drawOn(c,x,H-top-h)
    LAYOUT.append({'page':page_no,'type':'text','top':top,'bottom':top+h,'x':x,'right':x+width,'text':text})
    return top+h

def panel(top,height,fill=LIGHT):
    c.setFillColor(fill);c.setStrokeColor(LINE);c.setLineWidth(.6)
    c.roundRect(M,H-top-height,CW,height,9,stroke=1,fill=1)

def page(n,title,subtitle):
    global page_no
    page_no=n
    c.setFillColor(TEAL);c.rect(0,H-8,W,8,fill=1,stroke=0)
    para('GRIDOPOLY  /  玩家控制屏幕',M,25,CW,9,color=GRAY)
    para(title,M,58,CW,23,31,True)
    para(subtitle,M,100,CW,10.5,16,color=GRAY)
    c.setStrokeColor(LINE);c.line(M,42,W-M,42)
    para('R3 加固版  ·  毫米 mm  ·  装配参考',M,H-32,CW-80,8,color=GRAY)
    para(f'{n:02d} / 06',W-82,H-32,46,8,bold=True,color=TEAL)

def image(file,top,height,x=M,width=CW,markers=None):
    path=IMAGES/file
    with Image.open(path) as im:iw,ih=im.size
    s=min(width/iw,height/ih);w,h=iw*s,ih*s
    xx=x+(width-w)/2;tt=top+(height-h)/2
    c.drawImage(str(path),xx,H-tt-h,w,h,mask='auto')
    if markers:
        for num,pixel in markers:
            px,py=xx+pixel[0]*s,H-tt-pixel[1]*s
            c.setFillColor(TEAL);c.setStrokeColor(colors.white);c.setLineWidth(1.6)
            c.circle(px,py,10,fill=1,stroke=1)
            c.setFont('CNB',10);c.setFillColor(colors.white);c.drawCentredString(px,py-3.8,str(num))
    return tt+h

def note(top,text,height=54):
    panel(top,height)
    para(text,M+13,top+10,CW-26,10,15)

def labels(file,top,height,specs):
    data=POINTS[file];iw,ih=data['width'],data['height']
    s=min(CW/iw,height/ih);xx=M+(CW-iw*s)/2;tt=top+(height-ih*s)/2
    for index,label,bx,bt,bw in specs:
        px,py=index if isinstance(index,tuple) else data['points'][index]['pixel']
        tx,ty=xx+px*s,tt+py*s
        by=bt+10
        endx=bx+bw if bx+bw/2<tx else bx
        c.setStrokeColor(TEAL);c.setLineWidth(.7)
        c.line(tx,H-ty,endx,H-by)
        c.setFillColor(TEAL);c.circle(tx,H-ty,2.0,fill=1,stroke=0)
        c.setFillColor(colors.white);c.setStrokeColor(LINE)
        c.roundRect(bx,H-bt-21,bw,21,4,fill=1,stroke=1)
        para(label,bx+6,bt+3,bw-12,8.4,14,bold=True,color=TEAL)

def step(number,title,body,top):
    c.setFillColor(TEAL);c.circle(M+12,H-top-12,12,fill=1,stroke=0)
    c.setFont('CNB',11);c.setFillColor(colors.white);c.drawCentredString(M+12,H-top-16,str(number))
    para(title,M+34,top-1,CW-34,12,18,True)
    end=para(body,M+34,top+24,CW-34,10.5,17)
    return end+15

def table(headers,rows,widths,top,rowheight=None):
    def cell(txt,head=False):
        return Paragraph(txt,ParagraphStyle('cell',fontName='CNB' if head else 'CN',fontSize=9.4,
                           leading=14.8,textColor=colors.white if head else INK,wordWrap='CJK'))
    data=[[cell(t,True) for t in headers]]+[[cell(str(t)) for t in row] for row in rows]
    t=Table(data,colWidths=widths,rowHeights=rowheight)
    t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),TEAL),('VALIGN',(0,0),(-1,-1),'MIDDLE'),
        ('LEFTPADDING',(0,0),(-1,-1),9),('RIGHTPADDING',(0,0),(-1,-1),9),
        ('TOPPADDING',(0,0),(-1,-1),7),('BOTTOMPADDING',(0,0),(-1,-1),7),
        ('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,LIGHT]),('LINEBELOW',(0,0),(-1,-1),.5,LINE)]))
    _,h=t.wrap(CW,1000);t.drawOn(c,M,H-top-h)
    LAYOUT.append({'page':page_no,'type':'table','top':top,'bottom':top+h})
    return top+h

page(1,'玩家屏幕底座 R3','图文装配说明  /  Bambu H2D · PLA打印件')
image('00_assembled.png',137,385)
para('装好之后的整体外形',M,520,CW,10,color=GRAY)
para('先固定上部零件，再装PCB，最后连接屏幕与电池。',M,555,CW,14,22,True)
para('本说明适用于：02屏幕支架使用R3加固版；01连接板、03电池仓、04仓盖、05隔柱继续使用R2。全套为5种打印件，共8个实体。',M,591,CW,11,18)
note(655,'<b>只替换02支架。</b> 已打印的其他四种零件可继续使用。模型已完成几何检查；整机实物装配和60mm软排线余量仍需首次试配。',66)
c.showPage()

page(2,'准备零件与紧固件','先清理孔内毛刺，再按清单分好螺丝。M3螺丝长度均指头下长度。')
para('打印件',M,137,CW,13,bold=True)
y=table(['文件 / 零件','数量','识别要点'],[
    ['01 连接板 R2','1','大平板，带安装孔和接口缺口'],
    ['02 屏幕支架 R3','1','斜面圆环；两侧有加宽螺丝耳'],
    ['03 电池仓 R2','1','上方开放，侧面有出线缺口'],
    ['04 电池仓盖 R2','1','装配时内凹面朝下'],
    ['05 间隔柱 R2','4','高6mm，外径约6mm，通孔Ø3.6mm']
],[190,45,CW-235],160)
para('另需准备',M,y+20,CW,13,bold=True)
y=table(['配件规格','数量','安装位置'],[
    ['M3×18螺丝','4','连接板 → PCB → 尼龙底脚'],
    ['M3×12螺丝','4','屏幕支架2颗、电池仓2颗'],
    ['M3平垫片，约0.5mm厚','8','每颗螺丝头下各1片'],
    ['普通M3六角螺母，约2.4mm厚','4','支架和电池仓下方'],
    ['Ø6×8mm，M3贯通内螺纹尼龙底脚','4','PCB下方；不是6mm打印隔柱'],
    ['宽10mm、长约180-200mm软绑带','1','固定电池仓盖'],
    ['约0.5mm柔软绝缘薄垫','1','电池仓底部']
],[242,45,CW-287],y+46)
para('工具：与螺丝头匹配的螺丝刀，以及扶住M3螺母的小扳手或尖嘴钳。本版不需要自攻螺丝或热熔螺母。',M,y+15,CW,9.5,15,color=GRAY)
c.showPage()

page(3,'① 先把上部零件装到连接板','USB-C方向为前方；屏幕朝前，电池仓位于后方。暂不接电池和USB。')
image('01_upper_mounts.png',127,343)
labels('01_upper_mounts.png',127,343,[
    (3,'M3×12 螺丝 ×4',36,255,115),
    (4,'平垫片 ×4',36,326,102),
    (5,'板下M3螺母 ×4',36,422,120),
    (1,'03 空电池仓',438,188,118),
    (0,'02 R3屏幕支架',438,269,118),
    (2,'01 连接板',438,372,118)])
para('分解图：抬升距离只为展示安装关系，不代表实际安装间隙。',M,472,CW,9,color=GRAY)
y=step(1,'固定R3屏幕支架','对齐两个安装耳与连接板的孔。每孔使用1颗M3×12、1片垫片和1个M3螺母；从连接板下面扶住螺母，轻拧到贴合。',505)
y=step(2,'固定空电池仓','将电池仓放在连接板后方，出线缺口朝J3方向。另用2颗M3×12、2片垫片、2个螺母固定。',y)
note(y,'<b>两处共同顺序：</b> 螺丝头 → 垫片 → 安装耳 → 连接板 → 螺母。先完成这一步，再装PCB，便于操作下侧螺母。',58)
c.showPage()

page(4,'② 安装PCB、隔柱和底脚','PCB元件面朝上。四个主安装点使用M3×18，不另加普通螺母。')
image('02_pcb_stack.png',128,330,x=M,width=330)
# A clean vector stack complements the true CAD exploded view.
sx=M+349;sw=CW-349
para('每个安装点',sx,144,sw,11,17,True)
stack_labels=[('M3×18螺丝头',18),('0.5mm垫片',18),('4mm连接板',25),('6mm打印隔柱',30),('1.6mm PCB',20),('8mm内螺纹底脚',34)]
yy=177
for idx,(label,hh) in enumerate(stack_labels):
    fill=TEAL if idx in (2,3) else (colors.HexColor('#275A47') if idx==4 else colors.HexColor('#D9E2E5'))
    c.setFillColor(fill);c.roundRect(sx,H-yy-hh,sw,hh,3,fill=1,stroke=0)
    para(label,sx+5,yy+(hh-12)/2-2,sw-10,8.3,12,bold=True,color=colors.white if idx in (2,3,4) else INK)
    yy+=hh+15
    if idx<5:
        c.setStrokeColor(GRAY);c.setLineWidth(1);c.line(sx+sw/2,H-yy+13,sx+sw/2,H-yy+3)
        c.line(sx+sw/2,H-yy+3,sx+sw/2-3,H-yy+7);c.line(sx+sw/2,H-yy+3,sx+sw/2+3,H-yy+7)
para('顺序示意，不按比例',sx,yy+1,sw,8,color=GRAY)
y=step(3,'四角放入6mm间隔柱','在PCB四个孔处各放1个6mm打印隔柱，再放上已装好支架和电池仓的连接板。对齐四孔，避免用螺丝强行拉正。',487)
y=step(4,'从上方穿入M3×18','每颗螺丝头下放垫片，穿过连接板、隔柱和PCB，拧入下面的8mm尼龙内螺纹底脚。四角交替轻拧，保持PCB平整。',y)
note(y,'<b>检查下面的空间：</b> 螺母、螺丝尖端及打印件都应避开器件和焊点。不要随意换更长的螺丝，也不要把6mm隔柱与8mm底脚混用。',61)
c.showPage()

page(5,'③ 放入屏幕，试配软排线','先保持断电；排线自然弯曲，不拉紧，不夹在打印件接缝里。')
image('03_fpc_route.png',130,355)
labels('03_fpc_route.png',130,355,[
    ((1200,470),'屏幕定位圆环',425,214,132),
    (1,'排线槽口',36,284,93),
    (2,'软排线路径参考',425,332,132),
    (3,'J2 排线插座',425,393,132)])
para('图中橙色带仅表示约60mm软排线的空间路径。屏幕外壳已隐藏。',M,488,CW,9,color=GRAY)
y=step(5,'先穿线，再放入屏幕','先让屏幕后方的软排线通过支架预留槽，再对准原CW47.5定位柱与圆环放入屏幕。沿开放通道把排线送到PCB的J2。',519)
y=step(6,'核对J2触点和针序后锁紧','确认金手指方向与实物插座触点及针序对应，按插座锁扣结构打开、插入到位后锁紧。最后检查排线仍有松弛量，屏幕旋转不牵拉线根。',y)
note(y,'<b>J2不能随意正反插。</b> 当前机械资料未确认实物触点朝向，本说明不指定金手指朝上或朝下；请勿凭这张路径图判断电气接法。',61)
c.showPage()

page(6,'④ 放电池、盖仓盖，完成检查','电池包按56×34.5×10.3mm外形预留；仓盖由仓壁承托。')
image('04_battery.png',128,280)
labels('04_battery.png',128,280,[
    (0,'04 仓盖，凹面朝下',36,153,132),
    (1,'电池包',421,212,135),
    (2,'03 电池仓',421,265,135),
    (3,'侧面出线缺口',36,288,122),
    (5,'J3 电池插头',36,350,122),
    (6,'绑带穿槽',421,350,135)])
para('电池放置方向：56mm边横向，34.5mm边竖直，10.3mm厚度朝前后。',M,411,CW,10,16,True)
y=step(7,'放电池并接J3','仓底放约0.5mm绝缘薄垫。电池线从开放缺口引出，经过操作窗到J3；确认插头极性与PCB对应后再接入。不能夹住线根或挤压电池软包。',450)
y=step(8,'盖好仓盖，固定软绑带','仓盖内凹面朝下。软绑带穿连接板专用槽并绕过后沿，只收紧到盖子不松脱。',y)
para('通电前逐项确认',M,y+2,CW,12,18,True)
checks=['四角稳固、PCB平整，下面没有金属件碰到器件或焊点。','USB-C可插拔，POWER/RST可按，旋钮可自由转动。','排线和电池线不受拉扯，电池与仓盖之间没有挤压。']
yy=y+29
for text in checks:
    c.setStrokeColor(TEAL);c.setLineWidth(1);c.rect(M,H-yy-10,9,9,stroke=1,fill=0)
    yy=para(text,M+18,yy-3,CW-18,9.5,15)+8
para('搬动时握住底座，屏幕沿用定位配合，未被四颗PCB螺丝锁死。',M,yy+1,CW,9.5,15,True)
c.showPage();c.save()
reader=PdfReader(str(PDF));assert len(reader.pages)==6
text='\n'.join(p.extract_text() or '' for p in reader.pages)
for token in ['M3×18','M3×12','6mm','8mm','J2','J3','R3']:
    assert token in text,token
bad=[r for r in LAYOUT if r['bottom']>H-45 and r.get('top',0)<H-40]
assert not bad,bad
(HERE/'layout-check.json').write_text(json.dumps({'pages':6,'pdf':str(PDF),'text_chars':len(text),'layout_bounds_pass':True},ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'pdf':str(PDF),'pages':6,'bytes':PDF.stat().st_size},ensure_ascii=False))
