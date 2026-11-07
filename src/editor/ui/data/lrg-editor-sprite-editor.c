/* lrg-editor-sprite-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSpriteEditor: the `.rsprite` slicer and frame animation
 * editor (see the header).
 *
 * Layout:
 *
 *   +--------------------------------------+---------------------------+
 *   | [Grid][Auto][Manual]  slice fields   | FRAME 3 "hero_hit"        |
 *   | +----------------------------------+ | name, rect, pivot         |
 *   | | the image, every frame's rect     | | CLIPS [walk v] new remove |
 *   | | with its index, pivots as crosses | | fps, loop                 |
 *   | | (drag the selected pivot; Manual: | | strip: positions, widths  |
 *   | |  drag a new rectangle)            | |  by duration, event flags |
 *   | +----------------------------------+ | duration, event name      |
 *   |                                      | PREVIEW (pivot-aligned)   |
 *   +--------------------------------------+---------------------------+
 *
 * Model. As in the other engine-format editors: the document text is
 * parsed with lrg_sprite_sheet_new_from_rsprite_data() after every change;
 * an edit changes a fresh parse through the engine API and writes
 * lrg_sprite_sheet_save_rsprite_to_data() back over the whole document as
 * one undo group (drags merge by gesture id). Grid fields that the sheet
 * API does not expose (rows, margin, spacing, pivot) are read from the
 * document's `grid:` block. The preview advances its own clock in step()
 * and samples the selected clip with lrg_sprite_animation_sample(), so
 * scrubbing, the strip and the playing preview agree with the engine's
 * player.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-sprite-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../lrg-editor-server.h"
#include "../../../atlas/lrg-atlas-region.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-spin-box.h"
#include "../../../ui/lrg-combo-box.h"
#include "../../../ui/lrg-text-input.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <raylib.h>
#include <math.h>
#include <string.h>

#define INSTALLED_KEY "lrg-editor-sprite-installed"
#define PIVOT_HIT     (9.0f)

typedef enum
{
	MODE_GRID,
	MODE_ALPHA,
	MODE_MANUAL
} SliceMode;

static const gchar * const loop_labels[] = { "Once", "Loop", "Ping-pong" };
static const gchar * const loop_nicks[] = { "once", "loop", "ping-pong" };

struct _LrgEditorSpriteEditor
{
	LrgEditorDataEditor  parent_instance;

	/* The model */
	LrgSpriteSheet      *sheet;
	gchar               *parse_error;
	gint                 selected;        /* frame */
	gchar               *clip;            /* selected clip */
	gint                 position;        /* selected clip position, -1 */

	/* The image */
	gchar               *image_path;
	GrlTexture          *tex;
	gint                 img_w;
	gint                 img_h;
	gint64               img_mtime;

	/* Slicer view */
	SliceMode            mode;
	LrgWidget           *view;
	gboolean             dragging_pivot;
	gboolean             dragging_rect;
	guint                gesture;
	gfloat               drag_x0;
	gfloat               drag_y0;
	gfloat               drag_x1;
	gfloat               drag_y1;
	LrgWidget           *mode_chips[3];
	LrgWidget           *grid_box;
	LrgWidget           *alpha_box;
	LrgSpinBox          *fw, *fh, *cols, *rows, *count, *mx, *my, *sx, *sy, *px, *py;
	LrgSpinBox          *threshold, *min_size, *apx, *apy;

	/* Frame section */
	LrgWidget           *frame_title;
	LrgTextInput        *frame_name;
	LrgSpinBox          *fx, *fy, *fwidth, *fheight, *fpx, *fpy;

	/* Clips */
	LrgComboBox         *clip_combo;
	LrgTextInput        *clip_new;
	LrgSpinBox          *fps;
	LrgComboBox         *loop;
	LrgWidget           *strip;
	LrgSpinBox          *duration;
	LrgTextInput        *event_name;

	/* Preview */
	LrgWidget           *preview;
	LrgWidget           *play_chip;
	gboolean             playing;
	gfloat               time;

	gboolean             syncing;
	gboolean             stale;
	gchar               *sig_combo;
};

G_DEFINE_FINAL_TYPE (LrgEditorSpriteEditor, lrg_editor_sprite_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static LrgEditorDocument *
doc_of (LrgEditorSpriteEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static gchar *
base_dir (LrgEditorSpriteEditor *self)
{
	const gchar *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	const gchar *dir = lrg_editor_document_get_base_dir (doc_of (self));

	if (path != NULL)
		return g_path_get_dirname (path);
	return g_strdup (dir != NULL ? dir : ".");
}

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static void
report (LrgEditorSpriteEditor *self,
        GError                *error)
{
	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* ==========================================================================
 * The model
 * ========================================================================== */

static LrgSpriteSheet *
parse_doc (LrgEditorSpriteEditor  *self,
           GError                **error)
{
	g_autofree gchar *text = lrg_editor_document_to_data (doc_of (self), NULL);
	const gchar      *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	g_autofree gchar *source = (path != NULL) ? g_path_get_basename (path) : g_strdup ("sprite");

	return lrg_sprite_sheet_new_from_rsprite_data (text != NULL ? text : "", -1, source, error);
}

static void
sync_image (LrgEditorSpriteEditor *self)
{
	const gchar      *image = (self->sheet != NULL) ? lrg_sprite_sheet_get_texture_path (self->sheet) : NULL;
	g_autofree gchar *abs = NULL;
	GStatBuf          st;
	gint64            mtime = 0;

	if (image != NULL && *image != '\0')
	{
		g_autofree gchar *dir = base_dir (self);

		abs = g_path_is_absolute (image) ? g_strdup (image) : g_build_filename (dir, image, NULL);
	}
	if (abs != NULL && g_stat (abs, &st) == 0)
		mtime = (gint64) st.st_mtime;
	if (g_strcmp0 (abs, self->image_path) == 0 && mtime == self->img_mtime &&
	    (self->tex != NULL || !IsWindowReady () || abs == NULL))
		return;
	g_clear_object (&self->tex);
	g_free (self->image_path);
	self->image_path = g_strdup (abs);
	self->img_mtime = mtime;
	self->img_w = self->img_h = 0;
	if (abs == NULL || mtime == 0)
		return;
	if (IsWindowReady ())
	{
		GrlTexture *tex = grl_texture_new_from_file (abs);

		if (tex != NULL && grl_texture_is_valid (tex))
		{
			self->tex = tex;
			self->img_w = grl_texture_get_width (tex);
			self->img_h = grl_texture_get_height (tex);
			return;
		}
		g_clear_object (&tex);
	}
	{
		Image img = LoadImage (abs);

		self->img_w = img.width;
		self->img_h = img.height;
		UnloadImage (img);
	}
}

static LrgSpriteAnimation *
current_clip (LrgEditorSpriteEditor *self)
{
	return (self->sheet != NULL && self->clip != NULL) ? lrg_sprite_sheet_get_clip (self->sheet, self->clip) : NULL;
}

static void
load_model (LrgEditorSpriteEditor *self)
{
	g_autoptr(GError)   error = NULL;
	LrgSpriteAnimation *clip;

	g_clear_object (&self->sheet);
	g_clear_pointer (&self->parse_error, g_free);
	self->sheet = parse_doc (self, &error);
	if (self->sheet == NULL)
		self->parse_error = g_strdup (error != NULL ? error->message : "the sprite sheet does not load");
	if (self->sheet != NULL && self->selected >= (gint) lrg_sprite_sheet_get_frame_count (self->sheet))
		self->selected = (gint) lrg_sprite_sheet_get_frame_count (self->sheet) - 1;
	if (self->sheet != NULL && self->clip != NULL && lrg_sprite_sheet_get_clip (self->sheet, self->clip) == NULL)
		g_clear_pointer (&self->clip, g_free);
	if (self->sheet != NULL && self->clip == NULL && lrg_sprite_sheet_get_n_clips (self->sheet) > 0)
		self->clip = g_strdup (lrg_sprite_animation_get_name (lrg_sprite_sheet_get_clip_at (self->sheet, 0)));
	clip = current_clip (self);
	if (clip == NULL || self->position >= (gint) lrg_sprite_animation_get_n_frames (clip))
		self->position = (clip != NULL && lrg_sprite_animation_get_n_frames (clip) > 0)
		                 ? (gint) lrg_sprite_animation_get_n_frames (clip) - 1 : -1;
	sync_image (self);
	self->stale = TRUE;
}

static gboolean
commit (LrgEditorSpriteEditor  *self,
        LrgSpriteSheet         *sheet,
        const gchar            *label,
        guint                   gesture,
        GError                **error)
{
	g_autofree gchar *text = lrg_sprite_sheet_save_rsprite_to_data (sheet, NULL, error);
	g_autofree gchar *now = NULL;
	gboolean          ok;

	if (text == NULL)
		return FALSE;
	now = lrg_editor_document_to_data (doc_of (self), NULL);
	if (g_strcmp0 (now, text) == 0)
		return TRUE;
	/* a gesture's replaces merge into one step on their own; a group
	 * around each would keep them apart */
	if (gesture != 0)
		return lrg_editor_document_replace_gesture (doc_of (self), "", text, gesture, error);
	lrg_editor_document_begin_group (doc_of (self), label);
	ok = lrg_editor_document_replace_gesture (doc_of (self), "", text, 0, error);
	lrg_editor_document_end_group (doc_of (self));
	return ok;
}

static LrgSpriteAnimation *
find_clip (LrgSpriteSheet  *sheet,
           const gchar     *name,
           GError         **error)
{
	LrgSpriteAnimation *clip = (name != NULL) ? lrg_sprite_sheet_get_clip (sheet, name) : NULL;

	if (clip == NULL)
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no clip named \"%s\"",
		             name != NULL ? name : "");
	return clip;
}

static gboolean
check_frame (LrgSpriteSheet  *sheet,
             guint            frame,
             GError         **error)
{
	if (frame >= lrg_sprite_sheet_get_frame_count (sheet))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
		             "frame %u does not exist (the sheet has %u)", frame, lrg_sprite_sheet_get_frame_count (sheet));
		return FALSE;
	}
	return TRUE;
}

/* The number of grid frames (the first frames), 0 without a grid. */
static guint
grid_count (LrgSpriteSheet *sheet)
{
	guint count = 0;

	return lrg_sprite_sheet_get_grid (sheet, NULL, NULL, NULL, &count) ? count : 0;
}

/* ==========================================================================
 * Public edits: slicing and frames
 * ========================================================================== */

gboolean
lrg_editor_sprite_editor_slice_grid (LrgEditorSpriteEditor  *self,
                                     gint                    frame_width,
                                     gint                    frame_height,
                                     guint                   columns,
                                     guint                   rows,
                                     guint                   count,
                                     gint                    margin_x,
                                     gint                    margin_y,
                                     gint                    spacing_x,
                                     gint                    spacing_y,
                                     gfloat                  pivot_x,
                                     gfloat                  pivot_y,
                                     GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	gint                      iw, ih;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if (frame_width < 1 || frame_height < 1)
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		                     "a grid needs a frame size of at least 1 x 1");
		return FALSE;
	}
	if ((sheet = parse_doc (self, error)) == NULL)
		return FALSE;
	/* columns / rows that fit the image when not given */
	iw = self->img_w > 0 ? self->img_w : lrg_sprite_sheet_get_texture_width (sheet);
	ih = self->img_h > 0 ? self->img_h : lrg_sprite_sheet_get_texture_height (sheet);
	if (self->img_w > 0 && self->img_h > 0)
		lrg_sprite_sheet_set_texture_size (sheet, self->img_w, self->img_h);
	if (columns == 0)
		columns = (guint) MAX (1, (iw - margin_x + spacing_x) / MAX (1, frame_width + spacing_x));
	if (rows == 0)
		rows = (guint) MAX (1, (ih - margin_y + spacing_y) / MAX (1, frame_height + spacing_y));
	if (count > columns * rows)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		             "count %u is larger than %u columns x %u rows", count, columns, rows);
		return FALSE;
	}
	lrg_sprite_sheet_slice_grid (sheet, frame_width, frame_height, columns, rows, count, margin_x, margin_y, spacing_x,
	                             spacing_y, pivot_x, pivot_y);
	return commit (self, sheet, "Slice Grid", 0, error);
}

gboolean
lrg_editor_sprite_editor_slice_alpha (LrgEditorSpriteEditor  *self,
                                      guint8                  threshold,
                                      guint                   min_size,
                                      gfloat                  pivot_x,
                                      gfloat                  pivot_y,
                                      GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	Image                     img;
	gboolean                  ok;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if (self->image_path == NULL || !g_file_test (self->image_path, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_IO,
		                     "auto slicing needs the sheet's image on disk");
		return FALSE;
	}
	if ((sheet = parse_doc (self, error)) == NULL)
		return FALSE;
	img = LoadImage (self->image_path);
	if (img.data == NULL || img.width <= 0 || img.height <= 0)
	{
		UnloadImage (img);
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_IO, "cannot decode %s",
		             self->image_path);
		return FALSE;
	}
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	lrg_sprite_sheet_set_texture_size (sheet, img.width, img.height);
	lrg_sprite_sheet_slice_alpha (sheet, (const guint8 *) img.data, (gsize) img.width * (gsize) img.height * 4u,
	                              (guint) img.width, (guint) img.height, threshold, min_size, pivot_x, pivot_y);
	UnloadImage (img);
	ok = commit (self, sheet, "Auto Slice", 0, error);
	return ok;
}

