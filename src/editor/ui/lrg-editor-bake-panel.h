/* lrg-editor-bake-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBakePanel: the lighting bake in lrgeditor (0.9.0 M3).
 *
 * A docked panel over the level's `lighting:` block: the bake settings
 * (`lighting: settings:`, an #LrgLightmapBakeSettings in a property grid,
 * every edit one undo step), a Bake button running #LrgLightmapBaker on
 * worker threads with a progress bar per phase and Cancel, a results
 * summary, the atlas pages, a "light at the viewport centre" readout
 * (lrg_lightmap_baker_evaluate()), Save (the `.rlightmap` and `.rprobes`
 * next to the level plus the `lighting:` references, one undo step) and
 * Clear.  It also registers the `bake.*` IPC methods and the `bake.*`
 * actions.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-property-grid.h"
#include "../../lighting/lrg-lightmap-baker.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_BAKE_PANEL_ID:
 *
 * The dock panel id of the bake panel ("bake").
 */
#define LRG_EDITOR_BAKE_PANEL_ID "bake"

#define LRG_TYPE_EDITOR_BAKE_PANEL (lrg_editor_bake_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorBakePanel, lrg_editor_bake_panel, LRG, EDITOR_BAKE_PANEL, LrgContainer)

/**
 * lrg_editor_bake_panel_new:
 * @app: the editor application
 *
 * Creates the bake panel and registers its actions (`bake.show`,
 * `bake.start`, `bake.cancel`, `bake.save`, `bake.clear`,
 * `bake.toggle-preview`) and, when @app has an IPC server, the `bake.*`
 * methods.  The window docks it with the id #LRG_EDITOR_BAKE_PANEL_ID.
 *
 * Returns: (transfer full): a new #LrgEditorBakePanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorBakePanel * lrg_editor_bake_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_bake_panel_find:
 * @app: the editor application
 *
 * Returns: (transfer none) (nullable): the bake panel of @app, if one was
 *   created
 */
LRG_AVAILABLE_IN_ALL
LrgEditorBakePanel * lrg_editor_bake_panel_find (LrgEditorApp *app);

/**
 * lrg_editor_bake_panel_get_settings:
 * @self: an #LrgEditorBakePanel
 *
 * The level's bake settings as an object: the defaults with every key of
 * `lighting: settings:` applied.  The panel's grid edits it; setting a
 * property on it directly is the same as an edit in the grid (one undo
 * step on the level).
 *
 * Returns: (transfer none): the settings
 */
