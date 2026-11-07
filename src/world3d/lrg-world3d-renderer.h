/* lrg-world3d-renderer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorld3DRenderer: the shared 3D renderer.  It draws an #LrgWorld3D
 * (or any #LrgRenderList) through a camera: lights synced into an
 * #LrgLightSet, cascaded/cube shadow passes replaying the shadow-caster
 * list, the sky dome, the opaque, cutout and transparent passes, optional
 * static batching, debug draw and a preview post chain.  Games draw their
 * worlds with it and the editor viewport draws through it, so both look
 * the same.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-world3d.h"
#include "../graphics/lrg-light-set.h"
#include "../lighting/lrg-shadow-renderer.h"
#include "../graphics/lrg-sky-dome.h"
#include "../postprocess/lrg-post-processor.h"
#include "../debug/lrg-debug-draw.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

/**
 * LrgWorld3DRenderMode:
 * @LRG_WORLD3D_RENDER_MODE_LIT: textures and materials lit by the light
 *   set (sun, point lights, ambient, shadows)
 * @LRG_WORLD3D_RENDER_MODE_UNLIT: flat albedo (texture times colour)
 * @LRG_WORLD3D_RENDER_MODE_WIREFRAME: unlit triangles drawn as lines
 * @LRG_WORLD3D_RENDER_MODE_NORMALS: world-space normals as colours
 * @LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY: the lighting on a white surface
 *
 * How the colour passes shade surfaces.  The values match
 * #LrgEditorRenderMode one to one, so the editor's viewport modes map
 * straight onto the runtime renderer.
 */
typedef enum
{
	LRG_WORLD3D_RENDER_MODE_LIT,
	LRG_WORLD3D_RENDER_MODE_UNLIT,
	LRG_WORLD3D_RENDER_MODE_WIREFRAME,
	LRG_WORLD3D_RENDER_MODE_NORMALS,
	LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY
} LrgWorld3DRenderMode;

LRG_AVAILABLE_IN_ALL
GType lrg_world3d_render_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_WORLD3D_RENDER_MODE (lrg_world3d_render_mode_get_type ())

/**
 * LrgWorld3DRenderFlags:
 * @LRG_WORLD3D_RENDER_FLAG_NONE: none of the optional parts
 * @LRG_WORLD3D_RENDER_FLAG_SHADOWS: render the shadow passes of the
 *   light set's shadow renderer (lit modes, perspective views)
 * @LRG_WORLD3D_RENDER_FLAG_SKY: draw the sky dome behind the scene
 *   (perspective views)
 * @LRG_WORLD3D_RENDER_FLAG_POST: run the preview post chain of
 *   #LrgWorld3DRenderer:post-processor (needs a render texture target or
 *   the screen)
 * @LRG_WORLD3D_RENDER_FLAG_CLEAR: clear the target to
 *   #LrgWorld3DRenderer:background first
 * @LRG_WORLD3D_RENDER_FLAG_DEBUG_DRAW: draw #LrgWorld3DRenderer:debug-draw
 *   inside the 3D pass and its labels after it
 * @LRG_WORLD3D_RENDER_FLAG_BATCH_STATIC: merge the opaque meshes of static
 *   entities into an #LrgStaticBatch (lit mode only)
 *
 * The optional parts of a render.  See %LRG_WORLD3D_RENDER_FLAGS_DEFAULT.
 */
typedef enum /*< flags >*/
{
	LRG_WORLD3D_RENDER_FLAG_NONE         = 0,
	LRG_WORLD3D_RENDER_FLAG_SHADOWS      = 1 << 0,
	LRG_WORLD3D_RENDER_FLAG_SKY          = 1 << 1,
	LRG_WORLD3D_RENDER_FLAG_POST         = 1 << 2,
	LRG_WORLD3D_RENDER_FLAG_CLEAR        = 1 << 3,
	LRG_WORLD3D_RENDER_FLAG_DEBUG_DRAW   = 1 << 4,
	LRG_WORLD3D_RENDER_FLAG_BATCH_STATIC = 1 << 5
} LrgWorld3DRenderFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_world3d_render_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_WORLD3D_RENDER_FLAGS (lrg_world3d_render_flags_get_type ())

/**
 * LRG_WORLD3D_RENDER_FLAGS_DEFAULT:
 *
 * The flags a new renderer starts with: shadows, sky, clear and debug
 * draw (post-processing and static batching off).
 */