gint
lrg_editor_sprite_editor_add_frame (LrgEditorSpriteEditor  *self,
                                    const gchar            *name,
                                    gint                    x,
                                    gint                    y,
                                    gint                    width,
                                    gint                    height,
                                    gfloat                  pivot_x,
                                    gfloat                  pivot_y,
                                    GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	guint                     index;
	LrgAtlasRegion           *region;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), -1);
	if (width < 1 || height < 1)
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		                     "a frame needs a size of at least 1 x 1");
		return -1;
	}
	if ((sheet = parse_doc (self, error)) == NULL)
		return -1;
	if (name != NULL && *name != '\0' && lrg_sprite_sheet_get_frame_by_name (sheet, name) != NULL)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_EXISTS, "frame name \"%s\" is taken",
		             name);
		return -1;
	}
	index = lrg_sprite_sheet_add_frame_rect (sheet, (name != NULL && *name != '\0') ? name : NULL, x, y, width, height);
	region = lrg_sprite_sheet_get_frame (sheet, index);
	if (region != NULL)
		lrg_atlas_region_set_pivot (region, pivot_x, pivot_y);
	if (!commit (self, sheet, "Add Frame", 0, error))
		return -1;
	self->selected = (gint) index;
	self->stale = TRUE;
	return (gint) index;
}

gboolean
lrg_editor_sprite_editor_set_frame (LrgEditorSpriteEditor  *self,
                                    guint                   index,
                                    const gchar            *name,
                                    const gint             *rect,
                                    const gfloat           *pivot,
                                    guint                   gesture,
                                    GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	LrgAtlasRegion           *old;
	LrgAtlasRegion           *fresh;
	gint                      r[4];
	gfloat                    p[2];

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if ((sheet = parse_doc (self, error)) == NULL || !check_frame (sheet, index, error))
		return FALSE;
	if (rect != NULL && index < grid_count (sheet))
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		                     "a grid frame's rectangle comes from the grid: re-slice or add an explicit frame");
		return FALSE;
	}
	if (rect != NULL && (rect[2] < 1 || rect[3] < 1))
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		                     "a frame needs a size of at least 1 x 1");
		return FALSE;
	}
	if (name != NULL && *name != '\0')
	{
		LrgAtlasRegion *other = lrg_sprite_sheet_get_frame_by_name (sheet, name);

		if (other != NULL && other != lrg_sprite_sheet_get_frame (sheet, index))
		{
			g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_EXISTS, "frame name \"%s\" is taken",
			             name);
			return FALSE;
		}
	}
	old = lrg_sprite_sheet_get_frame (sheet, index);
	r[0] = lrg_atlas_region_get_x (old);
	r[1] = lrg_atlas_region_get_y (old);
	r[2] = lrg_atlas_region_get_width (old);
	r[3] = lrg_atlas_region_get_height (old);
	p[0] = lrg_atlas_region_get_pivot_x (old);
	p[1] = lrg_atlas_region_get_pivot_y (old);
	if (rect != NULL)
		memcpy (r, rect, sizeof r);
	if (pivot != NULL)
		memcpy (p, pivot, sizeof p);
	fresh = lrg_atlas_region_new ((name != NULL && *name != '\0') ? name : lrg_atlas_region_get_name (old), r[0], r[1], r[2],
	                              r[3]);
	lrg_atlas_region_set_pivot (fresh, p[0], p[1]);
	lrg_sprite_sheet_replace_frame (sheet, index, fresh);   /* takes @fresh */
	return commit (self, sheet, pivot != NULL && rect == NULL && name == NULL ? "Move Pivot" : "Edit Frame", gesture, error);
}

gboolean
lrg_editor_sprite_editor_remove_frame (LrgEditorSpriteEditor  *self,
                                       guint                   index,
                                       GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	guint                     i, n, k;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if ((sheet = parse_doc (self, error)) == NULL || !check_frame (sheet, index, error))
		return FALSE;
	if (index < grid_count (sheet))
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		                     "grid frames go with the grid: re-slice to change them");
		return FALSE;
	}
	/* clips may not point at the removed frame; later frames move down */
	for (i = 0; i < lrg_sprite_sheet_get_n_clips (sheet); i++)
	{
		LrgSpriteAnimation *clip = lrg_sprite_sheet_get_clip_at (sheet, i);

		n = lrg_sprite_animation_get_n_frames (clip);
		for (k = n; k-- > 0;)
		{
			gfloat d;
			guint  f = lrg_sprite_animation_get_frame (clip, k, &d);

			if (f == index)
				lrg_sprite_animation_remove_frame (clip, k);
			else if (f > index)
				lrg_sprite_animation_set_frame (clip, k, f - 1, d);
		}
	}
	lrg_sprite_sheet_remove_frame (sheet, index);
	return commit (self, sheet, "Remove Frame", 0, error);
}

/* ==========================================================================
 * Public edits: clips
 * ========================================================================== */

gboolean
lrg_editor_sprite_editor_add_clip (LrgEditorSpriteEditor  *self,
                                   const gchar            *name,
                                   const guint            *frames,
                                   guint                   n_frames,
                                   gfloat                  fps,
                                   LrgSpriteLoopMode       loop,
                                   GError                **error)
{
	g_autoptr(LrgSpriteSheet)     sheet = NULL;
	g_autoptr(LrgSpriteAnimation) clip = NULL;
	guint                         i;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if (name == NULL || *name == '\0')
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		                     "a clip needs a name");
		return FALSE;
	}
	if ((sheet = parse_doc (self, error)) == NULL)
		return FALSE;
	if (lrg_sprite_sheet_get_clip (sheet, name) != NULL)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_EXISTS, "a clip named \"%s\" exists",
		             name);
		return FALSE;
	}
	clip = lrg_sprite_animation_new (name);
	lrg_sprite_animation_set_fps (clip, fps > 0.0f ? fps : 10.0f);
	lrg_sprite_animation_set_loop_mode (clip, loop);
	for (i = 0; i < n_frames; i++)
	{
		if (!check_frame (sheet, frames[i], error))
			return FALSE;
		lrg_sprite_animation_add_frame (clip, frames[i], 0.0f);
	}
	lrg_sprite_sheet_add_clip (sheet, clip);
	if (!commit (self, sheet, "Add Clip", 0, error))
		return FALSE;
	g_free (self->clip);
	self->clip = g_strdup (name);
	self->position = n_frames > 0 ? 0 : -1;
	self->time = 0.0f;
	self->stale = TRUE;
	return TRUE;
}

gboolean
lrg_editor_sprite_editor_remove_clip (LrgEditorSpriteEditor  *self,
                                      const gchar            *name,
                                      GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if ((sheet = parse_doc (self, error)) == NULL || find_clip (sheet, name, error) == NULL)
		return FALSE;
	lrg_sprite_sheet_remove_clip (sheet, name);
	return commit (self, sheet, "Remove Clip", 0, error);
}

gboolean
lrg_editor_sprite_editor_set_clip (LrgEditorSpriteEditor  *self,
                                   const gchar            *name,
                                   const gchar            *new_name,
                                   gfloat                  fps,
                                   gint                    loop,
                                   GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	LrgSpriteAnimation       *clip;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if ((sheet = parse_doc (self, error)) == NULL || (clip = find_clip (sheet, name, error)) == NULL)
		return FALSE;
	if (fps > 0.0f)
		lrg_sprite_animation_set_fps (clip, fps);
	if (loop >= 0 && loop <= LRG_SPRITE_LOOP_PING_PONG)
		lrg_sprite_animation_set_loop_mode (clip, (LrgSpriteLoopMode) loop);
	if (new_name != NULL && g_strcmp0 (new_name, name) != 0)
	{
		g_autoptr(LrgSpriteAnimation) keep = NULL;

		if (*new_name == '\0' || lrg_sprite_sheet_get_clip (sheet, new_name) != NULL)
		{
			g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_EXISTS,
			             "cannot rename to \"%s\" (empty or taken)", new_name);
			return FALSE;
		}
		/* the sheet keys clips by name: take it out, rename, put it back */
		keep = g_object_ref (clip);
		lrg_sprite_sheet_remove_clip (sheet, name);
		lrg_sprite_animation_set_name (keep, new_name);
		lrg_sprite_sheet_add_clip (sheet, keep);
	}
	if (!commit (self, sheet, "Edit Clip", 0, error))
		return FALSE;
	if (new_name != NULL && g_strcmp0 (self->clip, name) == 0)
	{
		g_free (self->clip);
		self->clip = g_strdup (new_name);
	}
	return TRUE;
}

gboolean
lrg_editor_sprite_editor_insert_clip_frame (LrgEditorSpriteEditor  *self,
                                            const gchar            *clip_name,
                                            gint                    position,
                                            guint                   frame,
                                            gfloat                  duration,
                                            GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	LrgSpriteAnimation       *clip;
	guint                     n;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if ((sheet = parse_doc (self, error)) == NULL || (clip = find_clip (sheet, clip_name, error)) == NULL ||
	    !check_frame (sheet, frame, error))
		return FALSE;
	n = lrg_sprite_animation_get_n_frames (clip);
	if (position < 0 || (guint) position > n)
		position = (gint) n;
	lrg_sprite_animation_insert_frame (clip, (guint) position, frame, MAX (0.0f, duration));
	if (!commit (self, sheet, "Add Clip Frame", 0, error))
		return FALSE;
	if (g_strcmp0 (clip_name, self->clip) == 0)
		self->position = position;
	return TRUE;
}

gboolean
lrg_editor_sprite_editor_set_clip_frame (LrgEditorSpriteEditor  *self,
                                         const gchar            *clip_name,
                                         guint                   position,
                                         gint                    frame,
                                         gfloat                  duration,
                                         GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	LrgSpriteAnimation       *clip;
	gfloat                    d;
	guint                     f;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if ((sheet = parse_doc (self, error)) == NULL || (clip = find_clip (sheet, clip_name, error)) == NULL)
		return FALSE;
	if (position >= lrg_sprite_animation_get_n_frames (clip))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "clip \"%s\" has no position %u",
		             clip_name, position);
		return FALSE;
	}
	f = lrg_sprite_animation_get_frame (clip, position, &d);
	if (frame >= 0)
	{
		if (!check_frame (sheet, (guint) frame, error))
			return FALSE;
		f = (guint) frame;
	}
	if (duration >= 0.0f)
		d = duration;
	lrg_sprite_animation_set_frame (clip, position, f, d);
	return commit (self, sheet, "Edit Clip Frame", 0, error);
}

gboolean
lrg_editor_sprite_editor_remove_clip_frame (LrgEditorSpriteEditor  *self,
                                            const gchar            *clip_name,
                                            guint                   position,
                                            GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	LrgSpriteAnimation       *clip;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if ((sheet = parse_doc (self, error)) == NULL || (clip = find_clip (sheet, clip_name, error)) == NULL)
		return FALSE;
	if (!lrg_sprite_animation_remove_frame (clip, position))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "clip \"%s\" has no position %u",
		             clip_name, position);
		return FALSE;
	}
	return commit (self, sheet, "Remove Clip Frame", 0, error);
}

gboolean
lrg_editor_sprite_editor_move_clip_frame (LrgEditorSpriteEditor  *self,
                                          const gchar            *clip_name,
                                          guint                   from,
                                          guint                   to,
                                          GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	LrgSpriteAnimation       *clip;
	gfloat                    d;
	guint                     f, n, e;
	GPtrArray                *names;
	GArray                   *where;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if ((sheet = parse_doc (self, error)) == NULL || (clip = find_clip (sheet, clip_name, error)) == NULL)
		return FALSE;
	n = lrg_sprite_animation_get_n_frames (clip);
	if (from >= n || to >= n)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "clip \"%s\" has %u positions",
		             clip_name, n);
		return FALSE;
	}
	if (from == to)
		return TRUE;
	/* events stay on their frame: remember them by the moved position */
	names = g_ptr_array_new_with_free_func (g_free);
	where = g_array_new (FALSE, FALSE, sizeof (guint));
	while (lrg_sprite_animation_get_n_events (clip) > 0)
	{
		guint ef;

		g_ptr_array_add (names, g_strdup (lrg_sprite_animation_get_event (clip, 0, &ef)));
		g_array_append_val (where, ef);
		lrg_sprite_animation_remove_event (clip, 0);
	}
	f = lrg_sprite_animation_get_frame (clip, from, &d);
	lrg_sprite_animation_remove_frame (clip, from);
	lrg_sprite_animation_insert_frame (clip, to, f, d);
	for (e = 0; e < names->len; e++)
	{
		guint p = g_array_index (where, guint, e);

		/* the position each event pointed at, after the move */
		if (p == from)
			p = to;
		else if (from < to && p > from && p <= to)
			p--;
		else if (from > to && p >= to && p < from)
			p++;
		lrg_sprite_animation_add_event (clip, p, g_ptr_array_index (names, e));
	}
	g_ptr_array_unref (names);
	g_array_unref (where);
	if (!commit (self, sheet, "Move Clip Frame", 0, error))
		return FALSE;
	if (g_strcmp0 (clip_name, self->clip) == 0)
		self->position = (gint) to;
	return TRUE;
}

gboolean
lrg_editor_sprite_editor_add_event (LrgEditorSpriteEditor  *self,
                                    const gchar            *clip_name,
                                    guint                   position,
                                    const gchar            *name,
                                    GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	LrgSpriteAnimation       *clip;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if (name == NULL || *name == '\0')
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		                     "an event needs a name");
		return FALSE;
	}
	if ((sheet = parse_doc (self, error)) == NULL || (clip = find_clip (sheet, clip_name, error)) == NULL)
		return FALSE;
	if (position >= lrg_sprite_animation_get_n_frames (clip))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "clip \"%s\" has no position %u",
		             clip_name, position);
		return FALSE;
	}
	lrg_sprite_animation_add_event (clip, position, name);
	return commit (self, sheet, "Add Event", 0, error);
}

gboolean
lrg_editor_sprite_editor_remove_event (LrgEditorSpriteEditor  *self,
                                       const gchar            *clip_name,
                                       guint                   position,
                                       const gchar            *name,
                                       GError                **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	LrgSpriteAnimation       *clip;
	guint                     i, frame;
	gboolean                  removed = FALSE;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	if ((sheet = parse_doc (self, error)) == NULL || (clip = find_clip (sheet, clip_name, error)) == NULL)
		return FALSE;
	for (i = lrg_sprite_animation_get_n_events (clip); i-- > 0;)
	{
		const gchar *ev = lrg_sprite_animation_get_event (clip, i, &frame);

		if (frame == position && (name == NULL || g_strcmp0 (ev, name) == 0))
		{
			lrg_sprite_animation_remove_event (clip, i);
			removed = TRUE;
		}
	}
	if (!removed)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no such event at position %u",
		             position);
		return FALSE;
	}
	return commit (self, sheet, "Remove Event", 0, error);
}

