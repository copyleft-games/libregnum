/* lrg-editor-scene-renderer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSceneRenderer: draws the editor's 3D scene view (grid, nodes,
 * icons, selection bounds, transform gizmo, orientation axes, overlay
 * text) into the render texture of an editor viewport, and renders model
 * and font thumbnails for the asset browser.  The scene itself is drawn by
 * the engine's shared #LrgWorld3DRenderer, so the viewport and a game
 * look the same.
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
#include "../lrg-editor-math.h"
#include "../../graphics/lrg-light-set.h"
#include "../../lighting/lrg-shadow-renderer.h"
#include "../../graphics/lrg-sky-dome.h"
#include "../../postprocess/lrg-post-processor.h"
#include "../../world3d/lrg-world3d-renderer.h"
#include "../lrg-editor-play-session.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SCENE_RENDERER (lrg_editor_scene_renderer_get_type ())

/**
 * LrgEditorSceneRenderer:
 *
 * Renders the level of an #LrgEditor as seen by one camera of an
 * #LrgEditorViewState. The level's nodes are turned into a transient
 * #LrgRenderList every render and drawn by an #LrgWorld3DRenderer the
 * renderer owns (lit shader, primitive meshes, model and texture caches,
 * lights, shadows, sky, post); while a 3D level plays, the play session's
 * #LrgWorld3D is drawn instead (lrg_editor_scene_renderer_set_play_session()).
 * GPU resources are created lazily by the first
 * lrg_editor_scene_renderer_render() and freed by
 * lrg_editor_scene_renderer_release(). Without a window every call is a
 * harmless no-op.
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
 * lrg_editor_scene_renderer_set_play_session:
 * @self: an #LrgEditorSceneRenderer
 * @session: (nullable): the editor's play session, or %NULL
 *
 * While @session runs a 3D level (lrg_editor_play_session_get_world3d()
 * returns a world), every render draws that runtime world through the
 * shared #LrgWorld3DRenderer with the world's own lights, instead of the
 * authoring nodes. The grid and the 2D overlays (gizmo, axes, label,
 * stats) still draw; node icons, bounds and tilemap grids do not (the
 * play entities are not the authoring nodes).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_scene_renderer_set_play_session (LrgEditorSceneRenderer *self,
                                                 LrgEditorPlaySession   *session);

/**
 * lrg_editor_scene_renderer_get_play_session:
 * @self: an #LrgEditorSceneRenderer
 *
 * Returns: (transfer none) (nullable): the play session
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPlaySession * lrg_editor_scene_renderer_get_play_session (LrgEditorSceneRenderer *self);

/**
 * lrg_editor_scene_renderer_get_runtime_renderer:
 * @self: an #LrgEditorSceneRenderer
 *
 * The shared engine renderer the viewport draws through. Its render
 * mode, flags, background and post processor are set from the viewport
 * before every render; its asset caches, light set, shadow renderer and
 * sky dome are the viewport's.
 *
 * Returns: (transfer none): the runtime renderer
 */
LRG_AVAILABLE_IN_ALL
LrgWorld3DRenderer * lrg_editor_scene_renderer_get_runtime_renderer (LrgEditorSceneRenderer *self);

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
 * LRG_EDITOR_SCENE_RENDERER_PREVIEW_SLOT:
 *
 * The post-processing slot of the camera preview (picture in picture);
 * slots 0 .. %LRG_EDITOR_VIEW_MAX_VIEWPORTS - 1 belong to the viewports.
 */
#define LRG_EDITOR_SCENE_RENDERER_PREVIEW_SLOT (LRG_EDITOR_VIEW_MAX_VIEWPORTS)

