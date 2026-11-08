/* lrg-frame-capture.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFrameCapture: one frame of draw calls, for the frame debugger.
 *
 * Arm it, and the next lrg_frame_capture_begin_frame() records every
 * lrg_frame_capture_note() until end_frame().  A scrub index (or
 * lrg_frame_capture_replay()) makes the following frame draw only calls
 * 0..N; the list itself is CPU data and needs no GL.  Replaying into a
 * render target and saving a texture need a window.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_FRAME_CAPTURE_MAX_CALLS:
 *
 * At most this many draw calls are stored.  Further notes are accepted
 * (the draw still happens while recording) but not kept.
 */
#define LRG_FRAME_CAPTURE_MAX_CALLS (65536)

/* ==========================================================================
 * Errors
 * ========================================================================== */

/**
 * LrgFrameCaptureError:
 * @LRG_FRAME_CAPTURE_ERROR_IO: a `.rframe` file could not be read or written
 * @LRG_FRAME_CAPTURE_ERROR_PARSE: a file is not valid YAML
 * @LRG_FRAME_CAPTURE_ERROR_INVALID: YAML that is not a valid `.rframe`
 * @LRG_FRAME_CAPTURE_ERROR_GPU: a replay or texture read-back needs a
 *   window and a current GL context
 *
 * Errors from loading, saving and replaying a frame capture.
 */
typedef enum
{
	LRG_FRAME_CAPTURE_ERROR_IO,
	LRG_FRAME_CAPTURE_ERROR_PARSE,
	LRG_FRAME_CAPTURE_ERROR_INVALID,
	LRG_FRAME_CAPTURE_ERROR_GPU
} LrgFrameCaptureError;

/**
 * lrg_frame_capture_error_get_type:
 *
 * Gets the #GType of #LrgFrameCaptureError.
 *
 * Returns: the #GType
 */
LRG_AVAILABLE_IN_ALL
GType lrg_frame_capture_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FRAME_CAPTURE_ERROR (lrg_frame_capture_error_get_type ())

/**
 * LRG_FRAME_CAPTURE_ERROR:
 *
 * The error domain of #LrgFrameCaptureError.
 */
#define LRG_FRAME_CAPTURE_ERROR (lrg_frame_capture_error_quark ())

/**
 * lrg_frame_capture_error_quark:
 *
 * Gets the error quark of #LrgFrameCaptureError.
 *
 * Returns: the quark
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_frame_capture_error_quark (void);

/* ==========================================================================
 * Draw calls
 * ========================================================================== */

/**
 * LrgFrameCallKind:
 * @LRG_FRAME_CALL_KIND_MESH: a mesh
 * @LRG_FRAME_CALL_KIND_MODEL: a model
 * @LRG_FRAME_CALL_KIND_PRIMITIVE: a built-in primitive
 * @LRG_FRAME_CALL_KIND_BILLBOARD: a camera-facing quad
 * @LRG_FRAME_CALL_KIND_BATCH: a static batch (one call for the whole batch)
 * @LRG_FRAME_CALL_KIND_SKY: the sky
 * @LRG_FRAME_CALL_KIND_CLEAR: a colour clear
 * @LRG_FRAME_CALL_KIND_POST: the post chain
 * @LRG_FRAME_CALL_KIND_DEBUG: debug draw
 * @LRG_FRAME_CALL_KIND_CUSTOM: a custom draw callback
 * @LRG_FRAME_CALL_KIND_SSAO: the SSAO pass
 * @LRG_FRAME_CALL_KIND_VOLUMETRIC: volumetric fog
 * @LRG_FRAME_CALL_KIND_PREPASS: a depth-prepass marker (items use their own kind)
 * @LRG_FRAME_CALL_KIND_DECAL: a decal
 *
 * What a recorded draw call is.
 */