/* ==========================================================================
 * Getters, selection, preview
 * ========================================================================== */

LrgSpriteSheet *
lrg_editor_sprite_editor_get_sheet (LrgEditorSpriteEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), NULL);
	return self->sheet;
}

const gchar *
lrg_editor_sprite_editor_get_parse_error (LrgEditorSpriteEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), NULL);
	return self->parse_error;
}

gint
lrg_editor_sprite_editor_get_selected_frame (LrgEditorSpriteEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), -1);
	return self->selected;
}

void
lrg_editor_sprite_editor_set_selected_frame (LrgEditorSpriteEditor *self,
                                             gint                   index)
{
	g_return_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self));

	if (self->sheet == NULL || index < 0)
		index = -1;
	else
		index = MIN (index, (gint) lrg_sprite_sheet_get_frame_count (self->sheet) - 1);
	if (index != self->selected)
	{
		self->selected = index;
		self->stale = TRUE;
	}
}

const gchar *
lrg_editor_sprite_editor_get_selected_clip (LrgEditorSpriteEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), NULL);
	return self->clip;
}

gboolean
lrg_editor_sprite_editor_select_clip (LrgEditorSpriteEditor *self,
                                      const gchar           *name)
{
	LrgSpriteAnimation *clip;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);

	if (name == NULL)
	{
		g_clear_pointer (&self->clip, g_free);
		self->position = -1;
		self->stale = TRUE;
		return TRUE;
	}
	if (self->sheet == NULL || (clip = lrg_sprite_sheet_get_clip (self->sheet, name)) == NULL)
		return FALSE;
	if (g_strcmp0 (name, self->clip) != 0)
	{
		g_free (self->clip);
		self->clip = g_strdup (name);
		self->position = lrg_sprite_animation_get_n_frames (clip) > 0 ? 0 : -1;
		self->time = 0.0f;
	}
	self->stale = TRUE;
	return TRUE;
}

void
lrg_editor_sprite_editor_set_playing (LrgEditorSpriteEditor *self,
                                      gboolean               playing)
{
	g_return_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self));
	self->playing = playing;
	if (self->play_chip != NULL)
	{
		editor_chip_set_checked (self->play_chip, playing);
		editor_chip_set_icon (self->play_chip, playing ? LRG_ICON_PAUSE : LRG_ICON_PLAY);
		editor_chip_set_label (self->play_chip, playing ? "Pause" : "Play");
	}
}

gboolean
lrg_editor_sprite_editor_get_playing (LrgEditorSpriteEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), FALSE);
	return self->playing;
}

void
lrg_editor_sprite_editor_seek (LrgEditorSpriteEditor *self,
                               gfloat                 time)
{
	g_return_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self));
	self->time = MAX (0.0f, time);
}

gfloat
lrg_editor_sprite_editor_get_time (LrgEditorSpriteEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), 0.0f);
	return self->time;
}

gint
lrg_editor_sprite_editor_get_preview_frame (LrgEditorSpriteEditor *self,
                                            guint                 *position)
{
	LrgSpriteAnimation *clip;
	guint               pos;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), -1);

	if (position != NULL)
		*position = 0;
	clip = current_clip (self);
	if (clip == NULL || lrg_sprite_animation_get_n_frames (clip) == 0)
		return -1;
	pos = lrg_sprite_animation_sample (clip, self->time, NULL);
	if (position != NULL)
		*position = pos;
	return (gint) lrg_sprite_animation_get_frame (clip, pos, NULL);
}

/* ==========================================================================
 * The slicer view
 * ========================================================================== */

static void
view_transform (LrgEditorSpriteEditor *self,
                gfloat                 x,
                gfloat                 y,
                gfloat                 width,
                gfloat                 height,
                gfloat                *ox,
                gfloat                *oy,
                gfloat                *scale)
{
	gfloat iw = (gfloat) (self->img_w > 0 ? self->img_w
	                      : (self->sheet != NULL ? MAX (1, lrg_sprite_sheet_get_texture_width (self->sheet)) : 64));
	gfloat ih = (gfloat) (self->img_h > 0 ? self->img_h
	                      : (self->sheet != NULL ? MAX (1, lrg_sprite_sheet_get_texture_height (self->sheet)) : 64));

	*scale = MAX (0.05f, MIN ((width - 24.0f) / MAX (1.0f, iw), (height - 24.0f) / MAX (1.0f, ih)));
	*ox = x + (width - iw * *scale) * 0.5f;
	*oy = y + (height - ih * *scale) * 0.5f;
}

void
lrg_editor_sprite_editor_image_to_canvas (LrgEditorSpriteEditor *self,
                                          gfloat                 image_x,
                                          gfloat                 image_y,
                                          gfloat                *canvas_x,
                                          gfloat                *canvas_y)
{
	LrgWidget *w;
	gfloat     ox, oy, s;

	g_return_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self));
	g_return_if_fail (canvas_x != NULL && canvas_y != NULL);

	w = self->view;
	view_transform (self, lrg_widget_get_world_x (w), lrg_widget_get_world_y (w), lrg_widget_get_width (w),
	                lrg_widget_get_height (w), &ox, &oy, &s);
	*canvas_x = ox + image_x * s;
	*canvas_y = oy + image_y * s;
}

static void
view_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont               *font = lrg_theme_get_ui_font (theme);
	gfloat                 fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor        *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	const GrlColor        *warn = lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING);
	LrgSpriteAnimation    *clip = current_clip (self);
	gfloat                 ox, oy, s;
	guint                  i, n;
	GrlColor               c;

	lrg_paint_list_rect (list, x, y, width, height, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	if (self->sheet == NULL)
	{
		lrg_paint_list_text (list, font, self->parse_error != NULL ? self->parse_error : "Nothing to show", x + 12.0f,
		                     y + 12.0f, fs, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
		return;
	}
	view_transform (self, x, y, width, height, &ox, &oy, &s);
	lrg_paint_list_push_clip (list, x, y, width, height);
	/* a checker under the image shows transparency */
	{
		gfloat iw = (gfloat) MAX (self->img_w, 1) * s, ih = (gfloat) MAX (self->img_h, 1) * s, cs = 12.0f, cx, cy;

		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT), 1.0f, &c);
		lrg_paint_list_rect (list, ox, oy, iw, ih, 0.0f, &c);
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.25f, &c);
		for (cy = 0.0f; cy < ih; cy += cs)
			for (cx = ((gint) (cy / cs) % 2 == 0) ? 0.0f : cs; cx < iw; cx += 2.0f * cs)
				lrg_paint_list_rect (list, ox + cx, oy + cy, MIN (cs, iw - cx), MIN (cs, ih - cy), 0.0f, &c);
	}
	if (self->tex != NULL)
	{
		GrlRectangle all = { 0.0f, 0.0f, (gfloat) self->img_w, (gfloat) self->img_h };
		GrlColor     white = { 255, 255, 255, 255 };

		lrg_paint_list_image (list, self->tex, &all, ox, oy, (gfloat) self->img_w * s, (gfloat) self->img_h * s, 0.0f,
		                      &white);
	}

	n = lrg_sprite_sheet_get_frame_count (self->sheet);
	for (i = 0; i < n; i++)
	{
		LrgAtlasRegion   *r = lrg_sprite_sheet_get_frame (self->sheet, i);
		gfloat            rx, ry, rw, rh, pxp, pyp;
		gboolean          in_clip = FALSE;
		g_autofree gchar *num = g_strdup_printf ("%u", i);
		guint             k;

		if (r == NULL)
			continue;
		rx = ox + (gfloat) lrg_atlas_region_get_x (r) * s;
		ry = oy + (gfloat) lrg_atlas_region_get_y (r) * s;
		rw = (gfloat) lrg_atlas_region_get_width (r) * s;
		rh = (gfloat) lrg_atlas_region_get_height (r) * s;
		for (k = 0; clip != NULL && k < lrg_sprite_animation_get_n_frames (clip); k++)
			if (lrg_sprite_animation_get_frame (clip, k, NULL) == i)
				in_clip = TRUE;
		lrg_paint_color_with_alpha (in_clip ? accent : lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED),
		                            in_clip ? 0.9f : 0.6f, &c);
		lrg_paint_list_border (list, rx, ry, rw, rh, 0.0f, in_clip ? 1.5f : 1.0f, &c);
		if (rw >= 18.0f)
		{
			GrlColor paper = { 17, 17, 27, 170 };
			GrlColor ink = { 230, 230, 240, 255 };

			lrg_paint_list_rect (list, rx + 1.0f, ry + 1.0f, fs * 0.6f * (gfloat) strlen (num) + 4.0f, fs + 2.0f, 2.0f,
			                     &paper);
			lrg_paint_list_text (list, font, num, rx + 3.0f, ry + 2.0f, fs * 0.9f, 0.0f, &ink);
		}
		/* the pivot as a small cross */
		pxp = rx + lrg_atlas_region_get_pivot_x (r) * s;
		pyp = ry + lrg_atlas_region_get_pivot_y (r) * s;
		lrg_paint_list_line (list, pxp - 4.0f, pyp, pxp + 4.0f, pyp, 1.5f, warn);
		lrg_paint_list_line (list, pxp, pyp - 4.0f, pxp, pyp + 4.0f, 1.5f, warn);
	}
	if (self->selected >= 0 && (guint) self->selected < n)
	{
		LrgAtlasRegion *r = lrg_sprite_sheet_get_frame (self->sheet, (guint) self->selected);
		gfloat          rx = ox + (gfloat) lrg_atlas_region_get_x (r) * s;
		gfloat          ry = oy + (gfloat) lrg_atlas_region_get_y (r) * s;

		lrg_paint_list_border (list, rx - 2.0f, ry - 2.0f, (gfloat) lrg_atlas_region_get_width (r) * s + 4.0f,
		                       (gfloat) lrg_atlas_region_get_height (r) * s + 4.0f, 3.0f, 2.5f, accent);
		lrg_paint_list_circle (list, rx + lrg_atlas_region_get_pivot_x (r) * s, ry + lrg_atlas_region_get_pivot_y (r) * s,
		                       6.0f, warn);
	}
	if (self->dragging_rect)
	{
		lrg_paint_color_with_alpha (accent, 0.25f, &c);
		lrg_paint_list_rect (list, MIN (self->drag_x0, self->drag_x1), MIN (self->drag_y0, self->drag_y1),
		                     fabsf (self->drag_x1 - self->drag_x0), fabsf (self->drag_y1 - self->drag_y0), 0.0f, &c);
		lrg_paint_list_border (list, MIN (self->drag_x0, self->drag_x1), MIN (self->drag_y0, self->drag_y1),
		                       fabsf (self->drag_x1 - self->drag_x0), fabsf (self->drag_y1 - self->drag_y0), 0.0f, 1.5f,
		                       accent);
	}
	lrg_paint_list_pop_clip (list);
}

gint
lrg_editor_sprite_editor_frame_at (LrgEditorSpriteEditor *self,
                                   gfloat                 x,
                                   gfloat                 y)
{
	LrgWidget *w;
	gfloat     ox, oy, s, px, py;
	guint      i;
	gint       hit = -1;

	g_return_val_if_fail (LRG_IS_EDITOR_SPRITE_EDITOR (self), -1);

	w = self->view;
	if (self->sheet == NULL || w == NULL)
		return -1;
	view_transform (self, lrg_widget_get_world_x (w), lrg_widget_get_world_y (w), lrg_widget_get_width (w),
	                lrg_widget_get_height (w), &ox, &oy, &s);
	px = (x - ox) / s;
	py = (y - oy) / s;
	for (i = 0; i < lrg_sprite_sheet_get_frame_count (self->sheet); i++)
	{
		LrgAtlasRegion *r = lrg_sprite_sheet_get_frame (self->sheet, i);

		if (r != NULL && px >= (gfloat) lrg_atlas_region_get_x (r) && py >= (gfloat) lrg_atlas_region_get_y (r) &&
		    px < (gfloat) (lrg_atlas_region_get_x (r) + lrg_atlas_region_get_width (r)) &&
		    py < (gfloat) (lrg_atlas_region_get_y (r) + lrg_atlas_region_get_height (r)))
			hit = (gint) i;
	}
	return hit;
}

/* Canvas -> image pixel (rounded). */
static void
canvas_to_image (LrgEditorSpriteEditor *self,
                 gfloat                 x,
                 gfloat                 y,
                 gfloat                *ix,
                 gfloat                *iy)
{
	LrgWidget *w = self->view;
	gfloat     ox, oy, s;

	view_transform (self, lrg_widget_get_world_x (w), lrg_widget_get_world_y (w), lrg_widget_get_width (w),
	                lrg_widget_get_height (w), &ox, &oy, &s);
	*ix = roundf ((x - ox) / s);
	*iy = roundf ((y - oy) / s);
}

static gboolean
on_pivot (LrgEditorSpriteEditor *self,
          gfloat                 x,
          gfloat                 y)
{
	LrgAtlasRegion *r;
	gfloat          cx, cy;

	if (self->sheet == NULL || self->selected < 0 ||
	    (r = lrg_sprite_sheet_get_frame (self->sheet, (guint) self->selected)) == NULL)
		return FALSE;
	lrg_editor_sprite_editor_image_to_canvas (self, (gfloat) lrg_atlas_region_get_x (r) + lrg_atlas_region_get_pivot_x (r),
	                                          (gfloat) lrg_atlas_region_get_y (r) + lrg_atlas_region_get_pivot_y (r),
	                                          &cx, &cy);
	return fabsf (cx - x) <= PIVOT_HIT && fabsf (cy - y) <= PIVOT_HIT;
}

