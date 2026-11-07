/* lrg-editor-hud-layout-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The HUD layout editor: a stage (a virtual game screen with the HUD
 * pieces of the file drawn where the game puts them) and a property grid.
 *
 * The file is the player-state format of LrgHudLayout:
 *
 *   version: 1
 *   pieces:
 *     health: { anchor: top-left, x: 16, y: 16 }
 *     minimap: { anchor: top-right, x: -16, y: 16, locked: true }
 *     quest: { hidden: true }
 *
 * A piece without an anchor has no placement of its own (the game's
 * default applies); the stage shows such pieces stacked up from the
 * bottom-left corner, under the placed ones, with a dashed outline until
 * they are dragged. Sizes are not part
 * of the format: the stage keeps a preview size per piece (160 x 48 until
 * resized).
 *
 * The file also holds EditorDataFields, the document-backed property
 * source shared by the four editors of this group (see the header).
 */

#include "config.h"

#define LRG_EDITOR_DATA_INTERNAL
#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-hud-layout-editor.h"
#include "../lrg-editor-console-panel.h"
#include "../../../hud/lrg-hud-layout.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-property-source.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../ui/lrg-theme.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-icons.h"
#include "../../../lrg-enums.h"
#include <math.h>
#include <string.h>

/* ==========================================================================
 * EditorDataFields: a property source over document paths
 * ========================================================================== */

typedef struct
{
	LrgPropertyInfo *info;   /* owns the pspec */
	gchar           *path;   /* YAML path, NULL for a virtual row */
} FieldRow;

struct _EditorDataFields
{
	GObject              parent_instance;

	LrgEditorDataEditor *editor;    /* weak */
	GPtrArray           *rows;      /* FieldRow */
	EditorDataFieldGet   get;
	EditorDataFieldSet   set;
	gpointer             user_data;
	guint                gesture;   /* the grid's open gesture, 0 outside */
};

static void editor_data_fields_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorDataFields, editor_data_fields, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE,
                                                      editor_data_fields_iface_init))

static void
field_row_free (gpointer data)
{
	FieldRow *row = data;

	lrg_property_info_unref (row->info);
	g_free (row->path);
	g_free (row);
}

static FieldRow *
fields_find (EditorDataFields *self,
             const gchar      *name)
{
	guint i;

	for (i = 0; i < self->rows->len; i++)
	{
		FieldRow *row = g_ptr_array_index (self->rows, i);

		if (g_strcmp0 (lrg_property_info_get_name (row->info), name) == 0)
			return row;
	}
	return NULL;
}

static GPtrArray *
fields_list_properties (LrgPropertySource *source)
{
	EditorDataFields *self = EDITOR_DATA_FIELDS (source);
	GPtrArray        *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	guint             i;

	for (i = 0; i < self->rows->len; i++)
		g_ptr_array_add (out, lrg_property_info_ref (((FieldRow *) g_ptr_array_index (self->rows, i))->info));
	return out;
}

/*
 * Reads a row: a virtual row asks its callback; a path row parses the
 * document's scalar into the row's type (an enum from its nick, numbers,
 * booleans) and falls back to the pspec default when the path is missing
 * or does not parse.
 */
static gboolean
fields_get_value (LrgPropertySource *source,
                  const gchar       *name,
                  GValue            *value)
{
	EditorDataFields  *self = EDITOR_DATA_FIELDS (source);
	FieldRow          *row = fields_find (self, name);
	GParamSpec        *pspec;
	LrgEditorDocument *doc;
	const gchar       *text;

	if (row == NULL)
		return FALSE;
	pspec = lrg_property_info_get_pspec (row->info);
	g_value_init (value, G_PARAM_SPEC_VALUE_TYPE (pspec));
	g_param_value_set_default (pspec, value);

	if (row->path == NULL)
		return self->get != NULL ? (self->get (name, value, self->user_data), TRUE) : TRUE;

	if (self->editor == NULL)
		return TRUE;
	doc = lrg_editor_data_editor_get_document (self->editor);
	text = lrg_editor_document_get_scalar (doc, row->path);
	if (text != NULL)
	{
		GValue parsed = G_VALUE_INIT;

		g_value_init (&parsed, G_PARAM_SPEC_VALUE_TYPE (pspec));
		if (G_VALUE_HOLDS_STRING (&parsed))
			g_value_set_string (&parsed, text);
		else if (!lrg_property_value_from_string (&parsed, text))
			g_param_value_set_default (pspec, &parsed);
		g_value_copy (&parsed, value);
		g_value_unset (&parsed);
	}
	return TRUE;
}

/* Writes a row through the document with the open gesture's id. */
static gboolean
fields_set_value (LrgPropertySource *source,
                  const gchar       *name,
                  const GValue      *value)
{
	EditorDataFields  *self = EDITOR_DATA_FIELDS (source);
	FieldRow          *row = fields_find (self, name);
	g_autoptr(GError)  error = NULL;
	gboolean           ok;

	if (row == NULL || self->editor == NULL)
		return FALSE;
	/* the editor may swap the grid's source while this write refreshes it */
	g_object_ref (self);
	if (row->path == NULL)
		ok = self->set != NULL && self->set (name, value, self->gesture, self->user_data);
	else
	{
		ok = lrg_editor_document_set_value (lrg_editor_data_editor_get_document (self->editor), row->path, value,
		                                    self->gesture, &error);
		if (!ok)
			lrg_editor_data_editor_report_error (self->editor, error);
	}
	if (ok)
		lrg_property_source_emit_changed (source, name);
	g_object_unref (self);
	return ok;
}

/* A path row is "default" while the file does not write it. */
static LrgPropertyState
fields_get_state (LrgPropertySource *source,
                  const gchar       *name)
{
	EditorDataFields *self = EDITOR_DATA_FIELDS (source);
	FieldRow         *row = fields_find (self, name);

	if (row == NULL || row->path == NULL || self->editor == NULL)
		return LRG_PROPERTY_STATE_NONE;
	return lrg_editor_document_has_path (lrg_editor_data_editor_get_document (self->editor), row->path)
	       ? LRG_PROPERTY_STATE_NONE : LRG_PROPERTY_STATE_DEFAULT;
}

/* Reset removes the key so the loader's default applies again. */
static gboolean
fields_reset (LrgPropertySource *source,
              const gchar       *name)
{
	EditorDataFields  *self = EDITOR_DATA_FIELDS (source);
	FieldRow          *row = fields_find (self, name);
	LrgEditorDocument *doc;
	g_autoptr(GError)  error = NULL;

	if (row == NULL || self->editor == NULL)
		return FALSE;
	if (row->path == NULL)
	{
		GValue def = G_VALUE_INIT;
		gboolean ok;

		g_value_init (&def, G_PARAM_SPEC_VALUE_TYPE (lrg_property_info_get_pspec (row->info)));
		g_param_value_set_default (lrg_property_info_get_pspec (row->info), &def);
		ok = fields_set_value (source, name, &def);
		g_value_unset (&def);
		return ok;
	}
	doc = lrg_editor_data_editor_get_document (self->editor);
	if (!lrg_editor_document_has_path (doc, row->path))
		return TRUE;
	if (!lrg_editor_document_remove (doc, row->path, &error))
	{
		lrg_editor_data_editor_report_error (self->editor, error);
		return FALSE;
	}
	lrg_property_source_emit_changed (source, name);
	return TRUE;
}

