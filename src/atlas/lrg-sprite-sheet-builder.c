/* lrg-sprite-sheet-builder.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpriteSheetBuilder: frames kept as packed RGBA8 buffers, key colour
 * and alpha cleanup, a deterministic median-cut palette shared by all
 * frames, grid layout into one GrlImage, and the LrgSpriteSheet that
 * describes it.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ATLAS

#include "lrg-sprite-sheet-builder.h"
#include "../photomode/lrg-screenshot.h"
#include "../lrg-log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	gint    width;
	gint    height;
	guint8 *rgba;        /* width * height * 4, tightly packed */
} Frame;

typedef struct
{
	gchar  *name;
	GArray *frames;      /* guint */
	gfloat  duration;
	gboolean loop;
} Animation;

struct _LrgSpriteSheetBuilder
{
	GObject    parent_instance;

	GArray    *frames;   /* Frame */
	GPtrArray *anims;    /* Animation */

	gboolean   use_key;
	GrlColor   key;
	guint      key_tolerance;
	guint8     alpha_threshold;
	gboolean   alpha_binary;
	guint      palette_size;
	guint      columns;
	guint      padding;

	GrlImage  *image;    /* the last build */
	gint       cell_w;
	gint       cell_h;
};

G_DEFINE_FINAL_TYPE (LrgSpriteSheetBuilder, lrg_sprite_sheet_builder, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-sprite-sheet-builder-error-quark, lrg_sprite_sheet_builder_error)

GType
lrg_sprite_sheet_builder_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SPRITE_SHEET_BUILDER_ERROR_IO, "LRG_SPRITE_SHEET_BUILDER_ERROR_IO", "io" },
			{ LRG_SPRITE_SHEET_BUILDER_ERROR_EMPTY, "LRG_SPRITE_SHEET_BUILDER_ERROR_EMPTY", "empty" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgSpriteSheetBuilderError"),
		                                   values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

static void
frame_clear (gpointer data)
{
	g_free (((Frame *) data)->rgba);
}

static void
animation_free (gpointer data)
{
	Animation *anim = data;

	g_free (anim->name);
	g_array_unref (anim->frames);
	g_free (anim);
}

static void
lrg_sprite_sheet_builder_finalize (GObject *object)
{
	LrgSpriteSheetBuilder *self = LRG_SPRITE_SHEET_BUILDER (object);

	g_array_unref (self->frames);
	g_ptr_array_unref (self->anims);
	g_clear_object (&self->image);
	G_OBJECT_CLASS (lrg_sprite_sheet_builder_parent_class)->finalize (object);
}

static void
lrg_sprite_sheet_builder_class_init (LrgSpriteSheetBuilderClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_sprite_sheet_builder_finalize;
}

static void
lrg_sprite_sheet_builder_init (LrgSpriteSheetBuilder *self)
{
	self->frames = g_array_new (FALSE, TRUE, sizeof (Frame));
	g_array_set_clear_func (self->frames, frame_clear);
	self->anims = g_ptr_array_new_with_free_func (animation_free);
	self->padding = 1;
}

LrgSpriteSheetBuilder *
lrg_sprite_sheet_builder_new (void)
{
	return g_object_new (LRG_TYPE_SPRITE_SHEET_BUILDER, NULL);
}

/* ==========================================================================
 * Frames and animations
 * ========================================================================== */

gint
lrg_sprite_sheet_builder_add_frame_pixels (LrgSpriteSheetBuilder *self,
                                           const guint8          *rgba,
                                           gint                   width,
                                           gint                   height,
                                           gint                   stride)
{
	Frame frame;
	gsize row;
	gint y;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self), -1);
	g_return_val_if_fail (rgba != NULL, -1);
	g_return_val_if_fail (width > 0 && height > 0, -1);
	g_return_val_if_fail (stride >= width * 4, -1);

	row = (gsize) width * 4;
	frame.width = width;
	frame.height = height;
	frame.rgba = g_malloc (row * (gsize) height);
	for (y = 0; y < height; y++)
		memcpy (frame.rgba + row * (gsize) y, rgba + (gsize) stride * (gsize) y, row);
	g_array_append_val (self->frames, frame);
	return (gint) self->frames->len - 1;
}