typedef enum
{
	LRG_FRAME_CALL_KIND_MESH,
	LRG_FRAME_CALL_KIND_MODEL,
	LRG_FRAME_CALL_KIND_PRIMITIVE,
	LRG_FRAME_CALL_KIND_BILLBOARD,
	LRG_FRAME_CALL_KIND_BATCH,
	LRG_FRAME_CALL_KIND_SKY,
	LRG_FRAME_CALL_KIND_CLEAR,
	LRG_FRAME_CALL_KIND_POST,
	LRG_FRAME_CALL_KIND_DEBUG,
	LRG_FRAME_CALL_KIND_CUSTOM,
	LRG_FRAME_CALL_KIND_SSAO,
	LRG_FRAME_CALL_KIND_VOLUMETRIC,
	LRG_FRAME_CALL_KIND_PREPASS,
	LRG_FRAME_CALL_KIND_DECAL
} LrgFrameCallKind;

/**
 * lrg_frame_call_kind_get_type:
 *
 * Gets the #GType of #LrgFrameCallKind.
 *
 * Returns: the #GType
 */
LRG_AVAILABLE_IN_ALL
GType lrg_frame_call_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FRAME_CALL_KIND (lrg_frame_call_kind_get_type ())

/**
 * lrg_frame_call_kind_to_nick:
 * @kind: a #LrgFrameCallKind
 *
 * Gets the stable nick stored in `.rframe` files and IPC (`mesh`,
 * `primitive`, `sky`, ...).
 *
 * Returns: (transfer none): the nick
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_frame_call_kind_to_nick (LrgFrameCallKind kind);

/**
 * lrg_frame_call_kind_from_nick:
 * @nick: a kind nick, or %NULL
 * @kind: (out): the kind
 *
 * Parses a nick from lrg_frame_call_kind_to_nick().
 *
 * Returns: %TRUE when @nick is a known kind
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_call_kind_from_nick (const gchar      *nick,
                                        LrgFrameCallKind *kind);

/**
 * LrgFrameCall:
 * @pass: pass nick (`shadow`, `prepass`, `ssao`, `sky`, `opaque`,
 *   `cutout`, `volumetric`, `transparent`, `post`, `debug`, `batch`,
 *   `clear`); interned, never freed
 * @kind: what the call draws
 * @entity: (nullable): entity name, interned
 * @guid: (nullable): entity GUID, interned
 * @mesh: (nullable): mesh or primitive nick, interned
 * @material: (nullable): material name, interned
 * @shader: (nullable): shader nick, interned
 * @shader_id: GL shader program id, or 0
 * @texture_id: GL texture id, or 0
 * @vertices: vertex count (0 when the recorder does not know)
 * @triangles: triangle count
 * @instances: instance count (1 for a single draw)
 * @color: RGBA tint, 0..1
 * @transform: world matrix, column-major, 16 floats
 * @blend: blending enabled
 * @blend_mode: blend mode (0 = alpha)
 * @depth_test: depth test enabled
 * @depth_write: depth writes enabled
 * @cull: back-face culling enabled
 * @wireframe: wireframe fill
 * @framebuffer: GL framebuffer id the call targets, or 0
 *
 * One draw call.  Strings are interned; the capture owns the struct.
 */
typedef struct _LrgFrameCall LrgFrameCall;

struct _LrgFrameCall
{
	const gchar     *pass;
	LrgFrameCallKind kind;
	const gchar     *entity;
	const gchar     *guid;
	const gchar     *mesh;
	const gchar     *material;
	const gchar     *shader;
	guint            shader_id;
	guint            texture_id;
	guint            vertices;
	guint            triangles;
	guint            instances;
	gfloat           color[4];
	gfloat           transform[16];
	gboolean         blend;
	gint             blend_mode;
	gboolean         depth_test;
	gboolean         depth_write;
	gboolean         cull;
	gboolean         wireframe;
	guint            framebuffer;
};

/* ==========================================================================
 * Capture
 * ========================================================================== */

#define LRG_TYPE_FRAME_CAPTURE (lrg_frame_capture_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFrameCapture, lrg_frame_capture, LRG, FRAME_CAPTURE, GObject)

/**
 * LrgFrameCaptureReplayFunc:
 * @self: the capture
 * @up_to: the scrub index (-1 = the whole frame)
 * @user_data: the pointer given to lrg_frame_capture_set_replay_func()
 * @error: return location for a #GError
 *
 * Draws the captured frame into the replay target.  Called by
 * lrg_frame_capture_replay() outside of begin_frame(), so the callback
 * may render.  It must not call lrg_frame_capture_replay().
 *
 * Returns: %TRUE on success
 */
