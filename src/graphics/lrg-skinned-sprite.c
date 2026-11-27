/* lrg-skinned-sprite.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkinnedSprite: a deformable 2D sprite mesh (bind positions, region
 * UVs, up to four bone influences per vertex) skinned on the CPU and
 * drawn through rlgl.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <gio/gio.h>
#include <raylib.h>
#include <rlgl.h>

#include "graphics/lrg-skinned-sprite.h"
#include "graphics/lrg-skinned-sprite-private.h"
#include "graphics/lrg-image-canvas.h"
#include "graphics/lrg-image-raster.h"

/* One vertex: bind position, region UV and its influences, heaviest
 * first.  Unused influence slots hold bone -1 and weight 0. */
typedef struct
{
	gfloat x;
	gfloat y;
	gfloat u;
	gfloat v;
	gint   bones[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
	gfloat weights[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
	guint  n_weights;
} SpriteVertex;

/* Indices are drawn in chunks of this many triangles so a big mesh never
 * overflows rlgl's default batch inside one rlBegin()/rlEnd(). */
#define TRIANGLES_PER_CHUNK (1024)

struct _LrgSkinnedSprite
{
	GObject      parent_instance;

	GArray      *vertices;   /* SpriteVertex */
	GArray      *indices;    /* guint16, three per triangle */
	GArray      *deformed;   /* gfloat, two per vertex */

	GrlTexture  *texture;
	gchar       *image;
	gfloat       region[4];  /* x, y, width, height in pixels */

	/* CPU drawing (lrg_skinned_sprite_draw_to_canvas()): the RGBA8
	 * source image, the region cut out of it (a cropped copy, or a
	 * second reference to source_image when no region is set; never
	 * modified once made, so threads may share it) and whether loading
	 * #LrgSkinnedSprite:image into it was already tried */
	GrlImage    *source_image;
	GrlImage    *source_region;
	gboolean     source_tried;
};

G_DEFINE_FINAL_TYPE (LrgSkinnedSprite, lrg_skinned_sprite, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_TEXTURE,
	PROP_IMAGE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void update_source_region (LrgSkinnedSprite *self);

/* ------------------------------------------------------------------ */
/* GObject                                                             */
/* ------------------------------------------------------------------ */

static void
lrg_skinned_sprite_dispose (GObject *object)
{
	LrgSkinnedSprite *self = LRG_SKINNED_SPRITE (object);

	g_clear_object (&self->texture);
	g_clear_object (&self->source_image);
	g_clear_object (&self->source_region);

	G_OBJECT_CLASS (lrg_skinned_sprite_parent_class)->dispose (object);
}

static void
lrg_skinned_sprite_finalize (GObject *object)
{
	LrgSkinnedSprite *self = LRG_SKINNED_SPRITE (object);

	g_clear_pointer (&self->vertices, g_array_unref);
	g_clear_pointer (&self->indices, g_array_unref);
	g_clear_pointer (&self->deformed, g_array_unref);
	g_clear_pointer (&self->image, g_free);

	G_OBJECT_CLASS (lrg_skinned_sprite_parent_class)->finalize (object);
}

static void
lrg_skinned_sprite_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgSkinnedSprite *self = LRG_SKINNED_SPRITE (object);

	switch (prop_id)
	{
	case PROP_TEXTURE:
		g_value_set_object (value, self->texture);
		break;
	case PROP_IMAGE:
		g_value_set_string (value, self->image);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_skinned_sprite_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgSkinnedSprite *self = LRG_SKINNED_SPRITE (object);

	switch (prop_id)
	{
	case PROP_TEXTURE:
		lrg_skinned_sprite_set_texture (self, g_value_get_object (value));
		break;
	case PROP_IMAGE:
		lrg_skinned_sprite_set_image (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_skinned_sprite_class_init (LrgSkinnedSpriteClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_skinned_sprite_dispose;
	object_class->finalize = lrg_skinned_sprite_finalize;
	object_class->get_property = lrg_skinned_sprite_get_property;
	object_class->set_property = lrg_skinned_sprite_set_property;

	/**
	 * LrgSkinnedSprite:texture:
	 *
	 * The texture the region is cut from, or %NULL to draw untextured.
	 */
	properties[PROP_TEXTURE] =
		g_param_spec_object ("texture", "Texture", "Texture the region is cut from",
		                     GRL_TYPE_TEXTURE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkinnedSprite:image:
	 *
	 * Image path stored in `.rskel2d` files and loaded by
	 * lrg_skinned_sprite_load_texture().
	 */
	properties[PROP_IMAGE] =
		g_param_spec_string ("image", "Image", "Image path the texture is loaded from",
		                     NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_skinned_sprite_init (LrgSkinnedSprite *self)
{
	self->vertices = g_array_new (FALSE, TRUE, sizeof (SpriteVertex));
	self->indices = g_array_new (FALSE, FALSE, sizeof (guint16));
	self->deformed = g_array_new (FALSE, TRUE, sizeof (gfloat));
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

LrgSkinnedSprite *
lrg_skinned_sprite_new (void)
{
	return g_object_new (LRG_TYPE_SKINNED_SPRITE, NULL);
}

LrgSkinnedSprite *
lrg_skinned_sprite_new_quad (gfloat x,
                             gfloat y,
                             gfloat width,
                             gfloat height,
                             gint   bone)
{
	g_autoptr(LrgSkinnedSprite) self = lrg_skinned_sprite_new_grid (x, y, width, height, 1, 1);

	/* a rigid region: every corner fully on @bone */
	if (bone >= 0)
	{
		guint  i;
		gfloat one = 1.0f;

		for (i = 0; i < 4; i++)
			lrg_skinned_sprite_set_weights (self, i, 1, &bone, &one);
	}
	return g_steal_pointer (&self);
}

LrgSkinnedSprite *
lrg_skinned_sprite_new_grid (gfloat x,
                             gfloat y,
                             gfloat width,
                             gfloat height,
                             guint  columns,
                             guint  rows)
{
	g_autoptr(LrgSkinnedSprite) self = lrg_skinned_sprite_new ();
	guint r;
	guint c;

	columns = MAX (columns, 1u);
	rows = MAX (rows, 1u);
	g_return_val_if_fail ((columns + 1) * (rows + 1) <= LRG_SKINNED_SPRITE_MAX_VERTICES, NULL);

	/* vertices row by row, UVs spanning the region */
	for (r = 0; r <= rows; r++)
	{
		for (c = 0; c <= columns; c++)
		{
			gfloat u = (gfloat)c / (gfloat)columns;
			gfloat v = (gfloat)r / (gfloat)rows;

			lrg_skinned_sprite_add_vertex (self, x + u * width, y + v * height, u, v);
		}
	}

	/* two triangles per cell */
	for (r = 0; r < rows; r++)
	{
		for (c = 0; c < columns; c++)
		{
			guint tl = r * (columns + 1) + c;
			guint tr = tl + 1;
			guint bl = tl + columns + 1;
			guint br = bl + 1;

			lrg_skinned_sprite_add_triangle (self, tl, tr, bl);
			lrg_skinned_sprite_add_triangle (self, tr, br, bl);
		}
	}
	return g_steal_pointer (&self);
}

LrgSkinnedSprite *
lrg_skinned_sprite_copy (LrgSkinnedSprite *self)
{
	LrgSkinnedSprite *copy;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), NULL);

	copy = lrg_skinned_sprite_new ();
	g_array_append_vals (copy->vertices, self->vertices->data, self->vertices->len);
	g_array_append_vals (copy->indices, self->indices->data, self->indices->len);
	copy->image = g_strdup (self->image);
	memcpy (copy->region, self->region, sizeof copy->region);
	g_set_object (&copy->texture, self->texture);
	/* the CPU source and its region crop are immutable: share them */
	g_set_object (&copy->source_image, self->source_image);
	g_set_object (&copy->source_region, self->source_region);
	copy->source_tried = self->source_tried;
	return copy;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

/* The deformed buffer always has one (x, y) per vertex; a vertex added
 * after the last deform starts at its bind position. */
static void
sync_deformed (LrgSkinnedSprite *self)
{
	guint old = self->deformed->len / 2;
	guint i;

	g_array_set_size (self->deformed, self->vertices->len * 2);
	for (i = old; i < self->vertices->len; i++)
	{
		const SpriteVertex *sv = &g_array_index (self->vertices, SpriteVertex, i);

		g_array_index (self->deformed, gfloat, i * 2) = sv->x;
		g_array_index (self->deformed, gfloat, i * 2 + 1) = sv->y;
	}
}

gint
lrg_skinned_sprite_add_vertex (LrgSkinnedSprite *self,
                               gfloat            x,
                               gfloat            y,
                               gfloat            u,
                               gfloat            v)
{
	SpriteVertex sv;
	guint        i;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), -1);

	if (self->vertices->len >= LRG_SKINNED_SPRITE_MAX_VERTICES)
		return -1;

	sv.x = x;
	sv.y = y;
	sv.u = u;
	sv.v = v;
	for (i = 0; i < LRG_SKINNED_SPRITE_MAX_WEIGHTS; i++)
	{
		sv.bones[i] = -1;
		sv.weights[i] = 0.0f;
	}
	sv.n_weights = 0;
	g_array_append_val (self->vertices, sv);
	sync_deformed (self);
	return (gint)self->vertices->len - 1;
}

guint
lrg_skinned_sprite_get_vertex_count (LrgSkinnedSprite *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), 0);

	return self->vertices->len;
}

gboolean
lrg_skinned_sprite_get_vertex (LrgSkinnedSprite *self,
                               guint             index,
                               gfloat           *x,
                               gfloat           *y,
                               gfloat           *u,
                               gfloat           *v)
{
	const SpriteVertex *sv;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), FALSE);

	if (index >= self->vertices->len)
		return FALSE;
	sv = &g_array_index (self->vertices, SpriteVertex, index);
	if (x != NULL)
		*x = sv->x;
	if (y != NULL)
		*y = sv->y;
	if (u != NULL)
		*u = sv->u;
	if (v != NULL)
		*v = sv->v;
	return TRUE;
}

gboolean
lrg_skinned_sprite_set_vertex (LrgSkinnedSprite *self,
                               guint             index,
                               gfloat            x,
                               gfloat            y,
                               gfloat            u,
                               gfloat            v)
{
	SpriteVertex *sv;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), FALSE);

	if (index >= self->vertices->len)
		return FALSE;
	sv = &g_array_index (self->vertices, SpriteVertex, index);
	sv->x = x;
	sv->y = y;
	sv->u = u;
	sv->v = v;
	return TRUE;
}

gboolean
lrg_skinned_sprite_set_weights (LrgSkinnedSprite *self,
                                guint             index,
                                guint             n_weights,
                                const gint       *bones,
                                const gfloat     *weights)
{
	SpriteVertex *sv;
	gint          tb[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
	gfloat        tw[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
	guint         n = 0;
	gfloat        sum = 0.0f;
	guint         i;
	guint         j;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), FALSE);

	if (index >= self->vertices->len || n_weights > LRG_SKINNED_SPRITE_MAX_WEIGHTS)
		return FALSE;
	if (n_weights > 0 && (bones == NULL || weights == NULL))
		return FALSE;

	/* keep the positive influences, insertion-sorted heaviest first */
	for (i = 0; i < n_weights; i++)
	{
		if (bones[i] < 0)
			return FALSE;
		if (!(weights[i] > 0.0f))
			continue;
		j = n;
		while (j > 0 && tw[j - 1] < weights[i])
		{
			tw[j] = tw[j - 1];
			tb[j] = tb[j - 1];
			j--;
		}
		tw[j] = weights[i];
		tb[j] = bones[i];
		n++;
		sum += weights[i];
	}

	/* already normalised weights (a saved file read back) are kept bit
	 * for bit, so a load/save round trip is byte stable */
	if (fabsf (sum - 1.0f) < 1e-5f)
		sum = 1.0f;

	sv = &g_array_index (self->vertices, SpriteVertex, index);
	for (i = 0; i < LRG_SKINNED_SPRITE_MAX_WEIGHTS; i++)
	{
		sv->bones[i] = i < n ? tb[i] : -1;
		sv->weights[i] = i < n ? tw[i] / sum : 0.0f;
	}
	sv->n_weights = n;
	return TRUE;
}

guint
lrg_skinned_sprite_get_weights (LrgSkinnedSprite *self,
                                guint             index,
                                gint             *bones,
                                gfloat           *weights)
{
	const SpriteVertex *sv;
	guint               i;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), 0);

	if (index >= self->vertices->len)
		return 0;
	sv = &g_array_index (self->vertices, SpriteVertex, index);
	for (i = 0; i < LRG_SKINNED_SPRITE_MAX_WEIGHTS; i++)
	{
		if (bones != NULL)
			bones[i] = sv->bones[i];
		if (weights != NULL)
			weights[i] = sv->weights[i];
	}
	return sv->n_weights;
}

