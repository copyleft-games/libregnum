#!/bin/bash
# setup.sh - provision the linux-arm64 build container
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

# Runs once, inside `podman build'.  A true cross build, no emulation:
#
#   1. the host-arch side: Fedora's aarch64 cross gcc and binutils, plus
#      wayland-scanner (raylib generates protocol code at build time) and
#      qemu-user-static-aarch64 (runs the test binaries, no binfmt needed)
#   2. /opt/sysroot/aarch64: a Fedora aarch64 root with the -devel packages
#      libregnum links, installed with dnf --forcearch.  Scriptlets are
#      skipped (noscripts): they are aarch64 programs, and a sysroot only
#      needs headers, libraries and .pc files.  Its gcc provides the crt
#      files and libgcc the kernel-only cross gcc lacks
#      (build-aux/cross/lrg-aarch64-linux-gnu-gcc).
#   3. a smoke test: a GLib program built with the wrapper and run under qemu
#
# The cross gcc and the sysroot's gcc must share a major version; both come
# from the same Fedora release, and the smoke test fails if they do not.

set -euo pipefail

# shellcheck source=build-aux/containers/common/common.sh
source /usr/local/share/lrg-build/common/common.sh

SYSROOT=/opt/sysroot/aarch64

lrg_install_base

# dbus-daemon: GTestDBus spawns it from the aarch64 tests; under qemu-user a
# host program runs natively (only aarch64 ones need emulation)
lrg_dnf_install \
	gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
	wayland-devel qemu-user-static-aarch64 dbus-daemon shared-mime-info

# Fedora release of this image, so the sysroot matches it
releasever="$(rpm -E '%{fedora}')"

lrg_log "aarch64 sysroot (Fedora ${releasever}) in ${SYSROOT}"
# Fedora's usr-merge layout, made before anything is installed: otherwise an
# early package creates /lib64 as a directory and the filesystem package,
# which ships it as a symlink, fails to unpack
mkdir -p -- "${SYSROOT}/usr/bin" "${SYSROOT}/usr/lib" "${SYSROOT}/usr/lib64"
ln -s usr/bin "${SYSROOT}/bin"
ln -s usr/lib "${SYSROOT}/lib"
ln -s usr/lib64 "${SYSROOT}/lib64"
ln -s usr/sbin "${SYSROOT}/sbin"
ln -s bin "${SYSROOT}/usr/sbin"
dnf -y install \
	--installroot="${SYSROOT}" \
	--forcearch=aarch64 \
	--releasever="${releasever}" \
	--use-host-config \
	--setopt=install_weak_deps=False \
	--setopt=tsflags=noscripts \
	glibc-devel gcc \
	glib2-devel json-glib-devel libsoup3-devel libyaml-devel libdex-devel \
	openssl-devel sqlite-devel zlib-devel libpq-devel readline-devel \
	fribidi-devel luajit-devel alsa-lib-devel \
	mesa-libGL-devel mesa-libEGL-devel \
	libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel \
	wayland-devel libxkbcommon-devel \
	fontconfig google-noto-sans-vf-fonts shared-mime-info \
	--exclude='dejavu-*' \
	|| lrg_die 'could not install the aarch64 sysroot'

# What the skipped scriptlets would have done that the tests read (under
# qemu-aarch64 -L, absolute paths resolve inside the sysroot first): the
# compiled GSettings schemas and the MIME database (file type tests).
# Fonts: only Noto Sans, the host's UI font, so text metrics match a native
# run (fontconfig would otherwise prefer DejaVu)
glib-compile-schemas "${SYSROOT}/usr/share/glib-2.0/schemas" \
	|| lrg_die 'could not compile the sysroot GSettings schemas'
update-mime-database "${SYSROOT}/usr/share/mime" \
	|| lrg_die 'could not build the sysroot MIME database'
dnf -y --installroot="${SYSROOT}" --use-host-config clean all > /dev/null 2>&1 || true

# The wrappers ship in the checkout (build-aux/cross); copy them so the
# smoke test does not depend on a mount
install -m 755 /usr/local/share/lrg-build/cross/lrg-aarch64-linux-gnu-gcc /usr/local/bin/
install -m 755 /usr/local/share/lrg-build/cross/lrg-sysroot-pkg-config /usr/local/bin/

export LRG_SYSROOT="${SYSROOT}"
lrg_smoke lrg-aarch64-linux-gnu-gcc lrg-sysroot-pkg-config /tmp/smoke-arm64 \
	qemu-aarch64-static -L "${SYSROOT}"
rm -f -- /tmp/smoke-arm64
