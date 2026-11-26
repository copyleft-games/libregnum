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

#include <glib-object.h>
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

/* ==========================================================================
 * Storage
 * ========================================================================== */

/**
 * lrg_platform_get_external_data_dir:
 *
 * The directory where the player can put large files for the game by
 * hand (a USB file transfer, a file manager), as opposed to the private
 * internal storage lrg_platform_prepare() points `HOME` at.
 *
 * On Android this is the activity's `externalDataPath`
 * (`/sdcard/Android/data/<package>/files`): visible over USB, needs no
 * storage permission, and is removed with the app.  On every other
 * platform it is `g_get_user_data_dir()/<prgname>` (the last component
 * of `g_get_prgname()`, or `libregnum` when no program name is set), so a
 * game can look in one
 * place on every platform.  The directory is created when missing.
 *
 * Returns: (nullable) (type filename): the directory, or %NULL when there
 *   is none (Android without shared storage mounted) or it cannot be
 *   created; the string lives as long as the process
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_platform_get_external_data_dir (void);

/**
 * lrg_platform_get_dp_scale:
 *
 * The number of pixels in one density-independent pixel (dp, 1/160 inch)
 * on the current display: Android's screen density divided by 160
 * (`AConfiguration_getDensity()`), 1.0 everywhere else.  Touch layouts
 * are written in dp and multiplied by this.
 *
 * Returns: pixels per dp, at least 0.5
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gfloat       lrg_platform_get_dp_scale      (void);

/**
 * lrg_platform_get_display_size:
 * @width: (out) (optional): the display's width in pixels
 * @height: (out) (optional): the display's height in pixels
 *
 * The size of the screen area an app's window gets, in pixels, known
 * before any window opens: on Android, the configuration's screen size
 * in dp (`AConfiguration_getScreenWidthDp()`, `..._HeightDp()`) times
 * the dp scale, in the orientation the configuration reports (which may
 * still be the launcher's until the activity's own orientation applies:
 * a landscape game takes the larger as its width).  Open a game's window
 * at this size on Android: raylib renders a smaller window small and
 * upscales it, so everything placed in screen pixels (touch controls in
 * dp, text) comes out too large, and it cannot learn the display's size
 * itself before its window exists.  Everywhere else there is no such
 * answer (a desktop window is whatever size the game asks for).
 *
 * Returns: %TRUE with the size, %FALSE (outputs untouched) where the
 *   platform does not say
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_platform_get_display_size  (gint *width,
                                             gint *height);

/* ==========================================================================
 * Soft keyboard and text input
 * ========================================================================== */