static gboolean
view_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	gfloat                 ex = lrg_ui_event_get_x (event), ey = lrg_ui_event_get_y (event);
	g_autoptr(GError)      error = NULL;
	gfloat                 ix, iy;

	if (self->sheet == NULL)
		return FALSE;
	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (on_pivot (self, ex, ey))
		{
			self->dragging_pivot = TRUE;
			self->gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
			return TRUE;
		}
		{
			gint hit = lrg_editor_sprite_editor_frame_at (self, ex, ey);

			if (hit >= 0 && self->mode != MODE_MANUAL)
			{
				lrg_editor_sprite_editor_set_selected_frame (self, hit);
				return TRUE;
			}
			if (self->mode == MODE_MANUAL)
			{
				if (hit >= 0)
					lrg_editor_sprite_editor_set_selected_frame (self, hit);
				self->dragging_rect = TRUE;
				self->drag_x0 = self->drag_x1 = ex;
				self->drag_y0 = self->drag_y1 = ey;
			}
		}
		return TRUE;
	case LRG_UI_EVENT_MOUSE_MOVE:
		if (self->dragging_pivot && self->selected >= 0)
		{
			LrgAtlasRegion *r = lrg_sprite_sheet_get_frame (self->sheet, (guint) self->selected);
			gfloat          pivot[2];

			canvas_to_image (self, ex, ey, &ix, &iy);
			pivot[0] = ix - (gfloat) lrg_atlas_region_get_x (r);
			pivot[1] = iy - (gfloat) lrg_atlas_region_get_y (r);
			lrg_editor_sprite_editor_set_frame (self, (guint) self->selected, NULL, NULL, pivot, self->gesture, &error);
			report (self, error);
			return TRUE;
		}
		if (self->dragging_rect)
		{
			self->drag_x1 = ex;
			self->drag_y1 = ey;
			return TRUE;
		}
		return FALSE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (self->dragging_rect)
		{
			gfloat x0, y0, x1, y1;

			self->dragging_rect = FALSE;
			canvas_to_image (self, MIN (self->drag_x0, self->drag_x1), MIN (self->drag_y0, self->drag_y1), &x0, &y0);
			canvas_to_image (self, MAX (self->drag_x0, self->drag_x1), MAX (self->drag_y0, self->drag_y1), &x1, &y1);
			if (x1 - x0 >= 2.0f && y1 - y0 >= 2.0f)
			{
				lrg_editor_sprite_editor_add_frame (self, NULL, (gint) x0, (gint) y0, (gint) (x1 - x0), (gint) (y1 - y0),
				                                    roundf ((x1 - x0) * 0.5f), y1 - y0, &error);
				report (self, error);
			}
		}
		self->dragging_pivot = FALSE;
		self->gesture = 0;
		return FALSE;
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * The clip strip
 * ========================================================================== */

/* Width of one strip cell: its duration at 160 px per second, clamped. */
static gfloat
cell_width (LrgSpriteAnimation *clip,
            guint               position,
            gfloat              height)
{
	return CLAMP (lrg_sprite_animation_get_frame_duration (clip, position) * 240.0f, height * 0.6f, height * 2.5f);
}

static void
strip_measure (LrgCustomWidget     *widget,
               gfloat               avail_width,
               gfloat               avail_height,
               LrgCustomWidgetSize *size,
               gpointer             user_data)
{
	size->width = avail_width > 0.0f ? avail_width : 260.0f;
	size->height = 78.0f;
}

/* A sheet frame's image (or a placeholder) into a box. */
static void
paint_frame (LrgEditorSpriteEditor *self,
             LrgPaintList          *list,
             LrgTheme              *theme,
             guint                  frame,
             gfloat                 x,
             gfloat                 y,
             gfloat                 w,
             gfloat                 h)
{
	LrgAtlasRegion *r = (self->sheet != NULL) ? lrg_sprite_sheet_get_frame (self->sheet, frame) : NULL;
	gfloat          fw, fh, s;

	if (r == NULL)
		return;
	fw = (gfloat) MAX (1, lrg_atlas_region_get_width (r));
	fh = (gfloat) MAX (1, lrg_atlas_region_get_height (r));
	s = MIN (w / fw, h / fh);
	if (self->tex != NULL)
	{
		GrlRectangle src = { (gfloat) lrg_atlas_region_get_x (r), (gfloat) lrg_atlas_region_get_y (r), fw, fh };
		GrlColor     white = { 255, 255, 255, 255 };

		lrg_paint_list_image (list, self->tex, &src, x + (w - fw * s) * 0.5f, y + (h - fh * s) * 0.5f, fw * s, fh * s,
		                      0.0f, &white);
	}
	else
	{
		GrlColor c;

		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.35f, &c);
		lrg_paint_list_rect (list, x + (w - fw * s) * 0.5f, y + (h - fh * s) * 0.5f, fw * s, fh * s, 2.0f, &c);
	}
}

static void
strip_paint (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont               *font = lrg_theme_get_ui_font (theme);
	gfloat                 fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	LrgSpriteAnimation    *clip = current_clip (self);
	gfloat                 box = height - fs - 14.0f, cx = x;
	guint                  i, e, showing = 0;
	GrlColor               c;

	lrg_paint_list_rect (list, x, y, width, height, 4.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	if (clip == NULL || lrg_sprite_animation_get_n_frames (clip) == 0)
	{
		lrg_paint_list_text (list, font, clip == NULL ? "No clip: add one" : "Empty clip: add frames", x + 8.0f, y + 8.0f,
		                     fs, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		return;
	}
	lrg_editor_sprite_editor_get_preview_frame (self, &showing);
	lrg_paint_list_push_clip (list, x, y, width, height);
	for (i = 0; i < lrg_sprite_animation_get_n_frames (clip); i++)
	{
		gfloat            w = cell_width (clip, i, box);
		guint             frame = lrg_sprite_animation_get_frame (clip, i, NULL);
		g_autofree gchar *label = g_strdup_printf ("%u", frame);

		lrg_paint_list_rect (list, cx + 2.0f, y + 4.0f, w - 4.0f, box, 3.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
		paint_frame (self, list, theme, frame, cx + 4.0f, y + 6.0f, w - 8.0f, box - 4.0f);
		if ((gint) i == self->position)
			lrg_paint_list_border (list, cx + 1.0f, y + 3.0f, w - 2.0f, box + 2.0f, 3.0f, 2.0f,
			                       lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		if (i == showing && self->playing)
		{
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS), 0.9f, &c);
			lrg_paint_list_rect (list, cx + 2.0f, y + box + 5.0f, w - 4.0f, 3.0f, 1.0f, &c);
		}
		lrg_paint_list_text (list, font, label, cx + 4.0f, y + box + 8.0f, fs * 0.85f, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		/* events as flags on the top edge */
		for (e = 0; e < lrg_sprite_animation_get_n_events (clip); e++)
		{
			guint ef;

			lrg_sprite_animation_get_event (clip, e, &ef);
			if (ef == i)
			{
				lrg_paint_list_triangle (list, cx + 3.0f, y + 2.0f, cx + 13.0f, y + 2.0f, cx + 3.0f, y + 12.0f,
				                         lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
				break;
			}
		}
		cx += w;
	}
	lrg_paint_list_pop_clip (list);
}

static gboolean
strip_event (LrgCustomWidget  *widget,
             const LrgUIEvent *event,
             gpointer          user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	LrgSpriteAnimation    *clip = current_clip (self);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat                 fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                 box = lrg_widget_get_height (LRG_WIDGET (widget)) - fs - 14.0f;
	gfloat                 rx, cx = 0.0f;
	guint                  i;

	if (clip == NULL || lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN)
		return FALSE;
	rx = lrg_ui_event_get_x (event) - lrg_widget_get_world_x (LRG_WIDGET (widget));
	for (i = 0; i < lrg_sprite_animation_get_n_frames (clip); i++)
	{
		gfloat w = cell_width (clip, i, box);

		if (rx >= cx && rx < cx + w)
		{
			gfloat t = 0.0f;
			guint  k;

			self->position = (gint) i;
			/* scrub the preview to the start of that position */
			for (k = 0; k < i; k++)
				t += lrg_sprite_animation_get_frame_duration (clip, k);
			self->time = t + 1e-4f;
			self->stale = TRUE;
			return TRUE;
		}
		cx += w;
	}
	return TRUE;
}

/* ==========================================================================
 * The preview
 * ========================================================================== */

static void
preview_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont               *font = lrg_theme_get_ui_font (theme);
	gfloat                 fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	guint                  pos = 0;
	gint                   frame = lrg_editor_sprite_editor_get_preview_frame (self, &pos);
	LrgAtlasRegion        *r = (frame >= 0 && self->sheet != NULL) ? lrg_sprite_sheet_get_frame (self->sheet, (guint) frame)
	                                                                : NULL;
	gfloat                 gx = x + width * 0.5f, gy = y + height * 0.78f;
	g_autofree gchar      *label = NULL;
	GrlColor               c;

	lrg_paint_list_rect (list, x, y, width, height, 4.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	/* the ground line and the origin the pivot sits on */
	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.8f, &c);
	lrg_paint_list_line (list, x + 10.0f, gy, x + width - 10.0f, gy, 1.0f, &c);
	if (r != NULL)
	{
		gfloat fw = (gfloat) MAX (1, lrg_atlas_region_get_width (r));
		gfloat fh = (gfloat) MAX (1, lrg_atlas_region_get_height (r));
		gfloat s = MAX (1.0f, floorf (MIN ((width - 20.0f) / fw, (height * 0.7f) / fh)));

		if (self->tex != NULL)
		{
			GrlRectangle src = { (gfloat) lrg_atlas_region_get_x (r), (gfloat) lrg_atlas_region_get_y (r), fw, fh };
			GrlColor     white = { 255, 255, 255, 255 };

			lrg_paint_list_image (list, self->tex, &src, gx - lrg_atlas_region_get_pivot_x (r) * s,
			                      gy - lrg_atlas_region_get_pivot_y (r) * s, fw * s, fh * s, 0.0f, &white);
		}
		else
		{
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.4f, &c);
			lrg_paint_list_rect (list, gx - lrg_atlas_region_get_pivot_x (r) * s, gy - lrg_atlas_region_get_pivot_y (r) * s,
			                     fw * s, fh * s, 2.0f, &c);
		}
	}
	lrg_paint_list_circle (list, gx, gy, 3.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
	label = (frame >= 0) ? g_strdup_printf ("%s  %.2f s  position %u  frame %d", self->clip, self->time, pos, frame)
	                     : g_strdup ("Select a clip with frames to preview it");
	lrg_paint_list_text (list, font, label, x + 8.0f, y + 6.0f, fs, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
}

/* ==========================================================================
 * Widgets -> model
 * ========================================================================== */

static void
set_mode (LrgEditorSpriteEditor *self,
          SliceMode              mode)
{
	guint i;

	self->mode = mode;
	for (i = 0; i < 3; i++)
		editor_chip_set_checked (self->mode_chips[i], (guint) mode == i);
	lrg_widget_set_visible (self->grid_box, mode == MODE_GRID);
	lrg_widget_set_visible (self->alpha_box, mode == MODE_ALPHA);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_mode (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);

	set_mode (self, (SliceMode) GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (chip), "lrg-mode")));
}

static void
on_slice_grid (LrgWidget *chip,
               gpointer   user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	g_autoptr(GError)      error = NULL;

	lrg_editor_sprite_editor_slice_grid (self, (gint) lrg_spin_box_get_value (self->fw), (gint) lrg_spin_box_get_value (self->fh),
	                                     (guint) lrg_spin_box_get_value (self->cols),
	                                     (guint) lrg_spin_box_get_value (self->rows),
	                                     (guint) lrg_spin_box_get_value (self->count),
	                                     (gint) lrg_spin_box_get_value (self->mx), (gint) lrg_spin_box_get_value (self->my),
	                                     (gint) lrg_spin_box_get_value (self->sx), (gint) lrg_spin_box_get_value (self->sy),
	                                     (gfloat) lrg_spin_box_get_value (self->px),
	                                     (gfloat) lrg_spin_box_get_value (self->py), &error);
	report (self, error);
}

static void
on_slice_alpha (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	g_autoptr(GError)      error = NULL;

	lrg_editor_sprite_editor_slice_alpha (self, (guint8) lrg_spin_box_get_value (self->threshold),
	                                      (guint) lrg_spin_box_get_value (self->min_size),
	                                      (gfloat) lrg_spin_box_get_value (self->apx),
	                                      (gfloat) lrg_spin_box_get_value (self->apy), &error);
	report (self, error);
}

static void
on_frame_field (LrgSpinBox *spin,
                gdouble     value,
                gpointer    user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	g_autoptr(GError)      error = NULL;
	gfloat                 pivot[2];
	gint                   rect[4];

	if (self->syncing || self->sheet == NULL || self->selected < 0)
		return;
	pivot[0] = (gfloat) lrg_spin_box_get_value (self->fpx);
	pivot[1] = (gfloat) lrg_spin_box_get_value (self->fpy);
	rect[0] = (gint) lrg_spin_box_get_value (self->fx);
	rect[1] = (gint) lrg_spin_box_get_value (self->fy);
	rect[2] = (gint) lrg_spin_box_get_value (self->fwidth);
	rect[3] = (gint) lrg_spin_box_get_value (self->fheight);
	lrg_editor_sprite_editor_set_frame (self, (guint) self->selected, NULL,
	                                    (guint) self->selected >= grid_count (self->sheet) ? rect : NULL, pivot, 0, &error);
	report (self, error);
}

static void
on_frame_name (LrgTextInput *input,
               gpointer      user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	g_autoptr(GError)      error = NULL;

	if (self->sheet != NULL && self->selected >= 0)
		lrg_editor_sprite_editor_set_frame (self, (guint) self->selected, lrg_text_input_get_text (input), NULL, NULL, 0,
		                                    &error);
	report (self, error);
}

static void
on_frame_remove (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	g_autoptr(GError)      error = NULL;

	if (self->selected >= 0)
		lrg_editor_sprite_editor_remove_frame (self, (guint) self->selected, &error);
	report (self, error);
}

static void
on_clip_combo (LrgComboBox *combo,
               gpointer     user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);

	if (!self->syncing && lrg_combo_box_get_selected_text (combo) != NULL)
		lrg_editor_sprite_editor_select_clip (self, lrg_combo_box_get_selected_text (combo));
}

static void
on_clip_new (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	const gchar           *text = lrg_text_input_get_text (self->clip_new);
	g_autofree gchar      *name = NULL;
	g_autoptr(GError)      error = NULL;
	guint                  i = 1, first;

	if (self->sheet == NULL)
		return;
	if (text != NULL && *text != '\0')
		name = g_strdup (text);
	else
		do
			name = (g_free (name), g_strdup_printf ("clip%u", i++));
		while (lrg_sprite_sheet_get_clip (self->sheet, name) != NULL);
	first = self->selected >= 0 ? (guint) self->selected : 0;
	if (lrg_editor_sprite_editor_add_clip (self, name, &first, lrg_sprite_sheet_get_frame_count (self->sheet) > 0 ? 1 : 0,
	                                       10.0f, LRG_SPRITE_LOOP_LOOP, &error))
		lrg_text_input_set_text (self->clip_new, "");
	report (self, error);
}

static void
on_clip_remove (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	g_autoptr(GError)      error = NULL;

	if (self->clip != NULL)
		lrg_editor_sprite_editor_remove_clip (self, self->clip, &error);
	report (self, error);
}

static void
on_fps (LrgSpinBox *spin,
        gdouble     value,
        gpointer    user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	g_autoptr(GError)      error = NULL;

	if (!self->syncing && self->clip != NULL && value > 0.0)
		lrg_editor_sprite_editor_set_clip (self, self->clip, NULL, (gfloat) value, -1, &error);
	report (self, error);
}

static void
on_loop (LrgComboBox *combo,
         gpointer     user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	g_autoptr(GError)      error = NULL;

	if (!self->syncing && self->clip != NULL && lrg_combo_box_get_selected (combo) >= 0)
		lrg_editor_sprite_editor_set_clip (self, self->clip, NULL, 0.0f, lrg_combo_box_get_selected (combo), &error);
	report (self, error);
}

static void
on_strip_add (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	g_autoptr(GError)      error = NULL;

	if (self->clip != NULL && self->selected >= 0)
		lrg_editor_sprite_editor_insert_clip_frame (self, self->clip, self->position >= 0 ? self->position + 1 : -1,
		                                            (guint) self->selected, 0.0f, &error);
	report (self, error);
}

static void
on_strip_action (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	const gchar           *what = g_object_get_data (G_OBJECT (chip), "lrg-what");
	LrgSpriteAnimation    *clip = current_clip (self);
	g_autoptr(GError)      error = NULL;

	if (clip == NULL || self->position < 0)
		return;
	if (g_strcmp0 (what, "remove") == 0)
		lrg_editor_sprite_editor_remove_clip_frame (self, self->clip, (guint) self->position, &error);
	else if (g_strcmp0 (what, "left") == 0 && self->position > 0)
		lrg_editor_sprite_editor_move_clip_frame (self, self->clip, (guint) self->position, (guint) self->position - 1,
		                                          &error);
	else if (g_strcmp0 (what, "right") == 0 && (guint) self->position + 1 < lrg_sprite_animation_get_n_frames (clip))
		lrg_editor_sprite_editor_move_clip_frame (self, self->clip, (guint) self->position, (guint) self->position + 1,
		                                          &error);
	else if (g_strcmp0 (what, "event") == 0)
		lrg_editor_sprite_editor_add_event (self, self->clip, (guint) self->position,
		                                    lrg_text_input_get_text (self->event_name), &error);
	else if (g_strcmp0 (what, "unevent") == 0)
		lrg_editor_sprite_editor_remove_event (self, self->clip, (guint) self->position, NULL, &error);
	report (self, error);
}

static void
on_duration (LrgSpinBox *spin,
             gdouble     value,
             gpointer    user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);
	g_autoptr(GError)      error = NULL;

	if (!self->syncing && self->clip != NULL && self->position >= 0)
		lrg_editor_sprite_editor_set_clip_frame (self, self->clip, (guint) self->position, -1, (gfloat) MAX (0.0, value),
		                                         &error);
	report (self, error);
}