typedef gboolean (*LrgFrameCaptureReplayFunc) (LrgFrameCapture *self,
                                               gint             up_to,
                                               gpointer         user_data,
                                               GError         **error);

/**
 * lrg_frame_capture_new:
 *
 * Creates an empty capture.  Nothing is recorded until
 * lrg_frame_capture_arm().
 *
 * Returns: (transfer full): a new #LrgFrameCapture
 */
LRG_AVAILABLE_IN_ALL
LrgFrameCapture * lrg_frame_capture_new (void);

/**
 * lrg_frame_capture_get_default:
 *
 * Gets a process-wide capture.  The editor does not use this one: it
 * owns the capture it attaches to the viewport renderer.  This is for
 * games and tests that want a single shared list.
 *
 * Returns: (transfer none): the default #LrgFrameCapture
 */
LRG_AVAILABLE_IN_ALL
LrgFrameCapture * lrg_frame_capture_get_default (void);

/**
 * lrg_frame_capture_arm:
 * @self: an #LrgFrameCapture
 *
 * Arms the capture.  The next begin_frame() starts a new recording and
 * clears the previous call list.
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_capture_arm (LrgFrameCapture *self);

/**
 * lrg_frame_capture_is_armed:
 * @self: an #LrgFrameCapture
 *
 * Returns: %TRUE after arm() until the recording frame begins
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_capture_is_armed (LrgFrameCapture *self);

/**
 * lrg_frame_capture_is_recording:
 * @self: an #LrgFrameCapture
 *
 * Returns: %TRUE while the current frame is being recorded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_capture_is_recording (LrgFrameCapture *self);

/**
 * lrg_frame_capture_is_captured:
 * @self: an #LrgFrameCapture
 *
 * Returns: %TRUE when a finished call list is held
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_capture_is_captured (LrgFrameCapture *self);

/**
 * lrg_frame_capture_is_replaying:
 * @self: an #LrgFrameCapture
 *
 * Returns: %TRUE while the current frame is gated by the scrub
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_capture_is_replaying (LrgFrameCapture *self);

/**
 * lrg_frame_capture_is_active:
 * @self: an #LrgFrameCapture
 *
 * Recording or replaying.  Renderers test this before building a
 * #LrgFrameCall; when it is %FALSE, note() is a flag test and returns
 * %TRUE.
 *
 * Returns: %TRUE when note() will look at the call
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_capture_is_active (LrgFrameCapture *self);

/**
 * lrg_frame_capture_begin_frame:
 * @self: an #LrgFrameCapture
 * @width: framebuffer width in pixels
 * @height: framebuffer height in pixels
 *
 * Call at the start of a render.  If the capture is armed, recording
 * starts and the call list is cleared.  If a capture is held and a scrub
 * or replay is pending, the frame is a replay (notes gate draws, the
 * list is kept).  Otherwise this returns after a flag test.
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_capture_begin_frame (LrgFrameCapture *self,
                                    gint             width,
                                    gint             height);

/**
 * lrg_frame_capture_end_frame:
 * @self: an #LrgFrameCapture
 *
 * Call at the end of a render.  A recording becomes the held capture.
 * A replay stops gating.  Idle frames return after a flag test.
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_capture_end_frame (LrgFrameCapture *self);

/**
 * lrg_frame_capture_note:
 * @self: an #LrgFrameCapture
 * @call: (nullable): the draw about to happen; ignored when the capture
 *   is idle, so callers may pass %NULL on that path
 *
 * Records @call while recording (strings are interned here; the caller's
 * buffers are not kept).  While replaying, returns %FALSE when this
 * call's index is past the scrub so the caller skips the draw.  Idle,
 * and while recording, returns %TRUE.  Does not allocate when idle.
 *
 * Returns: %TRUE when the caller should draw
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_capture_note (LrgFrameCapture    *self,
                                 const LrgFrameCall *call);

/**
 * lrg_frame_capture_get_n_calls:
 * @self: an #LrgFrameCapture
 *
 * Returns: the number of stored draw calls
 */
LRG_AVAILABLE_IN_ALL
guint lrg_frame_capture_get_n_calls (LrgFrameCapture *self);

