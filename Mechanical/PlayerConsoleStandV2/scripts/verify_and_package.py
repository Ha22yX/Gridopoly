"""Independent exported-STL QA and one print object per geometry-only 3MF.

Does not invoke Blender, alter meshes, configure a slicer, or modify legacy files.
"""
from pathlib import Path
from collections import Counter,defaultdict
import json, math, struct, hashlib, zipfile
import xml.etree.ElementTree as ET
import numpy as np

ROOT=Path(__file__).resolve().parents[3]
OUT=Path('C:/Users/kicof/Downloads/Gridopoly_Player_Stand_V2_R1')
LEGACY=ROOT/'Mechanical/PlayerConsoleStand/outputs/models/Gridopoly_Player_Console_Stand_CW47_5.stl'
NS='http://schemas.microsoft.com/3dmanufacturing/core/2015/02'
EXPECTED={
 '01_Upper_CW47_5.stl':(94,82,45.864471435546875),
 '02_Battery_Tray.stl':(94,82,16),
 '03_Spacer_8mm.stl':(6,6,8),
 '03_Spacer_12mm.stl':(6,6,12)}

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])

def parse(path):
    raw=path.read_bytes();n=struct.unpack_from('<I',raw,80)[0]
    assert len(raw)==84+50*n,'Malformed binary STL'
    vertices=[];faces=[];ids={};stored_normals=[]
    for i in range(n):
        a=struct.unpack_from('<12fH',raw,84+50*i);face=[];stored_normals.append(a[:3])
        for j in (3,6,9):
            p=a[j:j+3]
            assert all(math.isfinite(x) for x in p)
            if p not in ids:ids[p]=len(vertices);vertices.append(p)
            face.append(ids[p])
        faces.append(tuple(face))
    return vertices,faces,stored_normals

def verify(path,vertices,faces,stored_normals):
    edges=Counter();directed=Counter();adj=defaultdict(set)
    degenerate=0;volume=0;downward=[];area=0;stored_bad=0
    for fi,(a,b,c) in enumerate(faces):
        pa,pb,pc=vertices[a],vertices[b],vertices[c]
        normal=cross(sub(pb,pa),sub(pc,pa));norm=math.sqrt(dot(normal,normal))
        if norm<1e-8:degenerate+=1
        area+=norm/2
        if norm>1e-8 and dot(normal,stored_normals[fi]) < 0:stored_bad+=1
        if norm>1e-8 and normal[2]/norm < -math.sin(math.radians(.01)) and max(pa[2],pb[2],pc[2])>1e-4:
            downward.append((fi,norm/2))
        volume+=dot(pa,cross(pb,pc))/6
        for u,v in ((a,b),(b,c),(c,a)):
            edges[tuple(sorted((u,v)))]+=1;directed[(u,v)]+=1
            adj[u].add(v);adj[v].add(u)
    unvisited=set(range(len(vertices)));components=0
    while unvisited:
        components+=1;q=[unvisited.pop()]
        while q:
            for a in adj[q.pop()]:
                if a in unvisited:unvisited.remove(a);q.append(a)
    bounds=[[min(p[i] for p in vertices),max(p[i] for p in vertices)] for i in range(3)]
    dim=[hi-lo for lo,hi in bounds]
    bad_edges=sum(v!=2 for v in edges.values())
    bad_winding=sum(directed[u,v]!=directed[v,u] for u,v in edges)
    duplicate_triangles=sum(n-1 for n in Counter(tuple(sorted(f)) for f in faces).values() if n>1)
    checks={'exact_vertex_topology_one_component':components==1,'closed_two_faces_each_edge':bad_edges==0,'consistent_oriented_edges':bad_winding==0,'nonzero_area_triangles':degenerate==0,'no_duplicate_triangles':duplicate_triangles==0,'outward_positive_signed_volume':volume>0,'stored_normals_not_opposed':stored_bad==0,'expected_dimensions':all(abs(a-b)<.001 for a,b in zip(dim,EXPECTED[path.name])),'flat_bottom_z_zero':abs(bounds[2][0])<1e-6}
    if not path.name.startswith('01_'):
        checks['no_downward_faces_above_bed']=len(downward)==0
    return {'file':path.name,'checks':checks,'pass':all(checks.values()),'units_interpreted':'millimeter, bound to model parameter dimensions; STL itself is unitless','vertices':len(vertices),'triangles':len(faces),'edges':len(edges),'connected_components':components,'non_manifold_or_boundary_edges':bad_edges,'inconsistent_edge_winding':bad_winding,'degenerate_triangles':degenerate,'duplicate_triangles':duplicate_triangles,'opposed_stored_normals':stored_bad,'signed_volume_mm3':volume,'surface_area_mm2':area,'bounds_mm':bounds,'dimensions_mm':dim,'downward_triangles_above_bed':len(downward),'downward_area_above_bed_mm2':sum(a for _,a in downward),'overhang_policy':'Upper retains legacy screen mating surfaces; their downward faces are reported, not automatically rejected.' if path.name.startswith('01_') else 'All added tray/spacer faces above Z=0 checked; only vertical-wall angular noise <0.01 degree ignored.','sha256':sha(path)}