#define LRG_WORLD3D_RENDER_FLAGS_DEFAULT \
	(LRG_WORLD3D_RENDER_FLAG_SHADOWS | LRG_WORLD3D_RENDER_FLAG_SKY | LRG_WORLD3D_RENDER_FLAG_CLEAR | \
	 LRG_WORLD3D_RENDER_FLAG_DEBUG_DRAW)

/**
 * LrgWorld3DRenderPass:
 * @LRG_WORLD3D_RENDER_PASS_SHADOW: shadow-caster draws (all shadow passes)
 * @LRG_WORLD3D_RENDER_PASS_SKY: the sky dome
 * @LRG_WORLD3D_RENDER_PASS_OPAQUE: opaque items (and static batch chunks)
 * @LRG_WORLD3D_RENDER_PASS_CUTOUT: alpha-tested items
 * @LRG_WORLD3D_RENDER_PASS_TRANSPARENT: blended items, back to front
 * @LRG_WORLD3D_RENDER_PASS_POST: the post chain
 *
 * The passes a render runs, in order.  lrg_world3d_renderer_get_pass_count()
 * reports what each pass drew in the last render.
 */
typedef enum
{
	LRG_WORLD3D_RENDER_PASS_SHADOW,
	LRG_WORLD3D_RENDER_PASS_SKY,
	LRG_WORLD3D_RENDER_PASS_OPAQUE,
	LRG_WORLD3D_RENDER_PASS_CUTOUT,
	LRG_WORLD3D_RENDER_PASS_TRANSPARENT,
	LRG_WORLD3D_RENDER_PASS_POST
} LrgWorld3DRenderPass;

LRG_AVAILABLE_IN_ALL
GType lrg_world3d_render_pass_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_WORLD3D_RENDER_PASS (lrg_world3d_render_pass_get_type ())

/**
 * LRG_WORLD3D_RENDER_PASS_COUNT:
 *
 * The number of #LrgWorld3DRenderPass values.
 */
#define LRG_WORLD3D_RENDER_PASS_COUNT (6)

/* ==========================================================================
 * LrgWorld3DView
 * ========================================================================== */

typedef struct _LrgWorld3DView LrgWorld3DView;

/**
 * LrgWorld3DView:
 * @position: eye position in world space
 * @target: the point the eye looks at
 * @up: the up direction
 * @fovy: vertical field of view in degrees (perspective)
 * @ortho_height: visible height in world units (orthographic)
 * @orthographic: %TRUE for an orthographic projection
 * @near_plane: near clip distance (> 0)
 * @far_plane: far clip distance (> @near_plane)
 * @width: target width in pixels
 * @height: target height in pixels
 * @cull_mask: render layers the view sees (bit N = layer N)
 *
 * A camera as the renderer sees it: the same fields (and the same matrix
 * conventions, lrg_math3d_look_at() and lrg_math3d_perspective()) as the
 * editor's #LrgEditorView, plus a render-layer cull mask.  Fill one with
 * lrg_world3d_view_init() or lrg_world3d_view_init_from_camera().
 */
struct _LrgWorld3DView
{
	gfloat   position[3];
	gfloat   target[3];
	gfloat   up[3];
	gfloat   fovy;
	gfloat   ortho_height;
	gboolean orthographic;
	gfloat   near_plane;
	gfloat   far_plane;
	gfloat   width;
	gfloat   height;
	guint32  cull_mask;

	/*< private >*/
	gpointer _reserved[4];
};

LRG_AVAILABLE_IN_ALL
GType lrg_world3d_view_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_WORLD3D_VIEW (lrg_world3d_view_get_type ())

/**
 * lrg_world3d_view_init:
 * @view: (out caller-allocates): the view to fill
 * @width: target width in pixels
 * @height: target height in pixels
 *
 * A perspective view at (8, 6, 8) looking at the origin with +Y up, a
 * 45 degree field of view, clip planes 0.05 and 2000, ortho height 10 and
 * every render layer (the editor's default camera).
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_view_init (LrgWorld3DView *view,
                            gfloat          width,
                            gfloat          height);

/**
 * lrg_world3d_view_init_from_camera:
 * @view: (out caller-allocates): the view to fill
 * @camera: a 3D camera
 * @width: target width in pixels
 * @height: target height in pixels
 *
 * The view through @camera: its render pose (lrg_camera3d_get_render_pose(),
 * so fixed-step interpolation is honoured), up vector, field of view (the
 * ortho height for an orthographic camera, as raylib reads fovy), rlgl's
 * cull distances as clip planes and the camera's #LrgCamera:cull-mask.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_view_init_from_camera (LrgWorld3DView *view,
                                        LrgCamera3D    *camera,
                                        gfloat          width,
                                        gfloat          height);

/**
 * lrg_world3d_view_copy:
 * @view: a view
 *
 * Returns: (transfer full): a heap copy of @view
 */