void
lrg_skinned_sprite_clear_weights (LrgSkinnedSprite *self)
{
	guint i;

	g_return_if_fail (LRG_IS_SKINNED_SPRITE (self));

	for (i = 0; i < self->vertices->len; i++)
		lrg_skinned_sprite_set_weights (self, i, 0, NULL, NULL);
}

gboolean
lrg_skinned_sprite_has_weights (LrgSkinnedSprite *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), FALSE);

	for (i = 0; i < self->vertices->len; i++)
	{
		if (g_array_index (self->vertices, SpriteVertex, i).n_weights > 0)
			return TRUE;
	}
	return FALSE;
}

gint
lrg_skinned_sprite_get_max_bone (LrgSkinnedSprite *self)
{
	gint  best = -1;
	guint i;
	guint k;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), -1);

	for (i = 0; i < self->vertices->len; i++)
	{
		const SpriteVertex *sv = &g_array_index (self->vertices, SpriteVertex, i);

		for (k = 0; k < sv->n_weights; k++)
			best = MAX (best, sv->bones[k]);
	}
	return best;
}

gboolean
lrg_skinned_sprite_add_triangle (LrgSkinnedSprite *self,
                                 guint             a,
                                 guint             b,
                                 guint             c)
{
	guint16 tri[3];

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), FALSE);

	if (a >= self->vertices->len || b >= self->vertices->len || c >= self->vertices->len)
		return FALSE;
	tri[0] = (guint16)a;
	tri[1] = (guint16)b;
	tri[2] = (guint16)c;
	g_array_append_vals (self->indices, tri, 3);
	return TRUE;
}