/**
 * lrg_frame_capture_get_call:
 * @self: an #LrgFrameCapture
 * @index: a call index
 *
 * Returns: (transfer none) (nullable): the call, or %NULL when out of range
 */
LRG_AVAILABLE_IN_ALL
const LrgFrameCall * lrg_frame_capture_get_call (LrgFrameCapture *self,
                                                 guint            index);

/**
 * lrg_frame_capture_get_width:
 * @self: an #LrgFrameCapture
 *
 * Returns: the width recorded with the capture, in pixels
 */
LRG_AVAILABLE_IN_ALL
gint lrg_frame_capture_get_width (LrgFrameCapture *self);

/**
 * lrg_frame_capture_get_height:
 * @self: an #LrgFrameCapture
 *
 * Returns: the height recorded with the capture, in pixels
 */
LRG_AVAILABLE_IN_ALL
gint lrg_frame_capture_get_height (LrgFrameCapture *self);

/**
 * lrg_frame_capture_count_pass:
 * @self: an #LrgFrameCapture
 * @pass: a pass nick
 *
 * Returns: how many stored calls use @pass
 */
LRG_AVAILABLE_IN_ALL
guint lrg_frame_capture_count_pass (LrgFrameCapture *self,
                                    const gchar     *pass);

/**
 * lrg_frame_capture_get_passes:
 * @self: an #LrgFrameCapture
 *
 * The pass nicks that occur, in first-seen order.
 *
 * Returns: (transfer full) (array zero-terminated=1): the nicks
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_frame_capture_get_passes (LrgFrameCapture *self);

/**
 * lrg_frame_capture_set_scrub:
 * @self: an #LrgFrameCapture
 * @index: the last call to draw, or -1 for the whole frame
 *
 * The next replayed frame draws calls 0..@index only.  The stored list
 * does not change.  Values below -1 are clamped to -1.
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_capture_set_scrub (LrgFrameCapture *self,
                                  gint             index);

/**
 * lrg_frame_capture_get_scrub:
 * @self: an #LrgFrameCapture
 *
 * Returns: the scrub index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_frame_capture_get_scrub (LrgFrameCapture *self);

/**
 * lrg_frame_capture_set_replay_func:
 * @self: an #LrgFrameCapture
 * @func: (scope notified) (nullable): draws the frame into a target
 * @user_data: (nullable): passed to @func; not owned
 *
 * The renderer installs this so lrg_frame_capture_replay() can re-render.
 * Pass %NULL to detach.  @user_data must outlive the func (the renderer
 * clears it from dispose).
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_capture_set_replay_func (LrgFrameCapture           *self,
                                        LrgFrameCaptureReplayFunc  func,
                                        gpointer                   user_data);

/**
 * lrg_frame_capture_replay:
 * @self: an #LrgFrameCapture
 * @error: return location for a #GError, or %NULL
 *
 * Asks the replay func to draw the held frame up to the scrub into its
 * target.  Fails with %LRG_FRAME_CAPTURE_ERROR_GPU when there is no
 * window, no held capture, or no replay func.  Does not call the func
 * from begin_frame(); the func's render is what consumes the request.
 *
 * Returns: %TRUE when the func drew the frame
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_capture_replay (LrgFrameCapture *self,
                                   GError         **error);

/**
 * lrg_frame_capture_set_replay_image:
 * @self: an #LrgFrameCapture
 * @texture_id: GL texture id of the replay image, or 0 to clear
 * @width: width in pixels
 * @height: height in pixels
 *
 * The renderer publishes the replay render-target colour texture.  The
 * capture does not own it.  Also registered as the named texture `replay`.
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_capture_set_replay_image (LrgFrameCapture *self,
                                         guint            texture_id,
                                         gint             width,
                                         gint             height);

/**
 * lrg_frame_capture_get_replay_texture:
 * @self: an #LrgFrameCapture
 * @width: (out) (optional): texture width
 * @height: (out) (optional): texture height
 *
 * Returns: the replay texture id, or 0 when none has been published
 */
LRG_AVAILABLE_IN_ALL
guint lrg_frame_capture_get_replay_texture (LrgFrameCapture *self,
                                            gint            *width,
                                            gint            *height);