LRG_AVAILABLE_IN_ALL
LrgWorld3DView * lrg_world3d_view_copy (const LrgWorld3DView *view);

/**
 * lrg_world3d_view_free:
 * @view: (nullable): a view from lrg_world3d_view_copy()
 *
 * Frees a heap view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_view_free (LrgWorld3DView *view);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgWorld3DView, lrg_world3d_view_free)

/**
 * lrg_world3d_view_get_view_matrix:
 * @view: a view
 * @out: (out caller-allocates) (array fixed-size=16): the column-major
 *   view matrix
 *
 * The world-to-eye matrix (lrg_math3d_look_at()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_view_get_view_matrix (const LrgWorld3DView *view,
                                       gfloat               *out);

/**
 * lrg_world3d_view_get_projection_matrix:
 * @view: a view
 * @out: (out caller-allocates) (array fixed-size=16): the column-major
 *   projection matrix
 *
 * The projection for the view's size: a perspective of @fovy degrees, or
 * an orthographic box @ortho_height high.  A non-positive near plane
 * becomes 0.05 and a far plane at or before it near + 1000, as in the
 * editor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_view_get_projection_matrix (const LrgWorld3DView *view,
                                             gfloat               *out);

/* ==========================================================================
 * LrgWorld3DRenderer
 * ========================================================================== */

#define LRG_TYPE_WORLD3D_RENDERER (lrg_world3d_renderer_get_type ())

/**
 * LrgWorld3DRenderer:
 *
 * Draws render lists through a view.  It owns every GPU resource it
 * needs (primitive meshes, the render-mode shaders, model and texture
 * caches, post targets); they are created by the first GPU render and
 * freed by lrg_world3d_renderer_release().  The light set, shadow
 * renderer and sky dome live from construction on (their bookkeeping is
 * headless).  Without a window, or with #LrgWorld3DRenderer:headless
 * set, a render runs every pass without drawing and records what it
 * would draw (lrg_world3d_renderer_get_pass_count()).
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWorld3DRenderer, lrg_world3d_renderer, LRG, WORLD3D_RENDERER, GObject)

/**
 * LrgWorld3DAssetResolver:
 * @asset: an asset string as written in a component or level (a path
 *   relative to the base directory, an absolute path, or a GUID)
 * @user_data: the resolver's data
 *
 * Turns an asset string into an absolute file path, for example by
 * looking a GUID up in an asset database.  Returning %NULL falls back to
 * the default resolution (absolute paths unchanged, relative ones against
 * #LrgWorld3DRenderer:base-dir).
 *
 * Returns: (transfer full) (nullable) (type filename): the absolute path
 */
typedef gchar * (*LrgWorld3DAssetResolver) (const gchar *asset,
                                            gpointer     user_data);

/**
 * lrg_world3d_renderer_new:
 *
 * Creates a renderer with %LRG_WORLD3D_RENDER_FLAGS_DEFAULT, the lit mode,
 * a dark blue background and the fallback sun on.  No GPU resources are
 * allocated until the first render.
 *
 * Returns: (transfer full): a new #LrgWorld3DRenderer
 */
LRG_AVAILABLE_IN_ALL
LrgWorld3DRenderer * lrg_world3d_renderer_new (void);

/* --- Rendering ----------------------------------------------------------- */

