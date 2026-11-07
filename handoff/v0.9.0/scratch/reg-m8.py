# register the M8 stubs (run from the worktree root after mkstub/mktest)
def ins(p, anchor, lines):
    s=open(p).read(); assert s.count(anchor)==1,(p,anchor); s=s.replace(anchor, anchor+''.join(lines),1); open(p,'w').write(s)
groups=[
 ('src/scripting/lrg-script-scheduler',['src/scripting/lrg-script-debugger','src/scripting/lrg-script-reloader','src/scripting/lrg-dap-server','src/scripting/lrg-scripting-graph','src/scripting/lrg-vscript-program']),
 ('src/debug/lrg-profiler',['src/debug/lrg-gpu-timer','src/debug/lrg-frame-capture','src/debug/lrg-memory-profiler']),
 ('src/net/lrg-net-client',['src/net/lrg-net-stats']),
 ('src/editor/lrg-editor-document',['src/editor/lrg-lsp-client','src/editor/lrg-editor-script-api']),
 ('src/editor/ui/lrg-editor-mixer-panel',['src/editor/ui/lrg-editor-debug-panel','src/editor/ui/lrg-editor-frame-debugger','src/editor/ui/lrg-editor-memory-panel','src/editor/ui/lrg-editor-network-panel']),
 ('src/editor/ui/data/lrg-editor-mixer-editor',['src/editor/ui/data/lrg-editor-vscript-editor','src/editor/ui/data/lrg-editor-blocks-editor']),
]
new_hdrs=[]
for anchor,items in groups:
    for ext in ('.h','.c'):
        ins('Makefile','\t%s%s \\\n'%(anchor,ext),['\t%s%s \\\n'%(i,ext) for i in items])
    ins('src/libregnum.h','#include "%s.h"\n'%anchor[4:],['#include "%s.h"\n'%i[4:] for i in items])
    if not anchor.startswith('src/editor'):
        new_hdrs+= [i+'.h' for i in items]
s=open('Makefile').read()
a='DOCS_CHECK_HEADERS := '
assert s.count(a)==1
s=s.replace(a,a+' '.join(new_hdrs)+' ',1); open('Makefile','w').write(s)
ins('tests/Makefile','\ttest-editor-app-2d.c \\\n',['\t%s \\\n'%t for t in ['test-lsp-client.c','test-editor-app-debug.c','test-editor-app-vscript.c','test-editor-app-toolscript.c','test-editor-app-frame.c','test-editor-app-memnet.c']])
ins('tests/Makefile','\ttest-script-scheduler.c \\\n',['\t%s \\\n'%t for t in ['test-script-reload.c','test-script-debugger.c','test-dap-server.c','test-vscript.c','test-profiler-capture.c','test-frame-capture.c','test-memory-profiler.c','test-net-stats.c','test-ui-dock-overflow.c']])