static void
editor_data_fields_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = fields_list_properties;
	iface->get_value = fields_get_value;
	iface->set_value = fields_set_value;
	iface->get_state = fields_get_state;
	iface->reset = fields_reset;
}

static void
editor_data_fields_finalize (GObject *object)
{
	EditorDataFields *self = EDITOR_DATA_FIELDS (object);

	g_clear_weak_pointer (&self->editor);
	g_ptr_array_unref (self->rows);
	G_OBJECT_CLASS (editor_data_fields_parent_class)->finalize (object);
}

static void
editor_data_fields_class_init (EditorDataFieldsClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = editor_data_fields_finalize;
}

static void
editor_data_fields_init (EditorDataFields *self)
{
	self->rows = g_ptr_array_new_with_free_func (field_row_free);
}

EditorDataFields *
editor_data_fields_new (LrgEditorDataEditor *editor)
{
	EditorDataFields *self = g_object_new (EDITOR_TYPE_DATA_FIELDS, NULL);

	g_set_weak_pointer (&self->editor, editor);
	return self;
}

/*
 * Adds a row. @pspec is sunk; @path NULL makes it virtual (served by the
 * callbacks of editor_data_fields_set_virtual()); @group and @hint are
 * the grid's section and editor hint.
 */
void
editor_data_fields_add (EditorDataFields *self,
                        GParamSpec       *pspec,
                        const gchar      *path,
                        const gchar      *group,
                        const gchar      *hint)
{
	FieldRow *row = g_new0 (FieldRow, 1);

	row->info = lrg_property_info_new (pspec);
	row->path = g_strdup (path);
	if (group != NULL)
		lrg_property_info_set_group (row->info, group);
	if (hint != NULL)
		lrg_property_info_set_hint (row->info, hint);
	g_ptr_array_add (self->rows, row);
}

void
editor_data_fields_set_virtual (EditorDataFields   *self,
                                EditorDataFieldGet  get,
                                EditorDataFieldSet  set,
                                gpointer            user_data)
{
	self->get = get;
	self->set = set;
	self->user_data = user_data;
}

const gchar *
editor_data_fields_get_path (EditorDataFields *self,
                             const gchar      *name)
{
	FieldRow *row = fields_find (self, name);

	return row != NULL ? row->path : NULL;
}

guint
editor_data_fields_get_count (EditorDataFields *self)
{
	return self->rows->len;
}

/* The grid's gestures become the document's merge ids. */
static void
fields_on_edit_begin (LrgPropertyGrid *grid,
                      const gchar     *name,
                      guint            gesture_id,
                      gpointer         user_data)
{
	EditorDataFields *self = EDITOR_DATA_FIELDS (user_data);

	if (self->editor != NULL)
		self->gesture = lrg_editor_data_editor_new_gesture (self->editor);
}

static void
fields_on_edit_end (LrgPropertyGrid *grid,
                    const gchar     *name,
                    guint            gesture_id,
                    gboolean         commit,
                    gpointer         user_data)
{
	EDITOR_DATA_FIELDS (user_data)->gesture = 0;
}

/* Shows @self in @grid and ties the grid's gestures to it. */
void
editor_data_fields_attach (EditorDataFields *self,
                           LrgPropertyGrid  *grid)
{
	g_signal_connect_object (grid, "edit-begin", G_CALLBACK (fields_on_edit_begin), self, 0);
	g_signal_connect_object (grid, "edit-end", G_CALLBACK (fields_on_edit_end), self, 0);
	lrg_property_grid_set_source (grid, LRG_PROPERTY_SOURCE (self));
}

/* Sets a row from text (IPC, tests): parsed into the row's type. */
void
editor_data_fields_set_value (EditorDataFields *self,
                              const gchar      *name,
                              const gchar      *text)
{
	FieldRow *row = fields_find (self, name);
	GValue    value = G_VALUE_INIT;

	if (row == NULL)
		return;
	g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (lrg_property_info_get_pspec (row->info)));
	if (G_VALUE_HOLDS_STRING (&value))
		g_value_set_string (&value, text);
	else if (!lrg_property_value_from_string (&value, text))
	{
		g_value_unset (&value);
		return;
	}
	fields_set_value (LRG_PROPERTY_SOURCE (self), name, &value);
	g_value_unset (&value);
}

/* @color with its alpha scaled by @alpha. */
GrlColor
editor_data_color_alpha (const GrlColor *color,
                         gfloat          alpha)
{
	GrlColor out = *color;

	out.a = (guint8) CLAMP ((gfloat) color->a * alpha, 0.0f, 255.0f);
	return out;
}

/* @text cut to @max_chars characters (UTF-8) with an ellipsis, one line. */
gchar *
editor_data_ellipsize (const gchar *text,
                       guint        max_chars)
{
	g_autofree gchar *line = NULL;
	gchar            *nl;
	glong             n;

	if (text == NULL)
		return g_strdup ("");
	line = g_strdup (text);
	nl = strchr (line, '\n');
	if (nl != NULL)
		*nl = '\0';
	n = g_utf8_strlen (line, -1);
	if (n <= (glong) max_chars && nl == NULL)
		return g_steal_pointer (&line);
	if (n > (glong) max_chars)
		*g_utf8_offset_to_pointer (line, (glong) max_chars - 1) = '\0';
	return g_strconcat (line, "…", NULL);
}

/* ==========================================================================
 * The HUD layout editor
 * ========================================================================== */

#define DEFAULT_PIECE_W   (160.0f)
#define DEFAULT_PIECE_H   (48.0f)
#define MIN_PIECE         (8.0f)
#define STAGE_MARGIN      (24.0f)
#define HANDLE_SIZE       (9.0f)

/* One piece as the stage shows it */
typedef struct
{
	gchar        *id;
	gboolean      placed;    /* has an anchor in the file */
	LrgHudAnchor  anchor;
	gfloat        x;
	gfloat        y;
	gboolean      locked;
	gboolean      hidden;
	gfloat        left;      /* resolved rectangle, screen units */
	gfloat        top;
	gfloat        width;
	gfloat        height;
} Piece;

typedef enum
{
	DRAG_NONE,
	DRAG_MOVE,
	DRAG_RESIZE
} DragMode;

struct _LrgEditorHudLayoutEditor
{
	LrgEditorDataEditor  parent_instance;

	/* Widgets */
	LrgSplitter         *split;
	LrgWidget           *stage;          /* LrgCustomWidget */
	LrgPropertyGrid     *grid;
	LrgWidget           *screen_chip;

	/* Model */
	GPtrArray           *pieces;         /* Piece, document order */
	GHashTable          *sizes;          /* id -> gfloat[2], preview sizes */
	gchar               *selected;
	gfloat               screen_w;
	gfloat               screen_h;

	/* Stage transform (screen units -> canvas), from the last paint/layout */
	gfloat               scale;
	gfloat               origin_x;
	gfloat               origin_y;

