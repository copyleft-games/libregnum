# register the M6/M7 editor-wave stubs (run from the worktree root after mkstub/mktest)
def ins(p, anchor, lines):
    s=open(p).read(); assert s.count(anchor)==1,(p,anchor); s=s.replace(anchor, anchor+''.join(lines),1); open(p,'w').write(s)
panels=['nav-panel','ai-debug-panel','terrain-panel','mixer-panel']
data=['bt-editor','fsm-editor','particle-editor','weather-editor','water-editor','mixer-editor','tileset-editor','sprite-editor']
items=['src/editor/ui/lrg-editor-'+p for p in panels]+['src/editor/ui/data/lrg-editor-'+d for d in data]
for ext in ('.h','.c'):
    ins('Makefile','\tsrc/editor/ui/data/lrg-editor-physics-asset-editor%s \\\n'%ext,['\t%s%s \\\n'%(i,ext) for i in items])
ins('src/libregnum.h','#include "editor/ui/data/lrg-editor-physics-asset-editor.h"\n',['#include "%s.h"\n'%i[4:] for i in items])
ins('tests/Makefile','\ttest-editor-app-physasset.c \\\n',['\ttest-editor-app-%s.c \\\n'%t for t in ['nav','ai','vfx','terrain','audio','2d']])
