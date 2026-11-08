/* lrg-frame-capture.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One frame of draw calls.
 *
 * Idle (not armed, not recording, not replaying, scrub -1) every hot
 * call returns after a flag test and allocates nothing.  Recording
 * appends interned copies of the call.  Replaying counts notes and
 * returns FALSE past the scrub so the renderer skips that draw.  The
 * replay func is invoked only from lrg_frame_capture_replay(), never
 * from begin_frame(), so a render cannot recurse into itself.
 *
 * `.rframe` is YAML written by hand so a load then a save is the same
 * bytes.  Texture read-back is only lrg_frame_capture_save_texture().
 */

#include "config.h"

#include <stdarg.h>
#include <string.h>
#include <yaml-glib.h>
#include <raylib.h>

#include "lrg-frame-capture.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DEBUG
#include "../lrg-log.h"

/* ==========================================================================
 * Errors and kinds
 * ========================================================================== */

G_DEFINE_QUARK (lrg-frame-capture-error-quark, lrg_frame_capture_error)

GType
lrg_frame_capture_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_FRAME_CAPTURE_ERROR_IO, "LRG_FRAME_CAPTURE_ERROR_IO", "io" },
			{ LRG_FRAME_CAPTURE_ERROR_PARSE, "LRG_FRAME_CAPTURE_ERROR_PARSE", "parse" },
			{ LRG_FRAME_CAPTURE_ERROR_INVALID, "LRG_FRAME_CAPTURE_ERROR_INVALID", "invalid" },
			{ LRG_FRAME_CAPTURE_ERROR_GPU, "LRG_FRAME_CAPTURE_ERROR_GPU", "gpu" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgFrameCaptureError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_frame_call_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_FRAME_CALL_KIND_MESH, "LRG_FRAME_CALL_KIND_MESH", "mesh" },
			{ LRG_FRAME_CALL_KIND_MODEL, "LRG_FRAME_CALL_KIND_MODEL", "model" },
			{ LRG_FRAME_CALL_KIND_PRIMITIVE, "LRG_FRAME_CALL_KIND_PRIMITIVE", "primitive" },
			{ LRG_FRAME_CALL_KIND_BILLBOARD, "LRG_FRAME_CALL_KIND_BILLBOARD", "billboard" },
			{ LRG_FRAME_CALL_KIND_BATCH, "LRG_FRAME_CALL_KIND_BATCH", "batch" },
			{ LRG_FRAME_CALL_KIND_SKY, "LRG_FRAME_CALL_KIND_SKY", "sky" },
			{ LRG_FRAME_CALL_KIND_CLEAR, "LRG_FRAME_CALL_KIND_CLEAR", "clear" },
			{ LRG_FRAME_CALL_KIND_POST, "LRG_FRAME_CALL_KIND_POST", "post" },
			{ LRG_FRAME_CALL_KIND_DEBUG, "LRG_FRAME_CALL_KIND_DEBUG", "debug" },
			{ LRG_FRAME_CALL_KIND_CUSTOM, "LRG_FRAME_CALL_KIND_CUSTOM", "custom" },
			{ LRG_FRAME_CALL_KIND_SSAO, "LRG_FRAME_CALL_KIND_SSAO", "ssao" },
			{ LRG_FRAME_CALL_KIND_VOLUMETRIC, "LRG_FRAME_CALL_KIND_VOLUMETRIC", "volumetric" },
			{ LRG_FRAME_CALL_KIND_PREPASS, "LRG_FRAME_CALL_KIND_PREPASS", "prepass" },
			{ LRG_FRAME_CALL_KIND_DECAL, "LRG_FRAME_CALL_KIND_DECAL", "decal" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgFrameCallKind"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

static const gchar * const kind_nicks[] = {
	"mesh", "model", "primitive", "billboard", "batch", "sky", "clear",
	"post", "debug", "custom", "ssao", "volumetric", "prepass", "decal"
};

const gchar *
lrg_frame_call_kind_to_nick (LrgFrameCallKind kind)
{
	if ((guint) kind >= G_N_ELEMENTS (kind_nicks))
		return "custom";
	return kind_nicks[kind];
}

gboolean
lrg_frame_call_kind_from_nick (const gchar      *nick,
                               LrgFrameCallKind *kind)
{
	guint i;

	if (nick == NULL || kind == NULL)
		return FALSE;
	for (i = 0; i < G_N_ELEMENTS (kind_nicks); i++)
	{
		if (g_strcmp0 (nick, kind_nicks[i]) == 0)
		{
			*kind = (LrgFrameCallKind) i;
			return TRUE;
		}
	}
	return FALSE;
}

/* ==========================================================================
 * Object
 * ========================================================================== */

typedef struct
{
	guint id;
	gint  width;
	gint  height;
} NamedTex;

struct _LrgFrameCapture
{
	GObject parent_instance;

	GArray     *calls;             /* LrgFrameCall, interned strings */
	GHashTable *textures;          /* interned nick -> NamedTex* */

	gint     width;
	gint     height;
	gint     scrub;                /* -1 = full frame */
	gboolean armed;
	gboolean recording;
	gboolean replaying;
	gboolean captured;
	gboolean replay_request;
	guint    replay_i;

	LrgFrameCaptureReplayFunc replay;
	gpointer                  replay_data;

	guint replay_tex;
	gint  replay_w;
	gint  replay_h;
};

G_DEFINE_FINAL_TYPE (LrgFrameCapture, lrg_frame_capture, G_TYPE_OBJECT)

static LrgFrameCapture *default_capture;

static const gchar *
intern_opt (const gchar *s)
{
	if (s == NULL || s[0] == '\0')
		return NULL;
	return g_intern_string (s);
}

static void
named_tex_free (gpointer data)
{
	g_free (data);
}

static void
lrg_frame_capture_finalize (GObject *object)
{
	LrgFrameCapture *self = LRG_FRAME_CAPTURE (object);

	g_clear_pointer (&self->calls, g_array_unref);
	g_clear_pointer (&self->textures, g_hash_table_unref);
	G_OBJECT_CLASS (lrg_frame_capture_parent_class)->finalize (object);
}

static void
lrg_frame_capture_class_init (LrgFrameCaptureClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_frame_capture_finalize;
}

static void
lrg_frame_capture_init (LrgFrameCapture *self)
{
	self->calls = g_array_new (FALSE, TRUE, sizeof (LrgFrameCall));
	self->textures = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, named_tex_free);
	self->scrub = -1;
}

