#!/bin/bash
# full milestone gate; logs to $SP/g67-*.log
cd /var/home/zach/source/projects/libregnum/trees/v0.9.0 || exit 1
SP=/tmp/claude-1000/-var-home-zach-source-projects-libregnum/c474e1e8-b6e5-4b4b-bd42-fe50c0433c3f/scratchpad/v090
L=$SP/build.lock
r () { local n="${1}"; shift; if flock "$L" "$@" > "$SP/g67-${n}.log" 2>&1; then echo "PASS ${n}"; else echo "FAIL ${n}"; fi; }
r all make -j"$(nproc)" all
r docs make docs-check
r ex make -j"$(nproc)" examples
r noed bash tests/check-level-noeditor.sh
r test build-aux/xrun.sh make -j"$(nproc)" test
r ctl build-aux/xrun.sh make test-editorctl
r emacs build-aux/xrun.sh make test-emacs
r x11 build-aux/xrun.sh make test-editor-x11
echo GATE-DONE