static void
on_play (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (user_data);

	lrg_editor_sprite_editor_set_playing (self, !self->playing);
}

/* ==========================================================================
 * Model -> widgets
 * ========================================================================== */

static void
set_spin (LrgSpinBox *spin,
          gdouble     value)
{
	LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (spin));
	LrgWidget *focus = LRG_IS_CANVAS (root) ? lrg_canvas_get_focused_widget (LRG_CANVAS (root)) : NULL;

	if (focus != NULL && (focus == LRG_WIDGET (spin) || lrg_widget_is_ancestor (focus, LRG_WIDGET (spin))))
		return;
	if (fabs (lrg_spin_box_get_value (spin) - value) > 1e-6)
		lrg_spin_box_set_value (spin, value);
}

/* A number of the document's grid: block (0 when missing). */
static gdouble
grid_value (LrgEditorSpriteEditor *self,
            const gchar           *path)
{
	const gchar *s = lrg_editor_document_get_scalar (doc_of (self), path);

	return s != NULL ? g_ascii_strtod (s, NULL) : 0.0;
}

static void
sync_widgets (LrgEditorSpriteEditor *self)
{
	LrgSpriteAnimation *clip = current_clip (self);
	GString            *sig = g_string_new (NULL);
	guint               i, n;

	self->stale = FALSE;
	self->syncing = TRUE;

	/* grid fields from the document's grid: block */
	if (self->sheet != NULL && grid_count (self->sheet) > 0)
	{
		set_spin (self->fw, grid_value (self, "/grid/frame_size/0"));
		set_spin (self->fh, grid_value (self, "/grid/frame_size/1"));
		set_spin (self->cols, grid_value (self, "/grid/columns"));
		set_spin (self->rows, grid_value (self, "/grid/rows"));
		set_spin (self->count, grid_value (self, "/grid/count"));
		set_spin (self->mx, grid_value (self, "/grid/margin/0"));
		set_spin (self->my, grid_value (self, "/grid/margin/1"));
		set_spin (self->sx, grid_value (self, "/grid/spacing/0"));
		set_spin (self->sy, grid_value (self, "/grid/spacing/1"));
		set_spin (self->px, grid_value (self, "/grid/pivot/0"));
		set_spin (self->py, grid_value (self, "/grid/pivot/1"));
	}

	/* the selected frame */
	if (self->sheet != NULL && self->selected >= 0)
	{
		LrgAtlasRegion   *r = lrg_sprite_sheet_get_frame (self->sheet, (guint) self->selected);
		gboolean          grid = (guint) self->selected < grid_count (self->sheet);
		g_autofree gchar *title = g_strdup_printf ("Frame %d  ·  %s", self->selected, grid ? "grid" : "explicit");
		LrgWidget        *root = lrg_widget_get_root (LRG_WIDGET (self));
		LrgWidget        *focus = LRG_IS_CANVAS (root) ? lrg_canvas_get_focused_widget (LRG_CANVAS (root)) : NULL;

		lrg_label_set_text (LRG_LABEL (self->frame_title), title);
		if (focus != LRG_WIDGET (self->frame_name))
			lrg_text_input_set_text (self->frame_name, lrg_atlas_region_get_name (r) != NULL ? lrg_atlas_region_get_name (r) : "");
		set_spin (self->fx, lrg_atlas_region_get_x (r));
		set_spin (self->fy, lrg_atlas_region_get_y (r));
		set_spin (self->fwidth, lrg_atlas_region_get_width (r));
		set_spin (self->fheight, lrg_atlas_region_get_height (r));
		set_spin (self->fpx, lrg_atlas_region_get_pivot_x (r));
		set_spin (self->fpy, lrg_atlas_region_get_pivot_y (r));
	}
	else
		lrg_label_set_text (LRG_LABEL (self->frame_title), "No frame selected");

	/* the clip combo */
	n = (self->sheet != NULL) ? lrg_sprite_sheet_get_n_clips (self->sheet) : 0;
	for (i = 0; i < n; i++)
		g_string_append_printf (sig, "%s|", lrg_sprite_animation_get_name (lrg_sprite_sheet_get_clip_at (self->sheet, i)));
	if (g_strcmp0 (sig->str, self->sig_combo) != 0)
	{
		g_free (self->sig_combo);
		self->sig_combo = g_strdup (sig->str);
		lrg_combo_box_remove_all (self->clip_combo);
		for (i = 0; i < n; i++)
			lrg_combo_box_append (self->clip_combo, lrg_sprite_animation_get_name (lrg_sprite_sheet_get_clip_at (self->sheet, i)));
	}
	g_string_free (sig, TRUE);
	for (i = 0; i < n; i++)
		if (g_strcmp0 (lrg_sprite_animation_get_name (lrg_sprite_sheet_get_clip_at (self->sheet, i)), self->clip) == 0 &&
		    lrg_combo_box_get_selected (self->clip_combo) != (gint) i)
			lrg_combo_box_set_selected (self->clip_combo, (gint) i);
	if (clip != NULL)
	{
		set_spin (self->fps, lrg_sprite_animation_get_fps (clip));
		if (lrg_combo_box_get_selected (self->loop) != (gint) lrg_sprite_animation_get_loop_mode (clip))
			lrg_combo_box_set_selected (self->loop, (gint) lrg_sprite_animation_get_loop_mode (clip));
		if (self->position >= 0 && (guint) self->position < lrg_sprite_animation_get_n_frames (clip))
		{
			gfloat d = 0.0f;

			lrg_sprite_animation_get_frame (clip, (guint) self->position, &d);
			set_spin (self->duration, d);
		}
	}
	self->syncing = FALSE;
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* ==========================================================================
 * LrgEditorDataEditor vfuncs
 * ========================================================================== */

static void
sprite_refresh (LrgEditorDataEditor *editor,
                const gchar         *path)
{
	load_model (LRG_EDITOR_SPRITE_EDITOR (editor));
}

static void
sprite_validate (LrgEditorDataEditor *editor,
                 GPtrArray           *issues)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (editor);
	guint                  i;

	if (self->sheet == NULL)
	{
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, "", 0, 0,
		                                                        self->parse_error != NULL ? self->parse_error
		                                                                                  : "the sheet does not load"));
		return;
	}
	if (self->image_path != NULL && !g_file_test (self->image_path, G_FILE_TEST_IS_REGULAR))
	{
		g_autofree gchar *msg = g_strdup_printf ("the image %s does not exist", lrg_sprite_sheet_get_texture_path (self->sheet));

		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, "/image", 0, 0, msg));
	}
	for (i = 0; i < lrg_sprite_sheet_get_n_clips (self->sheet); i++)
	{
		LrgSpriteAnimation *clip = lrg_sprite_sheet_get_clip_at (self->sheet, i);

		if (lrg_sprite_animation_get_n_frames (clip) == 0)
		{
			g_autofree gchar *msg = g_strdup_printf ("clip \"%s\" has no frames", lrg_sprite_animation_get_name (clip));
			g_autofree gchar *path = g_strdup_printf ("/animations/%u", i);

			g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path, 0, 0, msg));
		}
	}
	/* frames outside the image (known size only) */
	if (self->img_w > 0 && self->img_h > 0)
		for (i = 0; i < lrg_sprite_sheet_get_frame_count (self->sheet); i++)
		{
			LrgAtlasRegion *r = lrg_sprite_sheet_get_frame (self->sheet, i);

			if (lrg_atlas_region_get_x (r) + lrg_atlas_region_get_width (r) > self->img_w ||
			    lrg_atlas_region_get_y (r) + lrg_atlas_region_get_height (r) > self->img_h)
			{
				g_autofree gchar *msg = g_strdup_printf ("frame %u reaches outside the %d x %d image", i, self->img_w,
				                                         self->img_h);

				g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, "/frames", 0, 0, msg));
				break;
			}
		}
}

static gchar *
sprite_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (editor);

	return g_strdup_printf ("frame %d, clip %s, position %d, %s", self->selected, self->clip != NULL ? self->clip : "-",
	                        self->position, self->playing ? "playing" : "paused");
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static gboolean
lrg_editor_sprite_editor_step (LrgWidget *widget,
                               gfloat     delta_time)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (widget);
	gboolean               busy = LRG_WIDGET_CLASS (lrg_editor_sprite_editor_parent_class)->step (widget, delta_time);

	if (self->sheet != NULL)
		sync_image (self);
	if (self->playing)
	{
		self->time += delta_time;
		busy = TRUE;
	}
	if (self->stale)
		sync_widgets (self);
	return busy;
}

static LrgWidget *
heading (const gchar *text)
{
	LrgLabel       *label = lrg_label_new (text);
	const LrgInsets ins = { 0.0f, 8.0f, 0.0f, 2.0f };

	lrg_widget_add_class (LRG_WIDGET (label), "dim-label");
	lrg_widget_set_margin (LRG_WIDGET (label), &ins);
	return LRG_WIDGET (label);
}

static LrgWidget *
hrow (void)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());

	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	return row;
}

