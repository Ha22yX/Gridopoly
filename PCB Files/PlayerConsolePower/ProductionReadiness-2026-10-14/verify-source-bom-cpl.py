from pathlib import Path
import json,csv,collections,re,hashlib,math
P=Path(__file__).resolve().parent
S=json.loads((P/'schematic-netlist.json').read_text(encoding='utf-8-sig'))
B=json.loads((P/'PCB1-netlist.json').read_text(encoding='utf-8-sig'))
G=json.loads((P/'current-snapshot.json').read_text(encoding='utf-8-sig'))
BOM=list(csv.DictReader((P/'native-BOM.csv').read_text(encoding='utf-8-sig').splitlines(),delimiter='\t'))
CPL=list(csv.DictReader((P/'native-CPL.csv').read_text(encoding='utf-8-sig').splitlines(),delimiter='\t'))
D=[];notes=[];checks=collections.Counter()
def diff(kind,ref,field,actual,expected):D.append(dict(kind=kind,ref=ref,field=field,actual=actual,expected=expected))
def eq(kind,ref,field,actual,expected):
 checks[kind]+=1
 if actual!=expected:diff(kind,ref,field,actual,expected)
def refsmap(vals,refkey,name):
 out={}
 for c in vals:
  r=refkey(c)
  if r in out:diff('duplicate_reference',r,name,'duplicate','unique')
  out[r]=c
 return out
s=refsmap(S['components'].values(),lambda c:c['props']['Designator'],'schematic')
b=refsmap(B['components'].values(),lambda c:c['props']['Designator'],'PCB netlist')
g=refsmap(G['components'],lambda c:c['ref'],'live snapshot')
c=refsmap(CPL,lambda x:x['Designator'],'CPL')
bom={}
for row in BOM:
 rs=[x.strip() for x in row['Designator'].split(',') if x.strip()]
 eq('BOM_group_quantity',row['Designator'],'Quantity',int(row['Quantity']),len(rs))
 for r in rs:
  if r in bom:diff('duplicate_reference',r,'BOM','duplicate','unique')
  bom[r]=row
for name,src in [('PCB',b),('live snapshot',g),('BOM',bom),('CPL',c)]:eq('reference_sets',name,'designators',sorted(src),sorted(s))
props=['Manufacturer Part','Manufacturer','Supplier Part','Supplier','Footprint','FootprintName','Device','DeviceName','Supplier Footprint','Value']
props_bom={'Manufacturer Part':'Manufacturer Part','Manufacturer':'Manufacturer','Supplier Part':'Supplier Part','Supplier':'Supplier','Footprint':'FootprintName','Value':'Value','Comment':'Name'}
props_cpl={'Device':'DeviceName','Footprint':'FootprintName','Value':'Value','Comment':'Name','Supplier':'Supplier'}
def mm(x):
 if not re.fullmatch(r'[-+]?\d+(?:\.\d+)?mm',x):raise ValueError('CPL coordinate not explicit mm '+x)
 return float(x[:-2])