	/* Drag */
	DragMode             drag;
	guint                corner;         /* 0 tl, 1 tr, 2 bl, 3 br */
	gfloat               press_x;        /* canvas */
	gfloat               press_y;
	gfloat               start_left;     /* screen units */
	gfloat               start_top;
	gfloat               start_w;
	gfloat               start_h;
	gfloat               live_left;
	gfloat               live_top;
	gfloat               live_w;
	gfloat               live_h;
	gboolean             moved;
};

G_DEFINE_FINAL_TYPE (LrgEditorHudLayoutEditor, lrg_editor_hud_layout_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static const struct { gfloat w; gfloat h; } screen_presets[] = {
	{ 1280.0f, 720.0f }, { 1920.0f, 1080.0f }, { 1024.0f, 768.0f }, { 800.0f, 600.0f }, { 720.0f, 1280.0f }
};

static void rebuild_fields (LrgEditorHudLayoutEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorHudLayoutEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static void
piece_free (gpointer data)
{
	Piece *p = data;

	g_free (p->id);
	g_free (p);
}

/* "/pieces/<id>" or "/pieces/<id>/<key>" with the id escaped. */
static gchar *
piece_path (const gchar *id,
            const gchar *key)
{
	g_autofree gchar *base = lrg_editor_document_path_join ("/pieces", id);

	return key != NULL ? lrg_editor_document_path_join (base, key) : g_steal_pointer (&base);
}

/* The point of a w x h box an anchor names (column / row of the thirds). */
static void
anchor_point (LrgHudAnchor  anchor,
              gfloat        w,
              gfloat        h,
              gfloat       *px,
              gfloat       *py)
{
	*px = w * (gfloat) ((guint) anchor % 3u) * 0.5f;
	*py = h * (gfloat) ((guint) anchor / 3u) * 0.5f;
}

static gboolean
anchor_from_nick (const gchar  *nick,
                  LrgHudAnchor *out)
{
	GEnumClass *klass = g_type_class_ref (LRG_TYPE_HUD_ANCHOR);
	GEnumValue *value = nick != NULL ? g_enum_get_value_by_nick (klass, nick) : NULL;

	if (value != NULL)
		*out = (LrgHudAnchor) value->value;
	g_type_class_unref (klass);
	return value != NULL;
}

static const gchar *
anchor_nick (LrgHudAnchor anchor)
{
	GEnumClass *klass = g_type_class_ref (LRG_TYPE_HUD_ANCHOR);
	GEnumValue *value = g_enum_get_value (klass, (gint) anchor);
	const gchar *nick = value != NULL ? value->value_nick : "top-left";

	g_type_class_unref (klass);
	return nick;
}

static Piece *
find_piece (LrgEditorHudLayoutEditor *self,
            const gchar              *id)
{
	guint i;

	if (id == NULL)
		return NULL;
	for (i = 0; i < self->pieces->len; i++)
	{
		Piece *p = g_ptr_array_index (self->pieces, i);

		if (g_strcmp0 (p->id, id) == 0)
			return p;
	}
	return NULL;
}

static void
piece_size (LrgEditorHudLayoutEditor *self,
            const gchar              *id,
            gfloat                   *w,
            gfloat                   *h)
{
	gfloat *size = g_hash_table_lookup (self->sizes, id);

	*w = size != NULL ? size[0] : DEFAULT_PIECE_W;
	*h = size != NULL ? size[1] : DEFAULT_PIECE_H;
}

static gboolean
scalar_bool (LrgEditorDocument *doc,
             const gchar       *path)
{
	const gchar *text = lrg_editor_document_get_scalar (doc, path);

	return text != NULL && (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "yes") == 0 ||
	                        g_ascii_strcasecmp (text, "on") == 0 || g_strcmp0 (text, "1") == 0);
}

static gfloat
scalar_float (LrgEditorDocument *doc,
              const gchar       *path)
{
	const gchar *text = lrg_editor_document_get_scalar (doc, path);

	return text != NULL ? (gfloat) g_ascii_strtod (text, NULL) : 0.0f;
}

/*
 * Re-reads the pieces from the document and resolves every rectangle on
 * the virtual screen. Unplaced pieces are stacked down the left edge.
 */
static void
reload_pieces (LrgEditorHudLayoutEditor *self)
{
	LrgEditorDocument *doc = doc_of (self);
	g_auto(GStrv)      ids = NULL;
	guint              i, unplaced = 0;

	g_ptr_array_set_size (self->pieces, 0);
	if (doc == NULL)
		return;
	ids = lrg_editor_document_get_keys (doc, "/pieces");
	for (i = 0; ids != NULL && ids[i] != NULL; i++)
	{
		Piece            *p = g_new0 (Piece, 1);
		g_autofree gchar *apath = piece_path (ids[i], "anchor");
		g_autofree gchar *xpath = piece_path (ids[i], "x");
		g_autofree gchar *ypath = piece_path (ids[i], "y");
		g_autofree gchar *lpath = piece_path (ids[i], "locked");
		g_autofree gchar *hpath = piece_path (ids[i], "hidden");

		p->id = g_strdup (ids[i]);
		p->anchor = LRG_HUD_ANCHOR_TOP_LEFT;
		p->placed = anchor_from_nick (lrg_editor_document_get_scalar (doc, apath), &p->anchor);
		p->x = scalar_float (doc, xpath);
		p->y = scalar_float (doc, ypath);
		p->locked = scalar_bool (doc, lpath);
		p->hidden = scalar_bool (doc, hpath);
		piece_size (self, p->id, &p->width, &p->height);
		if (p->placed)
			lrg_hud_layout_resolve (p->anchor, p->x, p->y, p->width, p->height, self->screen_w, self->screen_h,
			                        &p->left, &p->top);
		else
		{
			/* the game's default is not in the file: stack them up from the
			 * bottom-left corner, under the placed pieces */
			p->left = 16.0f;
			p->top = self->screen_h - 16.0f - (gfloat) (unplaced + 1) * (p->height + 12.0f);
			g_ptr_array_insert (self->pieces, (gint) unplaced, p);
			unplaced++;
			continue;
		}
		g_ptr_array_add (self->pieces, p);
	}
	if (self->selected != NULL && find_piece (self, self->selected) == NULL)
	{
		g_clear_pointer (&self->selected, g_free);
		rebuild_fields (self);
	}
}

/* --------------------------------------------------------------------------
 * Writing placements
 * -------------------------------------------------------------------------- */

/* Writes anchor / x / y of a piece as one undo step (offsets rounded). */
static gboolean
write_placement (LrgEditorHudLayoutEditor *self,
                 const gchar              *id,
                 LrgHudAnchor              anchor,
                 gfloat                    x,
                 gfloat                    y,
                 const gchar              *label,
                 guint                     gesture)
{
	LrgEditorDocument *doc = doc_of (self);
	g_autofree gchar  *apath = piece_path (id, "anchor");
	g_autofree gchar  *xpath = piece_path (id, "x");
	g_autofree gchar  *ypath = piece_path (id, "y");
	g_autoptr(GError)  error = NULL;
	gboolean           ok;

	lrg_editor_document_begin_group (doc, label);
	ok = lrg_editor_document_set_string (doc, apath, anchor_nick (anchor), gesture, &error) &&
	     lrg_editor_document_set_int (doc, xpath, (gint64) lroundf (x), gesture, &error) &&
	     lrg_editor_document_set_int (doc, ypath, (gint64) lroundf (y), gesture, &error);
	lrg_editor_document_end_group (doc);
	if (!ok)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	return ok;
}

/* The offset that puts a w x h piece's @anchor point where the rectangle
 * at (left, top) has it. */
static void
offset_for_rect (LrgEditorHudLayoutEditor *self,
                 LrgHudAnchor              anchor,
                 gfloat                    left,
                 gfloat                    top,
                 gfloat                    w,
                 gfloat                    h,
                 gfloat                   *x,
                 gfloat                   *y)
{
	gfloat cx, cy, px, py;

	anchor_point (anchor, self->screen_w, self->screen_h, &cx, &cy);
	anchor_point (anchor, w, h, &px, &py);
	*x = left + px - cx;
	*y = top + py - cy;
}

/* A dragged rectangle: the engine's rule picks the anchor (the third
 * holding the centre) and the offset, exactly as a game's own HUD
 * editing does through lrg_hud_layout_place_rect(). */
static gboolean
commit_move (LrgEditorHudLayoutEditor *self,
             const gchar              *id,
             gfloat                    left,
             gfloat                    top,
             gfloat                    w,
             gfloat                    h)
{
	g_autoptr(LrgHudLayout) scratch = lrg_hud_layout_new ();
	LrgHudAnchor            anchor;
	gfloat                  x, y;

	lrg_hud_layout_add_piece (scratch, id, NULL, LRG_HUD_ANCHOR_TOP_LEFT, 0.0f, 0.0f);
	if (!lrg_hud_layout_place_rect (scratch, id, left, top, w, h, self->screen_w, self->screen_h) ||
	    !lrg_hud_layout_get_position (scratch, id, &anchor, &x, &y))
		return FALSE;
	return write_placement (self, id, anchor, x, y, "Move HUD piece", 0);
}

/* --------------------------------------------------------------------------
 * The stage
 * -------------------------------------------------------------------------- */

/* Screen units -> canvas for the stage's current rectangle. */
static void
update_transform (LrgEditorHudLayoutEditor *self)
{
	LrgWidget *w = self->stage;
	gfloat     sw = lrg_widget_get_width (w) - STAGE_MARGIN * 2.0f;
	gfloat     sh = lrg_widget_get_height (w) - STAGE_MARGIN * 2.0f;

	self->scale = MAX (0.01f, MIN (sw / self->screen_w, sh / self->screen_h));
	self->origin_x = lrg_widget_get_world_x (w) + (lrg_widget_get_width (w) - self->screen_w * self->scale) * 0.5f;
	self->origin_y = lrg_widget_get_world_y (w) + (lrg_widget_get_height (w) - self->screen_h * self->scale) * 0.5f;
}

/* The rectangle a piece shows now (the live one while it is dragged). */
static void
shown_rect (LrgEditorHudLayoutEditor *self,
            Piece                    *p,
            gfloat                   *l,
            gfloat                   *t,
            gfloat                   *w,
            gfloat                   *h)
{
	if (self->drag != DRAG_NONE && g_strcmp0 (p->id, self->selected) == 0)
	{
		*l = self->live_left;
		*t = self->live_top;
		*w = self->live_w;
		*h = self->live_h;
		return;
	}
	*l = p->left;
	*t = p->top;
	*w = p->width;
	*h = p->height;
}

static void
to_canvas (LrgEditorHudLayoutEditor *self,
           gfloat                    sx,
           gfloat                    sy,
           gfloat                   *cx,
           gfloat                   *cy)
{
	*cx = self->origin_x + sx * self->scale;
	*cy = self->origin_y + sy * self->scale;
}

/* Dashed rectangle outline (unplaced pieces). */
static void
dashed_rect (LrgPaintList   *list,
             gfloat          x,
             gfloat          y,
             gfloat          w,
             gfloat          h,
             const GrlColor *color)
{
	const gfloat dash = 6.0f, gap = 4.0f;
	gfloat       d;

	for (d = 0.0f; d < w; d += dash + gap)
	{
		lrg_paint_list_line (list, x + d, y, x + MIN (w, d + dash), y, 1.0f, color);
		lrg_paint_list_line (list, x + d, y + h, x + MIN (w, d + dash), y + h, 1.0f, color);
	}
	for (d = 0.0f; d < h; d += dash + gap)
	{
		lrg_paint_list_line (list, x, y + d, x, y + MIN (h, d + dash), 1.0f, color);
		lrg_paint_list_line (list, x + w, y + d, x + w, y + MIN (h, d + dash), 1.0f, color);
	}
}

static void
stage_paint (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (user_data);
	LrgTheme                 *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                  *font = lrg_theme_get_ui_font (theme);
	gfloat                    fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                    radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	const GrlColor           *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	const GrlColor           *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor           *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor           *border = lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER);
	GrlColor                  guide = editor_data_color_alpha (border, 0.7f);
	GrlColor                  shadow = editor_data_color_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW), 0.6f);
	g_autofree gchar         *caption = NULL;
	gfloat                    sx, sy, sw, sh;
	guint                     i, k;

	update_transform (self);

	/* --- backdrop and the virtual screen --- */
	lrg_paint_list_rect (list, x, y, width, height, radius, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
	sx = self->origin_x;
	sy = self->origin_y;
	sw = self->screen_w * self->scale;
	sh = self->screen_h * self->scale;
	lrg_paint_list_shadow (list, sx, sy, sw, sh, 6.0f, 18.0f, 0.0f, 0.0f, 6.0f, &shadow);
	lrg_paint_list_rect (list, sx, sy, sw, sh, 6.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW));
	lrg_paint_list_border (list, sx, sy, sw, sh, 6.0f, 1.0f, border);

	/* --- the thirds that name the anchors, and the nine anchor points --- */
	for (k = 1; k < 3; k++)
	{
		lrg_paint_list_line (list, sx + sw * (gfloat) k / 3.0f, sy, sx + sw * (gfloat) k / 3.0f, sy + sh, 1.0f, &guide);
		lrg_paint_list_line (list, sx, sy + sh * (gfloat) k / 3.0f, sx + sw, sy + sh * (gfloat) k / 3.0f, 1.0f, &guide);
	}
	for (k = 0; k < 9; k++)
	{
		gfloat ax, ay;

		anchor_point ((LrgHudAnchor) k, sw, sh, &ax, &ay);
		lrg_paint_list_circle (list, sx + ax, sy + ay, 3.0f, border);
	}
	caption = g_strdup_printf ("%.0f × %.0f", self->screen_w, self->screen_h);
	lrg_paint_list_text (list, font, caption, sx + 6.0f, sy + sh + 4.0f, fs, 0.0f, muted);

	/* --- pieces, in file order (later on top) --- */
	lrg_paint_list_push_clip (list, x, y, width, height);
	for (i = 0; i < self->pieces->len; i++)
	{
		Piece          *p = g_ptr_array_index (self->pieces, i);
		gboolean        sel = g_strcmp0 (p->id, self->selected) == 0;
		gfloat          l, t, w, h, cx, cy;
		gfloat          alpha = p->hidden ? 0.4f : 1.0f;
		GrlColor        fill = editor_data_color_alpha (accent, (sel ? 0.30f : 0.16f) * alpha);
		GrlColor        edge = editor_data_color_alpha (sel ? accent : text, (sel ? 1.0f : 0.45f) * alpha);
		GrlColor        label = editor_data_color_alpha (text, alpha);
		gfloat          ix;

		shown_rect (self, p, &l, &t, &w, &h);
		to_canvas (self, l, t, &cx, &cy);
		w *= self->scale;
		h *= self->scale;
		lrg_paint_list_rect (list, cx, cy, w, h, radius, &fill);
		if (p->placed || (sel && self->drag != DRAG_NONE))
			lrg_paint_list_border (list, cx, cy, w, h, radius, sel ? 2.0f : 1.0f, &edge);
		else
			dashed_rect (list, cx, cy, w, h, &edge);
		lrg_paint_list_push_clip (list, cx, cy, w, h);
		lrg_paint_list_text (list, font, p->id, cx + 6.0f, cy + 4.0f, fs, 0.0f, &label);
		if (!p->placed)
			lrg_paint_list_text (list, font, "default", cx + 6.0f, cy + 4.0f + fs * 1.2f, fs * 0.9f, 0.0f, muted);
		lrg_paint_list_pop_clip (list);

		/* lock / hidden markers at the top-right corner */
		ix = cx + w - fs - 4.0f;
		if (p->locked)
		{
			lrg_paint_list_icon (list, LRG_ICON_LOCK, ix, cy + 4.0f, fs, 1.5f, &label);
			ix -= fs + 4.0f;
		}
		if (p->hidden)
			lrg_paint_list_icon (list, LRG_ICON_EYE, ix, cy + 4.0f, fs, 1.5f, muted);

		if (sel && p->placed)
		{
			gfloat ax, ay, px, py;

			/* the anchor line: screen anchor point -> piece anchor point */
			anchor_point (p->anchor, self->screen_w * self->scale, self->screen_h * self->scale, &ax, &ay);
			anchor_point (p->anchor, w, h, &px, &py);
			lrg_paint_list_line (list, sx + ax, sy + ay, cx + px, cy + py, 1.5f, accent);
			lrg_paint_list_circle (list, sx + ax, sy + ay, 5.0f, accent);
			lrg_paint_list_circle (list, cx + px, cy + py, 3.0f, accent);
		}
		if (sel && !p->locked)
		{
			for (k = 0; k < 4; k++)
			{
				gfloat hx = cx + ((k & 1u) ? w : 0.0f) - HANDLE_SIZE * 0.5f;
				gfloat hy = cy + ((k & 2u) ? h : 0.0f) - HANDLE_SIZE * 0.5f;

				lrg_paint_list_rect (list, hx, hy, HANDLE_SIZE, HANDLE_SIZE, 2.0f,
				                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
				lrg_paint_list_border (list, hx, hy, HANDLE_SIZE, HANDLE_SIZE, 2.0f, 1.5f, accent);
			}
		}
	}
	lrg_paint_list_pop_clip (list);

	if (self->pieces->len == 0)
		lrg_paint_list_text (list, font, "No pieces: use Add Piece", sx + 12.0f, sy + 12.0f, fs, 0.0f, muted);
}

