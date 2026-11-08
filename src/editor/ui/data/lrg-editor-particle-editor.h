/* lrg-editor-particle-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorParticleEditor: the data editor of `.rparticles` particle
 * effects (#LrgParticleAsset), with a live 3D preview.
 *
 * The editor shows:
 *  - the effect settings (name, seed, duration, loop, prewarm, local
 *    space) in a property grid;
 *  - the emitter list: add, duplicate, remove, reorder, mute (the
 *    emitter's `enabled` key) and solo (preview only: only that emitter
 *    and the emitters it triggers are drawn);
 *  - the selected emitter's module stack grouped by stage (spawn, shape,
 *    init, force, collision, over life, event): add a module from
 *    lrg_particle_module_list_kinds(), remove, reorder, enable;
 *  - the selected emitter, its `renderer:` block or the selected module
 *    in a property grid (curves, gradients, ranges, colours and vectors
 *    get their own editors from the property hints of the engine);
 *  - a live preview: an #LrgParticleEffect of the document playing
 *    (play / pause / restart), a timeline scrubbed with
 *    lrg_particle_effect_seek(), the speed, a seed override, a ground
 *    plane, the background colour, and a GPU toggle that runs every
 *    emitter inside the GPU subset (lrg_gpu_particles_check_emitter()) on
 *    an #LrgGpuParticles and says why the others stay on the CPU;
 *  - statistics: live particles per emitter and the bounds;
 *  - the asset's unknown keys as warnings (typos are kept by the loader).
 *
 * Every edit is one undoable step of the `.rparticles` #LrgEditorDocument
 * (a drag in the grid is one step), written in the engine writer's
 * canonical form.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../../lrg-version.h"
#include "../../../particles/lrg-particle-asset.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_PARTICLE_EDITOR (lrg_editor_particle_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorParticleEditor, lrg_editor_particle_editor, LRG,
                      EDITOR_PARTICLE_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_particle_editor_new:
 * @app: (nullable): the editor application
 * @document: a particle effect document (`.rparticles`)
 *
 * Creates the editor without docking it; lrg_editor_data_editor_open()
 * (or lrg_editor_particle_editor_open()) is what the editor uses.
 *
 * Returns: (transfer full): a new #LrgEditorParticleEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorParticleEditor * lrg_editor_particle_editor_new (LrgEditorApp      *app,
                                                          LrgEditorDocument *document);

/**
 * lrg_editor_particle_editor_install:
 * @app: the editor application
 *
 * Registers the `particles.*` IPC methods on @app. Safe to call more than
 * once; every particle editor calls it when it is constructed and the
 * assets panel calls it when it is created.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_particle_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_particle_editor_open:
 * @app: the editor application
 * @path: a `.rparticles` file, absolute or relative to the project
 *   directory
 * @error: return location for a #GError
 *
 * Opens @path in a particle editor tab (or presents its open tab).
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgEditorParticleEditor * lrg_editor_particle_editor_open (LrgEditorApp  *app,
                                                           const gchar   *path,
                                                           GError       **error);

/**
 * lrg_editor_particle_editor_create:
 * @app: (nullable): the editor application (resolves relative paths
 *   against the project)
 * @path: the new `.rparticles` file
 * @template_name: (nullable): "default" (or %NULL: one looping emitter
 *   with spawn-rate, shape, velocity, lifetime, colour and alpha over
 *   life) or "empty" (no emitters)
 * @error: return location for a #GError
 *
 * Writes a new particle effect named after the file.
 *
 * Returns: %TRUE on success; %G_IO_ERROR_EXISTS when @path exists,
 *   %G_IO_ERROR_INVALID_ARGUMENT for an unknown template
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_create (LrgEditorApp  *app,
                                            const gchar   *path,
                                            const gchar   *template_name,
                                            GError       **error);

/**
 * lrg_editor_particle_editor_get_asset:
 * @self: an #LrgEditorParticleEditor
 *
 * The effect parsed from the document. It is replaced when the document
 * changes from outside the editor (undo, the YAML pane, a reload), so do
 * not keep it.
 *
 * Returns: (transfer none) (nullable): the asset, %NULL while the document
 *   does not load (see lrg_editor_particle_editor_get_load_error())
 */
LRG_AVAILABLE_IN_ALL
LrgParticleAsset * lrg_editor_particle_editor_get_asset (LrgEditorParticleEditor *self);