gint
lrg_sprite_sheet_builder_add_frame_image (LrgSpriteSheetBuilder *self,
                                          GrlImage              *image)
{
	g_autoptr(GrlImage) copy = NULL;
	const guint8 *pixels;
	gint w;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self), -1);
	g_return_val_if_fail (GRL_IS_IMAGE (image), -1);

	/* get_pixels() converts in place: do that to a copy, not the caller's */
	copy = grl_image_copy (image);
	pixels = grl_image_get_pixels (copy, NULL);
	w = grl_image_get_width (copy);
	g_return_val_if_fail (pixels != NULL, -1);
	return lrg_sprite_sheet_builder_add_frame_pixels (self, pixels, w, grl_image_get_height (copy),
	                                                  w * 4);
}

gint
lrg_sprite_sheet_builder_add_frame_file (LrgSpriteSheetBuilder  *self,
                                         const gchar            *path,
                                         GError                **error)
{
	g_autoptr(GrlImage) image = NULL;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self), -1);
	g_return_val_if_fail (path != NULL, -1);

	if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
		image = grl_image_new_from_file (path);
	if (image == NULL || grl_image_get_width (image) <= 0)
	{
		g_set_error (error, LRG_SPRITE_SHEET_BUILDER_ERROR, LRG_SPRITE_SHEET_BUILDER_ERROR_IO,
		             "cannot read the image '%s'", path);
		return -1;
	}
	return lrg_sprite_sheet_builder_add_frame_image (self, image);
}

guint
lrg_sprite_sheet_builder_get_n_frames (LrgSpriteSheetBuilder *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self), 0);
	return self->frames->len;
}

gboolean
lrg_sprite_sheet_builder_add_animation (LrgSpriteSheetBuilder *self,
                                        const gchar           *name,
                                        const guint           *frames,
                                        guint                  n_frames,
                                        gfloat                 frame_duration,
                                        gboolean               loop)
{
	Animation *anim;
	guint i;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (frames != NULL && n_frames > 0, FALSE);
	g_return_val_if_fail (frame_duration > 0.0f, FALSE);

	for (i = 0; i < n_frames; i++)
	{
		if (frames[i] >= self->frames->len)
			return FALSE;
	}
	for (i = 0; i < self->anims->len; i++)
	{
		if (g_strcmp0 (((Animation *) g_ptr_array_index (self->anims, i))->name, name) == 0)
			return FALSE;
	}

	anim = g_new0 (Animation, 1);
	anim->name = g_strdup (name);
	anim->frames = g_array_sized_new (FALSE, FALSE, sizeof (guint), n_frames);
	g_array_append_vals (anim->frames, frames, n_frames);
	anim->duration = frame_duration;
	anim->loop = loop;
	g_ptr_array_add (self->anims, anim);
	return TRUE;
}

gboolean
lrg_sprite_sheet_builder_add_animation_range (LrgSpriteSheetBuilder *self,
                                              const gchar           *name,
                                              guint                  first,
                                              guint                  count,
                                              gfloat                 frame_duration,
                                              gboolean               loop)
{
	g_autofree guint *frames = NULL;
	guint i;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self), FALSE);
	g_return_val_if_fail (count > 0, FALSE);

	frames = g_new (guint, count);
	for (i = 0; i < count; i++)
		frames[i] = first + i;
	return lrg_sprite_sheet_builder_add_animation (self, name, frames, count, frame_duration, loop);
}

/* ==========================================================================
 * Options
 * ========================================================================== */

void
lrg_sprite_sheet_builder_set_key_color (LrgSpriteSheetBuilder *self,
                                        const GrlColor        *key,
                                        guint                  tolerance)
{
	g_return_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self));

	self->use_key = (key != NULL);
	if (key != NULL)
		self->key = *key;
	self->key_tolerance = MIN (tolerance, 255u);
}

void
lrg_sprite_sheet_builder_set_alpha_threshold (LrgSpriteSheetBuilder *self,
                                              guint8                 threshold,
                                              gboolean               binary)
{
	g_return_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self));
	self->alpha_threshold = threshold;
	self->alpha_binary = binary;
}

void
lrg_sprite_sheet_builder_set_palette_size (LrgSpriteSheetBuilder *self,
                                           guint                  n_colors)
{
	g_return_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self));
	self->palette_size = (n_colors == 0) ? 0 : CLAMP (n_colors, 2u, 256u);
}

void
lrg_sprite_sheet_builder_set_layout (LrgSpriteSheetBuilder *self,
                                     guint                  columns,
                                     guint                  padding)
{
	g_return_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self));
	self->columns = columns;
	self->padding = padding;
}