/**
 * lrg_editor_scene_renderer_render_view:
 * @self: an #LrgEditorSceneRenderer
 * @view: the camera, sized to the target (width and height are replaced
 *   by @width and @height)
 * @view_2d: whether the camera is a 2D (XY plane) camera
 * @slot: the post-processing slot (a viewport index or
 *   %LRG_EDITOR_SCENE_RENDERER_PREVIEW_SLOT)
 * @mode: the render mode
 * @overlays: the overlays (grid, icons, bounds, scene lighting, shadows,
 *   sky, post); the 2D ones (gizmos, stats) are drawn only with
 *   lrg_editor_scene_renderer_set_draw_overlays() on
 * @width: render target width in pixels
 * @height: render target height in pixels
 *
 * Renders the level through an arbitrary camera into the bound render
 * target (the camera preview uses it with a camera node's view, see
 * lrg_editor_scene_renderer_camera_node_view()). Same contract as
 * lrg_editor_scene_renderer_render().
 *
 * Returns: %FALSE (and draws nothing) when no window is open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_scene_renderer_render_view (LrgEditorSceneRenderer *self,
                                                const LrgEditorView    *view,
                                                gboolean                view_2d,
                                                guint                   slot,
                                                LrgEditorRenderMode     mode,
                                                LrgEditorViewOverlays   overlays,
                                                gfloat                  width,
                                                gfloat                  height);

/**
 * lrg_editor_scene_renderer_set_draw_overlays:
 * @self: an #LrgEditorSceneRenderer
 * @draw_overlays: whether lrg_editor_scene_renderer_render() draws the
 *   2D overlays (gizmo, orientation axes, box select, view label, stats)
 *   into the image itself
 *
 * On by default for embedders that show the image as is. The editor's
 * viewport panels turn it off and paint those overlays with
 * #LrgPaintList instead (see lrg-editor-gizmo-paint.h), so they stay
 * crisp and update without re-rendering the scene.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_scene_renderer_set_draw_overlays (LrgEditorSceneRenderer *self,
                                                  gboolean                draw_overlays);

/**
 * lrg_editor_scene_renderer_get_draw_overlays:
 * @self: an #LrgEditorSceneRenderer
 *
 * Returns: whether the 2D overlays are drawn into the image
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_scene_renderer_get_draw_overlays (LrgEditorSceneRenderer *self);

/**
 * lrg_editor_scene_renderer_get_light_set:
 * @self: an #LrgEditorSceneRenderer
 *
 * The engine light set the lit render modes draw with. It is rebuilt
 * from the level by lrg_editor_scene_renderer_sync_lights() (every
 * render does that); its bookkeeping works without a window.
 *
 * Returns: (transfer none): the light set
 */
LRG_AVAILABLE_IN_ALL
LrgLightSet * lrg_editor_scene_renderer_get_light_set (LrgEditorSceneRenderer *self);

/**
 * lrg_editor_scene_renderer_sync_lights:
 * @self: an #LrgEditorSceneRenderer
 * @scene_lighting: use the level's LIGHT nodes (overlay
 *   %LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING); %FALSE lights the scene with
 *   the default editor sun only
 *
 * Rebuilds the light set from the level: every visible LIGHT node whose
 * "light_type" visual param is "directional" becomes the directional
 * light (the first one wins; it shines down the node's local -Z), every
 * other LIGHT node a point light at its world position with the
 * "range" param as radius, the "intensity" param as intensity and the
 * material colour (white without a material) as colour. There are no
 * spot lights in the engine's 3D light set: "spot" lights (or a
 * "spot_angle" above 0) are previewed as point lights. Without a
 * directional light in the level the default editor sun is used so the
 * scene stays readable. The lights go through
 * lrg_world3d_renderer_sync_lights() as #LrgLight3DDesc records, exactly
 * as a game's light components do. Works without a window.
 *
 * Returns: the number of the level's lights in the set (0 when
 *   @scene_lighting is %FALSE)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_scene_renderer_sync_lights (LrgEditorSceneRenderer *self,
                                             gboolean                scene_lighting);

/**
 * lrg_editor_scene_renderer_has_scene_sun:
 * @self: an #LrgEditorSceneRenderer
 *
 * Returns: %TRUE when the last lrg_editor_scene_renderer_sync_lights()
 *   took the directional light from the level, %FALSE when it used the
 *   default editor sun
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_scene_renderer_has_scene_sun (LrgEditorSceneRenderer *self);

/**
 * lrg_editor_scene_renderer_get_shadow_renderer:
 * @self: an #LrgEditorSceneRenderer
 *
 * The cascaded shadow maps of the directional light, used for
 * perspective viewports with overlay %LRG_EDITOR_VIEW_OVERLAY_SHADOWS.
 *
 * Returns: (transfer none): the shadow renderer
 */