guint
lrg_skinned_sprite_get_triangle_count (LrgSkinnedSprite *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), 0);

	return self->indices->len / 3;
}

gboolean
lrg_skinned_sprite_get_triangle (LrgSkinnedSprite *self,
                                 guint             index,
                                 guint            *a,
                                 guint            *b,
                                 guint            *c)
{
	const guint16 *idx;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), FALSE);

	if (index >= self->indices->len / 3)
		return FALSE;
	idx = &g_array_index (self->indices, guint16, index * 3);
	if (a != NULL)
		*a = idx[0];
	if (b != NULL)
		*b = idx[1];
	if (c != NULL)
		*c = idx[2];
	return TRUE;
}

void
lrg_skinned_sprite_clear (LrgSkinnedSprite *self)
{
	g_return_if_fail (LRG_IS_SKINNED_SPRITE (self));

	g_array_set_size (self->vertices, 0);
	g_array_set_size (self->indices, 0);
	g_array_set_size (self->deformed, 0);
}

/* ------------------------------------------------------------------ */
/* Texture                                                             */
/* ------------------------------------------------------------------ */

void
lrg_skinned_sprite_set_texture (LrgSkinnedSprite *self,
                                GrlTexture       *texture)
{
	g_return_if_fail (LRG_IS_SKINNED_SPRITE (self));
	g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

	if (g_set_object (&self->texture, texture))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE]);
}

