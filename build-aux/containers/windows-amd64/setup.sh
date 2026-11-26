#!/bin/bash
# setup.sh - provision the windows-amd64 build container
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

# Runs once, inside `podman build' (Containerfile next to it).  Fedora
# ships the whole MinGW-w64 toolchain and GLib stack as mingw64-* packages,
# so this is package installation plus a smoke test that builds a GLib
# program for Windows and runs it under Wine.
#
# The same package lists are what a NO_BUILD_CONTAINER=1 host needs
# (docs/cross-compile.org, "Building without containers").

set -euo pipefail

# shellcheck source=build-aux/containers/common/common.sh
source /usr/local/share/lrg-build/common/common.sh

lrg_install_base

# The toolchain (mingw64-filesystem provides x86_64-w64-mingw32-pkg-config)
# and every library libregnum always links; mingw64-postgresql is libpq
lrg_dnf_install \
	mingw64-gcc mingw64-binutils mingw64-filesystem \
	mingw64-glib2 mingw64-json-glib mingw64-zlib mingw64-libyaml \
	mingw64-libsoup3 mingw64-openssl mingw64-sqlite mingw64-postgresql \
	wine-core wine-common wine-ldap util-linux-script

# Libraries behind optional features (each gated by a HAS_* probe)
lrg_dnf_install_optional mingw64-fribidi

# Wine runs the test binaries (LRG_EXEC_WRAPPER in the target profile),
# on a pseudo-terminal from util-linux-script (build-aux/cross/lrg-wine-run).
# wine-ldap: libpq.dll imports WLDAP32.dll.  wine-common is only a weak
# dependency of wine-core (and weak dependencies are not installed), but
# without its /usr/share/wine/winmd files wineboot's wine.inf setup retries
# the missing copies forever and never finishes.  Prove the toolchain end
# to end with it; the prefix the smoke test creates is kept as
# /opt/lrg-wine and a test run copies it into its build directory
# (LRG_TEST_SETUP in the profile) instead of spending a minute creating one
export WINEPREFIX=/opt/lrg-wine
export WINEDEBUG=-all
export WINEDLLOVERRIDES='mscoree,mshtml='
export WINEPATH=/usr/x86_64-w64-mingw32/sys-root/mingw/bin
XDG_RUNTIME_DIR="$(mktemp -d)"
export XDG_RUNTIME_DIR
lrg_smoke x86_64-w64-mingw32-gcc x86_64-w64-mingw32-pkg-config /tmp/smoke.exe wine
wineserver -w
chmod -R a+rX -- "${WINEPREFIX}"
rm -rf -- "${XDG_RUNTIME_DIR}" /tmp/smoke.exe