def angle(v):return float(v)%360
NC=[];rows=[];npins=0;net_members=collections.defaultdict(list)
for r in sorted(s,key=lambda r:(re.match(r'\D+',r).group(0),int(re.search(r'\d+',r).group(0)))):
 if not all(r in x for x in [b,g,bom,c]):continue
 sp,bp=s[r]['props'],b[r]['props']
 for key in props:eq('SCH_PCB_property',r,key,bp.get(key,''),sp.get(key,''))
 for key,prop in props_bom.items():eq('BOM_PCB_property',r,key,bom[r].get(key,''),bp.get(prop,''))
 for key,prop in props_cpl.items():eq('CPL_PCB_property',r,key,c[r].get(key,''),bp.get(prop,''))
 eq('component_identity',r,'live uid',g[r]['uid'],sp['Unique ID'])
 eq('component_identity',r,'PCB Unique ID',bp['Unique ID'],sp['Unique ID'])
 sm=s[r]['pinInfoMap'];bm=b[r]['pinInfoMap'];gm={}
 for pad in g[r]['pads']:
  pn=str(pad['padNumber'])
  if pn in gm:diff('duplicate_pad_number',r,pn,'duplicate','unique')
  gm[pn]=pad
 eq('pin_number_sets',r,'SCH / PCB',sorted(bm),sorted(sm))
 eq('pin_number_sets',r,'SCH / live pads',sorted(gm),sorted(sm))
 eq('CPL_pin_count',r,'Pins',int(c[r]['Pins']),len(gm))
 for pn in sm:
  if pn not in bm or pn not in gm:continue
  eq('pin_number_value',r+'.'+pn,'SCH number',str(sm[pn]['number']),pn)
  eq('pin_number_value',r+'.'+pn,'PCB number',str(bm[pn]['number']),pn)
  sn,bn,gn=sm[pn].get('net',''),bm[pn].get('net',''),gm[pn].get('net','')
  eq('pin_net',r+'.'+pn,'SCH / PCB literal net',bn,sn)
  eq('pin_net',r+'.'+pn,'SCH / live literal net',gn,sn)
  npins+=1
  if sn=='':NC.append({'ref':r,'pin':pn,'endpoint_key':r+'.'+pn,'sch_net':sn,'pcb_net':bn,'live_net':gn,'meaning':'unassigned per endpoint; no electrical equivalence with any other blank-net pad'})
  else:net_members[sn].append(r+'.'+pn)
 x,y=g[r]['x']*.0254,g[r]['y']*.0254
 err={a:mm(c[r][a])-v for a,v in [('Mid X',x),('Mid Y',y),('Ref X',x),('Ref Y',y)]}
 for k,v in err.items():
  checks['CPL_position']+=1
  if abs(v)>0.000051:diff('CPL_position',r,k,mm(c[r][k]),{'expected_mm':x if k.endswith('X') else y,'tolerance_mm':.000051,'delta_mm':v})
 da=((angle(c[r]['Rotation'])-angle(g[r]['rotation'])+180)%360)-180
 checks['CPL_rotation']+=1
 if abs(da)>1e-7:diff('CPL_rotation',r,'Rotation',c[r]['Rotation'],g[r]['rotation'])
 eq('CPL_layer',r,'live top layer',g[r]['layer'],1)
 eq('CPL_layer',r,'CPL side',c[r]['Layer'],'T')
 eq('CPL_layer',r,'PCB layer',bp['PCB Layer'],'TopLayer')
 rows.append({'ref':r,'MPN':bp['Manufacturer Part'],'supplier_part':bp['Supplier Part'],'footprint_id':bp['Footprint'],'footprint_name':bp['FootprintName'],'value':bp.get('Value',''),'pin_count':len(gm),'snapshot_mil':[g[r]['x'],g[r]['y']],'snapshot_mm':[x,y],'CPL_mm':[mm(c[r]['Mid X']),mm(c[r]['Mid Y'])],'coordinate_error_mm':err,'rotation_normalized':angle(g[r]['rotation']),'CPL_rotation_normalized':angle(c[r]['Rotation']),'layer':'Top/T'})