GrlTexture *
lrg_skinned_sprite_get_texture (LrgSkinnedSprite *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), NULL);

	return self->texture;
}

void
lrg_skinned_sprite_set_image (LrgSkinnedSprite *self,
                              const gchar      *image)
{
	g_return_if_fail (LRG_IS_SKINNED_SPRITE (self));

	if (image != NULL && image[0] == '\0')
		image = NULL;
	if (g_strcmp0 (self->image, image) == 0)
		return;
	g_free (self->image);
	self->image = g_strdup (image);
	self->source_tried = FALSE;   /* a new path may load */
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IMAGE]);
}

const gchar *
lrg_skinned_sprite_get_image (LrgSkinnedSprite *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), NULL);

	return self->image;
}

gboolean
lrg_skinned_sprite_load_texture (LrgSkinnedSprite  *self,
                                 const gchar       *base_dir,
                                 GError           **error)
{
	g_autofree gchar         *path = NULL;
	g_autoptr(GrlTexture)     texture = NULL;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (self->image == NULL || self->texture != NULL)
		return TRUE;

	if (g_path_is_absolute (self->image) || base_dir == NULL)
		path = g_strdup (self->image);
	else
		path = g_build_filename (base_dir, self->image, NULL);

	if (!IsWindowReady ())
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
		             "%s: loading a texture needs an open window", path);
		return FALSE;
	}
	if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s: no such image", path);
		return FALSE;
	}
	texture = grl_texture_new_from_file (path);
	if (texture == NULL || !grl_texture_is_valid (texture))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: could not load the image", path);
		return FALSE;
	}
	lrg_skinned_sprite_set_texture (self, texture);
	return TRUE;
}

void
lrg_skinned_sprite_set_region (LrgSkinnedSprite *self,
                               gfloat            x,
                               gfloat            y,
                               gfloat            width,
                               gfloat            height)
{
	g_return_if_fail (LRG_IS_SKINNED_SPRITE (self));

	self->region[0] = x;
	self->region[1] = y;
	self->region[2] = MAX (width, 0.0f);
	self->region[3] = MAX (height, 0.0f);
	update_source_region (self);
}

gboolean
lrg_skinned_sprite_get_region (LrgSkinnedSprite *self,
                               gfloat           *x,
                               gfloat           *y,
                               gfloat           *width,
                               gfloat           *height)
{
	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), FALSE);

	if (x != NULL)
		*x = self->region[0];
	if (y != NULL)
		*y = self->region[1];
	if (width != NULL)
		*width = self->region[2];
	if (height != NULL)
		*height = self->region[3];
	return self->region[2] > 0.0f && self->region[3] > 0.0f;
}

/* ------------------------------------------------------------------ */
/* Skinning                                                            */
/* ------------------------------------------------------------------ */

void
lrg_skinned_sprite_deform (LrgSkinnedSprite *self,
                           const gfloat     *palette,
                           guint             n_floats,
                           gint              default_bone)
{
	const SpriteVertex *verts;
	gfloat             *out;
	guint               n_bones;
	const gfloat       *fallback = NULL;
	guint               i;

	g_return_if_fail (LRG_IS_SKINNED_SPRITE (self));

	sync_deformed (self);
	n_bones = palette != NULL ? n_floats / 6 : 0;
	if (default_bone >= 0 && (guint)default_bone < n_bones)
		fallback = palette + default_bone * 6;

	verts = (const SpriteVertex *)(gconstpointer)self->vertices->data;
	out = (gfloat *)(gpointer)self->deformed->data;

	/* Linear blend skinning: p' = sum w_k * M_k * p over the vertex's
	 * influences, renormalised over the bones the palette has. */
	for (i = 0; i < self->vertices->len; i++)
	{
		const SpriteVertex *sv = &verts[i];
		gfloat              px = 0.0f;
		gfloat              py = 0.0f;
		gfloat              wsum = 0.0f;
		guint               k;

		for (k = 0; k < sv->n_weights; k++)
		{
			const gfloat *m;
			gfloat        w = sv->weights[k];

			if ((guint)sv->bones[k] >= n_bones)
				continue;
			m = palette + sv->bones[k] * 6;
			px += w * (m[0] * sv->x + m[2] * sv->y + m[4]);
			py += w * (m[1] * sv->x + m[3] * sv->y + m[5]);
			wsum += w;
		}

		if (wsum > 0.0f)
		{
			out[i * 2] = px / wsum;
			out[i * 2 + 1] = py / wsum;
		}
		else if (fallback != NULL)
		{
			/* no usable influence: rigid on the default (slot) bone */
			out[i * 2] = fallback[0] * sv->x + fallback[2] * sv->y + fallback[4];
			out[i * 2 + 1] = fallback[1] * sv->x + fallback[3] * sv->y + fallback[5];
		}
		else
		{
			out[i * 2] = sv->x;
			out[i * 2 + 1] = sv->y;
		}
	}
}

