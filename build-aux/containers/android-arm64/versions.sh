#!/bin/bash
# versions.sh - pinned inputs of the android-arm64 build container
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

# Sourced by setup.sh and build-sysroot.sh.  Every download is pinned to a
# version and a checksum; changing any value here changes the image tag
# (build-aux/mk/container.mk hashes this directory), so bumping a pin is
# editing this file and running `make ANDROID=1 container-image'.
#
# Android SDK components come straight from Google's repository manifest
# (https://dl.google.com/android/repository/repository2-3.xml), the same
# archives and SHA-1 sums sdkmanager uses; no sdkmanager is needed.  They
# are distributed under the Android Software Development Kit License
# Agreement, which is not a free software licence; see
# docs/platforms/android.org, "Licensing".
#
# The GLib stack is built from source tarballs with SHA-256 sums.

# shellcheck disable=SC2034 # every variable here is read by the sourcing script

ANDROID_REPO_URL=https://dl.google.com/android/repository

# NDK r30 (ndk;30.0.16248370): clang, sysroot, 16 KB page alignment default
ANDROID_NDK_VERSION=30.0.16248370
ANDROID_NDK_ZIP=android-ndk-r30-linux.zip
ANDROID_NDK_SHA1=5107f898313790e449e87eee2183d9a20602dee9

# build-tools 37.0.0: aapt2, zipalign, apksigner (a jar; needs a JDK)
ANDROID_BUILD_TOOLS_VERSION=37.0.0
ANDROID_BUILD_TOOLS_ZIP=build-tools_r37_linux.zip
ANDROID_BUILD_TOOLS_SHA1=70954e99f4c3d9d46ee70fa32624672fe7cd6ebe

# platforms;android-36: android.jar, the API the APK is linked against
ANDROID_PLATFORM_API=36
ANDROID_PLATFORM_ZIP=platform-36_r02.zip
ANDROID_PLATFORM_SHA1=2c1a80dd4d9f7d0e6dd336ec603d9b5c55a6f576

# The oldest Android the libraries and APKs run on (bionic has iconv from
# 28; 29 is Android 10)
ANDROID_MIN_API=29

# The GLib stack, built static for arm64-v8a by build-sysroot.sh.  Versions
# follow Fedora 45 (the base image), so libregnum sees the same API on
# every target.  GNOME sums are from download.gnome.org's .sha256sum files;
# sqlite's was checked against the SHA3-256 on sqlite.org; libyaml and
# fribidi publish none, so theirs were recorded on first download.
# libffi, pcre2 and proxy-libintl come from GLib's own meson wraps (pinned
# by GLib), built as subprojects.
GLIB_VERSION=2.90.1
GLIB_URL=https://download.gnome.org/sources/glib/2.90/glib-2.90.1.tar.xz
GLIB_SHA256=93c941aa17d5eb1d53fe838365f29a8b4e539c222a256d974ec8f30fc413e396

JSON_GLIB_VERSION=1.10.8
JSON_GLIB_URL=https://download.gnome.org/sources/json-glib/1.10/json-glib-1.10.8.tar.xz
JSON_GLIB_SHA256=55c5c141a564245b8f8fbe7698663c87a45a7333c2a2c56f06f811ab73b212dd

LIBDEX_VERSION=1.2.0
LIBDEX_URL=https://download.gnome.org/sources/libdex/1.2/libdex-1.2.0.tar.xz
LIBDEX_SHA256=69f5b244c21f6402831c3ab8af85a01772f72fb118677388436f12f32bfb06d9

LIBYAML_VERSION=0.2.5
LIBYAML_URL=https://pyyaml.org/download/libyaml/yaml-0.2.5.tar.gz
LIBYAML_SHA256=c642ae9b75fee120b2d96c712538bd2cf283228d2337df2cf2988e3c02678ef4

FRIBIDI_VERSION=1.0.16
FRIBIDI_URL=https://github.com/fribidi/fribidi/releases/download/v1.0.16/fribidi-1.0.16.tar.xz
FRIBIDI_SHA256=1b1cde5b235d40479e91be2f0e88a309e3214c8ab470ec8a2744d82a5a9ea05c

SQLITE_VERSION=3.54.1
SQLITE_URL=https://www.sqlite.org/2026/sqlite-autoconf-3540100.tar.gz
SQLITE_SHA256=926ef8d3eab93fbf6e06011901273eec076b25594a4e1cdadd50c987f4f6d64e