def clipped_triangles(v,f,z=4.01):
    result=[]
    for face in f:
        poly=[np.array(v[i],dtype=float) for i in face];out=[]
        for a,b in zip(poly,poly[1:]+poly[:1]):
            ia=a[2]>=z;ib=b[2]>=z
            if ia:out.append(a)
            if ia!=ib:out.append(a+(b-a)*((z-a[2])/(b[2]-a[2])))
        for i in range(1,len(out)-1):
            tri=np.array([out[0],out[i],out[i+1]])
            if np.linalg.norm(np.cross(tri[1]-tri[0],tri[2]-tri[0]))>1e-9:result.append(tri)
    return np.array(result)

def surface_distance(samples,triangles):
    a,b,c=triangles[:,0],triangles[:,1],triangles[:,2]
    ab=b-a;ac=c-a;n=np.cross(ab,ac);nn=np.einsum('ij,ij->i',n,n)
    d00=np.einsum('ij,ij->i',ab,ab);d01=np.einsum('ij,ij->i',ab,ac);d11=np.einsum('ij,ij->i',ac,ac)
    den=d00*d11-d01*d01;result=[]
    for start in range(0,len(samples),32):
        p=samples[start:start+32,None,:];pa=p-a[None,:,:]
        pn=np.einsum('bti,ti->bt',pa,n);projected=pa-pn[:,:,None]/nn[None,:,None]*n[None,:,:]
        d20=np.einsum('bti,ti->bt',projected,ab);d21=np.einsum('bti,ti->bt',projected,ac)
        vv=(d11*d20-d01*d21)/den;ww=(d00*d21-d01*d20)/den
        inside=(vv>=-1e-9)&(ww>=-1e-9)&(vv+ww<=1+1e-9)
        dist=np.where(inside,pn*pn/nn,np.inf)
        for x,y in ((a,b),(b,c),(c,a)):
            d=y-x;dd=np.einsum('ij,ij->i',d,d)
            t=np.clip(np.einsum('bti,ti->bt',p-x[None,:,:],d)/dd,0,1)
            delta=p-x[None,:,:]-t[:,:,None]*d[None,:,:]
            dist=np.minimum(dist,np.einsum('bti,bti->bt',delta,delta))
        result.extend(np.sqrt(np.maximum(0,np.min(dist,axis=1))))
    return np.asarray(result)

def compare_legacy(newv,newf):
    oldv,oldf,_=parse(LEGACY)
    old=clipped_triangles(oldv,oldf);new=clipped_triangles(newv,newf)
    area=lambda tris:float(np.linalg.norm(np.cross(tris[:,1]-tris[:,0],tris[:,2]-tris[:,0]),axis=1).sum()/2)
    points=lambda tri:np.unique(np.round(np.concatenate([tri.reshape(-1,3),tri.mean(axis=1),(tri[:,0]+tri[:,1])/2,(tri[:,1]+tri[:,2])/2,(tri[:,2]+tri[:,0])/2]),8),axis=0)
    op,np_=points(old),points(new)
    # All clipped vertices plus triangle centroids and edge midpoints, both directions.
    d_old=surface_distance(op,new);d_new=surface_distance(np_,old)
    oldset={tuple(round(c,5) for c in p) for p in oldv if p[2]>4.01}
    newset={tuple(round(c,5) for c in p) for p in newv if p[2]>4.01}
    diff=abs(area(old)-area(new));maxd=max(float(d_old.max()),float(d_new.max()))
    return {'source':str(LEGACY),'source_sha256':sha(LEGACY),'region':'Surface clipped at z>=4.01mm in the printing coordinates; no new cut-plane cap added','method':'Compare all unique original vertices above plane at 1e-5mm precision; compare clipped surface area; bidirectional sampled point-to-triangle distance at all clipped vertices, edge midpoints and triangle centroids. Allows planar re-triangulation.','legacy_vertices_above_plane':len(oldset),'new_vertices_above_plane':len(newset),'vertices_removed':len(oldset-newset),'vertices_added':len(newset-oldset),'legacy_clipped_surface_area_mm2':area(old),'new_clipped_surface_area_mm2':area(new),'surface_area_difference_mm2':diff,'sample_count_old_to_new':len(op),'sample_count_new_to_old':len(np_),'maximum_sampled_surface_distance_mm':maxd,'distance_tolerance_mm':.0002,'surface_area_tolerance_mm2':.01,'pass':oldset==newset and diff<.01 and maxd<.0002,'scope':'Strong sampled geometric equivalence, not formal exact Boolean proof or physical fit validation.'}