/* The topmost piece under a canvas point, or NULL. */
static Piece *
piece_at (LrgEditorHudLayoutEditor *self,
          gfloat                    px,
          gfloat                    py)
{
	guint i;

	for (i = self->pieces->len; i > 0; i--)
	{
		Piece  *p = g_ptr_array_index (self->pieces, i - 1);
		gfloat  l, t, w, h, cx, cy;

		shown_rect (self, p, &l, &t, &w, &h);
		to_canvas (self, l, t, &cx, &cy);
		if (px >= cx && py >= cy && px <= cx + w * self->scale && py <= cy + h * self->scale)
			return p;
	}
	return NULL;
}

/* The corner handle of the selected piece under a canvas point, or -1. */
static gint
handle_at (LrgEditorHudLayoutEditor *self,
           gfloat                    px,
           gfloat                    py)
{
	Piece  *p = find_piece (self, self->selected);
	gfloat  l, t, w, h, cx, cy;
	guint   k;

	if (p == NULL || p->locked)
		return -1;
	shown_rect (self, p, &l, &t, &w, &h);
	to_canvas (self, l, t, &cx, &cy);
	for (k = 0; k < 4; k++)
	{
		gfloat hx = cx + ((k & 1u) ? w * self->scale : 0.0f);
		gfloat hy = cy + ((k & 2u) ? h * self->scale : 0.0f);

		if (fabsf (px - hx) <= HANDLE_SIZE && fabsf (py - hy) <= HANDLE_SIZE)
			return (gint) k;
	}
	return -1;
}