const gfloat *
lrg_skinned_sprite_get_deformed (LrgSkinnedSprite *self,
                                 guint            *n_floats)
{
	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), NULL);

	sync_deformed (self);
	if (n_floats != NULL)
		*n_floats = self->deformed->len;
	return (const gfloat *)(gconstpointer)self->deformed->data;
}

gboolean
lrg_skinned_sprite_get_deformed_bounds (LrgSkinnedSprite *self,
                                        gfloat           *min_x,
                                        gfloat           *min_y,
                                        gfloat           *max_x,
                                        gfloat           *max_y)
{
	const gfloat *p;
	gfloat        b[4];
	guint         i;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), FALSE);

	if (self->vertices->len == 0)
		return FALSE;
	sync_deformed (self);
	p = (const gfloat *)(gconstpointer)self->deformed->data;
	b[0] = b[2] = p[0];
	b[1] = b[3] = p[1];
	for (i = 1; i < self->vertices->len; i++)
	{
		b[0] = MIN (b[0], p[i * 2]);
		b[1] = MIN (b[1], p[i * 2 + 1]);
		b[2] = MAX (b[2], p[i * 2]);
		b[3] = MAX (b[3], p[i * 2 + 1]);
	}
	if (min_x != NULL)
		*min_x = b[0];
	if (min_y != NULL)
		*min_y = b[1];
	if (max_x != NULL)
		*max_x = b[2];
	if (max_y != NULL)
		*max_y = b[3];
	return TRUE;
}

void
lrg_skinned_sprite_draw (LrgSkinnedSprite *self,
                         const GrlColor   *tint)
{
	const SpriteVertex *verts;
	const guint16      *idx;
	const gfloat       *pos;
	Texture2D          *tex = NULL;
	gfloat              s0 = 0.0f;
	gfloat              t0 = 0.0f;
	gfloat              sw = 1.0f;
	gfloat              th = 1.0f;
	guint               n_idx;
	guint               start;

	g_return_if_fail (LRG_IS_SKINNED_SPRITE (self));

	n_idx = self->indices->len - self->indices->len % 3;
	if (n_idx == 0)
		return;
	sync_deformed (self);

	/* Texture coordinates: the vertex UV spans the region (or the whole
	 * texture), so normalise the region by the texture size. */
	if (self->texture != NULL)
	{
		tex = (Texture2D *)grl_texture_get_handle (self->texture);
		if (tex != NULL && tex->id != 0 && tex->width > 0 && tex->height > 0 &&
		    self->region[2] > 0.0f && self->region[3] > 0.0f)
		{
			s0 = self->region[0] / (gfloat)tex->width;
			t0 = self->region[1] / (gfloat)tex->height;
			sw = self->region[2] / (gfloat)tex->width;
			th = self->region[3] / (gfloat)tex->height;
		}
		if (tex != NULL && tex->id == 0)
			tex = NULL;
	}

	verts = (const SpriteVertex *)(gconstpointer)self->vertices->data;
	idx = (const guint16 *)(gconstpointer)self->indices->data;
	pos = (const gfloat *)(gconstpointer)self->deformed->data;

	/* chunks of TRIANGLES_PER_CHUNK triangles, each its own batch check */
	for (start = 0; start < n_idx; start += TRIANGLES_PER_CHUNK * 3)
	{
		guint end = MIN (n_idx, start + TRIANGLES_PER_CHUNK * 3);
		guint k;

		rlCheckRenderBatchLimit ((gint)(end - start));
		rlSetTexture (tex != NULL ? tex->id : rlGetTextureIdDefault ());
		rlBegin (RL_TRIANGLES);
		if (tint != NULL)
			rlColor4ub (tint->r, tint->g, tint->b, tint->a);
		else
			rlColor4ub (255, 255, 255, 255);
		for (k = start; k < end; k += 3)
		{
			guint  tri[3];
			gfloat cross;
			guint  e;

			/* raylib culls back faces, and its 2D front faces are
			 * counter-clockwise on the y-down screen (a negative cross
			 * product here).  Deformation and mirroring can flip a
			 * triangle, so each one is emitted in front-facing order. */
			tri[0] = idx[k];
			tri[1] = idx[k + 1];
			tri[2] = idx[k + 2];
			cross = (pos[tri[1] * 2] - pos[tri[0] * 2]) * (pos[tri[2] * 2 + 1] - pos[tri[0] * 2 + 1]) -
			        (pos[tri[1] * 2 + 1] - pos[tri[0] * 2 + 1]) * (pos[tri[2] * 2] - pos[tri[0] * 2]);
			if (cross > 0.0f)
			{
				tri[1] = idx[k + 2];
				tri[2] = idx[k + 1];
			}
			for (e = 0; e < 3; e++)
			{
				guint v = tri[e];

				rlTexCoord2f (s0 + verts[v].u * sw, t0 + verts[v].v * th);
				rlVertex2f (pos[v * 2], pos[v * 2 + 1]);
			}
		}
		rlEnd ();
		rlSetTexture (0);
	}
}

