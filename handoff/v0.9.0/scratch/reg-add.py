# usage: reg-add.py ANCHOR_NAME SPEC...   SPEC = name:get_type:TypeName[:include]
import sys
anchor=sys.argv[1]; specs=[a.split(':') for a in sys.argv[2:]]
p='src/core/lrg-registry.c'; s=open(p).read()
lines=s.split('\n')
idx=[i for i,l in enumerate(lines) if l.strip().startswith('{ "%s",'%anchor)]
assert len(idx)==1, idx
row=lambda n,f: '    { %-29s %s },'%('"%s",'%n, f)
lines[idx[0]+1:idx[0]+1]=[row(sp[0],sp[1]) for sp in specs]
incs=[]
for sp in specs:
    if len(sp)>3:
        inc='#include "%s"'%sp[3]
        if inc not in s and inc not in incs: incs.append(inc)
last=max(i for i,l in enumerate(lines) if l.startswith('#include "../'))
lines[last+1:last+1]=incs
open(p,'w').write('\n'.join(lines))
p='docs/modules/core/registry.org'; s=open(p).read(); lines=s.split('\n')
idx=[i for i,l in enumerate(lines) if l.startswith('|') and l.split('|')[2].strip()==anchor]
assert len(idx)==1, idx
lines[idx[0]+1:idx[0]+1]=['|                        | %-27s | %-29s |'%(sp[0],sp[2]) for sp in specs]
open(p,'w').write('\n'.join(lines))