/* Applies the pointer to the live rectangle while dragging. */
static void
drag_update (LrgEditorHudLayoutEditor *self,
             gfloat                    px,
             gfloat                    py)
{
	gfloat dx = (px - self->press_x) / self->scale;
	gfloat dy = (py - self->press_y) / self->scale;

	if (fabsf (px - self->press_x) > 2.0f || fabsf (py - self->press_y) > 2.0f)
		self->moved = TRUE;
	if (self->drag == DRAG_MOVE)
	{
		/* kept on the screen, like the engine keeps it */
		self->live_left = CLAMP (self->start_left + dx, 0.0f, MAX (0.0f, self->screen_w - self->start_w));
		self->live_top = CLAMP (self->start_top + dy, 0.0f, MAX (0.0f, self->screen_h - self->start_h));
		return;
	}

	/* resize from one corner; the opposite corner stays */
	{
		gfloat l = self->start_left, t = self->start_top;
		gfloat r = l + self->start_w, b = t + self->start_h;

		if (self->corner & 1u)
			r = MAX (l + MIN_PIECE, r + dx);
		else
			l = MIN (r - MIN_PIECE, l + dx);
		if (self->corner & 2u)
			b = MAX (t + MIN_PIECE, b + dy);
		else
			t = MIN (b - MIN_PIECE, t + dy);
		self->live_left = l;
		self->live_top = t;
		self->live_w = r - l;
		self->live_h = b - t;
	}
}

/* Release: one document edit for the whole gesture. */
static void
drag_end (LrgEditorHudLayoutEditor *self)
{
	DragMode          mode = self->drag;
	g_autofree gchar *id = g_strdup (self->selected);
	Piece            *p = find_piece (self, id);

	self->drag = DRAG_NONE;
	if (p == NULL || !self->moved)
		return;
	if (mode == DRAG_MOVE)
		commit_move (self, id, self->live_left, self->live_top, self->live_w, self->live_h);
	else
	{
		gfloat *size = g_new (gfloat, 2);
		gfloat  x, y;

		size[0] = roundf (self->live_w);
		size[1] = roundf (self->live_h);
		g_hash_table_replace (self->sizes, g_strdup (id), size);
		if (p->placed)
		{
			/* the anchor is respected: only its offset changes */
			offset_for_rect (self, p->anchor, self->live_left, self->live_top, size[0], size[1], &x, &y);
			if (lroundf (x) != lroundf (p->x) || lroundf (y) != lroundf (p->y))
				write_placement (self, id, p->anchor, x, y, "Resize HUD piece", 0);
			else
				reload_pieces (self);
		}
		else
			commit_move (self, id, self->live_left, self->live_top, size[0], size[1]);
	}
	reload_pieces (self);
	lrg_property_source_emit_changed (lrg_property_grid_get_source (self->grid, 0), NULL);
}