LRG_AVAILABLE_IN_ALL
LrgShadowRenderer * lrg_editor_scene_renderer_get_shadow_renderer (LrgEditorSceneRenderer *self);

/**
 * lrg_editor_scene_renderer_get_sky_dome:
 * @self: an #LrgEditorSceneRenderer
 *
 * The sky drawn behind perspective viewports with overlay
 * %LRG_EDITOR_VIEW_OVERLAY_SKY. Its sun follows the directional light.
 *
 * Returns: (transfer none): the sky dome
 */
LRG_AVAILABLE_IN_ALL
LrgSkyDome * lrg_editor_scene_renderer_get_sky_dome (LrgEditorSceneRenderer *self);

/**
 * lrg_editor_scene_renderer_get_post_processor:
 * @self: an #LrgEditorSceneRenderer
 * @slot: a viewport index or %LRG_EDITOR_SCENE_RENDERER_PREVIEW_SLOT
 *
 * The preview post-processing chain of one viewport: an
 * #LrgPostProcessor holding an #LrgFxaa ("fxaa"), #LrgBloom ("bloom"),
 * #LrgVignette ("vignette") and #LrgColorGrade ("color-grade"). Change
 * their parameters or enable flags to change the preview. Used only
 * with overlay %LRG_EDITOR_VIEW_OVERLAY_POST.
 *
 * Returns: (transfer none) (nullable): the post processor, %NULL for a
 *   slot out of range
 */
LRG_AVAILABLE_IN_ALL
LrgPostProcessor * lrg_editor_scene_renderer_get_post_processor (LrgEditorSceneRenderer *self,
                                                                 guint                   slot);

/**
 * lrg_editor_scene_renderer_get_generation:
 * @self: an #LrgEditorSceneRenderer
 *
 * A counter bumped whenever something the image depends on changed
 * outside the level (an asset reloaded, the base directory or the grid
 * settings changed), so viewports that render on demand know to redraw.
 *
 * Returns: the generation
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_scene_renderer_get_generation (LrgEditorSceneRenderer *self);

/**
 * lrg_editor_scene_renderer_is_asset_missing:
 * @self: an #LrgEditorSceneRenderer
 * @node: a node
 *
 * Whether @node references a model, sprite or tileset file that does
 * not exist (resolved against the base directory). Works without a
 * window; the viewport paints these nodes with magenta boxes.
 *
 * Returns: %TRUE when the node's asset is missing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_scene_renderer_is_asset_missing (LrgEditorSceneRenderer *self,
                                                     LrgNode                *node);

/**
 * lrg_editor_scene_renderer_camera_node_view:
 * @node: a node (normally a CAMERA node)
 * @width: target width in pixels
 * @height: target height in pixels
 * @out: (out caller-allocates): the view through @node
 *
 * The view through a camera node: its world position, looking down its
 * local -Z with its local +Y up, with the "fov" (degrees), "near" and
 * "far" visual params (60, 0.1 and 1000 when unset).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_scene_renderer_camera_node_view (LrgNode       *node,
                                                 gfloat         width,
                                                 gfloat         height,
                                                 LrgEditorView *out);

/**
 * lrg_editor_scene_renderer_get_shader_source:
 * @name: a shader name: "scene.vs", "lit.fs", "unlit.fs", "normals.fs"
 *   or "post.fs"
 *
 * The built-in GLSL of the viewport shaders, which are the shared
 * renderer's (lrg_world3d_renderer_get_shader_source()). The readable
 * copies live in data/editor/shaders/@name.glsl; the library embeds
 * them so an installed editor needs no data files.
 *
 * Returns: (nullable): the source, or %NULL for an unknown name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_scene_renderer_get_shader_source (const gchar *name);

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
