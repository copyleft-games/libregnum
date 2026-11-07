/* lrg-editor-viewport-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorViewportPanel: one of the editor's scene viewports.
 *
 * A container with a slim header bar (view preset, render mode, overlay
 * toggles, camera preview and maximise buttons) over an #LrgViewport in
 * %LRG_VIEWPORT_MODE_RENDER whose render function draws the level with
 * #LrgEditorSceneRenderer through camera @index of the app's
 * #LrgEditorViewState. The scene is re-rendered only when something it
 * shows changed (level, selection, camera, render mode, overlays), while
 * playing and while navigating or dragging; the gizmo, orientation axes,
 * box select, stats and play frame are painted with #LrgPaintList on top
 * (lrg-editor-gizmo-paint.h) and need no re-render.
 *
 * Pointer and key input go to the app's #LrgEditorViewportNav first (fly,
 * orbit, pan, zoom, the orientation gizmo), then to the transform gizmo,
 * tile painting and picking: a click selects, a drag on empty space box
 * selects, a right click opens the context menu. A press makes this
 * viewport the view state's active viewport. Assets dropped on the panel
 * are placed at the picked ground point. When the primary selection is
 * a camera node the active viewport shows a picture-in-picture preview
 * through it.
 *
 * Several panels live in an #LrgEditorViewportSplit; a lone panel
 * (lrg_editor_viewport_panel_new()) shows viewport 0.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-viewport.h"
#include "../lrg-node.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_VIEWPORT_PANEL (lrg_editor_viewport_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorViewportPanel, lrg_editor_viewport_panel, LRG, EDITOR_VIEWPORT_PANEL, LrgContainer)

/**
 * lrg_editor_viewport_panel_new:
 * @app: the application whose level, view state and navigation it shows
 *
 * A panel for viewport 0 (the single-viewport layout).
 *
 * Returns: (transfer full): a new #LrgEditorViewportPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewportPanel * lrg_editor_viewport_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_viewport_panel_new_for_index:
 * @app: the application
 * @index: the viewport (camera) index, 0 .. %LRG_EDITOR_VIEW_MAX_VIEWPORTS - 1
 *
 * A panel showing camera @index of the app's view state, with that
 * viewport's render mode and overlays.
 *
 * Returns: (transfer full): a new #LrgEditorViewportPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewportPanel * lrg_editor_viewport_panel_new_for_index (LrgEditorApp *app,
                                                                  guint         index);

/**
 * lrg_editor_viewport_panel_get_index:
 * @self: an #LrgEditorViewportPanel
 *
 * Returns: the viewport (camera) index the panel shows
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_viewport_panel_get_index (LrgEditorViewportPanel *self);

/**
 * lrg_editor_viewport_panel_get_viewport:
 * @self: an #LrgEditorViewportPanel
 *
 * Returns: (transfer none): the viewport widget showing the scene
 */
LRG_AVAILABLE_IN_ALL
LrgViewport * lrg_editor_viewport_panel_get_viewport (LrgEditorViewportPanel *self);

/**
 * lrg_editor_viewport_panel_get_header:
 * @self: an #LrgEditorViewportPanel
 *
 * The header bar: a view preset combo box ("view-preset"), a render
 * mode combo box ("render-mode"), the overlays menu button
 * ("overlays"), the camera preview toggle ("preview-toggle") and the
 * maximise toggle ("maximize"); names are prefixed with "vpN-" for
 * viewport N.
 *
 * Returns: (transfer none): the header widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_viewport_panel_get_header (LrgEditorViewportPanel *self);

/**
 * lrg_editor_viewport_panel_get_preview:
 * @self: an #LrgEditorViewportPanel
 *
 * The camera preview (picture in picture). It is visible while this is
 * the active viewport, the preview is enabled and the primary selection
 * is a camera node.
 *
 * Returns: (transfer none): the preview viewport widget
 */
LRG_AVAILABLE_IN_ALL
LrgViewport * lrg_editor_viewport_panel_get_preview (LrgEditorViewportPanel *self);