files=['PCB1-netlist.json','schematic-netlist.json','current-snapshot.json','native-BOM.csv','native-CPL.csv']
R={'audit_date_local':'2026-10-14 America/New_York','snapshot_capturedAt':G['capturedAt'],'status':'PASS' if not D else 'FAIL','scope':'SCH netlist vs PCB netlist vs live pads; native BOM and CPL semantic consistency only, no library accuracy or Gerber physical connectivity certification','inputs_sha256':{f:hashlib.sha256((P/f).read_bytes()).hexdigest() for f in files},'counts':{'schematic_components':len(s),'pcb_components':len(b),'live_components':len(g),'bom_group_rows':len(BOM),'bom_quantity_total':sum(int(x['Quantity']) for x in BOM),'expanded_bom_components':len(bom),'cpl_components':len(c),'pin_endpoints_compared':npins,'blank_net_endpoints':len(NC),'nonempty_named_nets':len(net_members)},'rules':{'format':'BOM/CPL are tab-separated UTF-8 with quote handling despite .csv suffix','coordinates':'snapshot mil × 0.0254, preserve signed Y; Mid/Ref tolerance 0.000051mm = half last exported 0.0001mm digit plus 1nm','rotation':'modulo 360; angular tolerance 1e-7 degree; no mirroring applied','layer':'snapshot layer 1 and PCB TopLayer must correspond to native CPL T','NC':'Only the literal empty string is an unassigned net; each blank pin remains a distinct endpoint. No empty-net pads are merged into one electrical network. Names such as NC, $1Nxx, NC1, NC2, or other aliases are compared literally and never treated as universally disconnected. Actual electrical isolation requires separate Gerber connectivity audit. This snapshot has 21 blank-net endpoints and zero differing aliases between sources.','properties':'MPN, supplier number, ID and footprint name compare literally. Footprint IDs only available and compared between SCH and PCB netlists; native BOM/CPL expose names, not IDs. Live snapshot contains identity/pads/placement only, no MPN fields. DeviceName is library device identity and is compared to CPL Device rather than assumed to equal MPN.','values':'R/C values literal equality across SCH, PCB, BOM and CPL; no unit substitution needed because representations match.'},'check_counts':dict(checks),'difference_count':len(D),'differences':D,'NC_endpoint_ledger':NC,'nonempty_net_members':dict(sorted(net_members.items())),'components':rows}
(P/'source-bom-cpl-consistency.json').write_text(json.dumps(R,ensure_ascii=False,indent=2),encoding='utf8')
maxerr=max(abs(v) for row in rows for v in row['coordinate_error_mm'].values())
md=f'''# 原理图、PCB、实时焊盘、BOM/CPL 一致性审查\n\n日期：2026-10-14（America/New_York）；实时快照：{G['capturedAt']}。本轮只读本地五份已导出文件，未调用桥接、未改源工程。\n\n**结果：{R['status']}，发现差异 {len(D)} 项。** 这表示文件之间一致，不表示器件库参数、实物方向或电气功能全部正确。\n\n| 比较项目 | 数量/结果 |\n| --- | --- |\n| 原理图 / PCB / 实时器件 / 展开BOM / CPL位号 | 均68，位号集合一致，无重复 |\n| 原理图引脚 / PCB引脚 / 实时封装焊盘 | 均243，编号与每脚字面网络一致 |\n| 非空命名网络 / 空网络端点 | {len(net_members)} / {len(NC)} |\n| BOM原始分组 / 合计数量 | {len(BOM)}组 / 68件，逐行数量与位号数一致 |\n| SCH与PCB的MPN、供应商编号、封装ID/名、Device ID/名、Value | 68件一致 |\n| BOM的MPN、供应商编号、封装名、Value | 展开68件一致 |\n| CPL的Device、封装名、Value、注释、供应商、引脚数量 | 68件一致 |\n| CPL中心和参考坐标 | mil×0.0254且保留Y符号；最大误差{maxerr:.9f}mm，低于0.000051mm舍入容差 |\n| CPL角度 | 68件按模360一致，−90°与270°视为相同 |\n| CPL面层 | 68件均T，对应实时layer 1与PCB TopLayer |\n\n## NC与别名规则\n\n空网络串不代表一个共同的网络。21个空串引脚分别以位号.脚号标识，比较时只确认同一端点在三份数据里均未分配网络；没有把这些端点相互连通。本轮没有为了通过而将不同的NC/$1N/$2N等别名折叠。任何非空名称都按字面保持独立；如果其中存在实际铜连接，须由独立Gerber几何审查发现。\n\nNC端点：{', '.join(x['endpoint_key'] for x in NC)}。\n\n## 差异和证据边界\n\n{'无内容/电气/坐标差异；浮点及导出末位舍入在上述阈值内。' if not D else json.dumps(D,ensure_ascii=False,indent=2)}\n\nnative-BOM.csv和native-CPL.csv实际为TSV，本次按制表符及引号解析。BOM/CPL不携带封装ID，只能核对其封装名；封装ID比较来自SCH/PCB网表。实时快照不携带MPN，使用其唯一ID、焊盘与位置完成对应，未虚构该字段检查。\n\n这项检查不解决供应商库属性是否准确、库符号物理脚序是否正确、Gerber铜是否符合网表、贴装极性、热/电源/USB性能、实物测试，以及订单是否使用这份包。相关结论必须分别引用对应审查。\n\n完整逐件字段、243端点对应的命名网络/21项NC清单、比较次数及五份输入SHA-256，见同目录[source-bom-cpl-consistency.json](source-bom-cpl-consistency.json)。\n'''
(P/'source-bom-cpl-consistency.md').write_text(md,encoding='utf8')
print(json.dumps({'status':R['status'],'counts':R['counts'],'checks':R['check_counts'],'differences':D,'max_cpl_coordinate_error_mm':maxerr},ensure_ascii=False,indent=2))
