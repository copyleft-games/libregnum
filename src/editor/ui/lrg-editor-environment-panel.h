/* lrg-editor-environment-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorEnvironmentPanel: the lrgeditor "Environment" panel.
 *
 * Edits the open level's `environment:` block (an #LrgEnvironment held by
 * the #LrgLevel) live and undoably: the sky source and its colours or
 * images, exposure and rotation, image based lighting, the flat ambient,
 * distance and height fog, volumetric fog and the level's post stack
 * reference. A panorama strip previews the sky as the engine samples it;
 * "Bake" builds the GPU maps (irradiance, prefiltered reflections, BRDF
 * table) and captures the reflection probes over a few frames with a
 * progress bar.
 *
 * Every edit is one level command (Ctrl+Z in the viewport undoes it; a
 * slider drag is one step), and the level file stores the block, so the
 * game sees exactly what the viewport shows. The panel registers the
 * environment.* IPC methods and the "environment.*" actions itself.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-property-grid.h"
#include "../../lighting/lrg-environment.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_ENVIRONMENT_PANEL_ID:
 *
 * The dock panel id of the environment panel ("environment").
 */
#define LRG_EDITOR_ENVIRONMENT_PANEL_ID "environment"

#define LRG_TYPE_EDITOR_ENVIRONMENT_PANEL (lrg_editor_environment_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorEnvironmentPanel, lrg_editor_environment_panel, LRG, EDITOR_ENVIRONMENT_PANEL, LrgContainer)

/**
 * lrg_editor_environment_panel_new:
 * @app: the editor application
 *
 * Creates the panel. It follows the editor's current level, registers the
 * environment.* IPC methods on the app's server and the actions
 * "environment.show", "environment.add", "environment.remove" and
 * "environment.bake". The window docks it with the id
 * %LRG_EDITOR_ENVIRONMENT_PANEL_ID; embedders may dock it themselves with
 * lrg_editor_app_add_panel().
 *
 * Returns: (transfer full): a new #LrgEditorEnvironmentPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorEnvironmentPanel * lrg_editor_environment_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_environment_panel_find:
 * @app: the editor application
 *
 * The panel created for @app (one per app).
 *
 * Returns: (transfer none) (nullable): the panel, or %NULL when none was
 *   created
 */
LRG_AVAILABLE_IN_ALL
LrgEditorEnvironmentPanel * lrg_editor_environment_panel_find (LrgEditorApp *app);

/**
 * lrg_editor_environment_panel_get_environment:
 * @self: an #LrgEditorEnvironmentPanel
 *
 * Returns: (transfer none) (nullable): the environment of the current
 *   level, %NULL when the level has no `environment:` block (or there is
 *   no level)
 */
LRG_AVAILABLE_IN_ALL
LrgEnvironment * lrg_editor_environment_panel_get_environment (LrgEditorEnvironmentPanel *self);