void
lrg_skinned_sprite_draw_wireframe (LrgSkinnedSprite *self,
                                   const GrlColor   *color)
{
	const guint16 *idx;
	const gfloat  *pos;
	guint          n_tri;
	guint          t;

	g_return_if_fail (LRG_IS_SKINNED_SPRITE (self));

	n_tri = self->indices->len / 3;
	if (n_tri == 0)
		return;
	sync_deformed (self);
	idx = (const guint16 *)(gconstpointer)self->indices->data;
	pos = (const gfloat *)(gconstpointer)self->deformed->data;

	for (t = 0; t < n_tri; t++)
	{
		guint e;

		if (t % TRIANGLES_PER_CHUNK == 0)
		{
			if (t > 0)
				rlEnd ();
			rlCheckRenderBatchLimit ((gint)(MIN (n_tri - t, (guint)TRIANGLES_PER_CHUNK) * 6));
			rlBegin (RL_LINES);
			if (color != NULL)
				rlColor4ub (color->r, color->g, color->b, color->a);
			else
				rlColor4ub (255, 255, 255, 255);
		}
		for (e = 0; e < 3; e++)
		{
			guint a = idx[t * 3 + e];
			guint b = idx[t * 3 + (e + 1) % 3];

			rlVertex2f (pos[a * 2], pos[a * 2 + 1]);
			rlVertex2f (pos[b * 2], pos[b * 2 + 1]);
		}
	}
	rlEnd ();
}

/* ------------------------------------------------------------------ */
/* CPU drawing                                                         */
/* ------------------------------------------------------------------ */

/*
 * Rebuilds source_region, the part of source_image the vertices' UVs
 * span: a cropped copy when a region is set (rounded to whole pixels and
 * clamped to the image), otherwise another reference to the whole image.
 * Cropping once here, instead of remapping UVs into the full image at
 * draw time, makes bilinear sampling clamp at the region's own border,
 * so neighbouring atlas cells never bleed in.  It runs when the image or
 * the region changes, never while drawing, so drawing reads only
 * immutable images.
 */
static void
update_source_region (LrgSkinnedSprite *self)
{
	gint w;
	gint h;
	gint x0;
	gint y0;
	gint x1;
	gint y1;

	g_clear_object (&self->source_region);
	if (self->source_image == NULL)
		return;

	w = grl_image_get_width (self->source_image);
	h = grl_image_get_height (self->source_image);
	if (self->region[2] > 0.0f && self->region[3] > 0.0f)
	{
		x0 = (gint)floorf (self->region[0] + 0.5f);
		y0 = (gint)floorf (self->region[1] + 0.5f);
		x1 = (gint)floorf (self->region[0] + self->region[2] + 0.5f);
		y1 = (gint)floorf (self->region[1] + self->region[3] + 0.5f);
		x0 = CLAMP (x0, 0, w);
		x1 = CLAMP (x1, 0, w);
		y0 = CLAMP (y0, 0, h);
		y1 = CLAMP (y1, 0, h);
		if (x1 > x0 && y1 > y0 && (x0 != 0 || y0 != 0 || x1 != w || y1 != h))
		{
			GrlRectangle rect;
			GrlImage    *crop = grl_image_copy (self->source_image);

			rect.x = (gfloat)x0;
			rect.y = (gfloat)y0;
			rect.width = (gfloat)(x1 - x0);
			rect.height = (gfloat)(y1 - y0);
			grl_image_crop (crop, &rect);
			self->source_region = crop;
			return;
		}
	}
	self->source_region = g_object_ref (self->source_image);
}

void
lrg_skinned_sprite_set_source_image (LrgSkinnedSprite *self,
                                     GrlImage         *image)
{
	g_return_if_fail (LRG_IS_SKINNED_SPRITE (self));
	g_return_if_fail (image == NULL || GRL_IS_IMAGE (image));

	/* the rasteriser reads RGBA8; converting a private copy keeps the
	 * caller's image untouched and the stored one immutable */
	if (image != NULL && grl_image_get_format (image) != GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
	{
		g_autoptr(GrlImage) rgba = grl_image_copy (image);

		grl_image_get_pixels (rgba, NULL);   /* converts in place */
		g_set_object (&self->source_image, rgba);
	}
	else
	{
		g_set_object (&self->source_image, image);
	}
	if (image == NULL)
		self->source_tried = FALSE;
	update_source_region (self);
}

gboolean
lrg_skinned_sprite_load_source_image (LrgSkinnedSprite  *self,
                                      const gchar       *base_dir,
                                      GError           **error)
{
	g_autofree gchar    *path = NULL;
	g_autoptr(GrlImage)  image = NULL;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (self->image == NULL || self->source_image != NULL)
		return TRUE;

	/* the same resolution as lrg_skinned_sprite_load_texture() */
	if (g_path_is_absolute (self->image) || base_dir == NULL)
		path = g_strdup (self->image);
	else
		path = g_build_filename (base_dir, self->image, NULL);

	self->source_tried = TRUE;
	if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s: no such image", path);
		return FALSE;
	}
	image = grl_image_new_from_file (path);
	if (image == NULL || !grl_image_is_valid (image))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: could not load the image", path);
		return FALSE;
	}
	lrg_skinned_sprite_set_source_image (self, image);
	return TRUE;
}

GrlImage *
_lrg_skinned_sprite_ensure_source (LrgSkinnedSprite *self,
                                   const gchar      *base_dir)
{
	if (self->source_image == NULL && self->image != NULL && !self->source_tried)
	{
		g_autoptr(GError) error = NULL;

		if (!lrg_skinned_sprite_load_source_image (self, base_dir, &error))
			g_warning ("skinned sprite: %s", error->message);
	}
	return self->source_image;
}