/* Arrow keys nudge the selected piece by one unit (Shift: ten). */
static gboolean
nudge (LrgEditorHudLayoutEditor *self,
       GrlKey                    key,
       LrgInputModifiers         mods)
{
	Piece  *p = find_piece (self, self->selected);
	gfloat  step = (mods & LRG_INPUT_MODIFIER_SHIFT) ? 10.0f : 1.0f;
	gfloat  dx = 0.0f, dy = 0.0f;

	switch (key)
	{
	case GRL_KEY_LEFT:  dx = -step; break;
	case GRL_KEY_RIGHT: dx = step; break;
	case GRL_KEY_UP:    dy = -step; break;
	case GRL_KEY_DOWN:  dy = step; break;
	default:            return FALSE;
	}
	if (p == NULL || p->locked)
		return p != NULL;
	if (p->placed)
		write_placement (self, p->id, p->anchor, p->x + dx, p->y + dy, "Nudge HUD piece", 0);
	else
		commit_move (self, p->id, p->left + dx, p->top + dy, p->width, p->height);
	return TRUE;
}

static gboolean
stage_event (LrgCustomWidget  *widget,
             const LrgUIEvent *event,
             gpointer          user_data)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (user_data);
	gfloat                    px = lrg_ui_event_get_x (event);
	gfloat                    py = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			gint   handle;
			Piece *p;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			update_transform (self);
			handle = handle_at (self, px, py);
			p = handle >= 0 ? find_piece (self, self->selected) : piece_at (self, px, py);
			lrg_editor_hud_layout_editor_select (self, p != NULL ? p->id : NULL);
			if (p == NULL || p->locked)
				return TRUE;
			self->drag = handle >= 0 ? DRAG_RESIZE : DRAG_MOVE;
			self->corner = handle >= 0 ? (guint) handle : 0u;
			self->press_x = px;
			self->press_y = py;
			self->start_left = self->live_left = p->left;
			self->start_top = self->live_top = p->top;
			self->start_w = self->live_w = p->width;
			self->start_h = self->live_h = p->height;
			self->moved = FALSE;
			return TRUE;
		}
	case LRG_UI_EVENT_MOUSE_MOVE:
		if (self->drag == DRAG_NONE)
			return FALSE;
		drag_update (self, px, py);
		return TRUE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (self->drag == DRAG_NONE)
			return FALSE;
		drag_update (self, px, py);
		drag_end (self);
		return TRUE;
	case LRG_UI_EVENT_CANCEL:
		if (self->drag == DRAG_NONE)
			return FALSE;
		self->drag = DRAG_NONE;
		return TRUE;
	case LRG_UI_EVENT_KEY_DOWN:
		return nudge (self, lrg_ui_event_get_key (event), lrg_ui_event_get_modifiers (event));
	default:
		return FALSE;
	}
}

/* --------------------------------------------------------------------------
 * Property grid
 * -------------------------------------------------------------------------- */

/* Virtual rows: anchor / x / y keep the piece where it is when the anchor
 * changes; width / height are the preview size. */
static gboolean
hud_field_get (const gchar *name,
               GValue      *value,
               gpointer     user_data)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (user_data);
	Piece                    *p = find_piece (self, self->selected);

	if (p == NULL)
		return FALSE;
	if (g_strcmp0 (name, "anchor") == 0)
		g_value_set_enum (value, (gint) p->anchor);
	else if (g_strcmp0 (name, "x") == 0)
		g_value_set_double (value, p->x);
	else if (g_strcmp0 (name, "y") == 0)
		g_value_set_double (value, p->y);
	else if (g_strcmp0 (name, "width") == 0)
		g_value_set_double (value, p->width);
	else if (g_strcmp0 (name, "height") == 0)
		g_value_set_double (value, p->height);
	return TRUE;
}

static gboolean
hud_field_set (const gchar  *name,
               const GValue *value,
               guint         gesture_id,
               gpointer      user_data)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (user_data);
	Piece                    *p = find_piece (self, self->selected);
	g_autofree gchar         *id = NULL;

	if (p == NULL)
		return FALSE;
	id = g_strdup (p->id);
	if (g_strcmp0 (name, "anchor") == 0)
	{
		LrgHudAnchor anchor = (LrgHudAnchor) g_value_get_enum (value);
		gfloat       x, y;

		offset_for_rect (self, anchor, p->left, p->top, p->width, p->height, &x, &y);
		return write_placement (self, id, anchor, x, y, "Set HUD anchor", gesture_id);
	}
	if (g_strcmp0 (name, "x") == 0)
		return write_placement (self, id, p->anchor, (gfloat) g_value_get_double (value), p->y, "Set HUD offset",
		                        gesture_id);
	if (g_strcmp0 (name, "y") == 0)
		return write_placement (self, id, p->anchor, p->x, (gfloat) g_value_get_double (value), "Set HUD offset",
		                        gesture_id);
	if (g_strcmp0 (name, "width") == 0)
		return lrg_editor_hud_layout_editor_set_piece_size (self, id, (gfloat) g_value_get_double (value), p->height);
	if (g_strcmp0 (name, "height") == 0)
		return lrg_editor_hud_layout_editor_set_piece_size (self, id, p->width, (gfloat) g_value_get_double (value));
	return FALSE;
}

/* Rows for the selection: the piece, or the file's version without one. */
static void
rebuild_fields (LrgEditorHudLayoutEditor *self)
{
	g_autoptr(EditorDataFields) fields = editor_data_fields_new (LRG_EDITOR_DATA_EDITOR (self));
	const GParamFlags           flags = G_PARAM_READWRITE;

	if (self->grid == NULL)
		return;
	editor_data_fields_set_virtual (fields, hud_field_get, hud_field_set, self);
	if (self->selected == NULL)
		editor_data_fields_add (fields, g_param_spec_int ("version", "Format version", NULL, 1,
		                                                  LRG_HUD_LAYOUT_FORMAT_VERSION, 1, flags),
		                        "/version", "File", NULL);
	else
	{
		g_autofree gchar *lpath = piece_path (self->selected, "locked");
		g_autofree gchar *hpath = piece_path (self->selected, "hidden");

		editor_data_fields_add (fields, g_param_spec_enum ("anchor", "Anchor", NULL, LRG_TYPE_HUD_ANCHOR,
		                                                   LRG_HUD_ANCHOR_TOP_LEFT, flags),
		                        NULL, "Placement", NULL);
		editor_data_fields_add (fields, g_param_spec_double ("x", "Offset X", NULL, -100000.0, 100000.0, 0.0, flags),
		                        NULL, "Placement", NULL);
		editor_data_fields_add (fields, g_param_spec_double ("y", "Offset Y", NULL, -100000.0, 100000.0, 0.0, flags),
		                        NULL, "Placement", NULL);
		editor_data_fields_add (fields, g_param_spec_boolean ("locked", "Locked", NULL, FALSE, flags),
		                        lpath, "Player", NULL);
		editor_data_fields_add (fields, g_param_spec_boolean ("hidden", "Hidden", NULL, FALSE, flags),
		                        hpath, "Player", NULL);
		editor_data_fields_add (fields, g_param_spec_double ("width", "Width", "Preview size (not saved)", MIN_PIECE,
		                                                     100000.0, DEFAULT_PIECE_W, flags),
		                        NULL, "Preview (editor only)", NULL);
		editor_data_fields_add (fields, g_param_spec_double ("height", "Height", "Preview size (not saved)",
		                                                     MIN_PIECE, 100000.0, DEFAULT_PIECE_H, flags),
		                        NULL, "Preview (editor only)", NULL);
	}
	editor_data_fields_attach (fields, self->grid);
}