/* ==========================================================================
 * Processing
 * ========================================================================== */

static void
remove_background (LrgSpriteSheetBuilder *self,
                   guint8                *px,
                   gsize                  n_pixels)
{
	gsize i;

	for (i = 0; i < n_pixels; i++)
	{
		guint8 *p = px + i * 4;

		if (self->use_key &&
		    (guint) ABS ((gint) p[0] - self->key.r) <= self->key_tolerance &&
		    (guint) ABS ((gint) p[1] - self->key.g) <= self->key_tolerance &&
		    (guint) ABS ((gint) p[2] - self->key.b) <= self->key_tolerance)
		{
			/* clear the colour too, so filtering does not bleed the key in */
			memset (p, 0, 4);
			continue;
		}
		if (self->alpha_threshold > 0)
		{
			if (p[3] < self->alpha_threshold)
				memset (p, 0, 4);
			else if (self->alpha_binary)
				p[3] = 255;
		}
	}
}

/* --- median cut ---------------------------------------------------------- */

typedef struct
{
	guint start;          /* into the colour array */
	guint count;
} Box;

static gint
compare_keys (const void *a,
              const void *b)
{
	guint32 ka = *(const guint32 *) a;
	guint32 kb = *(const guint32 *) b;

	return (ka < kb) ? -1 : (ka > kb);
}

/*
 * Sorts a box's colours on one channel.  Each colour becomes a key with
 * that channel in the top byte and the other two after it in a fixed
 * order: a total order, so the cut is the same on every libc, and no
 * comparator state (qsort has no user data in C89).
 */
static void
sort_box (guint8 *colors,
          guint   count,
          gint    channel)
{
	g_autofree guint32 *keys = g_new (guint32, count);
	gint second = (channel == 0) ? 1 : 0;
	gint third = (channel == 2) ? 1 : 2;
	guint i;

	for (i = 0; i < count; i++)
	{
		const guint8 *p = colors + (gsize) i * 3;

		keys[i] = ((guint32) p[channel] << 16) | ((guint32) p[second] << 8) | p[third];
	}
	qsort (keys, count, sizeof (guint32), compare_keys);
	for (i = 0; i < count; i++)
	{
		guint8 *p = colors + (gsize) i * 3;

		p[channel] = (guint8) (keys[i] >> 16);
		p[second] = (guint8) ((keys[i] >> 8) & 0xFF);
		p[third] = (guint8) (keys[i] & 0xFF);
	}
}

static void
box_range (const guint8 *colors,
           const Box    *box,
           gint         *channel,
           gint         *range)
{
	guint8 lo[3] = { 255, 255, 255 };
	guint8 hi[3] = { 0, 0, 0 };
	guint i;
	gint c;

	for (i = 0; i < box->count; i++)
	{
		const guint8 *p = colors + (gsize) (box->start + i) * 3;

		for (c = 0; c < 3; c++)
		{
			lo[c] = MIN (lo[c], p[c]);
			hi[c] = MAX (hi[c], p[c]);
		}
	}
	*channel = 0;
	*range = hi[0] - lo[0];
	for (c = 1; c < 3; c++)
	{
		if (hi[c] - lo[c] > *range)
		{
			*range = hi[c] - lo[c];
			*channel = c;
		}
	}
}

/* Builds at most @max palette entries (RGB) from the visible pixels. */
static guint
median_cut (guint8 *colors,
            guint   n_colors,
            guint   max,
            guint8 *palette)
{
	GArray *boxes = g_array_new (FALSE, FALSE, sizeof (Box));
	Box first = { 0, n_colors };
	guint n;
	guint b;

	g_array_append_val (boxes, first);
	while (boxes->len < max)
	{
		gint best = -1;
		gint best_range = 0;
		gint best_channel = 0;
		Box *box;
		Box upper;
		guint half;

		/* split the box with the widest channel (lowest index on ties) */
		for (b = 0; b < boxes->len; b++)
		{
			gint channel;
			gint range;

			box = &g_array_index (boxes, Box, b);
			if (box->count < 2)
				continue;
			box_range (colors, box, &channel, &range);
			if (range > best_range)
			{
				best = (gint) b;
				best_range = range;
				best_channel = channel;
			}
		}
		if (best < 0)
			break;
		box = &g_array_index (boxes, Box, best);
		sort_box (colors + (gsize) box->start * 3, box->count, best_channel);
		half = box->count / 2;
		upper.start = box->start + half;
		upper.count = box->count - half;
		box->count = half;
		g_array_append_val (boxes, upper);
	}

	n = boxes->len;
	for (b = 0; b < n; b++)
	{
		Box *box = &g_array_index (boxes, Box, b);
		guint64 sum[3] = { 0, 0, 0 };
		guint i;
		gint c;

		for (i = 0; i < box->count; i++)
			for (c = 0; c < 3; c++)
				sum[c] += colors[(gsize) (box->start + i) * 3 + c];
		for (c = 0; c < 3; c++)
			palette[b * 3 + c] = (guint8) ((sum[c] + box->count / 2) / MAX (box->count, 1u));
	}
	g_array_unref (boxes);
	return n;
}