GrlImage *
_lrg_skinned_sprite_peek_source_image (LrgSkinnedSprite *self)
{
	return self->source_image;
}

GrlImage *
lrg_skinned_sprite_get_source_image (LrgSkinnedSprite *self)
{
	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), NULL);

	return _lrg_skinned_sprite_ensure_source (self, NULL);
}

/* Draws the deformed mesh after the lazy load; @base_dir is where a
 * relative image path is looked for. */
static void
draw_to_canvas_from (LrgSkinnedSprite *self,
                     LrgImageCanvas   *canvas,
                     const GrlColor   *tint,
                     const gchar      *base_dir)
{
	const SpriteVertex   *verts;
	const guint16        *idx;
	const gfloat         *pos;
	GrlImage             *dst;
	GrlImage             *src = NULL;
	g_autoptr(GrlMatrix)  ctm = NULL;
	g_autofree gfloat    *screen = NULL;
	GrlColor              white = { 255, 255, 255, 255 };
	guint                 n_idx;
	guint                 n_verts;
	guint                 i;

	n_idx = self->indices->len - self->indices->len % 3;
	if (n_idx == 0)
		return;
	sync_deformed (self);
	dst = lrg_image_canvas_get_image (canvas);
	if (dst == NULL)
		return;

	/* the sampled image: the region of the source, loaded on first use */
	if (_lrg_skinned_sprite_ensure_source (self, base_dir) != NULL)
		src = self->source_region;

	/* deformed positions through the canvas transform (only its 2D
	 * affine part, as every canvas primitive uses it), once per vertex
	 * rather than once per triangle corner */
	verts = (const SpriteVertex *)(gconstpointer)self->vertices->data;
	idx = (const guint16 *)(gconstpointer)self->indices->data;
	pos = (const gfloat *)(gconstpointer)self->deformed->data;
	n_verts = self->vertices->len;
	ctm = grl_image_get_matrix (dst);
	screen = g_new (gfloat, (gsize)n_verts * 2);
	for (i = 0; i < n_verts; i++)
	{
		gfloat x = pos[i * 2];
		gfloat y = pos[i * 2 + 1];

		screen[i * 2] = ctm->m0 * x + ctm->m4 * y + ctm->m12;
		screen[i * 2 + 1] = ctm->m1 * x + ctm->m5 * y + ctm->m13;
	}

	/* one textured triangle per mesh triangle; the rasteriser's top-left
	 * rule keeps shared edges single, and winding does not matter */
	for (i = 0; i < n_idx; i += 3)
	{
		gfloat xy[6];
		gfloat uv[6];
		guint  e;

		for (e = 0; e < 3; e++)
		{
			guint v = idx[i + e];

			xy[e * 2] = screen[v * 2];
			xy[e * 2 + 1] = screen[v * 2 + 1];
			uv[e * 2] = verts[v].u;
			uv[e * 2 + 1] = verts[v].v;
		}
		lrg_image_draw_triangle_textured (dst, src, xy, uv, tint != NULL ? tint : &white);
	}
}

void
lrg_skinned_sprite_draw_to_canvas (LrgSkinnedSprite *self,
                                   LrgImageCanvas   *canvas,
                                   const GrlColor   *tint)
{
	g_return_if_fail (LRG_IS_SKINNED_SPRITE (self));
	g_return_if_fail (LRG_IS_IMAGE_CANVAS (canvas));

	draw_to_canvas_from (self, canvas, tint, NULL);
}

void
_lrg_skinned_sprite_draw_to_canvas_from (LrgSkinnedSprite *self,
                                         LrgImageCanvas   *canvas,
                                         const GrlColor   *tint,
                                         const gchar      *base_dir)
{
	draw_to_canvas_from (self, canvas, tint, base_dir);
}

/* ------------------------------------------------------------------ */
/* Automatic weights                                                   */
/* ------------------------------------------------------------------ */

/* distance from (px, py) to the segment (x0, y0)-(x1, y1) */
static gfloat
segment_distance (gfloat        px,
                  gfloat        py,
                  const gfloat *s)
{
	gfloat dx = s[2] - s[0];
	gfloat dy = s[3] - s[1];
	gfloat len2 = dx * dx + dy * dy;
	gfloat t = 0.0f;
	gfloat cx;
	gfloat cy;

	if (len2 > 1e-12f)
	{
		t = ((px - s[0]) * dx + (py - s[1]) * dy) / len2;
		t = CLAMP (t, 0.0f, 1.0f);
	}
	cx = s[0] + t * dx - px;
	cy = s[1] + t * dy - py;
	return sqrtf (cx * cx + cy * cy);
}