/**
 * lrg_platform_show_soft_keyboard:
 *
 * Asks the system to show its on-screen keyboard (Android:
 * `ANativeActivity_showSoftInput()` with
 * `ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED`).  Call it when a text field
 * gains focus.  Does nothing on platforms without one.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void         lrg_platform_show_soft_keyboard (void);

/**
 * lrg_platform_hide_soft_keyboard:
 *
 * Hides the on-screen keyboard again (Android:
 * `ANativeActivity_hideSoftInput()`).  Does nothing on platforms without
 * one.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void         lrg_platform_hide_soft_keyboard (void);

/**
 * lrg_platform_is_soft_keyboard_requested:
 *
 * Whether the last call was lrg_platform_show_soft_keyboard() rather than
 * lrg_platform_hide_soft_keyboard().  The system may still hide the
 * keyboard on its own (the back key); this only reports what the game
 * asked for.
 *
 * Returns: %TRUE after a show request
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_platform_is_soft_keyboard_requested (void);

/**
 * lrg_platform_get_char_pressed:
 *
 * The next typed character, like raylib's `GetCharPressed()`, but one
 * that also works on Android: raylib's Android layer never fills its
 * character queue, so libregnum translates key-down events from the
 * hardware or soft keyboard into characters itself (with the key's meta
 * state, through `KeyEvent.getUnicodeChar()`), once lrg_platform_poll()
 * has run.  Text widgets read characters through this function.
 *
 * Elsewhere it returns raylib's `GetCharPressed()`.  Call it in a loop
 * until it returns 0.  See docs/platforms/android.org, "Soft keyboard",
 * for what an Android IME can and cannot deliver.
 *
 * Returns: a Unicode code point, or 0 when the queue is empty
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gunichar     lrg_platform_get_char_pressed  (void);

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

#define LRG_TYPE_PLATFORM (lrg_platform_get_type ())

/**
 * LrgPlatform:
 *
 * The platform's application lifecycle as signals.  There is one, from
 * lrg_platform_get_default().
 *
 * #LrgPlatform::paused is emitted when the app stops being shown and
 * interactive (Android: it lost focus or its window), and
 * #LrgPlatform::resumed when it is back, saying whether the GL context
 * survived.  On Android they are emitted from inside raylib's event
 * processing (`EndDrawing()` / `PollInputEvents()`), as the events
 * arrive, so a handler runs while the app is going away: save there, do
 * not draw.  The desktop never emits them on its own;
 * lrg_platform_simulate_pause() and lrg_platform_simulate_resume() emit
 * them for tests and for games that want one code path.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPlatform, lrg_platform, LRG, PLATFORM, GObject)

/**
 * lrg_platform_get_default:
 *
 * Returns: (transfer none): the process's #LrgPlatform
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlatform *lrg_platform_get_default       (void);

/**
 * lrg_platform_poll:
 *
 * Per-frame platform work; #LrgEngine's update and #LrgGameTemplate's
 * frame call it, a game with its own loop calls it once per frame after
 * the window is open.  On Android the first call after `InitWindow()`
 * chains libregnum's handlers in front of raylib's app-command and
 * input-event callbacks (raylib's own handling still runs first and
 * unchanged), which is what emits the lifecycle signals and feeds
 * lrg_platform_get_char_pressed().  On Android it also follows the
 * native window's size (a foldable opened or closed, split screen, a
 * freeform window: lrg_platform_apply_window_size()), which raylib does
 * not; on the desktop it emits #LrgPlatform::window-resized for the
 * resizes raylib follows itself.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void         lrg_platform_poll              (void);

/**
 * lrg_platform_apply_window_size:
 * @self: an #LrgPlatform
 * @width: the native window's width, in pixels
 * @height: its height, in pixels
 *
 * Makes raylib draw to a native window that is now @width x @height
 * (graylib's grl_native_window_resize(): the screen, render and
 * framebuffer sizes, the viewport and the 2D projection; no stretching)
 * and emits #LrgPlatform::window-resized.  lrg_platform_poll() calls it
 * on Android, after resizing the native buffers; it is public for
 * embedders that learn of native resizes some other way, and for tests.
 * Not for a desktop GLFW window: raylib follows those itself.  Call it
 * between frames.
 *
 * Returns: %TRUE when the size changed; %FALSE when it is the size raylib
 *   already has, not positive, no window is open, or graylib cannot
 *   update this raylib (grl_native_window_can_resize())
 *
 * Since: 0.13.3
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_platform_apply_window_size (LrgPlatform *self,
                                             gint         width,
                                             gint         height);

/**
 * lrg_platform_is_running:
 * @self: an #LrgPlatform
 *
 * Returns: %FALSE between #LrgPlatform::paused and #LrgPlatform::resumed
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_platform_is_running        (LrgPlatform *self);

/**
 * lrg_platform_simulate_pause:
 * @self: an #LrgPlatform
 *
 * Emits #LrgPlatform::paused as Android would when the app goes to the
 * background (nothing when already paused).  For tests, and for desktop
 * games that pause on minimise with the same handlers.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void         lrg_platform_simulate_pause    (LrgPlatform *self);

/**
 * lrg_platform_simulate_resume:
 * @self: an #LrgPlatform
 * @gl_context_lost: the value #LrgPlatform::resumed reports
 *
 * Emits #LrgPlatform::resumed as Android would when the app comes back
 * (nothing when not paused).
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void         lrg_platform_simulate_resume   (LrgPlatform *self,
                                             gboolean     gl_context_lost);

G_END_DECLS