static void
apply_palette (GArray *frames,
               guint   palette_size)
{
	g_autofree guint8 *colors = NULL;
	guint8 palette[256 * 3];
	gsize total = 0;
	gsize k = 0;
	guint n_palette;
	guint f;

	for (f = 0; f < frames->len; f++)
	{
		Frame *frame = &g_array_index (frames, Frame, f);
		gsize i;

		for (i = 0; i < (gsize) frame->width * frame->height; i++)
			if (frame->rgba[i * 4 + 3] > 0)
				total++;
	}
	if (total == 0)
		return;

	/* every visible pixel of every frame votes, so all share one palette */
	colors = g_malloc (total * 3);
	for (f = 0; f < frames->len; f++)
	{
		Frame *frame = &g_array_index (frames, Frame, f);
		gsize i;

		for (i = 0; i < (gsize) frame->width * frame->height; i++)
		{
			if (frame->rgba[i * 4 + 3] == 0)
				continue;
			memcpy (colors + k * 3, frame->rgba + i * 4, 3);
			k++;
		}
	}
	n_palette = median_cut (colors, (guint) total, palette_size, palette);

	for (f = 0; f < frames->len; f++)
	{
		Frame *frame = &g_array_index (frames, Frame, f);
		gsize i;

		for (i = 0; i < (gsize) frame->width * frame->height; i++)
		{
			guint8 *p = frame->rgba + i * 4;
			guint best = 0;
			gint best_d = G_MAXINT;
			guint e;

			if (p[3] == 0)
				continue;
			for (e = 0; e < n_palette; e++)
			{
				gint dr = (gint) p[0] - palette[e * 3];
				gint dg = (gint) p[1] - palette[e * 3 + 1];
				gint db = (gint) p[2] - palette[e * 3 + 2];
				gint d = dr * dr + dg * dg + db * db;

				if (d < best_d)
				{
					best_d = d;
					best = e;
				}
			}
			memcpy (p, palette + best * 3, 3);
		}
	}
}

/* ==========================================================================
 * Build and save
 * ========================================================================== */