gboolean
lrg_skinned_sprite_compute_auto_weights (LrgSkinnedSprite *self,
                                         const gfloat     *segments,
                                         guint             n_floats,
                                         const gint       *bones,
                                         guint             n_bones,
                                         gfloat            falloff,
                                         guint             max_influences,
                                         guint             smooth_iterations)
{
	g_autofree gfloat *w = NULL;
	g_autofree gfloat *tmp = NULL;
	g_autofree guint  *adj_start = NULL;
	g_autofree guint  *adj = NULL;
	const SpriteVertex *verts;
	guint              n_seg;
	guint              n_vert;
	gfloat             minx;
	gfloat             miny;
	gfloat             maxx;
	gfloat             maxy;
	gfloat             eps;
	guint              i;
	guint              s;
	guint              it;

	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (self), FALSE);

	n_seg = segments != NULL ? n_floats / 4 : 0;
	if (n_seg == 0)
		return FALSE;
	if (bones != NULL && n_bones != n_seg)
		return FALSE;
	for (s = 0; bones != NULL && s < n_seg; s++)
	{
		if (bones[s] < 0)
			return FALSE;
	}
	falloff = CLAMP (falloff, 1.0f, 8.0f);
	max_influences = CLAMP (max_influences, 1u, (guint)LRG_SKINNED_SPRITE_MAX_WEIGHTS);
	n_vert = self->vertices->len;
	if (n_vert == 0)
		return TRUE;
	verts = (const SpriteVertex *)(gconstpointer)self->vertices->data;

	/* epsilon: 1% of the mesh's bind-space size */
	minx = maxx = verts[0].x;
	miny = maxy = verts[0].y;
	for (i = 1; i < n_vert; i++)
	{
		minx = MIN (minx, verts[i].x);
		maxx = MAX (maxx, verts[i].x);
		miny = MIN (miny, verts[i].y);
		maxy = MAX (maxy, verts[i].y);
	}
	eps = MAX (0.01f * MAX (maxx - minx, maxy - miny), 1e-4f);

	/* 1. inverse distance to every segment, normalised per vertex */
	w = g_new0 (gfloat, (gsize)n_vert * n_seg);
	for (i = 0; i < n_vert; i++)
	{
		gfloat sum = 0.0f;

		for (s = 0; s < n_seg; s++)
		{
			gfloat d = segment_distance (verts[i].x, verts[i].y, segments + s * 4);
			gfloat v = 1.0f / powf (d + eps, falloff);

			w[i * n_seg + s] = v;
			sum += v;
		}
		for (s = 0; s < n_seg; s++)
			w[i * n_seg + s] /= sum;
	}

	/* 2. optional smoothing: each pass blends a vertex's weights halfway
	 * toward the mean of its triangle neighbours (a few explicit steps of
	 * heat diffusion over the mesh graph) */
	if (smooth_iterations > 0 && self->indices->len >= 3)
	{
		const guint16 *idx = (const guint16 *)(gconstpointer)self->indices->data;
		guint          n_idx = self->indices->len - self->indices->len % 3;
		g_autofree guint *fill = NULL;

		/* adjacency in CSR form: each triangle edge both ways (duplicates
		 * are harmless; they weigh shared edges a little more) */
		adj_start = g_new0 (guint, n_vert + 1);
		for (i = 0; i < n_idx; i++)
			adj_start[idx[i] + 1] += 2;
		for (i = 0; i < n_vert; i++)
			adj_start[i + 1] += adj_start[i];
		adj = g_new (guint, adj_start[n_vert]);
		fill = g_new0 (guint, n_vert);
		for (i = 0; i < n_idx; i += 3)
		{
			guint e;

			for (e = 0; e < 3; e++)
			{
				guint a = idx[i + e];
				guint b = idx[i + (e + 1) % 3];
				guint c = idx[i + (e + 2) % 3];

				adj[adj_start[a] + fill[a]++] = b;
				adj[adj_start[a] + fill[a]++] = c;
			}
		}

		tmp = g_new (gfloat, (gsize)n_vert * n_seg);
		for (it = 0; it < smooth_iterations; it++)
		{
			for (i = 0; i < n_vert; i++)
			{
				guint deg = adj_start[i + 1] - adj_start[i];
				guint k;

				for (s = 0; s < n_seg; s++)
				{
					gfloat mean = 0.0f;

					if (deg == 0)
					{
						tmp[i * n_seg + s] = w[i * n_seg + s];
						continue;
					}
					for (k = adj_start[i]; k < adj_start[i + 1]; k++)
						mean += w[adj[k] * n_seg + s];
					mean /= (gfloat)deg;
					tmp[i * n_seg + s] = 0.5f * w[i * n_seg + s] + 0.5f * mean;
				}
			}
			memcpy (w, tmp, sizeof (gfloat) * n_vert * n_seg);
		}
	}

	/* 3. keep the heaviest influences per vertex (set_weights sorts and
	 * normalises) */
	for (i = 0; i < n_vert; i++)
	{
		gint   kb[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
		gfloat kw[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
		guint  n = 0;

		for (s = 0; s < n_seg; s++)
		{
			gfloat v = w[i * n_seg + s];
			gint   bone = bones != NULL ? bones[s] : (gint)s;
			guint  j;

			if (n < max_influences)
			{
				j = n++;
			}
			else
			{
				/* replace the lightest kept influence if this is heavier */
				guint lo = 0;

				for (j = 1; j < n; j++)
				{
					if (kw[j] < kw[lo])
						lo = j;
				}
				if (!(v > kw[lo]))
					continue;
				j = lo;
			}
			kw[j] = v;
			kb[j] = bone;
		}
		lrg_skinned_sprite_set_weights (self, i, n, kb, kw);
	}
	return TRUE;
}