/**
 * lrg_editor_environment_panel_get_grid:
 * @self: an #LrgEditorEnvironmentPanel
 *
 * The property grid of the panel. Its rows are named after the fields
 * lrg_editor_environment_panel_set_field() accepts.
 *
 * Returns: (transfer none): the grid
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_environment_panel_get_grid (LrgEditorEnvironmentPanel *self);

/**
 * lrg_editor_environment_panel_add_environment:
 * @self: an #LrgEditorEnvironmentPanel
 *
 * Gives the current level an environment block with the engine defaults
 * (a gradient sky), as one undoable command. Does nothing when the level
 * already has one.
 *
 * Returns: %TRUE when a block was added
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_environment_panel_add_environment (LrgEditorEnvironmentPanel *self);

/**
 * lrg_editor_environment_panel_remove_environment:
 * @self: an #LrgEditorEnvironmentPanel
 *
 * Removes the current level's environment block (undoable). The level
 * then renders with the editor's preview lighting again.
 *
 * Returns: %TRUE when a block was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_environment_panel_remove_environment (LrgEditorEnvironmentPanel *self);

/**
 * lrg_editor_environment_panel_set_field:
 * @self: an #LrgEditorEnvironmentPanel
 * @name: a field (grid row) name: "sky-source", "sky-color", "zenith",
 *   "horizon", "ground", "sky-path", "exposure", "rotation",
 *   "ibl-enabled", "diffuse-intensity", "specular-intensity",
 *   "resolution", "ambient-color", "ambient-intensity", "fog-enabled",
 *   "fog-type", "fog-color", "fog-density", "fog-start", "fog-end",
 *   "fog-height-base", "fog-height-falloff", "volumetric-enabled",
 *   "volumetric-density", "volumetric-anisotropy", "volumetric-steps",
 *   "volumetric-max-distance", "volumetric-intensity" or "post-stack"
 * @value: the value, of the field's type or transformable to it (colours
 *   are #GrlColor, enums their #GType, the rest double, int or string)
 *
 * Sets one field of the current level's environment as an undoable
 * command, creating the block first when the level has none. Consecutive
 * sets of the same field within one gesture (a slider drag) merge into one
 * undo step.
 *
 * Returns: %TRUE when the value was applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_environment_panel_set_field (LrgEditorEnvironmentPanel *self,
                                                 const gchar               *name,
                                                 const GValue              *value);

/**
 * lrg_editor_environment_panel_apply_data:
 * @self: an #LrgEditorEnvironmentPanel
 * @data: YAML (or JSON) text of a complete environment block, or "" to
 *   remove the block
 * @label: (nullable): the history label, %NULL for "Edit Environment"
 * @error: return location for a #GError
 *
 * Replaces the current level's environment with @data as one undoable
 * command (the IPC method environment.set patches the current block and
 * applies it this way). Unknown keys are kept; image sources are
 * reloaded only when their paths change.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_environment_panel_apply_data (LrgEditorEnvironmentPanel  *self,
                                                  const gchar                *data,
                                                  const gchar                *label,
                                                  GError                    **error);

/**
 * lrg_editor_environment_panel_bake:
 * @self: an #LrgEditorEnvironmentPanel
 * @error: return location for a #GError
 *
 * Starts a bake of the current environment: the next frames build the
 * environment's GPU maps (lrg_environment_bake()) and capture the
 * reflection probes through the viewport's runtime renderer, advancing
 * #LrgEditorEnvironmentPanel:bake-progress. Needs a window.
 *
 * Returns: %TRUE when the bake started; %FALSE with @error set without a
 *   window, without an environment, or while a bake is running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_environment_panel_bake (LrgEditorEnvironmentPanel  *self,
                                            GError                    **error);

/**
 * lrg_editor_environment_panel_step_bake:
 * @self: an #LrgEditorEnvironmentPanel
 *
 * Runs the next phase of a running bake. A main-loop timer calls it once
 * per frame while baking; tests and embedders without a main loop call it
 * themselves (outside BeginDrawing()).
 *
 * Returns: %TRUE while the bake still runs after this phase
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_environment_panel_step_bake (LrgEditorEnvironmentPanel *self);

/**
 * lrg_editor_environment_panel_get_baking:
 * @self: an #LrgEditorEnvironmentPanel
 *
 * Returns: whether a bake is running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_environment_panel_get_baking (LrgEditorEnvironmentPanel *self);

/**
 * lrg_editor_environment_panel_get_bake_progress:
 * @self: an #LrgEditorEnvironmentPanel
 *
 * Returns: the progress of the running (or last) bake, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_editor_environment_panel_get_bake_progress (LrgEditorEnvironmentPanel *self);

/**
 * lrg_editor_environment_panel_get_bake_phase:
 * @self: an #LrgEditorEnvironmentPanel
 *
 * Returns: (transfer none): what the bake does now: "idle", "starting",
 *   "environment", "probes", "done" or "failed"
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_environment_panel_get_bake_phase (LrgEditorEnvironmentPanel *self);

/**
 * lrg_editor_environment_panel_get_bake_error:
 * @self: an #LrgEditorEnvironmentPanel
 *
 * Returns: (transfer none) (nullable): the message of the last failed
 *   bake, %NULL after a successful one
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_environment_panel_get_bake_error (LrgEditorEnvironmentPanel *self);

/**
 * lrg_editor_environment_panel_sample_preview:
 * @self: an #LrgEditorEnvironmentPanel
 * @u: horizontal position in the panorama, 0 .. 1 (azimuth, 0 = -Z)
 * @v: vertical position, 0 (zenith) .. 1 (nadir)
 * @out_rgba: (out caller-allocates) (array fixed-size=4): the sRGB colour
 *   the preview strip shows there, 0 .. 1
 *
 * The colour of the sky preview at a point of the panorama: the
 * environment's sky (lrg_environment_sample_sky()) with its exposure and
 * rotation, tone mapped for display. Without an environment it is the
 * panel background.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_environment_panel_sample_preview (LrgEditorEnvironmentPanel *self,
                                                  gfloat                     u,
                                                  gfloat                     v,
                                                  gfloat                    *out_rgba);

G_END_DECLS