/* --------------------------------------------------------------------------
 * Toolbar
 * -------------------------------------------------------------------------- */

static void
on_add_piece (LrgWidget *chip,
              gpointer   user_data)
{
	g_autoptr(GError) error = NULL;

	if (!lrg_editor_hud_layout_editor_add_piece (LRG_EDITOR_HUD_LAYOUT_EDITOR (user_data), NULL, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (user_data), error);
}

static void
on_remove_piece (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (user_data);
	g_autofree gchar         *path = NULL;
	g_autoptr(GError)         error = NULL;

	if (self->selected == NULL)
		return;
	path = piece_path (self->selected, NULL);
	if (!lrg_editor_document_remove (doc_of (self), path, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* Reset: drop the player's placement (anchor, x, y) of the selection. */
static void
on_reset_piece (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (user_data);
	LrgEditorDocument        *doc = doc_of (self);
	static const gchar       *keys[] = { "anchor", "x", "y" };
	g_autoptr(GError)         error = NULL;
	guint                     i;

	if (self->selected == NULL)
		return;
	lrg_editor_document_begin_group (doc, "Reset HUD piece");
	for (i = 0; i < G_N_ELEMENTS (keys); i++)
	{
		g_autofree gchar *path = piece_path (self->selected, keys[i]);

		if (lrg_editor_document_has_path (doc, path) && !lrg_editor_document_remove (doc, path, &error))
			break;
	}
	lrg_editor_document_end_group (doc);
	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
sync_screen_chip (LrgEditorHudLayoutEditor *self)
{
	g_autofree gchar *label = g_strdup_printf ("%.0f×%.0f", self->screen_w, self->screen_h);

	if (self->screen_chip != NULL)
		editor_chip_set_label (self->screen_chip, label);
}

/* Cycles the simulated screen through common sizes. */
static void
on_screen_chip (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (user_data);
	guint                     i, next = 0;

	for (i = 0; i < G_N_ELEMENTS (screen_presets); i++)
		if (screen_presets[i].w == self->screen_w && screen_presets[i].h == self->screen_h)
			next = (i + 1) % G_N_ELEMENTS (screen_presets);
	lrg_editor_hud_layout_editor_set_screen_size (self, screen_presets[next].w, screen_presets[next].h);
}

/* --------------------------------------------------------------------------
 * LrgEditorDataEditor
 * -------------------------------------------------------------------------- */

static void
hud_refresh (LrgEditorDataEditor *editor,
             const gchar         *path)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (editor);
	LrgPropertySource        *source;

	reload_pieces (self);
	source = self->grid != NULL ? lrg_property_grid_get_source (self->grid, 0) : NULL;
	if (source != NULL)
		lrg_property_source_emit_changed (source, NULL);
}

/* "/pieces/<id>/..." selects the piece. */
static void
hud_reveal (LrgEditorDataEditor *editor,
            const gchar         *path,
            guint                line)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (editor);
	g_auto(GStrv)             parts = NULL;
	g_autofree gchar         *at_line = NULL;

	if ((path == NULL || *path == '\0') && line > 0)
		path = at_line = lrg_editor_document_get_path_at_line (doc_of (self), line);
	if (path == NULL)
		return;
	parts = lrg_editor_document_path_split (path, NULL);
	if (parts != NULL && g_strv_length (parts) >= 2 && g_strcmp0 (parts[0], "pieces") == 0)
		lrg_editor_hud_layout_editor_select (self, parts[1]);
}

static gchar *
hud_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (editor);

	return g_strdup_printf ("pieces=%u selected=%s screen=%.0fx%.0f", self->pieces->len,
	                        self->selected != NULL ? self->selected : "", self->screen_w, self->screen_h);
}

static void
lrg_editor_hud_layout_editor_constructed (GObject *object)
{
	static const LrgCustomWidgetFuncs stage_funcs = { NULL, stage_paint, NULL, stage_event, { NULL } };
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (object);
	LrgEditorDataEditor      *editor = LRG_EDITOR_DATA_EDITOR (object);
	LrgWidget                *chip;

	G_OBJECT_CLASS (lrg_editor_hud_layout_editor_parent_class)->constructed (object);

	/* --- toolbar tools --- */
	chip = editor_chip_new ("hud-add", LRG_ICON_PLUS, "Add Piece", on_add_piece, self);
	lrg_editor_data_editor_add_tool (editor, chip);
	g_object_unref (chip);
	chip = editor_chip_new ("hud-reset", LRG_ICON_REFRESH, "Reset", on_reset_piece, self);
	lrg_editor_data_editor_add_tool (editor, chip);
	g_object_unref (chip);
	chip = editor_chip_new ("hud-remove", LRG_ICON_TRASH, NULL, on_remove_piece, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	lrg_editor_data_editor_add_tool (editor, chip);
	g_object_unref (chip);
	self->screen_chip = editor_chip_new ("hud-screen", LRG_ICON_HOME, NULL, on_screen_chip, self);
	sync_screen_chip (self);
	lrg_editor_data_editor_add_tool (editor, self->screen_chip);
	g_object_unref (self->screen_chip);

	/* --- content: stage | property grid --- */
	self->split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (self->split), "hud-split");
	self->stage = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->stage, "hud-stage");
	lrg_widget_set_focusable (self->stage, TRUE);
	lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (self->stage), 320.0f);
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->stage), 200.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->stage), &stage_funcs, self, NULL);
	lrg_splitter_set_start_child (self->split, self->stage);
	g_object_unref (self->stage);

	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "hud-grid");
	lrg_splitter_set_end_child (self->split, LRG_WIDGET (self->grid));
	g_object_unref (self->grid);
	lrg_splitter_set_fraction (self->split, 0.64f, FALSE);
	lrg_splitter_set_min_sizes (self->split, 200.0f, 220.0f);

	lrg_editor_data_editor_set_content (editor, LRG_WIDGET (self->split));
	g_object_unref (self->split);

	reload_pieces (self);
	rebuild_fields (self);
}

static void
lrg_editor_hud_layout_editor_dispose (GObject *object)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (object);

	if (self->stage != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->stage), NULL, NULL, NULL);
	self->stage = NULL;
	self->grid = NULL;
	self->split = NULL;
	self->screen_chip = NULL;
	G_OBJECT_CLASS (lrg_editor_hud_layout_editor_parent_class)->dispose (object);
}

static void
lrg_editor_hud_layout_editor_finalize (GObject *object)
{
	LrgEditorHudLayoutEditor *self = LRG_EDITOR_HUD_LAYOUT_EDITOR (object);

	g_ptr_array_unref (self->pieces);
	g_hash_table_unref (self->sizes);
	g_free (self->selected);
	G_OBJECT_CLASS (lrg_editor_hud_layout_editor_parent_class)->finalize (object);
}