static LrgSpinBox *
spin_in (LrgEditorSpriteEditor *self,
         LrgWidget             *row,
         const gchar           *label,
         const gchar           *name,
         gdouble                min,
         gdouble                max,
         gdouble                value,
         GCallback              callback)
{
	LrgSpinBox *spin = lrg_spin_box_new_with_range (min, max, 1.0);

	if (label != NULL)
	{
		LrgWidget *l = LRG_WIDGET (lrg_label_new (label));

		lrg_widget_set_valign (l, LRG_ALIGN_CENTER);
		add_to (row, l);
	}
	lrg_spin_box_set_digits (spin, 0);
	lrg_spin_box_set_value (spin, value);
	lrg_widget_set_name (LRG_WIDGET (spin), name);
	lrg_widget_set_fill_weight (LRG_WIDGET (spin), 1.0f);
	lrg_widget_set_min_size (LRG_WIDGET (spin), 50.0f, 0.0f);
	if (callback != NULL)
		g_signal_connect (spin, "value-changed", callback, self);
	add_to (row, LRG_WIDGET (spin));
	return spin;
}

static LrgWidget *
chip_with (const gchar   *name,
           LrgIcon        icon,
           const gchar   *label,
           EditorChipFunc func,
           gpointer       self,
           const gchar   *what)
{
	LrgWidget *chip = editor_chip_new (name, icon, label, func, self);

	g_object_set_data (G_OBJECT (chip), "lrg-what", (gpointer) what);
	return chip;
}

static void
lrg_editor_sprite_editor_constructed (GObject *object)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (object);
	LrgEditorDataEditor   *base = LRG_EDITOR_DATA_EDITOR (object);
	static const LrgCustomWidgetFuncs view_funcs = { NULL, view_paint, NULL, view_event, { NULL } };
	static const LrgCustomWidgetFuncs strip_funcs = { strip_measure, strip_paint, NULL, strip_event, { NULL } };
	static const LrgCustomWidgetFuncs prev_funcs = { NULL, preview_paint, NULL, NULL, { NULL } };
	static const gchar * const mode_names[3] = { "Grid", "Auto", "Manual" };
	LrgSplitter           *split;
	LrgWidget             *left, *right, *scroll, *row, *chip;
	guint                  i;

	G_OBJECT_CLASS (lrg_editor_sprite_editor_parent_class)->constructed (object);

	/* Left: slicing modes, fields and the image */
	left = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (left), 6.0f);
	row = hrow ();
	for (i = 0; i < 3; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("sprite-mode-%s", mode_names[i]);

		self->mode_chips[i] = editor_chip_new (name, i == 0 ? LRG_ICON_MENU : i == 1 ? LRG_ICON_STAR : LRG_ICON_EDIT,
		                                       mode_names[i], on_mode, self);
		g_object_set_data (G_OBJECT (self->mode_chips[i]), "lrg-mode", GUINT_TO_POINTER (i));
		add_to (row, self->mode_chips[i]);
	}
	add_to (left, row);

	self->grid_box = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (self->grid_box), 4.0f);
	row = hrow ();
	self->fw = spin_in (self, row, "Frame", "sprite-grid-width", 1.0, 8192.0, 32.0, NULL);
	self->fh = spin_in (self, row, "x", "sprite-grid-height", 1.0, 8192.0, 32.0, NULL);
	self->count = spin_in (self, row, "Count", "sprite-grid-count", 0.0, 65536.0, 0.0, NULL);
	add_to (self->grid_box, row);
	row = hrow ();
	self->cols = spin_in (self, row, "Columns", "sprite-grid-columns", 0.0, 65536.0, 0.0, NULL);
	self->rows = spin_in (self, row, "Rows", "sprite-grid-rows", 0.0, 65536.0, 0.0, NULL);
	add_to (self->grid_box, row);
	row = hrow ();
	self->mx = spin_in (self, row, "Margin", "sprite-grid-margin-x", 0.0, 4096.0, 0.0, NULL);
	self->my = spin_in (self, row, NULL, "sprite-grid-margin-y", 0.0, 4096.0, 0.0, NULL);
	self->sx = spin_in (self, row, "Spacing", "sprite-grid-spacing-x", 0.0, 4096.0, 0.0, NULL);
	self->sy = spin_in (self, row, NULL, "sprite-grid-spacing-y", 0.0, 4096.0, 0.0, NULL);
	add_to (self->grid_box, row);
	row = hrow ();
	self->px = spin_in (self, row, "Pivot", "sprite-grid-pivot-x", -8192.0, 8192.0, 0.0, NULL);
	self->py = spin_in (self, row, "x", "sprite-grid-pivot-y", -8192.0, 8192.0, 0.0, NULL);
	add_to (row, editor_chip_new ("sprite-slice-grid", LRG_ICON_CHECK, "Slice", on_slice_grid, self));
	add_to (self->grid_box, row);
	add_to (left, self->grid_box);

	self->alpha_box = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (self->alpha_box), 4.0f);
	row = hrow ();
	self->threshold = spin_in (self, row, "Alpha >", "sprite-alpha-threshold", 0.0, 254.0, 0.0, NULL);
	self->min_size = spin_in (self, row, "Min size", "sprite-alpha-min", 1.0, 4096.0, 2.0, NULL);
	add_to (self->alpha_box, row);
	row = hrow ();
	self->apx = spin_in (self, row, "Pivot", "sprite-alpha-pivot-x", 0.0, 1.0, 0.5, NULL);
	lrg_spin_box_set_digits (self->apx, 2);
	lrg_spin_box_set_step (self->apx, 0.05);
	self->apy = spin_in (self, row, NULL, "sprite-alpha-pivot-y", 0.0, 1.0, 1.0, NULL);
	lrg_spin_box_set_digits (self->apy, 2);
	lrg_spin_box_set_step (self->apy, 0.05);
	add_to (row, editor_chip_new ("sprite-slice-alpha", LRG_ICON_STAR, "Auto-slice", on_slice_alpha, self));
	add_to (self->alpha_box, row);
	add_to (left, self->alpha_box);

	self->view = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->view, "sprite-view");
	lrg_widget_set_fill_weight (self->view, 1.0f);
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->view), 200.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->view), &view_funcs, self, NULL);
	add_to (left, self->view);
	row = hrow ();
	self->frame_title = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->frame_title, "sprite-frame-title");
	lrg_widget_set_valign (self->frame_title, LRG_ALIGN_CENTER);
	lrg_widget_set_min_size (self->frame_title, 140.0f, 0.0f);
	add_to (row, self->frame_title);
	self->frame_name = lrg_text_input_new_with_placeholder ("frame name");
	lrg_widget_set_name (LRG_WIDGET (self->frame_name), "sprite-frame-name");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->frame_name), 1.0f);
	g_signal_connect (self->frame_name, "submitted", G_CALLBACK (on_frame_name), self);
	add_to (row, LRG_WIDGET (self->frame_name));
	chip = editor_chip_new ("sprite-frame-remove", LRG_ICON_TRASH, NULL, on_frame_remove, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_to (row, chip);
	add_to (left, row);
	row = hrow ();
	self->fx = spin_in (self, row, "Rect", "sprite-frame-x", 0.0, 65536.0, 0.0, G_CALLBACK (on_frame_field));
	self->fy = spin_in (self, row, NULL, "sprite-frame-y", 0.0, 65536.0, 0.0, G_CALLBACK (on_frame_field));
	self->fwidth = spin_in (self, row, NULL, "sprite-frame-width", 1.0, 65536.0, 1.0, G_CALLBACK (on_frame_field));
	self->fheight = spin_in (self, row, NULL, "sprite-frame-height", 1.0, 65536.0, 1.0, G_CALLBACK (on_frame_field));
	add_to (left, row);
	row = hrow ();
	self->fpx = spin_in (self, row, "Pivot", "sprite-frame-pivot-x", -65536.0, 65536.0, 0.0, G_CALLBACK (on_frame_field));
	self->fpy = spin_in (self, row, "x", "sprite-frame-pivot-y", -65536.0, 65536.0, 0.0, G_CALLBACK (on_frame_field));
	add_to (left, row);


	/* Right: frame, clips, strip, preview */
	right = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (right, "sprite-properties");
	lrg_container_set_spacing (LRG_CONTAINER (right), 6.0f);
	add_to (right, heading ("Clips"));
	row = hrow ();
	self->clip_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->clip_combo), "sprite-clip");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->clip_combo), 1.0f);
	lrg_combo_box_set_placeholder (self->clip_combo, "(no clip)");
	g_signal_connect (self->clip_combo, "changed", G_CALLBACK (on_clip_combo), self);
	add_to (row, LRG_WIDGET (self->clip_combo));
	chip = editor_chip_new ("sprite-clip-remove", LRG_ICON_TRASH, NULL, on_clip_remove, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_to (row, chip);
	add_to (right, row);
	row = hrow ();
	self->clip_new = lrg_text_input_new_with_placeholder ("new clip name");
	lrg_widget_set_name (LRG_WIDGET (self->clip_new), "sprite-clip-name");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->clip_new), 1.0f);
	add_to (row, LRG_WIDGET (self->clip_new));
	add_to (row, editor_chip_new ("sprite-clip-new", LRG_ICON_PLUS, "New", on_clip_new, self));
	add_to (right, row);
	row = hrow ();
	self->fps = spin_in (self, row, "FPS", "sprite-clip-fps", 0.1, 240.0, 10.0, G_CALLBACK (on_fps));
	lrg_spin_box_set_digits (self->fps, 1);
	self->loop = lrg_combo_box_new ();
	for (i = 0; i < 3; i++)
		lrg_combo_box_append (self->loop, loop_labels[i]);
	lrg_combo_box_set_selected (self->loop, 1);
	lrg_widget_set_name (LRG_WIDGET (self->loop), "sprite-clip-loop");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->loop), 1.0f);
	g_signal_connect (self->loop, "changed", G_CALLBACK (on_loop), self);
	add_to (row, LRG_WIDGET (self->loop));
	add_to (right, row);

	self->strip = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->strip, "sprite-strip");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->strip), &strip_funcs, self, NULL);
	add_to (right, self->strip);
	row = hrow ();
	add_to (row, editor_chip_new ("sprite-strip-add", LRG_ICON_PLUS, "Frame", on_strip_add, self));
	add_to (row, chip_with ("sprite-strip-left", LRG_ICON_CHEVRON_LEFT, NULL, on_strip_action, self, "left"));
	add_to (row, chip_with ("sprite-strip-right", LRG_ICON_CHEVRON_RIGHT, NULL, on_strip_action, self, "right"));
	chip = chip_with ("sprite-strip-remove", LRG_ICON_TRASH, NULL, on_strip_action, self, "remove");
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_to (row, chip);
	self->duration = spin_in (self, row, "Duration", "sprite-strip-duration", 0.0, 60.0, 0.0, G_CALLBACK (on_duration));
	lrg_spin_box_set_digits (self->duration, 3);
	lrg_spin_box_set_step (self->duration, 0.01);
	add_to (right, row);
	row = hrow ();
	self->event_name = lrg_text_input_new_with_placeholder ("event name (e.g. footstep)");
	lrg_widget_set_name (LRG_WIDGET (self->event_name), "sprite-event-name");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->event_name), 1.0f);
	add_to (row, LRG_WIDGET (self->event_name));
	add_to (row, chip_with ("sprite-event-add", LRG_ICON_PLUS, "Event", on_strip_action, self, "event"));
	add_to (row, chip_with ("sprite-event-remove", LRG_ICON_MINUS, "Event", on_strip_action, self, "unevent"));
	add_to (right, row);

	add_to (right, heading ("Preview"));
	row = hrow ();
	self->play_chip = editor_chip_new ("sprite-play", LRG_ICON_PLAY, "Play", on_play, self);
	editor_chip_set_tint (self->play_chip, LRG_THEME_COLOR_SUCCESS);
	add_to (row, self->play_chip);
	add_to (right, row);
	self->preview = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->preview, "sprite-preview");
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->preview), 220.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->preview), &prev_funcs, self, NULL);
	add_to (right, self->preview);

	scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (scroll, "sprite-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), right);
	g_object_unref (right);

	split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (split), "sprite-split");
	lrg_splitter_set_start_child (split, left);
	g_object_unref (left);
	lrg_splitter_set_end_child (split, scroll);
	g_object_unref (scroll);
	lrg_splitter_set_default_fraction (split, 0.58f);
	lrg_splitter_set_fraction (split, 0.58f, FALSE);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (split));
	g_object_unref (split);

	load_model (self);
	if (self->sheet != NULL && lrg_sprite_sheet_get_frame_count (self->sheet) > 0)
		self->selected = 0;
	set_mode (self, (self->sheet != NULL && grid_count (self->sheet) == 0 && lrg_sprite_sheet_get_frame_count (self->sheet) > 0)
	                ? MODE_MANUAL : MODE_GRID);
	sync_widgets (self);
}

static void
lrg_editor_sprite_editor_dispose (GObject *object)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (object);

	g_clear_object (&self->sheet);
	g_clear_object (&self->tex);
	G_OBJECT_CLASS (lrg_editor_sprite_editor_parent_class)->dispose (object);
}

static void
lrg_editor_sprite_editor_finalize (GObject *object)
{
	LrgEditorSpriteEditor *self = LRG_EDITOR_SPRITE_EDITOR (object);

	g_free (self->parse_error);
	g_free (self->clip);
	g_free (self->image_path);
	g_free (self->sig_combo);
	G_OBJECT_CLASS (lrg_editor_sprite_editor_parent_class)->finalize (object);
}

static void
lrg_editor_sprite_editor_class_init (LrgEditorSpriteEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_sprite_editor_constructed;
	object_class->dispose = lrg_editor_sprite_editor_dispose;
	object_class->finalize = lrg_editor_sprite_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_sprite_editor_step;
	data_class->refresh = sprite_refresh;
	data_class->validate = sprite_validate;
	data_class->get_state = sprite_get_state;
}

static void
lrg_editor_sprite_editor_init (LrgEditorSpriteEditor *self)
{
	self->selected = -1;
	self->position = -1;
}

/* ==========================================================================
 * Construction, opening, creating
 * ========================================================================== */

LrgEditorSpriteEditor *
lrg_editor_sprite_editor_new (LrgEditorApp      *app,
                              LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_SPRITE_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorSpriteEditor *
lrg_editor_sprite_editor_open (LrgEditorApp  *app,
                               const gchar   *path,
                               GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_SPRITE_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a sprite sheet (.rsprite)", path);
		return NULL;
	}
	return LRG_EDITOR_SPRITE_EDITOR (e);
}