def make3mf(vertices,faces,path):
    ET.register_namespace('',NS)
    def e(parent,tag,attrs=None):return ET.SubElement(parent,'{'+NS+'}'+tag,attrs or {})
    model=ET.Element('{'+NS+'}model',{'unit':'millimeter','{http://www.w3.org/XML/1998/namespace}lang':'en-US'})
    e(model,'metadata',{'name':'Title'}).text=path.stem
    e(model,'metadata',{'name':'Description'}).text='Gridopoly Player Stand V2 R1 FIRST FIT. GEOMETRY ONLY: one independently printable part, no slicer, printer, material or process settings. Physical fit pending.'
    res=e(model,'resources');obj=e(res,'object',{'id':'1','type':'model','name':path.stem});mesh=e(obj,'mesh');vs=e(mesh,'vertices');origin=[min(p[i] for p in vertices) for i in range(3)]
    for p in vertices:e(vs,'vertex',{a:format(p[i]-origin[i],'.12g') for i,a in enumerate('xyz')})
    ts=e(mesh,'triangles')
    for f in faces:e(ts,'triangle',{f'v{i+1}':str(f[i]) for i in range(3)})
    build=e(model,'build');e(build,'item',{'objectid':'1'})
    types='<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>'
    rels='<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel" Target="/3D/3dmodel.model"/></Relationships>'
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml',types);z.writestr('_rels/.rels',rels);z.writestr('3D/3dmodel.model',ET.tostring(model,encoding='utf8',xml_declaration=True))
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        m=ET.fromstring(z.read('3D/3dmodel.model'));assert m.attrib['unit']=='millimeter'
        assert len(m.findall('.//{'+NS+'}object'))==1
        assert len(m.findall('.//{'+NS+'}item'))==1
        vr=m.findall('.//{'+NS+'}vertex');fr=m.findall('.//{'+NS+'}triangle')
        assert len(vr)==len(vertices) and len(fr)==len(faces)
        for p,row in zip(vertices,vr):assert all(abs(float(row.attrib[a])-(p[i]-origin[i]))<1e-8 for i,a in enumerate('xyz'))
        for f,row in zip(faces,fr):assert tuple(int(row.attrib[f'v{i+1}']) for i in range(3))==f
    return {'file':path.name,'single_print_object':True,'unit':'millimeter','zip_crc_pass':True,'stl_triangle_indices_unchanged':True,'stl_vertices_equal_after_translation':True,'translation_from_stl_mm':[-n for n in origin],'metadata_geometry_only':True,'slicer_project':False,'printer_settings_included':False,'sha256':sha(path)}

def main():
    result={'revision':'Player Stand V2 R1','performed_at_date':'2026-10-15','source':'Exported binary STL, independent of Blender mesh QA','parts':[],'limitations':['No slicer toolpath review performed.','No physical print/fit/load/heat or actual battery/cable fit verified.','Legacy upper negative-Z faces are retained and reported; no unsupported claim that all screen mating surfaces are support-free.','3MFs contain individual print parts, not the stacked assembly. Four copies of either spacer height are needed; alternatives are not stacked together.']}
    geometry={}
    for name in EXPECTED:
        v,f,n=parse(OUT/name);rep=verify(OUT/name,v,f,n);geometry[name]=(v,f)
        result['parts'].append(rep)
    result['legacy_screen_interface_comparison']=compare_legacy(*geometry['01_Upper_CW47_5.stl'])
    result['pass']=all(p['pass'] for p in result['parts']) and result['legacy_screen_interface_comparison']['pass']
    if result['pass']:
        for part in result['parts']:part['3mf']=make3mf(*geometry[part['file']],OUT/Path(part['file']).with_suffix('.3mf'))
    (OUT/'verification-report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({'pass':result['pass'],'parts':[{k:v for k,v in p.items() if k in ['file','pass','dimensions_mm','connected_components','non_manifold_or_boundary_edges','inconsistent_edge_winding','degenerate_triangles','downward_triangles_above_bed','downward_area_above_bed_mm2']} for p in result['parts']],'legacy':result['legacy_screen_interface_comparison']},ensure_ascii=False,indent=2))
    assert result['pass'],'STL/legacy QA failed; no new 3MF should be released.'

if __name__=='__main__':main()