static void
lrg_editor_hud_layout_editor_class_init (LrgEditorHudLayoutEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *editor_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_hud_layout_editor_constructed;
	object_class->dispose = lrg_editor_hud_layout_editor_dispose;
	object_class->finalize = lrg_editor_hud_layout_editor_finalize;
	editor_class->refresh = hud_refresh;
	editor_class->reveal = hud_reveal;
	editor_class->get_state = hud_get_state;
}

static void
lrg_editor_hud_layout_editor_init (LrgEditorHudLayoutEditor *self)
{
	self->pieces = g_ptr_array_new_with_free_func (piece_free);
	self->sizes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->screen_w = screen_presets[0].w;
	self->screen_h = screen_presets[0].h;
	self->scale = 1.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorHudLayoutEditor *
lrg_editor_hud_layout_editor_new (LrgEditorApp      *app,
                                  LrgEditorDocument *document)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);

	return g_object_new (LRG_TYPE_EDITOR_HUD_LAYOUT_EDITOR, "app", app, "document", document, NULL);
}

const gchar *
lrg_editor_hud_layout_editor_get_selected (LrgEditorHudLayoutEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HUD_LAYOUT_EDITOR (self), NULL);
	return self->selected;
}

gboolean
lrg_editor_hud_layout_editor_select (LrgEditorHudLayoutEditor *self,
                                     const gchar              *piece_id)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HUD_LAYOUT_EDITOR (self), FALSE);

	if (piece_id != NULL && find_piece (self, piece_id) == NULL)
		return FALSE;
	if (g_strcmp0 (piece_id, self->selected) == 0)
		return TRUE;
	g_free (self->selected);
	self->selected = g_strdup (piece_id);
	rebuild_fields (self);
	return TRUE;
}

gboolean
lrg_editor_hud_layout_editor_get_piece_rect (LrgEditorHudLayoutEditor *self,
                                             const gchar              *piece_id,
                                             gfloat                   *x,
                                             gfloat                   *y,
                                             gfloat                   *width,
                                             gfloat                   *height)
{
	Piece  *p;
	gfloat  l, t, w, h, cx, cy;

	g_return_val_if_fail (LRG_IS_EDITOR_HUD_LAYOUT_EDITOR (self), FALSE);

	p = find_piece (self, piece_id);
	if (p == NULL || self->stage == NULL || lrg_widget_get_width (self->stage) <= 0.0f)
		return FALSE;
	update_transform (self);
	shown_rect (self, p, &l, &t, &w, &h);
	to_canvas (self, l, t, &cx, &cy);
	if (x != NULL)
		*x = cx;
	if (y != NULL)
		*y = cy;
	if (width != NULL)
		*width = w * self->scale;
	if (height != NULL)
		*height = h * self->scale;
	return TRUE;
}

gboolean
lrg_editor_hud_layout_editor_get_piece_size (LrgEditorHudLayoutEditor *self,
                                             const gchar              *piece_id,
                                             gfloat                   *width,
                                             gfloat                   *height)
{
	gfloat w, h;

	g_return_val_if_fail (LRG_IS_EDITOR_HUD_LAYOUT_EDITOR (self), FALSE);

	if (find_piece (self, piece_id) == NULL)
		return FALSE;
	piece_size (self, piece_id, &w, &h);
	if (width != NULL)
		*width = w;
	if (height != NULL)
		*height = h;
	return TRUE;
}

gboolean
lrg_editor_hud_layout_editor_set_piece_size (LrgEditorHudLayoutEditor *self,
                                             const gchar              *piece_id,
                                             gfloat                    width,
                                             gfloat                    height)
{
	Piece            *p;
	gfloat           *size;
	gfloat            px0, py0, px1, py1, x, y;
	g_autofree gchar *id = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_HUD_LAYOUT_EDITOR (self), FALSE);

	p = find_piece (self, piece_id);
	if (p == NULL)
		return FALSE;
	id = g_strdup (piece_id);
	width = MAX (MIN_PIECE, width);
	height = MAX (MIN_PIECE, height);

	/* keep the anchor point where it is: the rectangle grows around it */
	anchor_point (p->anchor, p->width, p->height, &px0, &py0);
	anchor_point (p->anchor, width, height, &px1, &py1);
	size = g_new (gfloat, 2);
	size[0] = width;
	size[1] = height;
	g_hash_table_replace (self->sizes, g_strdup (id), size);
	if (p->placed)
	{
		offset_for_rect (self, p->anchor, p->left + px0 - px1, p->top + py0 - py1, width, height, &x, &y);
		if (lroundf (x) != lroundf (p->x) || lroundf (y) != lroundf (p->y))
			write_placement (self, id, p->anchor, x, y, "Resize HUD piece", 0);
	}
	reload_pieces (self);
	if (self->grid != NULL && lrg_property_grid_get_source (self->grid, 0) != NULL)
		lrg_property_source_emit_changed (lrg_property_grid_get_source (self->grid, 0), NULL);
	return TRUE;
}

void
lrg_editor_hud_layout_editor_get_screen_size (LrgEditorHudLayoutEditor *self,
                                              gfloat                   *width,
                                              gfloat                   *height)
{
	g_return_if_fail (LRG_IS_EDITOR_HUD_LAYOUT_EDITOR (self));

	if (width != NULL)
		*width = self->screen_w;
	if (height != NULL)
		*height = self->screen_h;
}

void
lrg_editor_hud_layout_editor_set_screen_size (LrgEditorHudLayoutEditor *self,
                                              gfloat                    width,
                                              gfloat                    height)
{
	g_return_if_fail (LRG_IS_EDITOR_HUD_LAYOUT_EDITOR (self));

	self->screen_w = MAX (64.0f, width);
	self->screen_h = MAX (64.0f, height);
	sync_screen_chip (self);
	reload_pieces (self);
}

gboolean
lrg_editor_hud_layout_editor_add_piece (LrgEditorHudLayoutEditor  *self,
                                        const gchar               *piece_id,
                                        GError                   **error)
{
	LrgEditorDocument *doc;
	g_autofree gchar  *id = NULL;
	gboolean           ok;
	guint              n;

	g_return_val_if_fail (LRG_IS_EDITOR_HUD_LAYOUT_EDITOR (self), FALSE);

	doc = doc_of (self);
	if (piece_id != NULL)
		id = g_strdup (piece_id);
	else
	{
		for (n = self->pieces->len + 1; ; n++)
		{
			g_free (id);
			id = g_strdup_printf ("piece-%u", n);
			if (find_piece (self, id) == NULL)
				break;
		}
	}

	/* `pieces:` may be missing or null in a fresh file */
	lrg_editor_document_begin_group (doc, "Add HUD piece");
	ok = TRUE;
	if (lrg_editor_document_get_kind (doc, "/pieces") != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		ok = lrg_editor_document_replace (doc, "/pieces", "{}", error);
	if (ok)
		ok = lrg_editor_document_add_key (doc, "/pieces", id, "{ anchor: top-left, x: 16, y: 16 }", -1, error);
	lrg_editor_document_end_group (doc);
	if (!ok)
		return FALSE;
	reload_pieces (self);
	lrg_editor_hud_layout_editor_select (self, id);
	return TRUE;
}