/**
 * lrg_world3d_renderer_render:
 * @self: an #LrgWorld3DRenderer
 * @world: the world to draw
 * @camera: (nullable): the camera, or %NULL for the world's active camera
 *   (lrg_world3d_get_active_camera(), which must be an #LrgCamera3D)
 * @target: (nullable): the render texture to draw into, or %NULL for the
 *   screen (then call it between BeginDrawing() and EndDrawing())
 * @width: target width in pixels, or 0 for the target's (or the
 *   screen's) width
 * @height: target height in pixels, or 0 for the target's (or the
 *   screen's) height
 *
 * The game entry point: collects @world's render list for @camera
 * (lrg_world3d_collect_render(): layer cull mask, frustum culling,
 * sorting, the registered lights), then draws it like
 * lrg_world3d_renderer_render_list() into @target or the screen.
 *
 * Returns: %TRUE when something was drawn on the GPU; %FALSE without a
 *   camera, or when the render only recorded (headless)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_renderer_render (LrgWorld3DRenderer *self,
                                      LrgWorld3D         *world,
                                      LrgCamera3D        *camera,
                                      GrlRenderTexture   *target,
                                      gint                width,
                                      gint                height);

/**
 * lrg_world3d_renderer_render_view:
 * @self: an #LrgWorld3DRenderer
 * @world: the world to draw
 * @view: the view (size and cull mask included)
 *
 * Collects every visible entity of @world (no frustum culling), keeps the
 * layers of @view's cull mask, sorts for @view's eye and draws into the
 * render target that is currently bound (a viewport's render texture, as
 * the editor's play view does).
 *
 * Returns: %TRUE when something was drawn on the GPU
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_renderer_render_view (LrgWorld3DRenderer   *self,
                                           LrgWorld3D           *world,
                                           const LrgWorld3DView *view);

/**
 * lrg_world3d_renderer_render_list:
 * @self: an #LrgWorld3DRenderer
 * @list: a render list (sorted: lrg_render_list_sort())
 * @view: the view (size and cull mask included)
 *
 * Draws @list into the render target that is currently bound:
 *
 * 1. syncs the list's lights into the light set
 *    (lrg_world3d_renderer_sync_lights());
 * 2. lit modes with %LRG_WORLD3D_RENDER_FLAG_SHADOWS in a perspective
 *    view: updates the shadow renderer around the view and replays the
 *    SHADOW_CASTER list once per pass (cascade or cube face) with the
 *    caster shader, keeping the items whose layer is in both the view's
 *    cull mask and the shadow renderer's caster mask;
 * 3. clears to #LrgWorld3DRenderer:background (%LRG_WORLD3D_RENDER_FLAG_CLEAR),
 *    into the post image when %LRG_WORLD3D_RENDER_FLAG_POST is on;
 * 4. loads the view's matrices, draws the sky (%LRG_WORLD3D_RENDER_FLAG_SKY,
 *    perspective only) and emits #LrgWorld3DRenderer::draw-background;
 * 5. the OPAQUE, CUTOUT (alpha cutoff 0.5) and TRANSPARENT (no depth
 *    writes) categories, each item with the render mode's shader;
 * 6. emits #LrgWorld3DRenderer::draw-overlay, draws the debug draw;
 * 7. ends the 3D mode, draws debug labels and runs the post chain.
 *
 * Items whose layer is not in the view's cull mask are skipped.  The
 * target is left bound with its 2D projection, ready for overlays.
 *
 * Returns: %TRUE when something was drawn on the GPU; %FALSE when the
 *   render only recorded (no window, or #LrgWorld3DRenderer:headless)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_renderer_render_list (LrgWorld3DRenderer   *self,
                                           LrgRenderList        *list,
                                           const LrgWorld3DView *view);

/**
 * lrg_world3d_renderer_get_render_list:
 * @self: an #LrgWorld3DRenderer
 *
 * The list lrg_world3d_renderer_render() and
 * lrg_world3d_renderer_render_view() collect into (reused every frame).
 *
 * Returns: (transfer none): the renderer's render list
 */
LRG_AVAILABLE_IN_ALL
LrgRenderList * lrg_world3d_renderer_get_render_list (LrgWorld3DRenderer *self);

/* --- Settings ------------------------------------------------------------ */

/**
 * lrg_world3d_renderer_set_render_mode:
 * @self: an #LrgWorld3DRenderer
 * @mode: the shading mode of the colour passes
 *
 * Sets #LrgWorld3DRenderer:render-mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_set_render_mode (LrgWorld3DRenderer   *self,
                                           LrgWorld3DRenderMode  mode);

/**
 * lrg_world3d_renderer_get_render_mode:
 * @self: an #LrgWorld3DRenderer
 *
 * Returns: the render mode
 */
LRG_AVAILABLE_IN_ALL
LrgWorld3DRenderMode lrg_world3d_renderer_get_render_mode (LrgWorld3DRenderer *self);