/**
 * lrg_editor_viewport_panel_get_preview_node:
 * @self: an #LrgEditorViewportPanel
 *
 * Returns: (transfer none) (nullable): the camera node the preview shows,
 *   or %NULL when the preview is hidden
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_viewport_panel_get_preview_node (LrgEditorViewportPanel *self);

/**
 * lrg_editor_viewport_panel_set_preview_enabled:
 * @self: an #LrgEditorViewportPanel
 * @enabled: whether a selected camera shows its preview (default %TRUE;
 *   the header's camera button toggles it)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_panel_set_preview_enabled (LrgEditorViewportPanel *self,
                                                    gboolean                enabled);

/**
 * lrg_editor_viewport_panel_get_preview_enabled:
 * @self: an #LrgEditorViewportPanel
 *
 * Returns: whether a selected camera shows its preview
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_panel_get_preview_enabled (LrgEditorViewportPanel *self);

/**
 * lrg_editor_viewport_panel_get_render_pending:
 * @self: an #LrgEditorViewportPanel
 *
 * Whether the scene image is out of date and a render will be queued on
 * the next frame (render on demand). Cleared when the render is queued.
 *
 * Returns: %TRUE when the scene changed since the last queued render
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_panel_get_render_pending (LrgEditorViewportPanel *self);

/**
 * lrg_editor_viewport_panel_drop_asset:
 * @self: an #LrgEditorViewportPanel
 * @path: a project-relative (or absolute) asset path
 * @u: viewport-local x in content pixels
 * @v: viewport-local y in content pixels
 *
 * What a drop of an "application/x-lrg-asset" payload does, as one undo
 * step: a model, texture or CAD file becomes a node at the ground point
 * under (@u, @v) (snapped when snapping is on), a prefab a linked
 * instance there, a script is attached to the node under the point and
 * a level is opened.
 *
 * Returns: (transfer none) (nullable): the node created or changed, or
 *   %NULL when nothing was placed
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_viewport_panel_drop_asset (LrgEditorViewportPanel *self,
                                                const gchar            *path,
                                                gfloat                  u,
                                                gfloat                  v);

/**
 * lrg_editor_viewport_panel_set_gpu_enabled:
 * @self: an #LrgEditorViewportPanel
 * @enabled: %FALSE frees the render textures of the scene and preview
 *   viewports (call before the window closes); %TRUE renders again
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_panel_set_gpu_enabled (LrgEditorViewportPanel *self,
                                                gboolean                enabled);

/**
 * lrg_editor_viewport_panel_set_layer_mask:
 * @self: an #LrgEditorViewportPanel
 * @mask: the render layers this viewport shows (bit N = layer N,
 *   %LRG_RENDER_LAYERS_MASK_ALL for every layer)
 *
 * The per-viewport render-layer picker (the layers button of the header):
 * hands @mask to the scene renderer as this viewport's cull mask
 * (lrg_editor_scene_renderer_set_cull_mask()) and renders again. The mask
 * is a view setting; it is not saved in the level. Since 0.9.0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_panel_set_layer_mask (LrgEditorViewportPanel *self,
                                               guint32                 mask);

/**
 * lrg_editor_viewport_panel_get_layer_mask:
 * @self: an #LrgEditorViewportPanel
 *
 * Returns: the render layers this viewport shows (default all)
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_editor_viewport_panel_get_layer_mask (LrgEditorViewportPanel *self);

/**
 * lrg_editor_viewport_panel_queue_render:
 * @self: an #LrgEditorViewportPanel
 *
 * Renders the scene again at the next frame. The panel follows the level,
 * selection and view state by itself; call this after changing something
 * else the image depends on (a post stack previewed on the runtime
 * renderer, a baked environment). Since 0.9.0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_panel_queue_render (LrgEditorViewportPanel *self);

/**
 * lrg_editor_viewport_panel_set_show_bones:
 * @self: an #LrgEditorViewportPanel
 * @show: whether to draw skeletons
 *
 * The "Bones" overlay (overlays menu, Animation section): the selected
 * nodes' skeletons as joint dots and bone lines, animated while playing
 * (the entity's animator or skinned model), else the bind pose of the
 * node's glTF model. Off by default; a view setting, not saved. Since 0.9.0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_panel_set_show_bones (LrgEditorViewportPanel *self,
                                               gboolean                show);

/**
 * lrg_editor_viewport_panel_get_show_bones:
 * @self: an #LrgEditorViewportPanel
 *
 * Returns: whether the Bones overlay is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_panel_get_show_bones (LrgEditorViewportPanel *self);

/**
 * lrg_editor_viewport_panel_set_show_ik_targets:
 * @self: an #LrgEditorViewportPanel
 * @show: whether to draw IK targets
 *
 * The "IK Targets" overlay: while playing, every enabled IK goal of a
 * selected node's #LrgAnimatorComponent3D draws its last solved target
 * (lrg_ik_goal_get_last_target()) as a ring labelled with the goal name,
 * with a line from the effector bone. On by default. Since 0.9.0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_panel_set_show_ik_targets (LrgEditorViewportPanel *self,
                                                    gboolean                show);

/**
 * lrg_editor_viewport_panel_get_show_ik_targets:
 * @self: an #LrgEditorViewportPanel
 *
 * Returns: whether the IK Targets overlay is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_viewport_panel_get_show_ik_targets (LrgEditorViewportPanel *self);

/**
 * lrg_editor_viewport_panel_get_anim_overlay_counts:
 * @self: an #LrgEditorViewportPanel
 * @n_bones: (out) (optional): joints the last overlay pass drew
 * @n_targets: (out) (optional): IK targets the last overlay pass drew
 *
 * What the animation overlays drew in the last paint (tests, tooling).
 * Since 0.9.0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_panel_get_anim_overlay_counts (LrgEditorViewportPanel *self,
                                                        guint                  *n_bones,
                                                        guint                  *n_targets);

G_END_DECLS