LrgFrameCapture *
lrg_frame_capture_new (void)
{
	return g_object_new (LRG_TYPE_FRAME_CAPTURE, NULL);
}

LrgFrameCapture *
lrg_frame_capture_get_default (void)
{
	if (default_capture == NULL)
		default_capture = lrg_frame_capture_new ();
	return default_capture;
}

void
lrg_frame_capture_arm (LrgFrameCapture *self)
{
	g_return_if_fail (LRG_IS_FRAME_CAPTURE (self));
	self->armed = TRUE;
}

gboolean
lrg_frame_capture_is_armed (LrgFrameCapture *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), FALSE);
	return self->armed;
}

gboolean
lrg_frame_capture_is_recording (LrgFrameCapture *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), FALSE);
	return self->recording;
}

gboolean
lrg_frame_capture_is_captured (LrgFrameCapture *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), FALSE);
	return self->captured;
}

gboolean
lrg_frame_capture_is_replaying (LrgFrameCapture *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), FALSE);
	return self->replaying;
}

gboolean
lrg_frame_capture_is_active (LrgFrameCapture *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), FALSE);
	return self->recording || self->replaying;
}

void
lrg_frame_capture_begin_frame (LrgFrameCapture *self,
                               gint             width,
                               gint             height)
{
	g_return_if_fail (LRG_IS_FRAME_CAPTURE (self));

	/* The common frame: nothing to record and nothing to gate. */
	if (G_LIKELY (!self->armed &&
	              !(self->captured && (self->replay_request || self->scrub >= 0))))
	{
		self->replaying = FALSE;
		return;
	}

	if (self->armed)
	{
		self->armed = FALSE;
		self->recording = TRUE;
		self->replaying = FALSE;
		self->replay_request = FALSE;
		self->captured = FALSE;
		self->width = width;
		self->height = height;
		self->replay_i = 0;
		g_array_set_size (self->calls, 0);
		return;
	}

	/* Held capture, scrub or an explicit replay: gate draws, keep the list. */
	self->replaying = TRUE;
	self->replay_request = FALSE;
	self->replay_i = 0;
}