/**
 * lrg_world3d_renderer_set_flags:
 * @self: an #LrgWorld3DRenderer
 * @flags: the optional parts to run
 *
 * Sets #LrgWorld3DRenderer:flags.  The editor sets them before every
 * viewport render from the viewport's overlays.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_set_flags (LrgWorld3DRenderer    *self,
                                     LrgWorld3DRenderFlags  flags);

/**
 * lrg_world3d_renderer_get_flags:
 * @self: an #LrgWorld3DRenderer
 *
 * Returns: the render flags
 */
LRG_AVAILABLE_IN_ALL
LrgWorld3DRenderFlags lrg_world3d_renderer_get_flags (LrgWorld3DRenderer *self);

/**
 * lrg_world3d_renderer_set_background:
 * @self: an #LrgWorld3DRenderer
 * @color: the clear colour
 *
 * Sets the colour %LRG_WORLD3D_RENDER_FLAG_CLEAR clears to (the sky, when
 * drawn, covers it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_set_background (LrgWorld3DRenderer *self,
                                          const GrlColor     *color);

/**
 * lrg_world3d_renderer_get_background:
 * @self: an #LrgWorld3DRenderer
 * @out: (out caller-allocates): the clear colour
 *
 * Gets the clear colour.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_get_background (LrgWorld3DRenderer *self,
                                          GrlColor           *out);

/**
 * lrg_world3d_renderer_set_ambient:
 * @self: an #LrgWorld3DRenderer
 * @color: the ambient light colour
 *
 * Sets the light set's ambient colour used from the next render on
 * (default 58, 62, 78).
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_set_ambient (LrgWorld3DRenderer *self,
                                       const GrlColor     *color);

/**
 * lrg_world3d_renderer_set_fallback_sun:
 * @self: an #LrgWorld3DRenderer
 * @fallback_sun: whether a scene without a directional light gets the
 *   default sun
 *
 * Sets #LrgWorld3DRenderer:fallback-sun.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_set_fallback_sun (LrgWorld3DRenderer *self,
                                            gboolean            fallback_sun);

/**
 * lrg_world3d_renderer_get_fallback_sun:
 * @self: an #LrgWorld3DRenderer
 *
 * Returns: whether the default sun lights scenes without a directional
 *   light
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_renderer_get_fallback_sun (LrgWorld3DRenderer *self);

/**
 * lrg_world3d_renderer_set_post_processor:
 * @self: an #LrgWorld3DRenderer
 * @processor: (nullable): the post chain to run with
 *   %LRG_WORLD3D_RENDER_FLAG_POST, or %NULL for none
 *
 * Sets #LrgWorld3DRenderer:post-processor.  The renderer reads the
 * parameters and enable flags of the processor's effects named "fxaa"
 * (#LrgFxaa), "bloom" (#LrgBloom), "vignette" (#LrgVignette) and
 * "color-grade" (#LrgColorGrade); other effects are ignored.  The images
 * the chain needs are kept per processor, so several viewports can each
 * use their own processor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_set_post_processor (LrgWorld3DRenderer *self,
                                              LrgPostProcessor   *processor);

/**
 * lrg_world3d_renderer_get_post_processor:
 * @self: an #LrgWorld3DRenderer
 *
 * Returns: (transfer none) (nullable): the post processor
 */
LRG_AVAILABLE_IN_ALL
LrgPostProcessor * lrg_world3d_renderer_get_post_processor (LrgWorld3DRenderer *self);

/**
 * lrg_world3d_renderer_new_preview_post_processor:
 *
 * A post processor holding the four effects the renderer understands,
 * with a subtle preview look: an #LrgFxaa ("fxaa"), an #LrgBloom ("bloom",
 * threshold 0.75, intensity 0.6), an #LrgVignette ("vignette", intensity
 * 0.35, radius 0.55, smoothness 0.45) and an #LrgColorGrade
 * ("color-grade", contrast 1.05, saturation 1.08).
 *
 * Returns: (transfer full): a new #LrgPostProcessor
 */
LRG_AVAILABLE_IN_ALL
LrgPostProcessor * lrg_world3d_renderer_new_preview_post_processor (void);

