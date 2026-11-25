/* lrg-platform.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Getting a process ready to run a game on platforms that need it
 * (Android): data directories, bundled assets, logging.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * SECTION:lrg-platform
 * @title: Platform preparation
 * @short_description: Data directories, bundled assets and logging per platform
 *
 * On a desktop a game starts with everything in place: a home directory,
 * XDG data/config/cache directories, its data files next to it (or
 * installed), and stderr for logs.  Android has none of that: an app
 * gets a private internal-storage directory, its files sit compressed
 * inside the APK, and stderr goes nowhere.
 *
 * lrg_platform_prepare() makes an Android app look like a desktop game to
 * the rest of the engine, before anything else runs:
 *
 * 1. `HOME`, `XDG_DATA_HOME`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME` and
 *    `TMPDIR` point into the app's internal storage
 *    (lrg_platform_set_data_root()), so g_get_user_data_dir() and friends,
 *    saves and settings work unchanged.
 * 2. The APK's `assets/` tree is copied into `<internal>/assets` the
 *    first time and whenever the APK changes (lrg_platform_extract_assets()
 *    with the manifest the APK packager writes), and the process changes
 *    into that directory, so relative data paths open as on a desktop.
 * 3. GLib's log output goes to logcat (`adb logcat -s libregnum`).
 *
 * Games built with LRG_DEFINE_GAME_MODULE() get this automatically
 * (lrg_game_run_standalone() calls it first), and #LrgEngine calls it at
 * startup as a safety net; a game with its own main() calls it as the
 * first thing.  On every other platform it does nothing and returns
 * %TRUE.  See docs/platforms/android.org.
 *
 * Since: 0.12.0
 */

/**
 * LRG_PLATFORM_ASSET_MANIFEST:
 *
 * The file in an APK's `assets/` (and in any directory given to
 * lrg_platform_extract_assets()) that lists what to extract.  The first
 * line is `# lrg-assets <stamp>`, where the stamp changes whenever the
 * contents do; every other non-empty line not starting with `#` is a
 * relative file path, `/`-separated.
 *
 * Since: 0.12.0
 */
#define LRG_PLATFORM_ASSET_MANIFEST "lrg-assets.list"

/**
 * LrgPlatformAssetReadFunc:
 * @path: a relative path from the asset manifest, or
 *   #LRG_PLATFORM_ASSET_MANIFEST itself
 * @user_data: the data given to lrg_platform_extract_assets()
 * @error: return location for a #GError
 *
 * Reads one bundled file whole.
 *
 * Returns: (transfer full) (nullable): its contents, or %NULL with @error set
 *
 * Since: 0.12.0
 */
typedef GBytes * (*LrgPlatformAssetReadFunc) (const gchar  *path,
                                              gpointer      user_data,
                                              GError      **error);

/**
 * lrg_platform_prepare:
 * @error: return location for a #GError
 *
 * Prepares the process for the platform it runs on (see the section
 * description).  Does nothing outside Android.  Safe to call more than
 * once: only the first call does the work, later calls return its result.
 *
 * Returns: %TRUE on success (always outside Android)
 *
 * Since: 0.12.0
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_platform_prepare           (GError                    **error);

/**
 * lrg_platform_set_data_root:
 * @root: (type filename): a writable directory
 * @error: return location for a #GError
 *
 * Makes @root the home of the process's per-user files: sets `HOME` to
 * @root and `XDG_DATA_HOME`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME` and
 * `TMPDIR` to `data`, `config`, `cache` and `tmp` below it, creating the
 * directories.  Call it before anything asks GLib for those directories:
 * GLib reads the environment once.
 *
 * Android does this with the app's internal storage; on a desktop it
 * gives a portable install (everything next to the game) or an isolated
 * test.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.12.0
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_platform_set_data_root     (const gchar                *root,
                                             GError                    **error);

/**
 * lrg_platform_extract_assets:
 * @read: (scope call) (closure user_data): reads one bundled file
 * @user_data: data for @read
 * @dest: (type filename): directory to extract into
 * @error: return location for a #GError
 *
 * Copies the bundled files listed in #LRG_PLATFORM_ASSET_MANIFEST into
 * @dest.  Nothing is copied when @dest already holds the manifest's stamp
 * (`<dest>/.lrg-assets-stamp`).  Otherwise the files go into a fresh
 * sibling directory first, which then replaces @dest, so an interrupted
 * extraction never leaves a mix of two versions and files the new
 * version dropped disappear.
 *
 * Every listed path must be relative, without `.` or `..` components or
 * empty components; a manifest with any other path is rejected
 * (%G_IO_ERROR_INVALID_DATA) before anything is written.
 *
 * Returns: %TRUE when @dest holds the manifest's files
 *
 * Since: 0.12.0
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_platform_extract_assets    (LrgPlatformAssetReadFunc    read,
                                             gpointer                    user_data,
                                             const gchar                *dest,
                                             GError                    **error);

G_END_DECLS
