#!/bin/bash
# versions.sh - pinned inputs of `make ANDROID=1 test-emulator'
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

# Sourced by emulator-test.sh.  Each archive comes from Google's Android
# repository manifests (repository2-3.xml and sys-img/google_apis/
# sys-img2-3.xml), with the SHA-1 those manifests give; sdkmanager is not
# needed.  They are distributed under the Android Software Development Kit
# License Agreement, which is not a free software licence (see
# docs/platforms/android.org, "Licensing"), and are only downloaded when
# test-emulator runs, into its cache, never into a build image.
#
# The system image is x86_64 so it runs under KVM at native speed; Google
# API images from Android 11 on run arm64-v8a apps through a translation
# layer (ro.system.product.cpu.abilist=x86_64,arm64-v8a), so the
# arm64-v8a APK runs unchanged.  On an aarch64 host use the arm64-v8a
# image of the same release instead (EMULATOR_SYSIMG_* below).

# shellcheck disable=SC2034 # every variable here is read by the sourcing script

ANDROID_REPO_URL=https://dl.google.com/android/repository

# emulator 37.3.10, stable channel
EMULATOR_ZIP=emulator-linux_x64-16970787.zip
EMULATOR_SHA1=5f7314368fc6c05c937f6991bb4106214dc389e7

# platform-tools r37.0.3: adb
PLATFORM_TOOLS_ZIP=platform-tools_r37.0.3-linux.zip
PLATFORM_TOOLS_SHA1=e1579b288a7ae4e9bc28dedec6a154d78b3c04ed

# system-images;android-35;google_apis;x86_64 (Android 15)
EMULATOR_SYSIMG_API=35
EMULATOR_SYSIMG_TAG=google_apis
EMULATOR_SYSIMG_ABI=x86_64
EMULATOR_SYSIMG_ZIP=sys-img/google_apis/x86_64-35_r09.zip
EMULATOR_SYSIMG_SHA1=0103e6dab21290c4b9d16550a3ce99476f884eef