/**
 * lrg_world3d_renderer_set_debug_draw:
 * @self: an #LrgWorld3DRenderer
 * @debug_draw: (nullable): the debug draw to show, or %NULL for none
 *
 * Sets #LrgWorld3DRenderer:debug-draw.  With
 * %LRG_WORLD3D_RENDER_FLAG_DEBUG_DRAW each render calls
 * lrg_debug_draw_draw_3d() inside its 3D pass and
 * lrg_debug_draw_draw_labels() after it; it does not end the debug
 * frame, so one batch can be drawn into several views (call
 * lrg_debug_draw_end_frame() once per frame).
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_set_debug_draw (LrgWorld3DRenderer *self,
                                          LrgDebugDraw       *debug_draw);

/**
 * lrg_world3d_renderer_get_debug_draw:
 * @self: an #LrgWorld3DRenderer
 *
 * Returns: (transfer none) (nullable): the debug draw
 */
LRG_AVAILABLE_IN_ALL
LrgDebugDraw * lrg_world3d_renderer_get_debug_draw (LrgWorld3DRenderer *self);

/**
 * lrg_world3d_renderer_set_headless:
 * @self: an #LrgWorld3DRenderer
 * @headless: whether renders only record
 *
 * Sets #LrgWorld3DRenderer:headless.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_set_headless (LrgWorld3DRenderer *self,
                                        gboolean            headless);

/**
 * lrg_world3d_renderer_get_headless:
 * @self: an #LrgWorld3DRenderer
 *
 * Returns: whether renders only record
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_renderer_get_headless (LrgWorld3DRenderer *self);

/* --- Lighting ------------------------------------------------------------ */

/**
 * lrg_world3d_renderer_sync_lights:
 * @self: an #LrgWorld3DRenderer
 * @lights: (array length=n_lights) (nullable): the lights
 * @n_lights: the number of @lights
 *
 * Rebuilds the light set: the first enabled directional light becomes
 * the sun (its colour times its intensity; later directional lights are
 * ignored), every enabled point light a point light at its position with
 * its range as radius (10 when not positive), its intensity and its
 * colour.  The engine's 3D light set has no spot lights yet (0.9 M2 adds
 * them): spot lights are previewed as point lights.  Lights' layer masks
 * are not applied per surface.  Without a directional light the default
 * sun (direction 0.5, -0.75, -0.45, warm white) is used while
 * #LrgWorld3DRenderer:fallback-sun is on.  Every render does this from its
 * list's lights; it works without a window.
 *
 * Returns: the number of the given lights in the set
 */
LRG_AVAILABLE_IN_ALL
guint lrg_world3d_renderer_sync_lights (LrgWorld3DRenderer   *self,
                                        const LrgLight3DDesc *lights,
                                        guint                 n_lights);

/**
 * lrg_world3d_renderer_has_scene_sun:
 * @self: an #LrgWorld3DRenderer
 *
 * Returns: %TRUE when the last light sync took the sun from the lights,
 *   %FALSE when it used the default sun (or none)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_renderer_has_scene_sun (LrgWorld3DRenderer *self);

/**
 * lrg_world3d_renderer_get_sun_direction:
 * @self: an #LrgWorld3DRenderer
 * @out: (out caller-allocates) (array fixed-size=3): the unit direction
 *   sunlight travels
 *
 * Gets the sun direction of the last light sync (the sky dome's sun sits
 * opposite it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_get_sun_direction (LrgWorld3DRenderer *self,
                                             gfloat             *out);

/**
 * lrg_world3d_renderer_get_light_set:
 * @self: an #LrgWorld3DRenderer
 *
 * The light set the lit modes draw with.  lrg_world3d_renderer_release()
 * replaces it (its shader belongs to the old context), so do not keep
 * the pointer across a release.
 *
 * Returns: (transfer none): the light set
 */
LRG_AVAILABLE_IN_ALL
LrgLightSet * lrg_world3d_renderer_get_light_set (LrgWorld3DRenderer *self);

/**
 * lrg_world3d_renderer_get_shadow_renderer:
 * @self: an #LrgWorld3DRenderer
 *
 * The shadow maps (3 cascades of 2048 pixels over 60 units, 3x3 PCF,
 * strength 0.85 by default; configure point-light shadows on it with
 * lrg_shadow_renderer_set_points()).  Its caster mask filters the
 * shadow-caster replay.
 *
 * Returns: (transfer none): the shadow renderer
 */
LRG_AVAILABLE_IN_ALL
LrgShadowRenderer * lrg_world3d_renderer_get_shadow_renderer (LrgWorld3DRenderer *self);