void
lrg_frame_capture_end_frame (LrgFrameCapture *self)
{
	g_return_if_fail (LRG_IS_FRAME_CAPTURE (self));

	if (G_LIKELY (!self->recording && !self->replaying))
		return;
	if (self->recording)
	{
		self->recording = FALSE;
		self->captured = TRUE;
	}
	self->replaying = FALSE;
}

gboolean
lrg_frame_capture_note (LrgFrameCapture    *self,
                        const LrgFrameCall *call)
{
	LrgFrameCall stored;
	guint        index;

	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), TRUE);

	if (G_LIKELY (!self->recording && !self->replaying))
		return TRUE;

	if (self->replaying)
	{
		index = self->replay_i++;
		return self->scrub < 0 || (gint) index <= self->scrub;
	}

	/* Recording.  Past the cap the draw still happens; the list stops growing. */
	if (self->calls->len >= LRG_FRAME_CAPTURE_MAX_CALLS)
		return TRUE;
	if (call == NULL)
		return TRUE;

	stored = *call;
	stored.pass = intern_opt (call->pass);
	if (stored.pass == NULL)
		stored.pass = g_intern_static_string ("opaque");
	stored.entity = intern_opt (call->entity);
	stored.guid = intern_opt (call->guid);
	stored.mesh = intern_opt (call->mesh);
	stored.material = intern_opt (call->material);
	stored.shader = intern_opt (call->shader);
	if (stored.instances == 0)
		stored.instances = 1;
	g_array_append_val (self->calls, stored);
	return TRUE;
}

guint
lrg_frame_capture_get_n_calls (LrgFrameCapture *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), 0);
	return self->calls->len;
}

const LrgFrameCall *
lrg_frame_capture_get_call (LrgFrameCapture *self,
                            guint            index)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), NULL);
	if (index >= self->calls->len)
		return NULL;
	return &g_array_index (self->calls, LrgFrameCall, index);
}

gint
lrg_frame_capture_get_width (LrgFrameCapture *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), 0);
	return self->width;
}

gint
lrg_frame_capture_get_height (LrgFrameCapture *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), 0);
	return self->height;
}

guint
lrg_frame_capture_count_pass (LrgFrameCapture *self,
                              const gchar     *pass)
{
	guint i, n = 0;

	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), 0);
	if (pass == NULL)
		return 0;
	for (i = 0; i < self->calls->len; i++)
		if (g_strcmp0 (g_array_index (self->calls, LrgFrameCall, i).pass, pass) == 0)
			n++;
	return n;
}

gchar **
lrg_frame_capture_get_passes (LrgFrameCapture *self)
{
	g_autoptr(GPtrArray) names = NULL;
	guint                i;

	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), NULL);

	names = g_ptr_array_new ();
	for (i = 0; i < self->calls->len; i++)
	{
		const gchar *pass = g_array_index (self->calls, LrgFrameCall, i).pass;
		guint        k;
		gboolean     seen = FALSE;

		if (pass == NULL)
			continue;
		for (k = 0; k < names->len; k++)
		{
			if (g_strcmp0 (g_ptr_array_index (names, k), pass) == 0)
			{
				seen = TRUE;
				break;
			}
		}
		if (!seen)
			g_ptr_array_add (names, (gpointer) pass);
	}
	g_ptr_array_add (names, NULL);
	return g_strdupv ((gchar **) names->pdata);
}

void
lrg_frame_capture_set_scrub (LrgFrameCapture *self,
                             gint             index)
{
	g_return_if_fail (LRG_IS_FRAME_CAPTURE (self));
	self->scrub = (index < -1) ? -1 : index;
}