/**
 * lrg_editor_particle_editor_get_load_error:
 * @self: an #LrgEditorParticleEditor
 *
 * Returns: (nullable): why the document does not load as a particle
 *   effect (the strict loader's message with the key path), or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_particle_editor_get_load_error (LrgEditorParticleEditor *self);

/**
 * lrg_editor_particle_editor_get_effect:
 * @self: an #LrgEditorParticleEditor
 *
 * Returns: (transfer none) (nullable): the preview effect (rebuilt when
 *   the asset is replaced), %NULL while the document does not load
 */
LRG_AVAILABLE_IN_ALL
LrgParticleEffect * lrg_editor_particle_editor_get_effect (LrgEditorParticleEditor *self);

/* --- selection --- */

/**
 * lrg_editor_particle_editor_select:
 * @self: an #LrgEditorParticleEditor
 * @emitter: an emitter index, or -1 for the effect settings
 * @module: a module index of that emitter, -1 for the emitter itself, or
 *   -2 for its `renderer:` block
 *
 * Shows the selection in the property grid and the stack.
 *
 * Returns: %FALSE when an index is out of range (nothing changes)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_select (LrgEditorParticleEditor *self,
                                            gint                     emitter,
                                            gint                     module);

/**
 * lrg_editor_particle_editor_get_selected_emitter:
 * @self: an #LrgEditorParticleEditor
 *
 * Returns: the selected emitter index, or -1 (the effect settings)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_particle_editor_get_selected_emitter (LrgEditorParticleEditor *self);

/**
 * lrg_editor_particle_editor_get_selected_module:
 * @self: an #LrgEditorParticleEditor
 *
 * Returns: the selected module index, -1 (the emitter) or -2 (its
 *   renderer)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_particle_editor_get_selected_module (LrgEditorParticleEditor *self);

/* --- edits (each one undo step) --- */

/**
 * lrg_editor_particle_editor_add_emitter:
 * @self: an #LrgEditorParticleEditor
 * @name: (nullable): the emitter name; %NULL picks "emitter", numbered
 *   when taken
 * @error: return location for a #GError
 *
 * Appends an emitter with a spawn-rate, a lifetime and a velocity module
 * and selects it.
 *
 * Returns: the new emitter index, or -1 with @error set (a taken name)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_particle_editor_add_emitter (LrgEditorParticleEditor  *self,
                                             const gchar              *name,
                                             GError                  **error);

/**
 * lrg_editor_particle_editor_duplicate_emitter:
 * @self: an #LrgEditorParticleEditor
 * @emitter: an emitter index
 * @error: return location for a #GError
 *
 * Inserts a copy of an emitter (modules, renderer, unknown keys) after
 * it, named "<name> copy" (numbered when taken), and selects it.
 *
 * Returns: the new emitter index, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_particle_editor_duplicate_emitter (LrgEditorParticleEditor  *self,
                                                   guint                     emitter,
                                                   GError                  **error);

/**
 * lrg_editor_particle_editor_remove_emitter:
 * @self: an #LrgEditorParticleEditor
 * @emitter: an emitter index
 * @error: return location for a #GError
 *
 * Removes an emitter. Fails when another emitter's sub-emitter module
 * still names it (the asset would not validate).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_remove_emitter (LrgEditorParticleEditor  *self,
                                                    guint                     emitter,
                                                    GError                  **error);

/**
 * lrg_editor_particle_editor_move_emitter:
 * @self: an #LrgEditorParticleEditor
 * @from: an emitter index
 * @to: its index after the move (emitter order is draw order)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_move_emitter (LrgEditorParticleEditor  *self,
                                                  guint                     from,
                                                  guint                     to,
                                                  GError                  **error);

/**
 * lrg_editor_particle_editor_add_module:
 * @self: an #LrgEditorParticleEditor
 * @emitter: an emitter index
 * @kind: a registered module kind (lrg_particle_module_list_kinds())
 * @index: where the module goes in the stack, -1 to append
 * @error: return location for a #GError
 *
 * Adds a module with its default parameters and selects it.
 *
 * Returns: the module index, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_particle_editor_add_module (LrgEditorParticleEditor  *self,
                                            guint                     emitter,
                                            const gchar              *kind,
                                            gint                      index,
                                            GError                  **error);

/**
 * lrg_editor_particle_editor_remove_module:
 * @self: an #LrgEditorParticleEditor
 * @emitter: an emitter index
 * @module: a module index
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_remove_module (LrgEditorParticleEditor  *self,
                                                   guint                     emitter,
                                                   guint                     module,
                                                   GError                  **error);

/**
 * lrg_editor_particle_editor_move_module:
 * @self: an #LrgEditorParticleEditor
 * @emitter: an emitter index
 * @from: a module index
 * @to: its index after the move (only the order among modules of one
 *   stage changes behaviour)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_move_module (LrgEditorParticleEditor  *self,
                                                 guint                     emitter,
                                                 guint                     from,
                                                 guint                     to,
                                                 GError                  **error);

/**
 * lrg_editor_particle_editor_set_value:
 * @self: an #LrgEditorParticleEditor
 * @object_path: the YAML path of the object: "" (the effect),
 *   "/emitters/N", "/emitters/N/renderer" or "/emitters/N/modules/M"
 * @key: a key of that object (`seed`, `max-particles`, `blend`, `speed`,
 *   `enabled`, ...)
 * @value: (nullable): the value in the file's syntax ("[4, 8]", "'#ffc040'",
 *   "0 1; 1 0 ease-out-quad", "true"), or %NULL to remove the key (back
 *   to the default)
 * @gesture: 0, or a gesture id (lrg_editor_data_editor_new_gesture()):
 *   consecutive edits of one object with the same id merge into one undo
 *   step
 * @error: return location for a #GError
 *
 * Sets one value through the engine's strict loader, so a bad value fails
 * with the loader's message (which names the key path) and changes
 * nothing. The object is then written in the writer's canonical form.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_set_value (LrgEditorParticleEditor  *self,
                                               const gchar              *object_path,
                                               const gchar              *key,
                                               const gchar              *value,
                                               guint                     gesture,
                                               GError                  **error);

/* --- preview --- */