gboolean
lrg_editor_sprite_editor_create (LrgEditorApp  *app,
                                 const gchar   *path,
                                 const gchar   *image,
                                 gint           frame_width,
                                 gint           frame_height,
                                 const gchar   *name,
                                 GError       **error)
{
	g_autofree gchar          *abs = NULL;
	g_autofree gchar          *dir = NULL;
	g_autofree gchar          *base = NULL;
	g_autoptr(LrgSpriteSheet)  sheet = NULL;
	gint                       iw = 0, ih = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	abs = editor_app_project_path (app, path);
	if (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", abs);
		return FALSE;
	}
	dir = g_path_get_dirname (abs);
	g_mkdir_with_parents (dir, 0755);
	base = g_path_get_basename (abs);
	if (strrchr (base, '.') != NULL && strrchr (base, '.') != base)
		*strrchr (base, '.') = '\0';
	sheet = lrg_sprite_sheet_new (name != NULL && *name != '\0' ? name : base);
	if (image != NULL && *image != '\0')
	{
		g_autofree gchar *img_abs = editor_app_project_path (app, image);
		g_autoptr(GFile)  from = g_file_new_for_path (dir);
		g_autoptr(GFile)  to = g_file_new_for_path (img_abs);
		g_autofree gchar *rel = g_file_get_relative_path (from, to);
		Image             img = LoadImage (img_abs);

		iw = img.width;
		ih = img.height;
		UnloadImage (img);
		lrg_sprite_sheet_set_texture_path (sheet, rel != NULL ? rel : img_abs);
		if (iw > 0 && ih > 0)
			lrg_sprite_sheet_set_texture_size (sheet, iw, ih);
	}
	if (frame_width > 0 && frame_height > 0 && iw >= frame_width && ih >= frame_height)
		lrg_sprite_sheet_slice_grid (sheet, frame_width, frame_height, (guint) (iw / frame_width),
		                             (guint) (ih / frame_height), 0, 0, 0, 0, 0, 0.0f, 0.0f);
	return lrg_sprite_sheet_save_rsprite_to_file (sheet, abs, error);
}

/* ==========================================================================
 * IPC: sprite.*
 * ========================================================================== */

static JsonNode *
object_node (JsonObject *o)
{
	JsonNode *node = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (node, o);
	return node;
}

static void
invalid (GError     **error,
         const gchar *message)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, message);
}

static void
fail (GError **error,
      GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && cause->domain == LRG_EDITOR_DOCUMENT_ERROR && cause->code == LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND)
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause != NULL ? cause->message : "failed");
}

static const gchar *
param_string (JsonObject  *params,
              const gchar *name)
{
	JsonNode *n = (params != NULL) ? json_object_get_member (params, name) : NULL;

	return (n != NULL && JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
	       ? json_node_get_string (n) : NULL;
}

static gboolean
param_number (JsonObject  *params,
              const gchar *name,
              gdouble     *out)
{
	JsonNode *n = (params != NULL) ? json_object_get_member (params, name) : NULL;

	if (n == NULL || !JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	switch (json_node_get_value_type (n))
	{
	case G_TYPE_INT64:   *out = (gdouble) json_node_get_int (n); return TRUE;
	case G_TYPE_DOUBLE:  *out = json_node_get_double (n); return TRUE;
	case G_TYPE_BOOLEAN: *out = json_node_get_boolean (n) ? 1.0 : 0.0; return TRUE;
	default:             return FALSE;
	}
}

static JsonArray *
param_array (JsonObject  *params,
             const gchar *name)
{
	JsonNode *n = (params != NULL) ? json_object_get_member (params, name) : NULL;

	return (n != NULL && JSON_NODE_HOLDS_ARRAY (n)) ? json_node_get_array (n) : NULL;
}

/* Two numbers of an array param into @a / @b. */
static gboolean
param_pair (JsonObject  *params,
            const gchar *name,
            gdouble     *a,
            gdouble     *b)
{
	JsonArray *arr = param_array (params, name);

	if (arr == NULL || json_array_get_length (arr) != 2)
		return FALSE;
	*a = json_array_get_double_element (arr, 0);
	*b = json_array_get_double_element (arr, 1);
	return TRUE;
}

static JsonArray *
numbers (const gdouble *v,
         guint          n)
{
	JsonArray *arr = json_array_new ();
	guint      i;

	for (i = 0; i < n; i++)
		json_array_add_double_element (arr, v[i]);
	return arr;
}

static LrgEditorSpriteEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar           *path = param_string (params, "path");
	LrgEditorSpriteEditor *self = NULL;
	g_autoptr(GError)      err = NULL;

	if (path != NULL)
	{
		self = lrg_editor_sprite_editor_open (app, path, &err);
		if (self == NULL)
		{
			fail (error, err);
			return NULL;
		}
	}
	else
	{
		LrgEditorDataEditor *focus = lrg_editor_data_editor_find (app, NULL);
		GList               *all, *l;

		if (focus != NULL && LRG_IS_EDITOR_SPRITE_EDITOR (focus))
			self = LRG_EDITOR_SPRITE_EDITOR (focus);
		all = lrg_editor_data_editor_list (app);
		for (l = all; self == NULL && l != NULL; l = l->next)
			if (LRG_IS_EDITOR_SPRITE_EDITOR (l->data))
				self = LRG_EDITOR_SPRITE_EDITOR (l->data);
		g_list_free (all);
		if (self == NULL)
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			                     "No sprite editor is open (pass \"path\")");
			return NULL;
		}
	}
	return self;
}

static LrgEditorSpriteEditor *
ipc_loaded (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	LrgEditorSpriteEditor *self = ipc_editor (app, params, error);

	if (self != NULL && self->sheet == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "the sprite sheet does not load: %s",
		             self->parse_error != NULL ? self->parse_error : "unknown error");
		return NULL;
	}
	return self;
}

static JsonObject *
frame_json (LrgSpriteSheet *sheet,
            guint           index)
{
	JsonObject     *o = json_object_new ();
	LrgAtlasRegion *r = lrg_sprite_sheet_get_frame (sheet, index);
	gdouble         rect[4], pivot[2];

	json_object_set_int_member (o, "index", index);
	if (r == NULL)
		return o;
	json_object_set_string_member (o, "name", lrg_atlas_region_get_name (r) != NULL ? lrg_atlas_region_get_name (r) : "");
	rect[0] = lrg_atlas_region_get_x (r);
	rect[1] = lrg_atlas_region_get_y (r);
	rect[2] = lrg_atlas_region_get_width (r);
	rect[3] = lrg_atlas_region_get_height (r);
	pivot[0] = lrg_atlas_region_get_pivot_x (r);
	pivot[1] = lrg_atlas_region_get_pivot_y (r);
	json_object_set_array_member (o, "rect", numbers (rect, 4));
	json_object_set_array_member (o, "pivot", numbers (pivot, 2));
	json_object_set_boolean_member (o, "grid", index < grid_count (sheet));
	return o;
}

static JsonObject *
clip_json (LrgSpriteAnimation *clip)
{
	JsonObject *o = json_object_new ();
	JsonArray  *frames = json_array_new ();
	JsonArray  *events = json_array_new ();
	guint       i;

	json_object_set_string_member (o, "name", lrg_sprite_animation_get_name (clip));
	json_object_set_double_member (o, "fps", lrg_sprite_animation_get_fps (clip));
	json_object_set_string_member (o, "loop", loop_nicks[MIN ((guint) lrg_sprite_animation_get_loop_mode (clip), 2u)]);
	for (i = 0; i < lrg_sprite_animation_get_n_frames (clip); i++)
	{
		JsonObject *f = json_object_new ();
		gfloat      d;

		json_object_set_int_member (f, "frame", lrg_sprite_animation_get_frame (clip, i, &d));
		json_object_set_double_member (f, "duration", d);
		json_array_add_object_element (frames, f);
	}
	for (i = 0; i < lrg_sprite_animation_get_n_events (clip); i++)
	{
		JsonObject  *e = json_object_new ();
		guint        frame;
		const gchar *name = lrg_sprite_animation_get_event (clip, i, &frame);

		json_object_set_int_member (e, "frame", frame);
		json_object_set_string_member (e, "name", name);
		json_array_add_object_element (events, e);
	}
	json_object_set_array_member (o, "frames", frames);
	json_object_set_array_member (o, "events", events);
	json_object_set_double_member (o, "duration", lrg_sprite_animation_get_duration (clip));
	return o;
}