gint
lrg_frame_capture_get_scrub (LrgFrameCapture *self)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), -1);
	return self->scrub;
}

void
lrg_frame_capture_set_replay_func (LrgFrameCapture           *self,
                                   LrgFrameCaptureReplayFunc  func,
                                   gpointer                   user_data)
{
	g_return_if_fail (LRG_IS_FRAME_CAPTURE (self));
	self->replay = func;
	self->replay_data = user_data;
}

gboolean
lrg_frame_capture_replay (LrgFrameCapture *self,
                          GError         **error)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!self->captured)
	{
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_GPU,
		             "No captured frame to replay");
		return FALSE;
	}
	if (self->replay == NULL)
	{
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_GPU,
		             "No replay renderer is attached");
		return FALSE;
	}
	if (!IsWindowReady ())
	{
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_GPU,
		             "Replaying a frame needs a window");
		return FALSE;
	}
	self->replay_request = TRUE;
	return self->replay (self, self->scrub, self->replay_data, error);
}

static void
remember_tex (LrgFrameCapture *self,
              const gchar     *name,
              guint            texture_id,
              gint             width,
              gint             height)
{
	NamedTex *tex;

	name = g_intern_string (name);
	if (texture_id == 0)
	{
		g_hash_table_remove (self->textures, name);
		return;
	}
	/* Replace in place: a renderer republishes depth/normal/ssao every
	 * frame the capture is live, and a fresh allocation each time would
	 * show up on the hot path. */
	tex = g_hash_table_lookup (self->textures, name);
	if (tex == NULL)
	{
		tex = g_new (NamedTex, 1);
		g_hash_table_insert (self->textures, (gpointer) name, tex);
	}
	tex->id = texture_id;
	tex->width = width;
	tex->height = height;
}

void
lrg_frame_capture_set_replay_image (LrgFrameCapture *self,
                                    guint            texture_id,
                                    gint             width,
                                    gint             height)
{
	g_return_if_fail (LRG_IS_FRAME_CAPTURE (self));
	self->replay_tex = texture_id;
	self->replay_w = width;
	self->replay_h = height;
	remember_tex (self, "replay", texture_id, width, height);
}

guint
lrg_frame_capture_get_replay_texture (LrgFrameCapture *self,
                                      gint            *width,
                                      gint            *height)
{
	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), 0);
	if (width != NULL)
		*width = self->replay_w;
	if (height != NULL)
		*height = self->replay_h;
	return self->replay_tex;
}

void
lrg_frame_capture_set_named_texture (LrgFrameCapture *self,
                                     const gchar     *name,
                                     guint            texture_id,
                                     gint             width,
                                     gint             height)
{
	g_return_if_fail (LRG_IS_FRAME_CAPTURE (self));
	g_return_if_fail (name != NULL);
	if (g_strcmp0 (name, "replay") == 0)
	{
		lrg_frame_capture_set_replay_image (self, texture_id, width, height);
		return;
	}
	remember_tex (self, name, texture_id, width, height);
}

guint
lrg_frame_capture_get_named_texture (LrgFrameCapture *self,
                                     const gchar     *name,
                                     gint            *width,
                                     gint            *height)
{
	NamedTex *tex;

	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), 0);
	if (name == NULL)
		return 0;
	if (g_strcmp0 (name, "replay") == 0)
		return lrg_frame_capture_get_replay_texture (self, width, height);
	tex = g_hash_table_lookup (self->textures, name);
	if (tex == NULL)
	{
		if (width != NULL)
			*width = 0;
		if (height != NULL)
			*height = 0;
		return 0;
	}
	if (width != NULL)
		*width = tex->width;
	if (height != NULL)
		*height = tex->height;
	return tex->id;
}

