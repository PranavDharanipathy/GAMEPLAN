import re,sys
txt=open(sys.argv[1]).read()
data=txt[txt.index('DATA;'):]
ents={}
for m in re.finditer(r'#(\d+)\s*=\s*(.*?);\s*(?=#\d+\s*=|ENDSEC)',data,re.S):
    ents[int(m.group(1))]=' '.join(m.group(2).split())
def refs(s): return [int(x) for x in re.findall(r'#(\d+)',s)]
def typ(s): return s.split('(')[0].strip()
# product names: PRODUCT -> PRODUCT_DEFINITION_FORMATION -> PRODUCT_DEFINITION
prodname={}
for i,s in ents.items():
    if typ(s)=='PRODUCT': m2=re.match(r"PRODUCT\('(.*?)',",s); prodname[i]=m2.group(1) if m2 else s[:60]
pdf2prod={}
for i,s in ents.items():
    if typ(s).startswith('PRODUCT_DEFINITION_FORMATION'): pdf2prod[i]=refs(s)[0]
pd2name={}
for i,s in ents.items():
    if typ(s)=='PRODUCT_DEFINITION':
        r=refs(s); pd2name[i]=prodname.get(pdf2prod.get(r[0]),'?')
# NAUO: relating(parent) related(child)
nauo={}
for i,s in ents.items():
    if typ(s)=='NEXT_ASSEMBLY_USAGE_OCCURRENCE':
        r=refs(s); nauo[i]=(r[0],r[1])
# PRODUCT_DEFINITION_SHAPE(... , #nauo)
pds2nauo={}
for i,s in ents.items():
    if typ(s)=='PRODUCT_DEFINITION_SHAPE':
        r=refs(s)
        if r and r[0] in nauo: pds2nauo[i]=r[0]
def cart(i):
    return [float(x) for x in re.findall(r'[-\d.E+]+',ents[i].split(',',1)[1])][:3]
def axis(i):
    r=refs(ents[i]); loc=cart(r[0])
    z=cart(r[1]) if len(r)>1 else [0,0,1]; x=cart(r[2]) if len(r)>2 else [1,0,0]
    return loc,z,x
# CDSR: (representation_relationship, pds)
out=[]
for i,s in ents.items():
    if typ(s)=='CONTEXT_DEPENDENT_SHAPE_REPRESENTATION':
        rr,pds=refs(s)[:2]
        if pds not in pds2nauo: continue
        rrs=ents[rr]
        idt=[x for x in refs(rrs) if typ(ents[x])=='ITEM_DEFINED_TRANSFORMATION']
        if not idt: continue
        a1,a2=refs(ents[idt[0]])[:2]
        p,c=nauo[pds2nauo[pds]]
        out.append((pd2name.get(p),pd2name.get(c),axis(a1),axis(a2)))
for p,c,a1,a2 in out:
    print(f"{p!s:40.40} > {c!s:45.45} to={[round(v,4) for v in a2[0]]} z={[round(v,3) for v in a2[1]]} x={[round(v,3) for v in a2[2]]} from={[round(v,4) for v in a1[0]]}")