/**
 * lrg_world3d_renderer_get_sky_dome:
 * @self: an #LrgWorld3DRenderer
 *
 * The sky drawn with %LRG_WORLD3D_RENDER_FLAG_SKY.  Every render points
 * its sun opposite the light set's sun and sizes it to half the far plane
 * (1 .. 500 units).
 *
 * Returns: (transfer none): the sky dome
 */
LRG_AVAILABLE_IN_ALL
LrgSkyDome * lrg_world3d_renderer_get_sky_dome (LrgWorld3DRenderer *self);

/* --- Assets -------------------------------------------------------------- */

/**
 * lrg_world3d_renderer_set_base_dir:
 * @self: an #LrgWorld3DRenderer
 * @dir: (nullable) (type filename): the directory relative asset paths
 *   resolve against, or %NULL for the working directory
 *
 * Sets #LrgWorld3DRenderer:base-dir.  Changing it forgets the per-string
 * lookups; assets already loaded by absolute path stay cached.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_set_base_dir (LrgWorld3DRenderer *self,
                                        const gchar        *dir);

/**
 * lrg_world3d_renderer_get_base_dir:
 * @self: an #LrgWorld3DRenderer
 *
 * Returns: (nullable) (type filename): the asset base directory
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_world3d_renderer_get_base_dir (LrgWorld3DRenderer *self);

/**
 * lrg_world3d_renderer_set_asset_resolver:
 * @self: an #LrgWorld3DRenderer
 * @resolver: (nullable) (scope notified) (closure user_data) (destroy destroy):
 *   the resolver, or %NULL for the default resolution
 * @user_data: data for @resolver
 * @destroy: (nullable): frees @user_data
 *
 * Installs a resolver for asset strings (GUIDs, virtual paths).  It runs
 * once per distinct string; the result is cached until the base
 * directory or the resolver changes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_set_asset_resolver (LrgWorld3DRenderer      *self,
                                              LrgWorld3DAssetResolver  resolver,
                                              gpointer                 user_data,
                                              GDestroyNotify           destroy);

/**
 * lrg_world3d_renderer_resolve_asset:
 * @self: an #LrgWorld3DRenderer
 * @asset: an asset string
 *
 * Resolves @asset as the caches do: the resolver first, then absolute
 * paths unchanged and relative ones against the base directory (or the
 * working directory).
 *
 * Returns: (transfer full) (type filename): the absolute path
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_world3d_renderer_resolve_asset (LrgWorld3DRenderer *self,
                                            const gchar        *asset);

/**
 * lrg_world3d_renderer_get_model:
 * @self: an #LrgWorld3DRenderer
 * @asset: a model asset string (path or GUID)
 *
 * The cached model for @asset, loaded on first use (needs a window).
 * Failures are cached too and logged once.  Put the result in an
 * #LrgRenderItem's model field.
 *
 * Returns: (transfer none) (nullable): the model, or %NULL when it cannot
 *   be loaded (or there is no window)
 */
LRG_AVAILABLE_IN_ALL
GrlModel * lrg_world3d_renderer_get_model (LrgWorld3DRenderer *self,
                                           const gchar        *asset);

/**
 * lrg_world3d_renderer_get_texture:
 * @self: an #LrgWorld3DRenderer
 * @asset: an image asset string (path or GUID; .pep is decoded by graylib)
 *
 * As lrg_world3d_renderer_get_model(), for textures.
 *
 * Returns: (transfer none) (nullable): the texture
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_world3d_renderer_get_texture (LrgWorld3DRenderer *self,
                                               const gchar        *asset);

/**
 * lrg_world3d_renderer_is_asset_missing:
 * @self: an #LrgWorld3DRenderer
 * @asset: an asset string
 *
 * Works without a window.
 *
 * Returns: %TRUE when @asset resolves to a file that does not exist, or
 *   whose load already failed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_renderer_is_asset_missing (LrgWorld3DRenderer *self,
                                                const gchar        *asset);

/**
 * lrg_world3d_renderer_forget_asset:
 * @self: an #LrgWorld3DRenderer
 * @abs_path: (type filename): absolute path of an asset that changed
 *
 * Drops the cached copy so the next use loads it again (live reload).
 * Unknown paths are ignored.  Items of a list built before the call may
 * still point at the dropped model: rebuild the list.
 *
 * Returns: %TRUE when something was cached for @abs_path
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_renderer_forget_asset (LrgWorld3DRenderer *self,
                                            const gchar        *abs_path);

/**
 * lrg_world3d_renderer_reload_asset:
 * @self: an #LrgWorld3DRenderer
 * @abs_path: (type filename): absolute path of a model or image
 *
 * Forgets @abs_path and loads it again right away (a model when it was
 * cached as one or has a model extension, a texture otherwise).
 *
 * Returns: %TRUE when the new copy loaded (needs a window)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_renderer_reload_asset (LrgWorld3DRenderer *self,
                                            const gchar        *abs_path);

/**
 * lrg_world3d_renderer_get_generation:
 * @self: an #LrgWorld3DRenderer
 *
 * A counter bumped whenever cached assets or their resolution changed
 * (forget, reload, base directory, resolver), for views that render on
 * demand.
 *
 * Returns: the generation
 */