gboolean
lrg_frame_capture_save_texture (LrgFrameCapture *self,
                                guint            texture_id,
                                gint             width,
                                gint             height,
                                const gchar     *path,
                                GError         **error)
{
	Texture2D tex;
	Image     image;
	gboolean  ok;

	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!IsWindowReady ())
	{
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_GPU,
		             "Saving a texture needs a window");
		return FALSE;
	}
	if (texture_id == 0 || width <= 0 || height <= 0)
	{
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_GPU,
		             "Texture %u has no size (%dx%d)", texture_id, width, height);
		return FALSE;
	}

	memset (&tex, 0, sizeof tex);
	tex.id = texture_id;
	tex.width = width;
	tex.height = height;
	tex.mipmaps = 1;
	tex.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	image = LoadImageFromTexture (tex);
	if (image.data == NULL)
	{
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_GPU,
		             "Could not read texture %u", texture_id);
		return FALSE;
	}
	ok = ExportImage (image, path) ? TRUE : FALSE;
	UnloadImage (image);
	if (!ok)
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_IO,
		             "Could not write %s", path);
	return ok;
}

void
lrg_frame_capture_clear (LrgFrameCapture *self)
{
	g_return_if_fail (LRG_IS_FRAME_CAPTURE (self));
	g_array_set_size (self->calls, 0);
	g_hash_table_remove_all (self->textures);
	self->armed = FALSE;
	self->recording = FALSE;
	self->replaying = FALSE;
	self->captured = FALSE;
	self->replay_request = FALSE;
	self->scrub = -1;
	self->replay_i = 0;
	self->replay_tex = 0;
	self->replay_w = 0;
	self->replay_h = 0;
	self->width = 0;
	self->height = 0;
}

/* ==========================================================================
 * .rframe YAML
 * ========================================================================== */

static gboolean
invalid (GError      **error,
         const gchar  *fmt,
         ...) G_GNUC_PRINTF (2, 3);

static gboolean
invalid (GError      **error,
         const gchar  *fmt,
         ...)
{
	va_list           ap;
	g_autofree gchar *msg = NULL;

	va_start (ap, fmt);
	msg = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_INVALID, "%s", msg);
	return FALSE;
}

static void
put_quoted (GString     *out,
            const gchar *s)
{
	const gchar *p;

	g_string_append_c (out, '"');
	if (s == NULL)
	{
		g_string_append_c (out, '"');
		return;
	}
	for (p = s; *p != '\0'; p++)
	{
		switch (*p)
		{
		case '\\':
		case '"':
			g_string_append_c (out, '\\');
			g_string_append_c (out, *p);
			break;
		case '\n':
			g_string_append (out, "\\n");
			break;
		default:
			if ((guchar) *p < 0x20)
				g_string_append_printf (out, "\\x%02x", (guint) (guchar) *p);
			else
				g_string_append_c (out, *p);
			break;
		}
	}
	g_string_append_c (out, '"');
}

static void
put_double (GString *out,
            gdouble  v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	g_ascii_formatd (buf, sizeof buf, "%.9g", v);
	if ((gfloat) g_ascii_strtod (buf, NULL) != (gfloat) v)
		g_ascii_formatd (buf, sizeof buf, "%.17g", v);
	g_string_append (out, buf);
}

static void
put_bool (GString  *out,
          gboolean  v)
{
	g_string_append (out, v ? "true" : "false");
}

