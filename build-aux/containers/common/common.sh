#!/bin/bash
# common.sh - shared steps of every libregnum build container
# Copyright (C) 2026  Zach Podbielniak
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Sourced by build-aux/containers/<target>/setup.sh while an image is built
# (never on the host).  Provides:
#
#   lrg_log MESSAGE...             progress line
#   lrg_die MESSAGE...             error line, exit 1
#   lrg_dnf_install PACKAGE...     install; every package must exist
#   lrg_dnf_install_optional PACKAGE...
#                                  install what exists, report the rest
#   lrg_install_base               the packages every build needs
#   lrg_smoke CC PKG_CONFIG OUT [RUNNER...]
#                                  compile common/smoke.c for the target,
#                                  and run it through RUNNER if one is given
#
# The image's setup lives under /usr/local/share/lrg-build/ (common/ and
# target/), so a container shell can see how it was made.

LRG_SHARE=/usr/local/share/lrg-build

lrg_log () {
	printf '==> %s\n' "$*"
}

lrg_die () {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

lrg_dnf_install () {
	if [[ $# -eq 0 ]]
	then
		lrg_die 'lrg_dnf_install() requires at least one package'
	fi

	lrg_log "dnf install $*"
	dnf -y install --setopt=install_weak_deps=False "$@" || lrg_die "dnf install failed: $*"
}

# Packages that may not exist on every Fedora release (a MinGW port that
# was dropped, say): install the ones that do and name the ones that do
# not, so the build's own feature probes decide what gets compiled
lrg_dnf_install_optional () {
	if [[ $# -eq 0 ]]
	then
		lrg_die 'lrg_dnf_install_optional() requires at least one package'
	fi

	local package
	local present=()
	local missing=()

	for package in "$@"
	do
		# The output is read whole, not piped into grep -q: an early exit
		# would kill dnf with SIGPIPE and pipefail would call it missing
		if [[ -n "$(dnf -q repoquery --available "${package}" 2>/dev/null)" ]]
		then
			present+=("${package}")
		else
			missing+=("${package}")
		fi
	done
	if [[ ${#present[@]} -gt 0 ]]
	then
		lrg_dnf_install "${present[@]}"
	fi
	if [[ ${#missing[@]} -gt 0 ]]
	then
		lrg_log "not available on this release (features that need them are built off): ${missing[*]}"
		printf '%s\n' "${missing[@]}" > "${LRG_SHARE}/missing-packages"
	fi
}

# What every target's build runs on the build side: make, the host-side
# code generators (glib-mkenums is python), git (graylib asks it about the
# raylib submodule), rsync (out-of-tree raylib), the smoke test's compiler
lrg_install_base () {
	lrg_dnf_install \
		make gcc pkgconf-pkg-config glib2-devel python3 \
		git-core rsync findutils diffutils sed gawk file which \
		ShellCheck
}

lrg_smoke () {
	if [[ $# -lt 3 ]]
	then
		lrg_die 'lrg_smoke() requires CC PKG_CONFIG OUT [RUNNER...]'
	fi

	local cc="${1}"
	local pkg_config="${2}"
	local out="${3}"
	local flags
	shift 3

	lrg_log "smoke test: ${cc} -> ${out}"
	flags="$("${pkg_config}" --cflags --libs glib-2.0 gobject-2.0 gio-2.0 json-glib-1.0)" \
		|| lrg_die "${pkg_config} does not find the GLib stack"
	# shellcheck disable=SC2086 # pkg-config output is a word list
	"${cc}" -std=gnu89 -Wall -Wextra -Werror -o "${out}" "${LRG_SHARE}/common/smoke.c" ${flags} \
		|| lrg_die "${cc} cannot build a GLib program"
	file "${out}"
	if [[ $# -gt 0 ]]
	then
		"$@" "${out}" || lrg_die "the smoke test does not run under: $*"
	fi
}
