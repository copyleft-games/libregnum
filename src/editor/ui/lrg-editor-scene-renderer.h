/* lrg-editor-scene-renderer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSceneRenderer: draws the editor's 3D scene view (grid, nodes,
 * icons, selection bounds, transform gizmo, orientation axes, overlay
 * text) into the render texture of an editor viewport, and renders model
 * and font thumbnails for the asset browser.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../lrg-editor-view-state.h"
#include "../lrg-editor-viewport-nav.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SCENE_RENDERER (lrg_editor_scene_renderer_get_type ())

/**
 * LrgEditorSceneRenderer:
 *
 * Renders the level of an #LrgEditor as seen by one camera of an
 * #LrgEditorViewState. The renderer owns every GPU resource it needs
 * (lit shader, primitive meshes, model and texture caches); they are
 * created lazily by the first lrg_editor_scene_renderer_render() and
 * freed by lrg_editor_scene_renderer_release(). Without a window every
 * call is a harmless no-op.
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSceneRenderer, lrg_editor_scene_renderer, LRG, EDITOR_SCENE_RENDERER, GObject)

/**
 * lrg_editor_scene_renderer_new:
 * @editor: the #LrgEditor whose level, selection and tool are drawn
 *
 * Creates a renderer. No GPU resources are allocated until the first
 * render.
 *
 * Returns: (transfer full): a new #LrgEditorSceneRenderer
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSceneRenderer * lrg_editor_scene_renderer_new (LrgEditor *editor);

/**
 * lrg_editor_scene_renderer_get_editor:
 * @self: an #LrgEditorSceneRenderer
 *
 * Returns: (transfer none): the editor this renderer draws
 */
LRG_AVAILABLE_IN_ALL
LrgEditor * lrg_editor_scene_renderer_get_editor (LrgEditorSceneRenderer *self);