gchar *
lrg_frame_capture_save_to_data (LrgFrameCapture *self,
                                gsize           *length)
{
	GString *out;
	guint    i, k;

	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), NULL);

	out = g_string_new ("# libregnum frame capture: one frame of draw calls\n");
	g_string_append (out, "format: rframe\nversion: 1\n");
	g_string_append_printf (out, "width: %d\nheight: %d\nscrub: %d\n",
	                        self->width, self->height, self->scrub);
	if (self->calls->len == 0)
		g_string_append (out, "calls: []\n");
	else
		g_string_append (out, "calls:\n");
	for (i = 0; i < self->calls->len; i++)
	{
		const LrgFrameCall *c = &g_array_index (self->calls, LrgFrameCall, i);

		g_string_append (out, "  - pass: ");
		put_quoted (out, c->pass);
		g_string_append (out, "\n    kind: ");
		put_quoted (out, lrg_frame_call_kind_to_nick (c->kind));
		g_string_append (out, "\n    entity: ");
		put_quoted (out, c->entity);
		g_string_append (out, "\n    guid: ");
		put_quoted (out, c->guid);
		g_string_append (out, "\n    mesh: ");
		put_quoted (out, c->mesh);
		g_string_append (out, "\n    material: ");
		put_quoted (out, c->material);
		g_string_append (out, "\n    shader: ");
		put_quoted (out, c->shader);
		g_string_append_printf (out, "\n    shader_id: %u\n    texture_id: %u\n",
		                        c->shader_id, c->texture_id);
		g_string_append_printf (out, "    vertices: %u\n    triangles: %u\n    instances: %u\n",
		                        c->vertices, c->triangles, c->instances);
		g_string_append (out, "    color: [");
		for (k = 0; k < 4; k++)
		{
			if (k > 0)
				g_string_append (out, ", ");
			put_double (out, c->color[k]);
		}
		g_string_append (out, "]\n    transform: [");
		for (k = 0; k < 16; k++)
		{
			if (k > 0)
				g_string_append (out, ", ");
			put_double (out, c->transform[k]);
		}
		g_string_append (out, "]\n    state:\n");
		g_string_append (out, "      blend: ");
		put_bool (out, c->blend);
		g_string_append_printf (out, "\n      blend_mode: %d\n", c->blend_mode);
		g_string_append (out, "      depth_test: ");
		put_bool (out, c->depth_test);
		g_string_append (out, "\n      depth_write: ");
		put_bool (out, c->depth_write);
		g_string_append (out, "\n      cull: ");
		put_bool (out, c->cull);
		g_string_append (out, "\n      wireframe: ");
		put_bool (out, c->wireframe);
		g_string_append_printf (out, "\n      framebuffer: %u\n", c->framebuffer);
	}
	if (length != NULL)
		*length = out->len;
	return g_string_free (out, FALSE);
}

gboolean
lrg_frame_capture_save_to_file (LrgFrameCapture *self,
                                const gchar     *path,
                                GError         **error)
{
	g_autofree gchar *data = NULL;
	gsize             length = 0;
	GError           *local = NULL;

	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	data = lrg_frame_capture_save_to_data (self, &length);
	if (!g_file_set_contents (path, data, (gssize) length, &local))
	{
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_IO, "%s", local->message);
		g_error_free (local);
		return FALSE;
	}
	return TRUE;
}

