#!/bin/bash
# check-level-noeditor.sh - prove the runtime level module needs no editor
#
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds libregnum with BUILD_EDITOR=0 BUILD_EDITOR_UI=0 into a separate
# build directory (default build/noeditor, so the normal build/release tree
# is untouched), then:
#   1. nm-checks the shared library: the runtime level API (lrg_level_*,
#      lrg_level_instantiate, the serializer, prefab load/instantiate,
#      LrgNode world matrices, core lrg_math3d_*) must be exported and the
#      editor objects (lrg_editor_new, lrg_editor_math_compose,
#      lrg_level_import_scene_file) must not;
#   2. builds tests/test-level-runtime.c against that library (without
#      -DLRG_BUILD_EDITOR) and runs it, so a .rlevel + .rprefab are loaded
#      and instantiated by a binary that cannot see the editor at all.
#
# The build directory must be relative to the repository root (the tests
# Makefile links with ../$(BUILDDIR)/lib).  In the shared 0.9.0 worktree
# wrap the call in the build lock:
#   flock "$L" tests/check-level-noeditor.sh

set -euo pipefail

usage () {
	echo 'usage: tests/check-level-noeditor.sh [-j N] [-k] [BUILDDIR]'
	echo
	echo '  BUILDDIR   build directory relative to the repo root (default: build/noeditor)'
	echo '  -j N       parallel make jobs (default: nproc)'
	echo '  -k         keep going: report every missing symbol before failing'
	echo
	echo 'examples:'
	echo '  tests/check-level-noeditor.sh'
	echo '  tests/check-level-noeditor.sh -j 8 build/noeditor-ci'
}

main () {
	local jobs
	local keep_going=0
	local builddir='build/noeditor'
	local root
	local so
	local sym
	local failed=0
	local -a make_vars
	local -a required=(
		lrg_level_new
		lrg_level_get_root
		lrg_level_find_node
		lrg_level_instantiate
		lrg_level_to_scene
		lrg_level_serializer_new
		lrg_level_serializer_load_from_file
		lrg_level_serializer_save_to_file
		lrg_node_new
		lrg_node_get_world_matrix
		lrg_node_visual_new
		lrg_node_tilemap_to_runtime
		lrg_timeline_new
		lrg_component_desc_new
		lrg_script_binding_new
		lrg_prefab_load
		lrg_prefab_load_resolved
		lrg_prefab_instantiate
		lrg_prefab_instantiate_linked
		lrg_math3d_compose
		lrg_math3d_decompose
		lrg_math3d_euler_to_quat
	)
	local -a forbidden=(
		lrg_editor_new
		lrg_editor_math_compose
		lrg_editor_selection_new
		lrg_level_import_scene_file
	)

	jobs="$(nproc)"
	while [[ $# -gt 0 ]]
	do
		case "${1}" in
			-h|--help)
				usage
				exit 0
				;;
			--license)
				echo 'AGPL-3.0-or-later'
				exit 0
				;;
			-j)
				jobs="${2:?-j needs a number}"
				shift 2
				;;
			-k)
				keep_going=1
				shift
				;;
			-*)
				usage >&2
				exit 2
				;;
			*)
				builddir="${1}"
				shift
				;;
		esac
	done

	if [[ "${builddir}" == /* ]]
	then
		echo "check-level-noeditor: BUILDDIR must be relative to the repo root" >&2
		exit 2
	fi

	root="$(realpath "$(dirname "${BASH_SOURCE[0]}")/..")"
	make_vars=(BUILD_EDITOR=0 BUILD_EDITOR_UI=0 "BUILDDIR=${builddir}")

	# 1. the library without the editor
	echo "== building libregnum with ${make_vars[*]}"
	make -C "${root}" -j"${jobs}" "${make_vars[@]}" lib

	so="${root}/${builddir}/lib/liblibregnum.so"
	if [[ ! -e "${so}" ]]
	then
		echo "check-level-noeditor: ${so} was not produced" >&2
		exit 1
	fi

	# 2. exported symbol check
	echo "== checking exported symbols of ${so}"
	local exported
	exported="$(nm -D --defined-only "${so}" | awk '{ print $3 }')"
	for sym in "${required[@]}"
	do
		if ! grep -qx "${sym}" <<< "${exported}"
		then
			echo "MISSING  ${sym}" >&2
			failed=1
			[[ ${keep_going} -eq 1 ]] || exit 1
		else
			echo "ok       ${sym}"
		fi
	done
	for sym in "${forbidden[@]}"
	do
		if grep -qx "${sym}" <<< "${exported}"
		then
			echo "PRESENT  ${sym} (editor code leaked into BUILD_EDITOR=0)" >&2
			failed=1
			[[ ${keep_going} -eq 1 ]] || exit 1
		else
			echo "absent   ${sym}"
		fi
	done
	[[ ${failed} -eq 0 ]] || exit 1

	# 3. runtime-only programs against that library
	local t
	for t in test-level-runtime test-level-instantiate-3d
	do
		echo "== building and running ${t} against it"
		make -C "${root}/tests" "${make_vars[@]}" "${builddir}/tests/${t}"
		LD_LIBRARY_PATH="${root}/${builddir}/lib:${root}/deps/graylib/build/lib:${root}/deps/yaml-glib/build${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
			"${root}/tests/${builddir}/tests/${t}"
	done

	echo "== BUILD_EDITOR=0 provides the runtime level module"
}

main "$@"