/**
 * lrg_editor_scene_renderer_set_settings:
 * @self: an #LrgEditorSceneRenderer
 * @settings: (nullable): the editor settings, or %NULL for defaults
 *
 * Binds the settings whose "grid-size" and "grid-extent" shape the
 * viewport grid. The values are re-read only after a change notification,
 * not every frame. Without settings the grid uses 1.0 and 50 cells.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_scene_renderer_set_settings (LrgEditorSceneRenderer *self,
                                             LrgEditorSettings      *settings);

/**
 * lrg_editor_scene_renderer_set_base_dir:
 * @self: an #LrgEditorSceneRenderer
 * @dir: (nullable) (type filename): the directory relative asset paths
 *   resolve against (project or level directory), or %NULL for the
 *   working directory
 *
 * Changing the directory forgets the per-asset-string lookups, so nodes
 * resolve their assets again on the next render. Models and textures
 * already loaded by absolute path stay cached.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_scene_renderer_set_base_dir (LrgEditorSceneRenderer *self,
                                             const gchar            *dir);

/**
 * lrg_editor_scene_renderer_get_base_dir:
 * @self: an #LrgEditorSceneRenderer
 *
 * Returns: (nullable) (type filename): the asset base directory
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_scene_renderer_get_base_dir (LrgEditorSceneRenderer *self);

/**
 * lrg_editor_scene_renderer_set_gizmo:
 * @self: an #LrgEditorSceneRenderer
 * @gizmo: (nullable): the transform gizmo to draw, or %NULL for none
 *
 * Sets the transform gizmo drawn over the active viewport. The renderer
 * only reads it (origin, axes, size, hovered and active handle).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_scene_renderer_set_gizmo (LrgEditorSceneRenderer *self,
                                          LrgEditorGizmo         *gizmo);

/**
 * lrg_editor_scene_renderer_set_playing:
 * @self: an #LrgEditorSceneRenderer
 * @playing: whether a play session is running
 *
 * While playing the background turns a darker blue so play mode is
 * visible at a glance.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_scene_renderer_set_playing (LrgEditorSceneRenderer *self,
                                            gboolean                playing);

/**
 * lrg_editor_scene_renderer_get_playing:
 * @self: an #LrgEditorSceneRenderer
 *
 * Returns: whether the play-mode background is used
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_scene_renderer_get_playing (LrgEditorSceneRenderer *self);

/**
 * lrg_editor_scene_renderer_render:
 * @self: an #LrgEditorSceneRenderer
 * @view: the view state holding the camera, render mode and overlays
 * @viewport: the camera index for lrg_editor_view_state_get_camera(), or
 *   -1 for the active viewport
 * @nav: (nullable): the viewport's navigation controller, for the
 *   orientation gizmo and the box-select rectangle
 * @active: whether this is the active viewport (only it shows the
 *   transform gizmo)
 * @width: render target width in pixels
 * @height: render target height in pixels
 *
 * Draws the scene into the render target that is currently bound. Call it
 * between BeginTextureMode() and EndTextureMode() on a target of @width x
 * @height pixels (an #LrgViewport render callback); it clears the target
 * itself. The 3D pass draws the grid (overlay GRID), the nodes (wire
 * mode for %LRG_EDITOR_RENDER_MODE_WIREFRAME), node icons (overlay
 * ICONS) and selection bounds; the 2D pass then draws, in viewport-local
 * pixels, the transform gizmo (when @active and overlay GIZMOS), the
 * orientation axes and box-select rectangle from @nav, the view mode
 * label and, with overlay STATS, a node and selection count line.
 *
 * GPU resources are created on the first call.
 *
 * Returns: %FALSE (and draws nothing) when no window is open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_scene_renderer_render (LrgEditorSceneRenderer *self,
                                           LrgEditorViewState     *view,
                                           gint                    viewport,
                                           LrgEditorViewportNav   *nav,
                                           gboolean                active,
                                           gfloat                  width,
                                           gfloat                  height);

/**
 * lrg_editor_scene_renderer_get_drawn:
 * @self: an #LrgEditorSceneRenderer
 *
 * Returns: the number of draw calls (nodes, model meshes, tilemap layers)
 *   issued by the last render
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_scene_renderer_get_drawn (LrgEditorSceneRenderer *self);

/**
 * lrg_editor_scene_renderer_forget_asset:
 * @self: an #LrgEditorSceneRenderer
 * @abs_path: (type filename): absolute path of an asset that changed on
 *   disk
 *
 * Drops the cached GPU copy of a model or texture so the next render
 * loads it again (live reload). Unknown paths are ignored.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_scene_renderer_forget_asset (LrgEditorSceneRenderer *self,
                                             const gchar            *abs_path);

/**
 * lrg_editor_scene_renderer_render_model_thumbnail:
 * @self: an #LrgEditorSceneRenderer
 * @abs_path: (type filename): absolute path of a model file
 * @size: edge length of the square thumbnail in pixels
 *
 * Renders a model from a 3/4 angle, framed by its bounding box and lit
 * like the viewport, into a @size x @size image. Needs a window and must
 * be called outside any BeginTextureMode() / EndTextureMode() pair.
 *
 * Returns: (transfer full) (nullable): the thumbnail, or %NULL when there
 *   is no window or the model cannot be loaded
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_editor_scene_renderer_render_model_thumbnail (LrgEditorSceneRenderer *self,
                                                             const gchar            *abs_path,
                                                             guint                   size);

/**
 * lrg_editor_scene_renderer_render_font_thumbnail:
 * @self: an #LrgEditorSceneRenderer
 * @abs_path: (type filename): absolute path of a font file
 * @size: edge length of the square thumbnail in pixels
 *
 * Loads the font with the size and code points of its ".meta" sidecar
 * (32 pixels and 32-126 without one) and draws the sample "Aa Bb 123"
 * centred on a dark background into a @size x @size image. Needs a
 * window and must be called outside any BeginTextureMode() /
 * EndTextureMode() pair.
 *
 * Returns: (transfer full) (nullable): the thumbnail, or %NULL when there
 *   is no window or the font cannot be loaded
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_editor_scene_renderer_render_font_thumbnail (LrgEditorSceneRenderer *self,
                                                            const gchar            *abs_path,
                                                            guint                   size);

/**
 * lrg_editor_scene_renderer_release:
 * @self: an #LrgEditorSceneRenderer
 *
 * Frees every GPU resource (shader, meshes, cached models and textures).
 * Call it before CloseWindow(). Safe to call twice and without a window
 * (then only the bookkeeping is dropped); the next render recreates what
 * it needs.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_scene_renderer_release (LrgEditorSceneRenderer *self);

G_END_DECLS