static const gchar *
node_scalar (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

static gboolean
node_int (YamlNode *node,
          gint64   *out)
{
	const gchar *s = node_scalar (node);

	return s != NULL && g_ascii_string_to_signed (s, 10, G_MININT64, G_MAXINT64, out, NULL);
}

static gboolean
node_double (YamlNode *node,
             gdouble  *out)
{
	const gchar *s = node_scalar (node);
	gchar       *end = NULL;

	if (s == NULL || *s == '\0')
		return FALSE;
	*out = g_ascii_strtod (s, &end);
	return end != NULL && *end == '\0';
}

static gboolean
node_bool (YamlNode *node,
           gboolean *out)
{
	const gchar *s = node_scalar (node);

	if (s == NULL)
		return FALSE;
	if (g_ascii_strcasecmp (s, "true") == 0 || g_strcmp0 (s, "1") == 0)
	{
		*out = TRUE;
		return TRUE;
	}
	if (g_ascii_strcasecmp (s, "false") == 0 || g_strcmp0 (s, "0") == 0)
	{
		*out = FALSE;
		return TRUE;
	}
	return FALSE;
}

static gboolean
node_floats (YamlNode *node,
             gfloat   *out,
             guint     n)
{
	YamlSequence *seq;
	guint         i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return FALSE;
	seq = yaml_node_get_sequence (node);
	if (yaml_sequence_get_length (seq) != n)
		return FALSE;
	for (i = 0; i < n; i++)
	{
		gdouble d;

		if (!node_double (yaml_sequence_get_element (seq, i), &d))
			return FALSE;
		out[i] = (gfloat) d;
	}
	return TRUE;
}

static gboolean
parse_call (YamlNode    *node,
            guint        index,
            LrgFrameCall *out,
            GError     **error)
{
	YamlMapping      *map;
	YamlMapping      *state;
	YamlNode         *n;
	g_autofree gchar *path = NULL;
	const gchar      *s;
	gint64            v;
	LrgFrameCallKind  kind;

	path = g_strdup_printf ("calls[%u]", index);
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return invalid (error, "%s: expected a mapping", path);
	map = yaml_node_get_mapping (node);
	memset (out, 0, sizeof *out);
	out->transform[0] = 1.0f;
	out->transform[5] = 1.0f;
	out->transform[10] = 1.0f;
	out->transform[15] = 1.0f;
	out->color[0] = out->color[1] = out->color[2] = out->color[3] = 1.0f;
	out->instances = 1;
	out->depth_test = TRUE;
	out->depth_write = TRUE;
	out->cull = TRUE;

	s = node_scalar (yaml_mapping_get_member (map, "pass"));
	if (s == NULL || s[0] == '\0')
		return invalid (error, "%s.pass: expected a string", path);
	out->pass = g_intern_string (s);
	s = node_scalar (yaml_mapping_get_member (map, "kind"));
	if (!lrg_frame_call_kind_from_nick (s, &kind))
		return invalid (error, "%s.kind: unknown kind", path);
	out->kind = kind;
	out->entity = intern_opt (node_scalar (yaml_mapping_get_member (map, "entity")));
	out->guid = intern_opt (node_scalar (yaml_mapping_get_member (map, "guid")));
	out->mesh = intern_opt (node_scalar (yaml_mapping_get_member (map, "mesh")));
	out->material = intern_opt (node_scalar (yaml_mapping_get_member (map, "material")));
	out->shader = intern_opt (node_scalar (yaml_mapping_get_member (map, "shader")));

	if (!node_int (yaml_mapping_get_member (map, "shader_id"), &v) || v < 0)
		return invalid (error, "%s.shader_id: expected a non-negative integer", path);
	out->shader_id = (guint) v;
	if (!node_int (yaml_mapping_get_member (map, "texture_id"), &v) || v < 0)
		return invalid (error, "%s.texture_id: expected a non-negative integer", path);
	out->texture_id = (guint) v;
	if (!node_int (yaml_mapping_get_member (map, "vertices"), &v) || v < 0)
		return invalid (error, "%s.vertices: expected a non-negative integer", path);
	out->vertices = (guint) v;
	if (!node_int (yaml_mapping_get_member (map, "triangles"), &v) || v < 0)
		return invalid (error, "%s.triangles: expected a non-negative integer", path);
	out->triangles = (guint) v;
	if (!node_int (yaml_mapping_get_member (map, "instances"), &v) || v < 0)
		return invalid (error, "%s.instances: expected a non-negative integer", path);
	out->instances = (guint) v;

	if (!node_floats (yaml_mapping_get_member (map, "color"), out->color, 4))
		return invalid (error, "%s.color: expected 4 numbers", path);
	if (!node_floats (yaml_mapping_get_member (map, "transform"), out->transform, 16))
		return invalid (error, "%s.transform: expected 16 numbers", path);

	n = yaml_mapping_get_member (map, "state");
	if (n == NULL || yaml_node_get_node_type (n) != YAML_NODE_MAPPING)
		return invalid (error, "%s.state: expected a mapping", path);
	state = yaml_node_get_mapping (n);
	if (!node_bool (yaml_mapping_get_member (state, "blend"), &out->blend))
		return invalid (error, "%s.state.blend: expected a bool", path);
	if (!node_int (yaml_mapping_get_member (state, "blend_mode"), &v))
		return invalid (error, "%s.state.blend_mode: expected an integer", path);
	out->blend_mode = (gint) v;
	if (!node_bool (yaml_mapping_get_member (state, "depth_test"), &out->depth_test) ||
	    !node_bool (yaml_mapping_get_member (state, "depth_write"), &out->depth_write) ||
	    !node_bool (yaml_mapping_get_member (state, "cull"), &out->cull) ||
	    !node_bool (yaml_mapping_get_member (state, "wireframe"), &out->wireframe))
		return invalid (error, "%s.state: expected bools for depth_test, depth_write, cull, wireframe", path);
	if (!node_int (yaml_mapping_get_member (state, "framebuffer"), &v) || v < 0)
		return invalid (error, "%s.state.framebuffer: expected a non-negative integer", path);
	out->framebuffer = (guint) v;
	return TRUE;
}

static void
capture_adopt (LrgFrameCapture *self,
               LrgFrameCapture *src)
{
	GArray *swap;

	swap = self->calls;
	self->calls = src->calls;
	src->calls = swap;
	g_array_set_size (src->calls, 0);
	self->width = src->width;
	self->height = src->height;
	self->scrub = src->scrub;
	self->armed = FALSE;
	self->recording = FALSE;
	self->replaying = FALSE;
	self->replay_request = FALSE;
	self->replay_i = 0;
	self->captured = TRUE;
}

static LrgFrameCapture *
capture_from_root (YamlNode *root,
                   GError  **error)
{
	g_autoptr(LrgFrameCapture) self = NULL;
	YamlMapping               *map;
	YamlNode                  *n;
	const gchar               *format;
	gint64                     version;
	gint64                     v;
	guint                      i;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		invalid (error, "the document must be a mapping");
		return NULL;
	}
	map = yaml_node_get_mapping (root);
	format = node_scalar (yaml_mapping_get_member (map, "format"));
	if (g_strcmp0 (format, "rframe") != 0)
	{
		invalid (error, "format: expected \"rframe\"");
		return NULL;
	}
	if (!node_int (yaml_mapping_get_member (map, "version"), &version) || version != 1)
	{
		invalid (error, "version: expected 1");
		return NULL;
	}

	self = lrg_frame_capture_new ();
	if (node_int (yaml_mapping_get_member (map, "width"), &v))
		self->width = (gint) v;
	if (node_int (yaml_mapping_get_member (map, "height"), &v))
		self->height = (gint) v;
	if (node_int (yaml_mapping_get_member (map, "scrub"), &v))
		self->scrub = (gint) v;

	n = yaml_mapping_get_member (map, "calls");
	if (n != NULL && yaml_node_get_node_type (n) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (n);

		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			LrgFrameCall call;

			if (!parse_call (yaml_sequence_get_element (seq, i), i, &call, error))
				return NULL;
			g_array_append_val (self->calls, call);
		}
	}
	else if (n != NULL && yaml_node_get_node_type (n) != YAML_NODE_NULL)
	{
		invalid (error, "calls: expected a sequence");
		return NULL;
	}
	self->captured = TRUE;
	return g_steal_pointer (&self);
}