/**
 * lrg_editor_particle_editor_set_playing:
 * @self: an #LrgEditorParticleEditor
 * @playing: %TRUE to play the preview, %FALSE to pause it
 *
 * Plays or pauses the preview. Playing a finished non-looping effect
 * starts it over.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_particle_editor_set_playing (LrgEditorParticleEditor *self,
                                             gboolean                 playing);

/**
 * lrg_editor_particle_editor_get_playing:
 * @self: an #LrgEditorParticleEditor
 *
 * Returns: whether the preview plays
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_get_playing (LrgEditorParticleEditor *self);

/**
 * lrg_editor_particle_editor_restart:
 * @self: an #LrgEditorParticleEditor
 *
 * Starts the preview over from time 0 (with prewarm) and plays it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_particle_editor_restart (LrgEditorParticleEditor *self);

/**
 * lrg_editor_particle_editor_seek:
 * @self: an #LrgEditorParticleEditor
 * @time: seconds from the start
 *
 * Puts the preview at @time with fixed 1/60 s steps
 * (lrg_particle_effect_seek()): the same time always shows the same
 * particles. A playing preview keeps playing from there.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_particle_editor_seek (LrgEditorParticleEditor *self,
                                      gfloat                   time);

/**
 * lrg_editor_particle_editor_get_time:
 * @self: an #LrgEditorParticleEditor
 *
 * Returns: the preview time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_particle_editor_get_time (LrgEditorParticleEditor *self);

/**
 * lrg_editor_particle_editor_advance:
 * @self: an #LrgEditorParticleEditor
 * @seconds: preview time to run now
 *
 * Steps a playing preview by @seconds in 1/60 s frames at the preview
 * speed, as the panel does every frame (tests, IPC). Does nothing while
 * paused.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_particle_editor_advance (LrgEditorParticleEditor *self,
                                         gfloat                   seconds);

/**
 * lrg_editor_particle_editor_set_speed:
 * @self: an #LrgEditorParticleEditor
 * @speed: the playback rate, clamped to 0.05 .. 4
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_particle_editor_set_speed (LrgEditorParticleEditor *self,
                                           gfloat                   speed);

/**
 * lrg_editor_particle_editor_get_speed:
 * @self: an #LrgEditorParticleEditor
 *
 * Returns: the playback rate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_particle_editor_get_speed (LrgEditorParticleEditor *self);

/**
 * lrg_editor_particle_editor_set_seed_override:
 * @self: an #LrgEditorParticleEditor
 * @seed: a seed for the preview, or -1 to use the asset's seed
 *
 * Plays the preview with another seed (lrg_particle_effect_set_seed())
 * without editing the document, then restarts it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_particle_editor_set_seed_override (LrgEditorParticleEditor *self,
                                                   gint64                   seed);

/**
 * lrg_editor_particle_editor_get_seed_override:
 * @self: an #LrgEditorParticleEditor
 *
 * Returns: the preview seed override, or -1
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_editor_particle_editor_get_seed_override (LrgEditorParticleEditor *self);

/**
 * lrg_editor_particle_editor_set_show_ground:
 * @self: an #LrgEditorParticleEditor
 * @show: whether the preview draws its ground grid at y = 0
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_particle_editor_set_show_ground (LrgEditorParticleEditor *self,
                                                 gboolean                 show);

/**
 * lrg_editor_particle_editor_get_show_ground:
 * @self: an #LrgEditorParticleEditor
 *
 * Returns: whether the ground grid is drawn
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_get_show_ground (LrgEditorParticleEditor *self);

/**
 * lrg_editor_particle_editor_set_background:
 * @self: an #LrgEditorParticleEditor
 * @color: the preview's clear colour (alpha ignored)
 *
 * Additive effects read best on dark backgrounds, smoke on light ones.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_particle_editor_set_background (LrgEditorParticleEditor *self,
                                                const GrlColor          *color);

/**
 * lrg_editor_particle_editor_get_background:
 * @self: an #LrgEditorParticleEditor
 * @color: (out caller-allocates): the preview's clear colour
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_particle_editor_get_background (LrgEditorParticleEditor *self,
                                                GrlColor                *color);

/**
 * lrg_editor_particle_editor_set_solo:
 * @self: an #LrgEditorParticleEditor
 * @emitter: an emitter index, or -1 to draw every emitter
 *
 * Draws only @emitter and the emitters its sub-emitter modules trigger
 * (every emitter keeps simulating). Preview state only: the document is
 * not changed (mute an emitter with its `enabled` key instead).
 *
 * Returns: %FALSE when @emitter is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_set_solo (LrgEditorParticleEditor *self,
                                              gint                     emitter);

/**
 * lrg_editor_particle_editor_get_solo:
 * @self: an #LrgEditorParticleEditor
 *
 * Returns: the soloed emitter, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_particle_editor_get_solo (LrgEditorParticleEditor *self);

/**
 * lrg_editor_particle_editor_set_gpu:
 * @self: an #LrgEditorParticleEditor
 * @gpu: %TRUE to run the emitters inside the GPU subset on
 *   #LrgGpuParticles in the preview
 *
 * Preview only (a game picks the backend per emitter itself). Emitters
 * outside the subset, and every emitter without a GL 3.3 context, stay on
 * the CPU; lrg_editor_particle_editor_get_gpu_status() says why.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_particle_editor_set_gpu (LrgEditorParticleEditor *self,
                                         gboolean                 gpu);

/**
 * lrg_editor_particle_editor_get_gpu:
 * @self: an #LrgEditorParticleEditor
 *
 * Returns: whether the GPU toggle is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_get_gpu (LrgEditorParticleEditor *self);

/**
 * lrg_editor_particle_editor_get_gpu_status:
 * @self: an #LrgEditorParticleEditor
 * @emitter: an emitter index
 * @message: (out) (optional) (transfer full) (nullable): why the emitter
 *   cannot run on the GPU (lrg_gpu_particles_check_emitter()'s message),
 *   %NULL when it can
 *
 * Whether an emitter is inside the GPU subset, whether or not the toggle
 * is on and a GL context exists.
 *
 * Returns: %TRUE when the emitter can run on the GPU
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_get_gpu_status (LrgEditorParticleEditor  *self,
                                                    guint                     emitter,
                                                    gchar                   **message);

/**
 * lrg_editor_particle_editor_get_emitter_on_gpu:
 * @self: an #LrgEditorParticleEditor
 * @emitter: an emitter index
 *
 * Returns: whether the preview simulates and draws @emitter on the GPU
 *   right now (toggle on, inside the subset, GL available)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_particle_editor_get_emitter_on_gpu (LrgEditorParticleEditor *self,
                                                        guint                    emitter);

/**
 * lrg_editor_particle_editor_get_active_count:
 * @self: an #LrgEditorParticleEditor
 * @emitter: an emitter index, or -1 for all
 *
 * Returns: live particles in the preview (GPU emitters count their
 *   alive particles)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_particle_editor_get_active_count (LrgEditorParticleEditor *self,
                                                   gint                     emitter);

/**
 * lrg_editor_particle_editor_render_preview:
 * @self: an #LrgEditorParticleEditor
 * @size: width and height in pixels
 *
 * Renders the preview into an image, as the panel draws it. Needs a
 * graphics context.
 *
 * Returns: (transfer full) (nullable): the image, %NULL without a graphics
 *   context
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_editor_particle_editor_render_preview (LrgEditorParticleEditor *self,
                                                      guint                    size);

G_END_DECLS