/**
 * lrg_frame_capture_set_named_texture:
 * @self: an #LrgFrameCapture
 * @name: a target nick (`replay`, `depth`, `normal`, `ssao`, ...)
 * @texture_id: GL texture id, or 0 to forget it
 * @width: width in pixels
 * @height: height in pixels
 *
 * Remembers a texture the frame debugger can save.  The capture does
 * not own the GL object.  There is no `shadow` entry: the shadow atlas
 * id is not public.
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_capture_set_named_texture (LrgFrameCapture *self,
                                          const gchar     *name,
                                          guint            texture_id,
                                          gint             width,
                                          gint             height);

/**
 * lrg_frame_capture_get_named_texture:
 * @self: an #LrgFrameCapture
 * @name: a target nick
 * @width: (out) (optional): texture width
 * @height: (out) (optional): texture height
 *
 * Returns: the texture id, or 0 when @name was not published
 */
LRG_AVAILABLE_IN_ALL
guint lrg_frame_capture_get_named_texture (LrgFrameCapture *self,
                                           const gchar     *name,
                                           gint            *width,
                                           gint            *height);

/**
 * lrg_frame_capture_save_texture:
 * @self: an #LrgFrameCapture
 * @texture_id: a GL texture id
 * @width: width in pixels
 * @height: height in pixels
 * @path: the PNG to write
 * @error: return location for a #GError, or %NULL
 *
 * Reads @texture_id back and writes a PNG.  Needs a current GL context
 * (fails with %LRG_FRAME_CAPTURE_ERROR_GPU otherwise).  This is a
 * read-back: call it when saving, not from a render loop.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_capture_save_texture (LrgFrameCapture *self,
                                         guint            texture_id,
                                         gint             width,
                                         gint             height,
                                         const gchar     *path,
                                         GError         **error);

/**
 * lrg_frame_capture_save_to_data:
 * @self: an #LrgFrameCapture
 * @length: (out) (optional): length of the result in bytes
 *
 * Serializes the call list as `.rframe` YAML.  The output is byte-stable.
 * No GL.
 *
 * Returns: (transfer full): the YAML text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_frame_capture_save_to_data (LrgFrameCapture *self,
                                        gsize           *length);

/**
 * lrg_frame_capture_save_to_file:
 * @self: an #LrgFrameCapture
 * @path: the file to write (conventionally `*.rframe`)
 * @error: return location for a #GError, or %NULL
 *
 * Writes the call list.  No GL.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_capture_save_to_file (LrgFrameCapture *self,
                                         const gchar     *path,
                                         GError         **error);

/**
 * lrg_frame_capture_new_from_data:
 * @data: (array length=length) (element-type guint8): `.rframe` YAML
 * @length: length of @data in bytes, or -1 when nul-terminated
 * @error: return location for a #GError, or %NULL
 *
 * Parses a capture.  The result is a held capture (not armed).  No GL.
 *
 * Returns: (transfer full) (nullable): the capture, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgFrameCapture * lrg_frame_capture_new_from_data (const gchar *data,
                                                   gssize       length,
                                                   GError     **error);

/**
 * lrg_frame_capture_new_from_file:
 * @path: a `.rframe` file
 * @error: return location for a #GError, or %NULL
 *
 * Loads a capture saved by lrg_frame_capture_save_to_file().  No GL.
 *
 * Returns: (transfer full) (nullable): the capture, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgFrameCapture * lrg_frame_capture_new_from_file (const gchar *path,
                                                   GError     **error);

/**
 * lrg_frame_capture_load_file:
 * @self: an #LrgFrameCapture
 * @path: a `.rframe` file
 * @error: return location for a #GError, or %NULL
 *
 * Replaces @self's call list with the file.  The replay func and named
 * textures stay.  The result is a held capture.  No GL.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_capture_load_file (LrgFrameCapture *self,
                                      const gchar     *path,
                                      GError         **error);

/**
 * lrg_frame_capture_clear:
 * @self: an #LrgFrameCapture
 *
 * Drops the call list, the scrub and the published texture ids.  The
 * replay func stays.  No GL calls.
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_capture_clear (LrgFrameCapture *self);

G_END_DECLS