LrgFrameCapture *
lrg_frame_capture_new_from_data (const gchar *data,
                                 gssize       length,
                                 GError     **error)
{
	g_autoptr(YamlParser) parser = NULL;
	GError               *local = NULL;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_PARSE,
		             "Frame capture YAML: %s", local->message);
		g_error_free (local);
		return NULL;
	}
	return capture_from_root (yaml_parser_get_root (parser), error);
}

LrgFrameCapture *
lrg_frame_capture_new_from_file (const gchar *path,
                                 GError     **error)
{
	g_autofree gchar *data = NULL;
	gsize             length = 0;
	GError           *local = NULL;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &data, &length, &local))
	{
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_IO, "%s", local->message);
		g_error_free (local);
		return NULL;
	}
	return lrg_frame_capture_new_from_data (data, (gssize) length, error);
}

gboolean
lrg_frame_capture_load_file (LrgFrameCapture *self,
                             const gchar     *path,
                             GError         **error)
{
	g_autoptr(LrgFrameCapture) loaded = NULL;

	g_return_val_if_fail (LRG_IS_FRAME_CAPTURE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	loaded = lrg_frame_capture_new_from_file (path, error);
	if (loaded == NULL)
		return FALSE;
	capture_adopt (self, loaded);
	return TRUE;
}