LRG_AVAILABLE_IN_ALL
LrgLightmapBakeSettings * lrg_editor_bake_panel_get_settings (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_get_grid:
 * @self: an #LrgEditorBakePanel
 *
 * Returns: (transfer none): the settings grid
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_bake_panel_get_grid (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_set_setting:
 * @self: an #LrgEditorBakePanel
 * @key: a bake setting (`texels_per_unit`, `samples`, `bounces`,
 *   `max_lightmap_size`, `padding`, `denoise`, `probe_spacing`,
 *   `probe_samples`, `auto_probe_volume`, `seed`, `threads`,
 *   `light_radius`, `sun_angle`; `-` works for `_`)
 * @value: the value (booleans as 0 / 1)
 * @error: (nullable): return location for an error
 *
 * Writes one key of `lighting: settings:` (one undo step), clamped into
 * the setting's range.
 *
 * Returns: %TRUE on success; %FALSE for an unknown key or without a level
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bake_panel_set_setting (LrgEditorBakePanel  *self,
                                            const gchar         *key,
                                            gdouble              value,
                                            GError             **error);

/**
 * lrg_editor_bake_panel_reset_settings:
 * @self: an #LrgEditorBakePanel
 *
 * Drops every `lighting: settings:` key (one undo step), so the bake runs
 * with the defaults.
 *
 * Returns: %TRUE when something changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bake_panel_reset_settings (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_bake:
 * @self: an #LrgEditorBakePanel
 * @error: (nullable): return location for an error
 *
 * Starts a bake of the open level: the level is instantiated into a
 * private #LrgWorld3D (models from the viewport's cache), stepped once,
 * handed to an #LrgLightmapBaker with the panel's settings and the
 * probe-volume nodes, and baked on worker threads
 * (lrg_lightmap_baker_bake_async()).  Progress arrives on the main
 * context.  When it finishes the viewport shows the result at once; with
 * auto save on and a saved level the files and references are written
 * too (lrg_editor_bake_panel_save()).  Works without a window: the baker
 * runs on the CPU.
 *
 * Returns: %TRUE when the bake started
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bake_panel_bake (LrgEditorBakePanel  *self,
                                     GError             **error);

/**
 * lrg_editor_bake_panel_cancel:
 * @self: an #LrgEditorBakePanel
 *
 * Cancels a running bake; the level and the viewport keep the previous
 * result.
 *
 * Returns: %TRUE when a bake was running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bake_panel_cancel (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_get_baking:
 * @self: an #LrgEditorBakePanel
 *
 * Returns: whether a bake is running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bake_panel_get_baking (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_get_progress:
 * @self: an #LrgEditorBakePanel
 *
 * Returns: the running (or last) bake's progress, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_editor_bake_panel_get_progress (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_get_phase:
 * @self: an #LrgEditorBakePanel
 *
 * The bake state: `idle`, `prepare`, `lightmaps`, `postprocess`,
 * `probes`, `done`, `cancelled` or `failed`.
 *
 * Returns: (transfer none): the phase name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_bake_panel_get_phase (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_get_error:
 * @self: an #LrgEditorBakePanel
 *
 * Returns: (transfer none) (nullable): why the last bake failed
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_bake_panel_get_error (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_get_lightmap:
 * @self: an #LrgEditorBakePanel
 *
 * Returns: (transfer none) (nullable): the last bake's lightmap
 */
LRG_AVAILABLE_IN_ALL
LrgLightmap3D * lrg_editor_bake_panel_get_lightmap (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_get_probe_grids:
 * @self: an #LrgEditorBakePanel
 *
 * Returns: (transfer none) (element-type LrgProbeGrid): the last bake's
 *   probe grids (empty before a bake)
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_bake_panel_get_probe_grids (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_has_unsaved:
 * @self: an #LrgEditorBakePanel
 *
 * Returns: %TRUE when the last bake is not written to disk yet
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bake_panel_has_unsaved (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_set_auto_save:
 * @self: an #LrgEditorBakePanel
 * @auto_save: whether a finished bake saves itself (when the level has a
 *   file)
 *
 * On by default.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_bake_panel_set_auto_save (LrgEditorBakePanel *self,
                                          gboolean            auto_save);

/**
 * lrg_editor_bake_panel_get_auto_save:
 * @self: an #LrgEditorBakePanel
 *
 * Returns: whether a finished bake saves itself
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bake_panel_get_auto_save (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_save:
 * @self: an #LrgEditorBakePanel
 * @dir: (nullable): the directory to write into, %NULL for the level's
 * @error: (nullable): return location for an error
 *
 * Writes the last bake: `<level>.rlightmap` (with its `.hdr` pages) and
 * `<level>-<n>.rprobes` per probe grid, then points the level's
 * `lighting: lightmap:` / `probes:` at them (relative to the level when
 * @dir is its directory) in one undo step.  Undo restores the previous
 * references; the files stay on disk.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bake_panel_save (LrgEditorBakePanel  *self,
                                     const gchar         *dir,
                                     GError             **error);

/**
 * lrg_editor_bake_panel_clear:
 * @self: an #LrgEditorBakePanel
 *
 * Drops the level's lightmap and probe references (one undo step; the
 * settings and the files stay) and the unsaved result.
 *
 * Returns: %TRUE when something changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bake_panel_clear (LrgEditorBakePanel *self);

/**
 * lrg_editor_bake_panel_evaluate:
 * @self: an #LrgEditorBakePanel
 * @position: (array fixed-size=3): a world point on a surface
 * @normal: (array fixed-size=3): the surface normal there
 * @out_rgb: (out caller-allocates) (array fixed-size=3): linear
 *   irradiance / pi, as a lightmap texel there would hold it
 *
 * Probes the baked light at one point with the current settings
 * (lrg_lightmap_baker_evaluate()), over the scene of the last bake or a
 * scene collected now.
 *
 * Returns: %FALSE without a level, while a bake runs, or with nothing to
 *   trace
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bake_panel_evaluate (LrgEditorBakePanel *self,
                                         const gfloat       *position,
                                         const gfloat       *normal,
                                         gfloat             *out_rgb);

/**
 * lrg_editor_bake_panel_sample_cursor:
 * @self: an #LrgEditorBakePanel
 * @out_position: (out caller-allocates) (array fixed-size=3): the point
 * @out_normal: (out caller-allocates) (array fixed-size=3): its normal
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the light there
 *
 * The panel's "light at cursor" readout: the surface under the active
 * viewport's centre (crosshair) is picked (lrg_editor_pick()), its
 * normal taken from the face of the node's bounds the ray enters, and
 * the light there evaluated (lrg_editor_bake_panel_evaluate()).
 *
 * Returns: %FALSE when the ray hits nothing or nothing can be traced
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bake_panel_sample_cursor (LrgEditorBakePanel *self,
                                              gfloat             *out_position,
                                              gfloat             *out_normal,
                                              gfloat             *out_rgb);

/**
 * lrg_editor_bake_panel_set_page:
 * @self: an #LrgEditorBakePanel
 * @page: the atlas page the preview shows
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_bake_panel_set_page (LrgEditorBakePanel *self,
                                     guint               page);

/**
 * lrg_editor_bake_panel_get_page:
 * @self: an #LrgEditorBakePanel
 *
 * Returns: the atlas page the preview shows
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_bake_panel_get_page (LrgEditorBakePanel *self);

G_END_DECLS