LRG_AVAILABLE_IN_ALL
guint lrg_world3d_renderer_get_generation (LrgWorld3DRenderer *self);

/* --- Statistics ---------------------------------------------------------- */

/**
 * lrg_world3d_renderer_get_drawn:
 * @self: an #LrgWorld3DRenderer
 *
 * Returns: the draws (items, model meshes, batch chunks, shadow casters)
 *   of the last render, recorded ones included
 */
LRG_AVAILABLE_IN_ALL
guint lrg_world3d_renderer_get_drawn (LrgWorld3DRenderer *self);

/**
 * lrg_world3d_renderer_get_pass_count:
 * @self: an #LrgWorld3DRenderer
 * @pass: a pass
 *
 * What @pass drew in the last render (GPU or recorded): items for the
 * colour passes and the shadow pass (each caster once per shadow pass),
 * 1 for a sky or post pass that ran, 0 otherwise.
 *
 * Returns: the count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_world3d_renderer_get_pass_count (LrgWorld3DRenderer   *self,
                                           LrgWorld3DRenderPass  pass);

/**
 * lrg_world3d_renderer_get_recorded_entity:
 * @self: an #LrgWorld3DRenderer
 * @pass: %LRG_WORLD3D_RENDER_PASS_SHADOW, _OPAQUE, _CUTOUT or _TRANSPARENT
 * @index: a draw index below lrg_world3d_renderer_get_pass_count()
 *
 * The entity of the @index-th draw of @pass in the last render, in draw
 * order (for tests and profilers).  Batched static items are not listed.
 *
 * Returns: (transfer none) (nullable): the entity, %NULL for an item
 *   without one or an index out of range
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_world3d_renderer_get_recorded_entity (LrgWorld3DRenderer   *self,
                                                        LrgWorld3DRenderPass  pass,
                                                        guint                 index);

/**
 * lrg_world3d_renderer_get_shadow_pass_count:
 * @self: an #LrgWorld3DRenderer
 *
 * Returns: the shadow passes (cascades and cube faces) the last render
 *   ran, recorded ones included
 */
LRG_AVAILABLE_IN_ALL
guint lrg_world3d_renderer_get_shadow_pass_count (LrgWorld3DRenderer *self);

/**
 * lrg_world3d_renderer_get_batched_count:
 * @self: an #LrgWorld3DRenderer
 *
 * Returns: the static items the last render drew through the static
 *   batch instead of one by one
 */
LRG_AVAILABLE_IN_ALL
guint lrg_world3d_renderer_get_batched_count (LrgWorld3DRenderer *self);

/* --- Resources ----------------------------------------------------------- */

/**
 * lrg_world3d_renderer_get_shader_source:
 * @name: "scene.vs", "lit.fs", "unlit.fs", "normals.fs" or "post.fs"
 *
 * The built-in GLSL 330 of the render-mode shaders: the vertex shader of
 * every mode, the fallback lit shader (one two-sided directional light
 * plus ambient, used when the light set's shader is unavailable), the
 * unlit and normals modes and the post pass.  The readable copies live in
 * data/editor/shaders/@name.glsl.
 *
 * Returns: (nullable): the source, or %NULL for an unknown name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_world3d_renderer_get_shader_source (const gchar *name);

/**
 * lrg_world3d_renderer_release:
 * @self: an #LrgWorld3DRenderer
 *
 * Frees every GPU resource (shaders, meshes, cached models and textures,
 * post images, shadow atlases, the static batch) and replaces the light
 * set.  Call it before CloseWindow().  Safe to call twice and without a
 * window (then only the bookkeeping is dropped); the next render
 * recreates what it needs.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_renderer_release (LrgWorld3DRenderer *self);

G_END_DECLS