static JsonObject *
editor_json (LrgEditorSpriteEditor *self)
{
	JsonObject *o = json_object_new ();
	JsonArray  *frames = json_array_new ();
	JsonArray  *clips = json_array_new ();
	JsonArray  *issues = json_array_new ();
	GPtrArray  *list = lrg_editor_data_editor_get_issues (LRG_EDITOR_DATA_EDITOR (self));
	guint       i, position = 0;
	gint        fw, fh;
	guint       cols, count;

	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_boolean_member (o, "dirty", lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_int_member (o, "selected_frame", self->selected);
	if (self->clip != NULL)
		json_object_set_string_member (o, "selected_clip", self->clip);
	else
		json_object_set_null_member (o, "selected_clip");
	json_object_set_boolean_member (o, "playing", self->playing);
	json_object_set_double_member (o, "time", self->time);
	json_object_set_int_member (o, "preview_frame", lrg_editor_sprite_editor_get_preview_frame (self, &position));
	json_object_set_int_member (o, "preview_position", position);
	if (self->parse_error != NULL)
		json_object_set_string_member (o, "error", self->parse_error);
	for (i = 0; list != NULL && i < list->len; i++)
		json_array_add_string_element (issues, ((const LrgEditorDocumentIssue *) g_ptr_array_index (list, i))->message);
	json_object_set_array_member (o, "issues", issues);
	if (self->sheet == NULL)
		return o;
	json_object_set_string_member (o, "name", lrg_sprite_sheet_get_name (self->sheet) != NULL
	                                          ? lrg_sprite_sheet_get_name (self->sheet) : "");
	json_object_set_string_member (o, "image", lrg_sprite_sheet_get_texture_path (self->sheet) != NULL
	                                           ? lrg_sprite_sheet_get_texture_path (self->sheet) : "");
	{
		gdouble size[2] = { self->img_w > 0 ? self->img_w : lrg_sprite_sheet_get_texture_width (self->sheet),
		                    self->img_h > 0 ? self->img_h : lrg_sprite_sheet_get_texture_height (self->sheet) };

		json_object_set_array_member (o, "image_size", numbers (size, 2));
	}
	if (lrg_sprite_sheet_get_grid (self->sheet, &fw, &fh, &cols, &count))
	{
		JsonObject *g = json_object_new ();
		gdouble     fs[2] = { fw, fh };

		json_object_set_array_member (g, "frame_size", numbers (fs, 2));
		json_object_set_int_member (g, "columns", cols);
		json_object_set_int_member (g, "count", count);
		json_object_set_object_member (o, "grid", g);
	}
	else
		json_object_set_null_member (o, "grid");
	for (i = 0; i < lrg_sprite_sheet_get_frame_count (self->sheet); i++)
		json_array_add_object_element (frames, frame_json (self->sheet, i));
	json_object_set_array_member (o, "frames", frames);
	for (i = 0; i < lrg_sprite_sheet_get_n_clips (self->sheet); i++)
		json_array_add_object_element (clips, clip_json (lrg_sprite_sheet_get_clip_at (self->sheet, i)));
	json_object_set_array_member (o, "clips", clips);
	return o;
}

static JsonNode *
m_create (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = param_string (params, "path");
	g_autoptr(GError)  err = NULL;
	g_autofree gchar  *abs = NULL;
	gdouble            fw = 0.0, fh = 0.0;
	JsonObject        *o;

	if (path == NULL)
	{
		invalid (error, "Missing string parameter 'path'");
		return NULL;
	}
	param_pair (params, "frame_size", &fw, &fh);
	if (!lrg_editor_sprite_editor_create (app, path, param_string (params, "image"), (gint) fw, (gint) fh,
	                                      param_string (params, "name"), &err))
	{
		fail (error, err);
		return NULL;
	}
	abs = editor_app_project_path (app, path);
	o = json_object_new ();
	json_object_set_string_member (o, "path", abs);
	return object_node (o);
}

static JsonNode *
m_open (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSpriteEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject            *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "panel", lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorSpriteEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return self != NULL ? object_node (editor_json (self)) : NULL;
}

static JsonNode *
m_slice (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorSpriteEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	const gchar           *mode = param_string (params, "mode");
	g_autoptr(GError)      err = NULL;
	gboolean               ok;
	gdouble                a = 0.0, b = 0.0, v;

	if (self == NULL)
		return NULL;
	if (mode == NULL || g_strcmp0 (mode, "grid") == 0)
	{
		gdouble fw, fh, mx = 0.0, my = 0.0, sx = 0.0, sy = 0.0, px = 0.0, py = 0.0, cols = 0.0, rows = 0.0, count = 0.0;

		if (!param_pair (params, "frame_size", &fw, &fh))
		{
			invalid (error, "frame_size: expected [width, height]");
			return NULL;
		}
		param_pair (params, "margin", &mx, &my);
		param_pair (params, "spacing", &sx, &sy);
		param_pair (params, "pivot", &px, &py);
		param_number (params, "columns", &cols);
		param_number (params, "rows", &rows);
		param_number (params, "count", &count);
		ok = lrg_editor_sprite_editor_slice_grid (self, (gint) fw, (gint) fh, (guint) MAX (0.0, cols),
		                                          (guint) MAX (0.0, rows), (guint) MAX (0.0, count), (gint) mx, (gint) my,
		                                          (gint) sx, (gint) sy, (gfloat) px, (gfloat) py, &err);
	}
	else if (g_strcmp0 (mode, "alpha") == 0)
	{
		guint8 threshold = param_number (params, "threshold", &v) ? (guint8) CLAMP (v, 0.0, 254.0) : 0;
		guint  min_size = param_number (params, "min_size", &v) ? (guint) MAX (1.0, v) : 2;

		a = 0.5;
		b = 1.0;
		param_pair (params, "pivot", &a, &b);
		ok = lrg_editor_sprite_editor_slice_alpha (self, threshold, min_size, (gfloat) a, (gfloat) b, &err);
	}
	else
	{
		invalid (error, "mode: expected grid or alpha");
		return NULL;
	}
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	return object_node (editor_json (self));
}

static JsonNode *
m_frame (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorSpriteEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	const gchar           *op = param_string (params, "op");
	g_autoptr(GError)      err = NULL;
	JsonArray             *rect_a = param_array (params, "rect");
	gint                   rect[4] = { 0, 0, 0, 0 };
	gfloat                 pivot[2] = { 0.0f, 0.0f };
	gboolean               has_rect = FALSE, has_pivot = FALSE, ok = FALSE;
	gdouble                v, a, b;
	gint                   index = -1;

	if (self == NULL)
		return NULL;
	if (rect_a != NULL && json_array_get_length (rect_a) == 4)
	{
		guint i;

		for (i = 0; i < 4; i++)
			rect[i] = (gint) json_array_get_double_element (rect_a, i);
		has_rect = TRUE;
	}
	if (param_pair (params, "pivot", &a, &b))
	{
		pivot[0] = (gfloat) a;
		pivot[1] = (gfloat) b;
		has_pivot = TRUE;
	}
	if (param_number (params, "index", &v))
		index = (gint) v;
	if (g_strcmp0 (op, "add") == 0)
	{
		if (!has_rect)
		{
			invalid (error, "rect: expected [x, y, width, height]");
			return NULL;
		}
		index = lrg_editor_sprite_editor_add_frame (self, param_string (params, "name"), rect[0], rect[1], rect[2], rect[3],
		                                            pivot[0], pivot[1], &err);
		ok = index >= 0;
	}
	else if (g_strcmp0 (op, "set") == 0 && index >= 0)
		ok = lrg_editor_sprite_editor_set_frame (self, (guint) index, param_string (params, "name"), has_rect ? rect : NULL,
		                                         has_pivot ? pivot : NULL, 0, &err);
	else if (g_strcmp0 (op, "remove") == 0 && index >= 0)
		ok = lrg_editor_sprite_editor_remove_frame (self, (guint) index, &err);
	else if (g_strcmp0 (op, "select") == 0 && index >= 0)
	{
		lrg_editor_sprite_editor_set_selected_frame (self, index);
		ok = TRUE;
	}
	else
	{
		invalid (error, "op: expected add, set, remove or select (set / remove / select need index)");
		return NULL;
	}
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	if (g_strcmp0 (op, "remove") == 0 || self->sheet == NULL)
	{
		JsonObject *o = json_object_new ();

		json_object_set_boolean_member (o, "removed", TRUE);
		return object_node (o);
	}
	return object_node (frame_json (self->sheet, (guint) index));
}

/* A clip frame reference: an index or a frame name. */
static gboolean
parse_frame_ref (LrgSpriteSheet *sheet,
                 JsonNode       *n,
                 guint          *frame,
                 gfloat         *duration)
{
	*duration = 0.0f;
	if (JSON_NODE_HOLDS_OBJECT (n))
	{
		JsonObject *o = json_node_get_object (n);

		if (json_object_has_member (o, "duration"))
			*duration = (gfloat) json_object_get_double_member (o, "duration");
		n = json_object_get_member (o, "frame");
		if (n == NULL)
			return FALSE;
	}
	if (!JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	if (json_node_get_value_type (n) == G_TYPE_STRING)
	{
		guint i;

		for (i = 0; i < lrg_sprite_sheet_get_frame_count (sheet); i++)
			if (g_strcmp0 (lrg_atlas_region_get_name (lrg_sprite_sheet_get_frame (sheet, i)), json_node_get_string (n)) == 0)
			{
				*frame = i;
				return TRUE;
			}
		return FALSE;
	}
	*frame = (guint) json_node_get_int (n);
	return TRUE;
}

static gint
loop_from_nick (const gchar *nick)
{
	guint i;

	for (i = 0; nick != NULL && i < G_N_ELEMENTS (loop_nicks); i++)
		if (g_strcmp0 (nick, loop_nicks[i]) == 0)
			return (gint) i;
	return -1;
}

static JsonNode *
m_clip (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSpriteEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	const gchar           *op = param_string (params, "op");
	const gchar           *name = param_string (params, "name");
	g_autoptr(GError)      err = NULL;
	gboolean               ok = FALSE;
	gdouble                v;
	gfloat                 fps = 0.0f;
	gint                   loop = loop_from_nick (param_string (params, "loop"));
	LrgSpriteAnimation    *clip;

	if (self == NULL)
		return NULL;
	if (name == NULL)
	{
		invalid (error, "Missing string parameter 'name'");
		return NULL;
	}
	if (param_string (params, "loop") != NULL && loop < 0)
	{
		invalid (error, "loop: expected once, loop or ping-pong");
		return NULL;
	}
	if (param_number (params, "fps", &v))
		fps = (gfloat) v;
	if (g_strcmp0 (op, "add") == 0)
	{
		JsonArray          *frames = param_array (params, "frames");
		guint               n = (frames != NULL) ? json_array_get_length (frames) : 0, i;
		g_autofree guint   *idx = g_new0 (guint, n + 1);
		g_autofree gfloat  *dur = g_new0 (gfloat, n + 1);

		for (i = 0; i < n; i++)
			if (!parse_frame_ref (self->sheet, json_array_get_element (frames, i), &idx[i], &dur[i]))
			{
				invalid (error, "frames: expected frame indices, names or {frame, duration}");
				return NULL;
			}
		ok = lrg_editor_sprite_editor_add_clip (self, name, idx, n, fps > 0.0f ? fps : 10.0f,
		                                        loop >= 0 ? (LrgSpriteLoopMode) loop : LRG_SPRITE_LOOP_LOOP, &err);
		for (i = 0; ok && i < n; i++)
			if (dur[i] > 0.0f)
				ok = lrg_editor_sprite_editor_set_clip_frame (self, name, i, -1, dur[i], &err);
	}
	else if (g_strcmp0 (op, "remove") == 0)
	{
		ok = lrg_editor_sprite_editor_remove_clip (self, name, &err);
		if (ok)
		{
			JsonObject *o = json_object_new ();

			json_object_set_boolean_member (o, "removed", TRUE);
			return object_node (o);
		}
	}
	else if (g_strcmp0 (op, "set") == 0)
	{
		ok = lrg_editor_sprite_editor_set_clip (self, name, param_string (params, "new_name"), fps, loop, &err);
		if (ok && param_string (params, "new_name") != NULL)
			name = param_string (params, "new_name");
		if (ok && param_array (params, "frames") != NULL)
		{
			JsonArray *frames = param_array (params, "frames");
			guint      i;

			/* replace the frame list: clear, then insert in order */
			clip = lrg_sprite_sheet_get_clip (self->sheet, name);
			while (ok && clip != NULL && lrg_sprite_animation_get_n_frames (clip) > 0)
			{
				ok = lrg_editor_sprite_editor_remove_clip_frame (self, name, 0, &err);
				clip = lrg_sprite_sheet_get_clip (self->sheet, name);
			}
			for (i = 0; ok && i < json_array_get_length (frames); i++)
			{
				guint  f;
				gfloat d;

				if (!parse_frame_ref (self->sheet, json_array_get_element (frames, i), &f, &d))
				{
					invalid (error, "frames: expected frame indices, names or {frame, duration}");
					return NULL;
				}
				ok = lrg_editor_sprite_editor_insert_clip_frame (self, name, -1, f, d, &err);
			}
		}
	}
	else if (g_strcmp0 (op, "select") == 0)
	{
		ok = lrg_editor_sprite_editor_select_clip (self, name);
		if (!ok)
			g_set_error_literal (&err, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no such clip");
	}
	else
	{
		invalid (error, "op: expected add, remove, set or select");
		return NULL;
	}
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	clip = (self->sheet != NULL) ? lrg_sprite_sheet_get_clip (self->sheet, name) : NULL;
	return object_node (clip != NULL ? clip_json (clip) : json_object_new ());
}

static JsonNode *
m_clip_frame (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorSpriteEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	const gchar           *op = param_string (params, "op");
	const gchar           *name = param_string (params, "clip");
	g_autoptr(GError)      err = NULL;
	gboolean               ok;
	gdouble                v;
	gint                   index = -1, to = -1, frame = -1;
	gfloat                 duration = -1.0f;
	LrgSpriteAnimation    *clip;

	if (self == NULL)
		return NULL;
	if (name == NULL)
	{
		invalid (error, "Missing string parameter 'clip'");
		return NULL;
	}
	if (param_number (params, "index", &v))
		index = (gint) v;
	if (param_number (params, "to", &v))
		to = (gint) v;
	if (json_object_has_member (params, "frame"))
	{
		guint  f;
		gfloat d;

		if (!parse_frame_ref (self->sheet, json_object_get_member (params, "frame"), &f, &d))
		{
			invalid (error, "frame: expected a frame index or name");
			return NULL;
		}
		frame = (gint) f;
	}
	if (param_number (params, "duration", &v))
		duration = (gfloat) v;
	if (g_strcmp0 (op, "add") == 0 || g_strcmp0 (op, "insert") == 0)
	{
		if (frame < 0)
		{
			invalid (error, "Missing parameter 'frame'");
			return NULL;
		}
		ok = lrg_editor_sprite_editor_insert_clip_frame (self, name, g_strcmp0 (op, "add") == 0 ? -1 : index, (guint) frame,
		                                                 MAX (0.0f, duration), &err);
	}
	else if (g_strcmp0 (op, "remove") == 0 && index >= 0)
		ok = lrg_editor_sprite_editor_remove_clip_frame (self, name, (guint) index, &err);
	else if (g_strcmp0 (op, "move") == 0 && index >= 0 && to >= 0)
		ok = lrg_editor_sprite_editor_move_clip_frame (self, name, (guint) index, (guint) to, &err);
	else if (g_strcmp0 (op, "set") == 0 && index >= 0)
		ok = lrg_editor_sprite_editor_set_clip_frame (self, name, (guint) index, frame, duration, &err);
	else
	{
		invalid (error, "op: expected add, insert, remove, move or set (with index / to as needed)");
		return NULL;
	}
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	clip = (self->sheet != NULL) ? lrg_sprite_sheet_get_clip (self->sheet, name) : NULL;
	return object_node (clip != NULL ? clip_json (clip) : json_object_new ());
}

static JsonNode *
m_event (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorSpriteEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	const gchar           *op = param_string (params, "op");
	const gchar           *name = param_string (params, "clip");
	g_autoptr(GError)      err = NULL;
	gboolean               ok;
	gdouble                v;
	LrgSpriteAnimation    *clip;

	if (self == NULL)
		return NULL;
	if (name == NULL || !param_number (params, "frame", &v) || v < 0.0)
	{
		invalid (error, "Missing parameters 'clip' and 'frame' (a clip position)");
		return NULL;
	}
	if (g_strcmp0 (op, "add") == 0)
		ok = lrg_editor_sprite_editor_add_event (self, name, (guint) v, param_string (params, "name"), &err);
	else if (g_strcmp0 (op, "remove") == 0)
		ok = lrg_editor_sprite_editor_remove_event (self, name, (guint) v, param_string (params, "name"), &err);
	else
	{
		invalid (error, "op: expected add or remove");
		return NULL;
	}
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	clip = (self->sheet != NULL) ? lrg_sprite_sheet_get_clip (self->sheet, name) : NULL;
	return object_node (clip != NULL ? clip_json (clip) : json_object_new ());
}

static JsonNode *
m_play (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSpriteEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	JsonObject            *o;
	gdouble                v;
	guint                  position = 0;

	if (self == NULL)
		return NULL;
	if (param_string (params, "clip") != NULL && !lrg_editor_sprite_editor_select_clip (self, param_string (params, "clip")))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such clip");
		return NULL;
	}
	if (param_number (params, "time", &v))
		lrg_editor_sprite_editor_seek (self, (gfloat) v);
	if (json_object_has_member (params, "playing") && param_number (params, "playing", &v))
		lrg_editor_sprite_editor_set_playing (self, v != 0.0);
	o = json_object_new ();
	if (self->clip != NULL)
		json_object_set_string_member (o, "clip", self->clip);
	else
		json_object_set_null_member (o, "clip");
	json_object_set_boolean_member (o, "playing", self->playing);
	json_object_set_double_member (o, "time", self->time);
	json_object_set_int_member (o, "frame", lrg_editor_sprite_editor_get_preview_frame (self, &position));
	json_object_set_int_member (o, "position", position);
	return object_node (o);
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSpriteEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)      err = NULL;
	JsonObject            *o;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (self), &err))
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

void
lrg_editor_sprite_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "sprite.create", m_create },
		{ "sprite.open", m_open },
		{ "sprite.get", m_get },
		{ "sprite.slice", m_slice },
		{ "sprite.frame", m_frame },
		{ "sprite.clip", m_clip },
		{ "sprite.clip_frame", m_clip_frame },
		{ "sprite.event", m_event },
		{ "sprite.play", m_play },
		{ "sprite.save", m_save }
	};
	LrgEditorServer *server;
	guint            i;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));
	if (g_object_get_data (G_OBJECT (app), INSTALLED_KEY) != NULL)
		return;
	server = lrg_editor_app_get_server (app);
	if (server == NULL)
		return;
	g_object_set_data (G_OBJECT (app), INSTALLED_KEY, GINT_TO_POINTER (1));
	for (i = 0; i < G_N_ELEMENTS (methods); i++)
		lrg_editor_server_register_method (server, methods[i].name, methods[i].func, app, NULL);
}