LrgSpriteSheet *
lrg_sprite_sheet_builder_build (LrgSpriteSheetBuilder  *self,
                                const gchar            *name,
                                GError                **error)
{
	g_autoptr(GArray) work = NULL;
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	g_autofree guint8 *canvas = NULL;
	guint n;
	guint cols;
	guint rows;
	guint pad;
	gint cw = 0;
	gint ch = 0;
	gint img_w;
	gint img_h;
	guint f;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	n = self->frames->len;
	if (n == 0)
	{
		g_set_error (error, LRG_SPRITE_SHEET_BUILDER_ERROR, LRG_SPRITE_SHEET_BUILDER_ERROR_EMPTY,
		             "no frames to build '%s' from", name);
		return NULL;
	}

	/* process copies: building twice with other options starts over */
	work = g_array_sized_new (FALSE, TRUE, sizeof (Frame), n);
	g_array_set_clear_func (work, frame_clear);
	for (f = 0; f < n; f++)
	{
		Frame *src = &g_array_index (self->frames, Frame, f);
		Frame copy;

		copy.width = src->width;
		copy.height = src->height;
		copy.rgba = g_memdup2 (src->rgba, (gsize) src->width * src->height * 4);
		remove_background (self, copy.rgba, (gsize) copy.width * copy.height);
		g_array_append_val (work, copy);
		cw = MAX (cw, src->width);
		ch = MAX (ch, src->height);
	}
	if (self->palette_size > 0)
		apply_palette (work, self->palette_size);

	cols = (self->columns > 0) ? MIN (self->columns, n) : (guint) ceil (sqrt ((gdouble) n));
	rows = (n + cols - 1) / cols;
	pad = self->padding;
	img_w = (gint) (cols * (guint) cw + (cols + 1) * pad);
	img_h = (gint) (rows * (guint) ch + (rows + 1) * pad);
	canvas = g_malloc0 ((gsize) img_w * img_h * 4);

	sheet = lrg_sprite_sheet_new (name);
	lrg_sprite_sheet_set_texture_size (sheet, img_w, img_h);
	for (f = 0; f < n; f++)
	{
		Frame *frame = &g_array_index (work, Frame, f);
		gint cell_x = (gint) (pad + (f % cols) * ((guint) cw + pad));
		gint cell_y = (gint) (pad + (f / cols) * ((guint) ch + pad));
		/* centred horizontally, standing on the cell's bottom edge */
		gint ox = cell_x + (cw - frame->width) / 2;
		gint oy = cell_y + (ch - frame->height);
		g_autofree gchar *frame_name = g_strdup_printf ("frame-%u", f);
		gint y;

		for (y = 0; y < frame->height; y++)
			memcpy (canvas + ((gsize) (oy + y) * img_w + ox) * 4,
			        frame->rgba + (gsize) y * frame->width * 4, (gsize) frame->width * 4);
		lrg_sprite_sheet_add_frame_rect (sheet, frame_name, cell_x, cell_y, cw, ch);
	}
	for (f = 0; f < self->anims->len; f++)
	{
		Animation *anim = g_ptr_array_index (self->anims, f);

		lrg_sprite_sheet_define_animation_frames (sheet, anim->name,
		                                          (const guint *) anim->frames->data,
		                                          anim->frames->len, anim->duration, anim->loop);
	}

	g_clear_object (&self->image);
	self->image = grl_image_new_from_pixels (img_w, img_h, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
	                                         canvas);
	self->cell_w = cw;
	self->cell_h = ch;
	return g_steal_pointer (&sheet);
}

GrlImage *
lrg_sprite_sheet_builder_get_image (LrgSpriteSheetBuilder *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self), NULL);
	return self->image;
}

void
lrg_sprite_sheet_builder_get_cell_size (LrgSpriteSheetBuilder *self,
                                        gint                  *width,
                                        gint                  *height)
{
	g_return_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self));
	if (width != NULL)
		*width = self->cell_w;
	if (height != NULL)
		*height = self->cell_h;
}

gboolean
lrg_sprite_sheet_builder_save (LrgSpriteSheetBuilder  *self,
                               const gchar            *name,
                               const gchar            *png_path,
                               const gchar            *yaml_path,
                               GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	g_autoptr(GError) local = NULL;
	g_autofree gchar *png_dir = NULL;
	g_autofree gchar *yaml_dir = NULL;
	g_autofree gchar *texture = NULL;
	const guint8 *pixels;
	gint w;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET_BUILDER (self), FALSE);
	g_return_val_if_fail (png_path != NULL && yaml_path != NULL, FALSE);

	sheet = lrg_sprite_sheet_builder_build (self, name, error);
	if (sheet == NULL)
		return FALSE;

	pixels = grl_image_get_pixels (self->image, NULL);
	w = grl_image_get_width (self->image);
	if (!lrg_screenshot_write_png (png_path, pixels, w, grl_image_get_height (self->image), w * 4,
	                               &local))
	{
		g_set_error (error, LRG_SPRITE_SHEET_BUILDER_ERROR, LRG_SPRITE_SHEET_BUILDER_ERROR_IO,
		             "%s", local->message);
		return FALSE;
	}

	png_dir = g_path_get_dirname (png_path);
	yaml_dir = g_path_get_dirname (yaml_path);
	texture = (g_strcmp0 (png_dir, yaml_dir) == 0) ? g_path_get_basename (png_path)
	                                              : g_strdup (png_path);
	lrg_sprite_sheet_set_texture_path (sheet, texture);
	if (!lrg_sprite_sheet_save_to_file (sheet, yaml_path, &local))
	{
		g_set_error (error, LRG_SPRITE_SHEET_BUILDER_ERROR, LRG_SPRITE_SHEET_BUILDER_ERROR_IO,
		             "%s", local != NULL ? local->message : "cannot save the sprite sheet");
		return FALSE;
	}
	return TRUE;
}
