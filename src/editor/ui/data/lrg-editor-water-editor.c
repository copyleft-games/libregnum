/* lrg-editor-water-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorWaterEditor: the `.rwater` data editor and the water.* IPC
 * methods (see the header).
 *
 * Layout:
 *
 *   +---------------------------------+---------------------------------+
 *   | (one scrolling column)          | +-----------------------------+ |
 *   | BODY: property grid             | |  animated surface over a    | |
 *   | WAVES  [+]                      | |  ground plane; drag orbits, | |
 *   |  0  (1.0, 0.3)  a 0.08 l 6      | |  the wheel zooms            | |
 *   | SELECTED WAVE: property grid    | |                             | |
 *   | OUTLINE  [+]                    | +-----------------------------+ |
 *   |  0  -10, -10                    |  lake - 4 points - 1 wave       |
 *   | SELECTED POINT: property grid   |                                 |
 *   +---------------------------------+---------------------------------+
 *
 * The document is the truth (an engine-backed LrgEditorDocument). After
 * every change its text is parsed into self->body with the engine loader;
 * every edit takes a fresh copy (lrg_editor_document_dup_object()),
 * changes it through the LrgWaterBody API and hands it back with
 * lrg_editor_document_commit_object(): one undo step, or one per grid
 * gesture. The parsed body is also copied into the editor's shared
 * terrain / water store for the file, so the level's water nodes show the
 * unsaved state; the viewport's outline handles edit the store's body and
 * push it back here (lrg_editor_water_editor_sync_from_store()).
 *
 * The preview draws with a private LrgWorld3DRenderer (made on the first
 * GPU frame): a sand-coloured ground plane under the body and the body
 * through an LrgWaterComponent on a private entity, its wave clock
 * advancing every frame.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-water-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../lrg-editor-terrain-panel.h"
#include "../../lrg-editor-server.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-property-source.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../world3d/lrg-entity3d.h"
#include "../../../world3d/lrg-world3d-renderer.h"
#include "../../../world3d/components/lrg-water-component.h"
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>
#include <raylib.h>

#define INSTALL_KEY   "lrg-editor-water-installed"
#define LIST_ROW_H    (24.0f)
#define LIST_BTN_W    (22.0f)
#define MAX_WAVES     (4)

typedef enum
{
	PART_BODY,
	PART_WAVE,
	PART_POINT
} Part;

/* ==========================================================================
 * Body helpers (shared with the store)
 * ========================================================================== */

void
editor_water_copy_into (LrgWaterBody *dst,
                        LrgWaterBody *src)
{
	GrlColor shallow, deep, foam;
	gfloat   a, b, c, d;
	guint    i;

	g_return_if_fail (LRG_IS_WATER_BODY (dst));
	g_return_if_fail (LRG_IS_WATER_BODY (src));

	lrg_water_body_set_name (dst, lrg_water_body_get_name (src));
	lrg_water_body_set_shape (dst, lrg_water_body_get_shape (src));
	lrg_water_body_set_level (dst, lrg_water_body_get_level (src));
	lrg_water_body_get_size (src, &a, &b);
	lrg_water_body_set_size (dst, a, b);
	lrg_water_body_clear_points (dst);
	for (i = 0; i < lrg_water_body_get_n_points (src); i++)
	{
		gfloat p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		lrg_water_body_get_point (src, i, p);
		lrg_water_body_add_point (dst, p[0], p[1], p[2], p[3]);
	}
	lrg_water_body_set_cell_size (dst, lrg_water_body_get_cell_size (src));
	lrg_water_body_get_foam (src, &a, &b);
	lrg_water_body_set_foam (dst, a, b);
	lrg_water_body_set_wave_count (dst, lrg_water_body_get_wave_count (src));
	for (i = 0; i < lrg_water_body_get_wave_count (src); i++)
	{
		LrgWaterWave w;

		if (lrg_water_body_get_wave (src, i, &w))
			lrg_water_body_set_wave (dst, i, &w);
	}
	lrg_water_body_set_wave_scale (dst, lrg_water_body_get_wave_scale (src));
	lrg_water_body_get_colors (src, &shallow, &deep, &foam);
	lrg_water_body_set_colors (dst, &shallow, &deep, &foam);
	lrg_water_body_get_optics (src, &a, &b, &c, &d);
	lrg_water_body_set_optics (dst, a, b, c, d);
}

static gboolean
to_double (const GValue *value,
           gdouble      *out)
{
	GValue d = G_VALUE_INIT;

	if (!g_value_type_transformable (G_VALUE_TYPE (value), G_TYPE_DOUBLE))
		return FALSE;
	g_value_init (&d, G_TYPE_DOUBLE);
	if (!g_value_transform (value, &d))
	{
		g_value_unset (&d);
		return FALSE;
	}
	*out = g_value_get_double (&d);
	g_value_unset (&d);
	return TRUE;
}

static gboolean
to_pair (const GValue *value,
         gfloat       *pair)
{
	if (G_VALUE_HOLDS (value, GRL_TYPE_VECTOR2) && g_value_get_boxed (value) != NULL)
	{
		const GrlVector2 *v = g_value_get_boxed (value);

		pair[0] = v->x;
		pair[1] = v->y;
		return TRUE;
	}
	if (G_VALUE_HOLDS_STRING (value) && g_value_get_string (value) != NULL)
	{
		g_auto(GStrv) parts = g_strsplit (g_value_get_string (value), ",", 2);

		if (g_strv_length (parts) != 2)
			return FALSE;
		pair[0] = (gfloat) g_ascii_strtod (parts[0], NULL);
		pair[1] = (gfloat) g_ascii_strtod (parts[1], NULL);
		return TRUE;
	}
	return FALSE;
}

static gboolean
to_color (const GValue *value,
          GrlColor     *out)
{
	if (G_VALUE_HOLDS (value, GRL_TYPE_COLOR) && g_value_get_boxed (value) != NULL)
	{
		*out = *(const GrlColor *) g_value_get_boxed (value);
		return TRUE;
	}
	if (G_VALUE_HOLDS_STRING (value) && g_value_get_string (value) != NULL)
	{
		const gchar *s = g_value_get_string (value);
		guint64      v;
		gsize        len;

		if (*s != '#')
			return FALSE;
		len = strlen (s + 1);
		v = g_ascii_strtoull (s + 1, NULL, 16);
		if (len == 6)
			v = (v << 8) | 0xff;
		else if (len != 8)
			return FALSE;
		out->r = (guint8) ((v >> 24) & 0xff);
		out->g = (guint8) ((v >> 16) & 0xff);
		out->b = (guint8) ((v >> 8) & 0xff);
		out->a = (guint8) (v & 0xff);
		return TRUE;
	}
	return FALSE;
}

gboolean
lrg_editor_water_body_apply_field (LrgWaterBody *body,
                                   const gchar  *name,
                                   const GValue *value)
{
	gdouble  d;
	gfloat   pair[2], a, b, c, e;
	GrlColor col, shallow, deep, foam;

	g_return_val_if_fail (LRG_IS_WATER_BODY (body), FALSE);
	g_return_val_if_fail (name != NULL && value != NULL, FALSE);

	if (g_strcmp0 (name, "name") == 0 && G_VALUE_HOLDS_STRING (value))
		lrg_water_body_set_name (body, g_value_get_string (value));
	else if (g_strcmp0 (name, "shape") == 0)
	{
		GEnumClass *klass = g_type_class_ref (LRG_TYPE_WATER_BODY_SHAPE);
		GEnumValue *ev = G_VALUE_HOLDS_STRING (value) ? g_enum_get_value_by_nick (klass, g_value_get_string (value))
		                                              : NULL;

		if (ev == NULL && G_VALUE_HOLDS_ENUM (value))
			ev = g_enum_get_value (klass, g_value_get_enum (value));
		g_type_class_unref (klass);
		if (ev == NULL)
			return FALSE;
		lrg_water_body_set_shape (body, (LrgWaterBodyShape) ev->value);
	}
	else if (g_strcmp0 (name, "level") == 0 && to_double (value, &d))
		lrg_water_body_set_level (body, (gfloat) d);
	else if (g_strcmp0 (name, "size") == 0 && to_pair (value, pair) && pair[0] > 0.0f && pair[1] > 0.0f)
		lrg_water_body_set_size (body, pair[0], pair[1]);
	else if (g_strcmp0 (name, "cell-size") == 0 && to_double (value, &d) && d > 0.0)
		lrg_water_body_set_cell_size (body, (gfloat) d);
	else if (g_strcmp0 (name, "wave-scale") == 0 && to_double (value, &d) && d >= 0.0)
		lrg_water_body_set_wave_scale (body, (gfloat) d);
	else if (g_strcmp0 (name, "foam") == 0 && to_pair (value, pair))
		lrg_water_body_set_foam (body, MAX (pair[0], 0.0f), MAX (pair[1], 0.0f));
	else if (g_str_has_suffix (name, "-color") && to_color (value, &col))
	{
		lrg_water_body_get_colors (body, &shallow, &deep, &foam);
		if (g_strcmp0 (name, "shallow-color") == 0)
			shallow = col;
		else if (g_strcmp0 (name, "deep-color") == 0)
			deep = col;
		else if (g_strcmp0 (name, "foam-color") == 0)
			foam = col;
		else
			return FALSE;
		lrg_water_body_set_colors (body, &shallow, &deep, &foam);
	}
	else if ((g_strcmp0 (name, "transparency") == 0 || g_strcmp0 (name, "reflection") == 0 ||
	          g_strcmp0 (name, "refraction") == 0 || g_strcmp0 (name, "reflect-distance") == 0) &&
	         to_double (value, &d))
	{
		lrg_water_body_get_optics (body, &a, &b, &c, &e);
		if (g_strcmp0 (name, "transparency") == 0)
			a = (gfloat) CLAMP (d, 0.0, 1.0);
		else if (g_strcmp0 (name, "reflection") == 0)
			b = (gfloat) CLAMP (d, 0.0, 1.0);
		else if (g_strcmp0 (name, "refraction") == 0)
			c = (gfloat) MAX (d, 0.0);
		else
			e = (gfloat) MAX (d, 0.0);
		lrg_water_body_set_optics (body, a, b, c, e);
	}
	else
		return FALSE;
	return TRUE;
}

/* ==========================================================================
 * The editor
 * ========================================================================== */

#define EDITOR_TYPE_WATER_SOURCE (editor_water_source_get_type ())
G_DECLARE_FINAL_TYPE (EditorWaterSource, editor_water_source, EDITOR, WATER_SOURCE, GObject)

struct _EditorWaterSource
{
	GObject               parent_instance;
	LrgEditorWaterEditor *editor;   /* weak */
	Part                  part;
};

static void editor_water_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorWaterSource, editor_water_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, editor_water_source_iface_init))

struct _LrgEditorWaterEditor
{
	LrgEditorDataEditor  parent_instance;

	LrgWaterBody        *body;         /* parsed from the document */
	gchar               *load_error;
	gint                 sel_wave;
	gint                 sel_point;
	guint                gesture;
	gchar               *signature;

	LrgPropertyGrid     *grid;
	LrgPropertyGrid     *wave_grid;
	LrgPropertyGrid     *point_grid;
	LrgWidget           *wave_list;
	LrgWidget           *point_list;
	LrgWidget           *point_heading;
	LrgWidget           *add_wave_chip;
	EditorWaterSource   *src[3];

	/* preview */
	LrgViewport         *preview;
	LrgWidget           *caption;
	LrgWorld3DRenderer  *renderer;
	LrgRenderList       *list;
	LrgEntity3D         *entity;
	LrgWaterComponent   *comp;
	gfloat               yaw;
	gfloat               pitch;
	gfloat               distance;
	gfloat               time;
	gboolean             orbiting;
	gboolean             body_changed;
};

G_DEFINE_FINAL_TYPE (LrgEditorWaterEditor, lrg_editor_water_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void sync_views (LrgEditorWaterEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorWaterEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorWaterEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

/* ==========================================================================
 * Edits: dup, change, commit
 * ========================================================================== */

typedef gboolean (*BodyChange) (LrgWaterBody *body,
                                gpointer      data,
                                GError      **error);

static gboolean
edit_body (LrgEditorWaterEditor  *self,
           const gchar           *label,
           guint                  gesture,
           BodyChange             change,
           gpointer               data,
           GError               **error)
{
	g_autoptr(GObject) obj = lrg_editor_document_dup_object (doc_of (self), error);

	if (obj == NULL)
		return FALSE;
	if (!change (LRG_WATER_BODY (obj), data, error))
		return FALSE;
	return lrg_editor_document_commit_object (doc_of (self), obj, label, gesture, error);
}

typedef struct
{
	const gchar        *name;
	const GValue       *value;
	guint               index;
	gint                at;
	const gfloat       *point;
	const LrgWaterWave *wave;
	gint                result;
} Change;

static gboolean
change_field (LrgWaterBody  *body,
              gpointer       data,
              GError       **error)
{
	Change *c = data;

	if (lrg_editor_water_body_apply_field (body, c->name, c->value))
		return TRUE;
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Cannot set water field \"%s\"", c->name);
	return FALSE;
}

static gboolean
change_wave (LrgWaterBody  *body,
             gpointer       data,
             GError       **error)
{
	Change *c = data;

	if (c->index >= MAX_WAVES)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "A body has at most %d waves", MAX_WAVES);
		return FALSE;
	}
	if (lrg_water_body_get_wave_count (body) <= c->index)
		lrg_water_body_set_wave_count (body, c->index + 1);
	lrg_water_body_set_wave (body, c->index, c->wave);
	return TRUE;
}

static gboolean
change_wave_count (LrgWaterBody  *body,
                   gpointer       data,
                   GError       **error)
{
	Change *c = data;
	guint   old = lrg_water_body_get_wave_count (body), i;

	if (c->index > MAX_WAVES)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "A body has at most %d waves", MAX_WAVES);
		return FALSE;
	}
	lrg_water_body_set_wave_count (body, c->index);
	for (i = old; i < c->index; i++)
	{
		/* a gentle swell turned a little from the last one */
		gfloat       a = 0.4f + 0.9f * (gfloat) i;
		LrgWaterWave w;

		w.direction[0] = cosf (a);
		w.direction[1] = sinf (a);
		w.amplitude = 0.08f / (gfloat) (i + 1);
		w.wavelength = 7.0f / (gfloat) (i + 1);
		w.speed = 1.2f;
		w.steepness = 0.3f;
		lrg_water_body_set_wave (body, i, &w);
	}
	return TRUE;
}

static gboolean
change_add_point (LrgWaterBody  *body,
                  gpointer       data,
                  GError       **error)
{
	Change *c = data;
	guint   n = lrg_water_body_get_n_points (body), i;
	GArray *pts;

	if (n >= LRG_WATER_BODY_MAX_POINTS)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Too many points");
		return FALSE;
	}
	if (c->at < 0 || (guint) c->at >= n)
	{
		c->result = lrg_water_body_add_point (body, c->point[0], c->point[1], c->point[2], c->point[3]);
		return c->result >= 0;
	}

	/* insert: rebuild the outline with the point at @at */
	pts = g_array_new (FALSE, FALSE, sizeof (gfloat));
	for (i = 0; i < n; i++)
	{
		gfloat p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		if ((gint) i == c->at)
			g_array_append_vals (pts, c->point, 4);
		lrg_water_body_get_point (body, i, p);
		g_array_append_vals (pts, p, 4);
	}
	lrg_water_body_clear_points (body);
	for (i = 0; i + 3 < pts->len; i += 4)
		lrg_water_body_add_point (body, g_array_index (pts, gfloat, i), g_array_index (pts, gfloat, i + 1),
		                          g_array_index (pts, gfloat, i + 2), g_array_index (pts, gfloat, i + 3));
	g_array_unref (pts);
	c->result = c->at;
	return TRUE;
}

static gboolean
change_set_point (LrgWaterBody  *body,
                  gpointer       data,
                  GError       **error)
{
	Change *c = data;

	if (lrg_water_body_set_point (body, c->index, c->point[0], c->point[1], c->point[2], c->point[3]))
		return TRUE;
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "No point %u", c->index);
	return FALSE;
}

static gboolean
change_remove_point (LrgWaterBody  *body,
                     gpointer       data,
                     GError       **error)
{
	Change *c = data;

	if (lrg_water_body_remove_point (body, c->index))
		return TRUE;
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "No point %u", c->index);
	return FALSE;
}

/* ==========================================================================
 * Public
 * ========================================================================== */

LrgWaterBody *
lrg_editor_water_editor_get_body (LrgEditorWaterEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), NULL);
	return self->body;
}

LrgPropertyGrid *
lrg_editor_water_editor_get_grid (LrgEditorWaterEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), NULL);
	return self->grid;
}

LrgPropertyGrid *
lrg_editor_water_editor_get_wave_grid (LrgEditorWaterEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), NULL);
	return self->wave_grid;
}

LrgPropertyGrid *
lrg_editor_water_editor_get_point_grid (LrgEditorWaterEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), NULL);
	return self->point_grid;
}

LrgViewport *
lrg_editor_water_editor_get_preview (LrgEditorWaterEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), NULL);
	return self->preview;
}

gint
lrg_editor_water_editor_get_selected_wave (LrgEditorWaterEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), -1);
	return self->sel_wave;
}

void
lrg_editor_water_editor_set_selected_wave (LrgEditorWaterEditor *self,
                                           gint                  index)
{
	g_return_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self));
	if (self->body == NULL || index < 0 || (guint) index >= lrg_water_body_get_wave_count (self->body))
		index = -1;
	self->sel_wave = index;
	sync_views (self);
}

gint
lrg_editor_water_editor_get_selected_point (LrgEditorWaterEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), -1);
	return self->sel_point;
}

void
lrg_editor_water_editor_set_selected_point (LrgEditorWaterEditor *self,
                                            gint                  index)
{
	g_return_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self));
	if (self->body == NULL || index < 0 || (guint) index >= lrg_water_body_get_n_points (self->body))
		index = -1;
	self->sel_point = index;
	sync_views (self);
}

gboolean
lrg_editor_water_editor_set_field (LrgEditorWaterEditor  *self,
                                   const gchar           *name,
                                   const GValue          *value,
                                   guint                  gesture,
                                   GError               **error)
{
	Change            c = { 0 };
	g_autofree gchar *label = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), FALSE);
	g_return_val_if_fail (name != NULL && value != NULL, FALSE);
	c.name = name;
	c.value = value;
	label = g_strdup_printf ("Water: %s", name);
	return edit_body (self, label, gesture, change_field, &c, error);
}

gboolean
lrg_editor_water_editor_set_wave (LrgEditorWaterEditor  *self,
                                  guint                  index,
                                  const LrgWaterWave    *wave,
                                  guint                  gesture,
                                  GError               **error)
{
	Change c = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), FALSE);
	g_return_val_if_fail (wave != NULL, FALSE);
	c.index = index;
	c.wave = wave;
	return edit_body (self, "Water: wave", gesture, change_wave, &c, error);
}

gboolean
lrg_editor_water_editor_set_wave_count (LrgEditorWaterEditor  *self,
                                        guint                  count,
                                        GError               **error)
{
	Change c = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), FALSE);
	c.index = count;
	if (!edit_body (self, "Water: waves", 0, change_wave_count, &c, error))
		return FALSE;
	if (self->sel_wave >= (gint) count || (self->sel_wave < 0 && count > 0))
		self->sel_wave = (gint) count - 1;
	sync_views (self);
	return TRUE;
}

gint
lrg_editor_water_editor_add_point (LrgEditorWaterEditor  *self,
                                   gint                   index,
                                   const gfloat          *point,
                                   GError               **error)
{
	Change c = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), -1);
	g_return_val_if_fail (point != NULL, -1);
	c.at = index;
	c.point = point;
	c.result = -1;
	if (!edit_body (self, "Water: add point", 0, change_add_point, &c, error))
		return -1;
	self->sel_point = c.result;
	sync_views (self);
	return c.result;
}

gboolean
lrg_editor_water_editor_set_point (LrgEditorWaterEditor  *self,
                                   guint                  index,
                                   const gfloat          *point,
                                   guint                  gesture,
                                   GError               **error)
{
	Change c = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), FALSE);
	g_return_val_if_fail (point != NULL, FALSE);
	c.index = index;
	c.point = point;
	return edit_body (self, "Water: point", gesture, change_set_point, &c, error);
}

gboolean
lrg_editor_water_editor_remove_point (LrgEditorWaterEditor  *self,
                                      guint                  index,
                                      GError               **error)
{
	Change c = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_WATER_EDITOR (self), FALSE);
	c.index = index;
	return edit_body (self, "Water: remove point", 0, change_remove_point, &c, error);
}

void
lrg_editor_water_editor_sync_from_store (LrgEditorApp *app,
                                         const gchar  *path)
{
	LrgEditorDataEditor *ed;
	LrgWaterBody        *stored;
	g_autoptr(GError)    error = NULL;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));
	g_return_if_fail (path != NULL);

	ed = lrg_editor_data_editor_find (app, path);
	if (ed == NULL || !LRG_IS_EDITOR_WATER_EDITOR (ed))
		return;
	stored = lrg_editor_terrain_store_get_water (lrg_editor_app_get_editor (app), path, NULL);
	if (stored == NULL)
		return;
	if (!lrg_editor_document_commit_object (doc_of (LRG_EDITOR_WATER_EDITOR (ed)), G_OBJECT (stored),
	                                        "Water: outline", 0, &error))
		g_message ("Water: %s", error->message);
}

/* ==========================================================================
 * Property source: the body, the selected wave, the selected point
 * ========================================================================== */

static GParamSpec *
vpspec (const gchar *name,
        GType        type)
{
	static GHashTable *specs = NULL;
	g_autofree gchar  *key = g_strdup_printf ("%s/%s", name, g_type_name (type));
	GParamSpec        *pspec;

	if (specs == NULL)
		specs = g_hash_table_new (g_str_hash, g_str_equal);
	pspec = g_hash_table_lookup (specs, key);
	if (pspec != NULL)
		return pspec;
	if (type == G_TYPE_STRING)
		pspec = g_param_spec_string (g_intern_string (name), NULL, NULL, NULL,
		                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	else if (type == G_TYPE_DOUBLE)
		pspec = g_param_spec_double (g_intern_string (name), NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
		                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	else
		pspec = g_param_spec_boxed (g_intern_string (name), NULL, NULL, type,
		                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_param_spec_ref_sink (pspec);
	g_hash_table_insert (specs, (gpointer) g_intern_string (key), pspec);
	return pspec;
}

static void
add_row (GPtrArray   *infos,
         const gchar *name,
         GType        type,
         const gchar *label,
         const gchar *group,
         const gchar *hint,
         const gchar *tooltip)
{
	LrgPropertyInfo *info = lrg_property_info_new (vpspec (name, type));

	lrg_property_info_set_label (info, label);
	if (group != NULL)
		lrg_property_info_set_group (info, group);
	if (hint != NULL)
		lrg_property_info_set_hint (info, hint);
	if (tooltip != NULL)
		lrg_property_info_set_tooltip (info, tooltip);
	g_ptr_array_add (infos, info);
}

static GPtrArray *
editor_water_source_list_properties (LrgPropertySource *source)
{
	EditorWaterSource    *src = EDITOR_WATER_SOURCE (source);
	LrgEditorWaterEditor *ed = src->editor;
	GPtrArray            *infos = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	LrgWaterBodyShape     shape;

	if (ed == NULL || ed->body == NULL)
		return infos;
	shape = lrg_water_body_get_shape (ed->body);
	switch (src->part)
	{
	case PART_BODY:
		add_row (infos, "name", G_TYPE_STRING, "Name", "Body", NULL, NULL);
		add_row (infos, "shape", G_TYPE_STRING, "Shape", "Body", "enum-strings:plane,lake,river",
		         "plane: a rectangle; lake: a polygon; river: a spline with a width per point");
		if (shape != LRG_WATER_BODY_RIVER)
			add_row (infos, "level", G_TYPE_DOUBLE, "Level", "Body", "range:-1000,1000,0.05",
			         "Rest height of the surface (body-local)");
		if (shape == LRG_WATER_BODY_PLANE)
			add_row (infos, "size", GRL_TYPE_VECTOR2, "Size", "Body", "vector2", "Width and length");
		add_row (infos, "cell-size", G_TYPE_DOUBLE, "Cell size", "Body", "range:0.05,64,0.05",
		         "Surface grid spacing: smaller is finer and slower");
		add_row (infos, "wave-scale", G_TYPE_DOUBLE, "Wave scale", "Waves", "range:0,4,0.01",
		         "Multiplies every wave's amplitude");
		add_row (infos, "foam", GRL_TYPE_VECTOR2, "Foam", "Foam", "vector2",
		         "Band width along the outline and the shallow depth that foams");
		add_row (infos, "shallow-color", GRL_TYPE_COLOR, "Shallow", "Colours", "color", NULL);
		add_row (infos, "deep-color", GRL_TYPE_COLOR, "Deep", "Colours", "color", NULL);
		add_row (infos, "foam-color", GRL_TYPE_COLOR, "Foam", "Colours", "color", NULL);
		add_row (infos, "transparency", G_TYPE_DOUBLE, "Transparency", "Optics", "range:0,1,0.01", NULL);
		add_row (infos, "reflection", G_TYPE_DOUBLE, "Reflection", "Optics", "range:0,1,0.01", NULL);
		add_row (infos, "refraction", G_TYPE_DOUBLE, "Refraction", "Optics", "range:0,1,0.01",
		         "Distortion of what is seen through the surface");
		add_row (infos, "reflect-distance", G_TYPE_DOUBLE, "Reflect dist.", "Optics", "range:0,2000,1",
		         "How far reflections reach");
		break;
	case PART_WAVE:
		if (ed->sel_wave < 0)
			break;
		add_row (infos, "direction", GRL_TYPE_VECTOR2, "Direction", NULL, "vector2",
		         "Travel direction on the XZ plane (normalised)");
		add_row (infos, "amplitude", G_TYPE_DOUBLE, "Amplitude", NULL, "range:0,4,0.005", "Crest height");
		add_row (infos, "wavelength", G_TYPE_DOUBLE, "Wavelength", NULL, "range:0.1,200,0.1", "Crest to crest");
		add_row (infos, "speed", G_TYPE_DOUBLE, "Speed", NULL, "range:0,20,0.05", "Units per second");
		add_row (infos, "steepness", G_TYPE_DOUBLE, "Steepness", NULL, "range:0,1,0.01",
		         "0 a sine swell, 1 sharp crests");
		break;
	case PART_POINT:
	default:
		if (ed->sel_point < 0)
			break;
		add_row (infos, "x", G_TYPE_DOUBLE, "X", NULL, "range:-10000,10000,0.1", NULL);
		if (shape == LRG_WATER_BODY_RIVER)
			add_row (infos, "y", G_TYPE_DOUBLE, "Y", NULL, "range:-1000,1000,0.05", "Surface height here");
		add_row (infos, "z", G_TYPE_DOUBLE, "Z", NULL, "range:-10000,10000,0.1", NULL);
		if (shape == LRG_WATER_BODY_RIVER)
			add_row (infos, "width", G_TYPE_DOUBLE, "Width", NULL, "range:0.1,1000,0.1", "River width here");
		break;
	}
	return infos;
}

static void
value_color (GValue         *v,
             const GrlColor *c)
{
	g_value_init (v, GRL_TYPE_COLOR);
	g_value_set_boxed (v, c);
}

static void
value_pair (GValue *v,
            gfloat  a,
            gfloat  b)
{
	GrlVector2 p;

	p.x = a;
	p.y = b;
	g_value_init (v, GRL_TYPE_VECTOR2);
	g_value_set_boxed (v, &p);
}

static void
value_double (GValue *v,
              gfloat  d)
{
	g_value_init (v, G_TYPE_DOUBLE);
	g_value_set_double (v, (gdouble) d);
}

static gboolean
editor_water_source_get_value (LrgPropertySource *source,
                               const gchar       *name,
                               GValue            *value)
{
	EditorWaterSource    *src = EDITOR_WATER_SOURCE (source);
	LrgEditorWaterEditor *ed = src->editor;
	LrgWaterBody         *body = (ed != NULL) ? ed->body : NULL;
	gfloat                a, b, c, d;
	GrlColor              shallow, deep, foam;

	if (body == NULL)
		return FALSE;
	if (src->part == PART_WAVE)
	{
		LrgWaterWave w;

		if (ed->sel_wave < 0 || !lrg_water_body_get_wave (body, (guint) ed->sel_wave, &w))
			return FALSE;
		if (g_strcmp0 (name, "direction") == 0)
			value_pair (value, w.direction[0], w.direction[1]);
		else if (g_strcmp0 (name, "amplitude") == 0)
			value_double (value, w.amplitude);
		else if (g_strcmp0 (name, "wavelength") == 0)
			value_double (value, w.wavelength);
		else if (g_strcmp0 (name, "speed") == 0)
			value_double (value, w.speed);
		else if (g_strcmp0 (name, "steepness") == 0)
			value_double (value, w.steepness);
		else
			return FALSE;
		return TRUE;
	}
	if (src->part == PART_POINT)
	{
		gfloat p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		if (ed->sel_point < 0 || !lrg_water_body_get_point (body, (guint) ed->sel_point, p))
			return FALSE;
		if (g_strcmp0 (name, "x") == 0)
			value_double (value, p[0]);
		else if (g_strcmp0 (name, "y") == 0)
			value_double (value, p[1]);
		else if (g_strcmp0 (name, "z") == 0)
			value_double (value, p[2]);
		else if (g_strcmp0 (name, "width") == 0)
			value_double (value, p[3]);
		else
			return FALSE;
		return TRUE;
	}

	if (g_strcmp0 (name, "name") == 0 || g_strcmp0 (name, "shape") == 0)
	{
		g_value_init (value, G_TYPE_STRING);
		if (g_strcmp0 (name, "name") == 0)
			g_value_set_string (value, lrg_water_body_get_name (body));
		else
		{
			GEnumClass *klass = g_type_class_ref (LRG_TYPE_WATER_BODY_SHAPE);
			GEnumValue *ev = g_enum_get_value (klass, (gint) lrg_water_body_get_shape (body));

			g_value_set_string (value, ev != NULL ? ev->value_nick : "");
			g_type_class_unref (klass);
		}
		return TRUE;
	}
	if (g_strcmp0 (name, "level") == 0)
		value_double (value, lrg_water_body_get_level (body));
	else if (g_strcmp0 (name, "size") == 0)
	{
		lrg_water_body_get_size (body, &a, &b);
		value_pair (value, a, b);
	}
	else if (g_strcmp0 (name, "cell-size") == 0)
		value_double (value, lrg_water_body_get_cell_size (body));
	else if (g_strcmp0 (name, "wave-scale") == 0)
		value_double (value, lrg_water_body_get_wave_scale (body));
	else if (g_strcmp0 (name, "foam") == 0)
	{
		lrg_water_body_get_foam (body, &a, &b);
		value_pair (value, a, b);
	}
	else if (g_str_has_suffix (name, "-color"))
	{
		lrg_water_body_get_colors (body, &shallow, &deep, &foam);
		value_color (value, g_strcmp0 (name, "shallow-color") == 0 ? &shallow
		                    : g_strcmp0 (name, "deep-color") == 0 ? &deep : &foam);
	}
	else
	{
		lrg_water_body_get_optics (body, &a, &b, &c, &d);
		if (g_strcmp0 (name, "transparency") == 0)
			value_double (value, a);
		else if (g_strcmp0 (name, "reflection") == 0)
			value_double (value, b);
		else if (g_strcmp0 (name, "refraction") == 0)
			value_double (value, c);
		else if (g_strcmp0 (name, "reflect-distance") == 0)
			value_double (value, d);
		else
			return FALSE;
	}
	return TRUE;
}

static gboolean
editor_water_source_set_value (LrgPropertySource *source,
                               const gchar       *name,
                               const GValue      *value)
{
	EditorWaterSource    *src = EDITOR_WATER_SOURCE (source);
	LrgEditorWaterEditor *ed = src->editor;
	g_autoptr(GError)     error = NULL;
	gboolean              ok = FALSE;
	gdouble               d;

	if (ed == NULL || ed->body == NULL)
		return FALSE;
	if (src->part == PART_BODY)
		ok = lrg_editor_water_editor_set_field (ed, name, value, ed->gesture, &error);
	else if (src->part == PART_WAVE && ed->sel_wave >= 0)
	{
		LrgWaterWave w;
		gfloat       pair[2];

		if (lrg_water_body_get_wave (ed->body, (guint) ed->sel_wave, &w))
		{
			if (g_strcmp0 (name, "direction") == 0 && to_pair (value, pair))
			{
				w.direction[0] = pair[0];
				w.direction[1] = pair[1];
				ok = TRUE;
			}
			else if (to_double (value, &d))
			{
				ok = TRUE;
				if (g_strcmp0 (name, "amplitude") == 0)
					w.amplitude = (gfloat) MAX (d, 0.0);
				else if (g_strcmp0 (name, "wavelength") == 0)
					w.wavelength = (gfloat) MAX (d, 0.01);
				else if (g_strcmp0 (name, "speed") == 0)
					w.speed = (gfloat) d;
				else if (g_strcmp0 (name, "steepness") == 0)
					w.steepness = (gfloat) CLAMP (d, 0.0, 1.0);
				else
					ok = FALSE;
			}
			if (ok)
				ok = lrg_editor_water_editor_set_wave (ed, (guint) ed->sel_wave, &w, ed->gesture, &error);
		}
	}
	else if (src->part == PART_POINT && ed->sel_point >= 0 && to_double (value, &d))
	{
		gfloat p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		if (lrg_water_body_get_point (ed->body, (guint) ed->sel_point, p))
		{
			ok = TRUE;
			if (g_strcmp0 (name, "x") == 0)
				p[0] = (gfloat) d;
			else if (g_strcmp0 (name, "y") == 0)
				p[1] = (gfloat) d;
			else if (g_strcmp0 (name, "z") == 0)
				p[2] = (gfloat) d;
			else if (g_strcmp0 (name, "width") == 0)
				p[3] = (gfloat) MAX (d, 0.1);
			else
				ok = FALSE;
			if (ok)
				ok = lrg_editor_water_editor_set_point (ed, (guint) ed->sel_point, p, ed->gesture, &error);
		}
	}
	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (ed), error);
	if (ok)
		lrg_property_source_emit_changed (source, name);
	return ok;
}

static void
editor_water_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = editor_water_source_list_properties;
	iface->get_value = editor_water_source_get_value;
	iface->set_value = editor_water_source_set_value;
}

static void
editor_water_source_finalize (GObject *object)
{
	g_clear_weak_pointer (&EDITOR_WATER_SOURCE (object)->editor);
	G_OBJECT_CLASS (editor_water_source_parent_class)->finalize (object);
}

static void
editor_water_source_class_init (EditorWaterSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = editor_water_source_finalize;
}

static void
editor_water_source_init (EditorWaterSource *self)
{
}

static void
on_edit_begin (LrgPropertyGrid *grid,
               const gchar     *name,
               guint            gesture_id,
               gpointer         user_data)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (user_data);

	self->gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
}

static void
on_edit_end (LrgPropertyGrid *grid,
             const gchar     *name,
             guint            gesture_id,
             gboolean         commit_,
             gpointer         user_data)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (user_data);

	self->gesture = 0;
	sync_views (self);
}

/* ==========================================================================
 * Wave and point lists
 * ========================================================================== */

static guint
list_count (LrgEditorWaterEditor *self,
            gboolean              points)
{
	if (self->body == NULL)
		return 0;
	return points ? lrg_water_body_get_n_points (self->body) : lrg_water_body_get_wave_count (self->body);
}

static void
list_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (user_data);
	gboolean              points = widget == LRG_CUSTOM_WIDGET (self->point_list);
	guint                 n = list_count (self, points);

	size->width = 160.0f;
	size->height = (gfloat) MAX (MIN (n, 12u), 1u) * LIST_ROW_H + 2.0f;
}

static void
list_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (user_data);
	LrgTheme             *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gboolean              points = widget == LRG_CUSTOM_WIDGET (self->point_list);
	guint                 n = list_count (self, points), i;
	gint                  sel = points ? self->sel_point : self->sel_wave;
	gfloat                fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlFont              *font = lrg_theme_get_ui_font (theme);
	const GrlColor       *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor       *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	GrlColor              track = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);

	track.a = (guint8) (track.a / 2);
	lrg_paint_list_rect (list, x, y, width, height, radius, &track);
	if (n == 0)
	{
		lrg_paint_list_text (list, font, points ? "No points" : "No waves: a flat surface", x + 10.0f,
		                     roundf (y + (LIST_ROW_H - fs) * 0.5f), fs, 0.0f, muted);
		return;
	}
	lrg_paint_list_push_clip (list, x, y, width, height);
	for (i = 0; i < n; i++)
	{
		gfloat            ry = y + 1.0f + (gfloat) i * LIST_ROW_H;
		g_autofree gchar *label = NULL;
		gfloat            is = 12.0f;
		gfloat            bx = x + width - LIST_BTN_W - 2.0f;

		if (ry > y + height)
			break;
		if ((gint) i == sel)
			lrg_paint_list_rect (list, x + 2.0f, ry, width - 4.0f, LIST_ROW_H - 1.0f, radius,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION));
		if (points)
		{
			gfloat p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

			lrg_water_body_get_point (self->body, i, p);
			if (lrg_water_body_get_shape (self->body) == LRG_WATER_BODY_RIVER)
				label = g_strdup_printf ("%u   %.2f, %.2f, %.2f   width %.2f", i, (gdouble) p[0], (gdouble) p[1],
				                         (gdouble) p[2], (gdouble) p[3]);
			else
				label = g_strdup_printf ("%u   %.2f, %.2f", i, (gdouble) p[0], (gdouble) p[2]);
		}
		else
		{
			LrgWaterWave w;

			lrg_water_body_get_wave (self->body, i, &w);
			label = g_strdup_printf ("%u   dir %.2f, %.2f   amp %.3g   len %.3g   speed %.3g", i,
			                         (gdouble) w.direction[0], (gdouble) w.direction[1], (gdouble) w.amplitude,
			                         (gdouble) w.wavelength, (gdouble) w.speed);
		}
		lrg_paint_list_circle (list, x + 11.0f, ry + LIST_ROW_H * 0.5f, 3.5f,
		                       lrg_theme_get_color (theme, points ? LRG_THEME_COLOR_HANDLE : LRG_THEME_COLOR_LINK));
		lrg_paint_list_push_clip (list, x + 20.0f, ry, MAX (width - 20.0f - LIST_BTN_W - 6.0f, 0.0f), LIST_ROW_H);
		lrg_paint_list_text (list, font, label, x + 20.0f, roundf (ry + (LIST_ROW_H - fs) * 0.5f - 1.0f), fs, 0.0f,
		                     text);
		lrg_paint_list_pop_clip (list);
		lrg_paint_list_icon (list, LRG_ICON_TRASH, bx + (LIST_BTN_W - is) * 0.5f, ry + (LIST_ROW_H - is) * 0.5f, is,
		                     0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
	}
	lrg_paint_list_pop_clip (list);
}

static gboolean
list_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (user_data);
	gboolean              points = widget == LRG_CUSTOM_WIDGET (self->point_list);
	gfloat                x = lrg_widget_get_world_x (LRG_WIDGET (widget));
	gfloat                y = lrg_widget_get_world_y (LRG_WIDGET (widget));
	gfloat                w = lrg_widget_get_width (LRG_WIDGET (widget));
	g_autoptr(GError)     error = NULL;
	guint                 n, row;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || lrg_ui_event_get_button (event) != 0)
		return FALSE;
	n = list_count (self, points);
	if (lrg_ui_event_get_y (event) < y + 1.0f || n == 0)
		return FALSE;
	row = (guint) ((lrg_ui_event_get_y (event) - y - 1.0f) / LIST_ROW_H);
	if (row >= n)
		return TRUE;
	if (lrg_ui_event_get_x (event) >= x + w - LIST_BTN_W - 2.0f)
	{
		if (points)
			lrg_editor_water_editor_remove_point (self, row, &error);
		else
		{
			/* waves are a list: remove by shifting the rest down */
			LrgWaterWave waves[MAX_WAVES];
			guint        i, k = 0;

			for (i = 0; i < n && i < MAX_WAVES; i++)
				if (i != row)
					lrg_water_body_get_wave (self->body, i, &waves[k++]);
			for (i = 0; i < k && error == NULL; i++)
				lrg_editor_water_editor_set_wave (self, i, &waves[i], 0, &error);
			if (error == NULL)
				lrg_editor_water_editor_set_wave_count (self, k, &error);
		}
		if (error != NULL)
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return TRUE;
	}
	if (points)
		lrg_editor_water_editor_set_selected_point (self, (gint) row);
	else
		lrg_editor_water_editor_set_selected_wave (self, (gint) row);
	return TRUE;
}

static void
on_add_wave (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (user_data);
	g_autoptr(GError)     error = NULL;
	guint                 n = list_count (self, FALSE);

	if (n >= MAX_WAVES)
		return;
	if (lrg_editor_water_editor_set_wave_count (self, n + 1, &error))
		lrg_editor_water_editor_set_selected_wave (self, (gint) n);
	else
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_add_point (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (user_data);
	g_autoptr(GError)     error = NULL;
	gfloat                p[4] = { 0.0f, 0.0f, 0.0f, 4.0f };
	guint                 n = list_count (self, TRUE);

	/* after the selected point: halfway to the next one */
	if (n > 0)
	{
		guint  at = (self->sel_point >= 0) ? (guint) self->sel_point : n - 1;
		gfloat a[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, b[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		lrg_water_body_get_point (self->body, at, a);
		lrg_water_body_get_point (self->body, (at + 1) % n, b);
		if (n == 1)
		{
			b[0] = a[0] + 5.0f;
			b[2] = a[2];
		}
		p[0] = (a[0] + b[0]) * 0.5f;
		p[1] = (a[1] + b[1]) * 0.5f;
		p[2] = (a[2] + b[2]) * 0.5f;
		p[3] = a[3] > 0.0f ? a[3] : 4.0f;
		if (at + 1 == n)
		{
			/* past the end: continue the last segment */
			gfloat c[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

			if (n > 1 && lrg_water_body_get_shape (self->body) == LRG_WATER_BODY_RIVER)
			{
				lrg_water_body_get_point (self->body, n - 2, c);
				p[0] = a[0] + (a[0] - c[0]);
				p[2] = a[2] + (a[2] - c[2]);
			}
		}
		if (lrg_editor_water_editor_add_point (self, (gint) at + 1 < (gint) n ? (gint) at + 1 : -1, p, &error) < 0)
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return;
	}
	if (lrg_editor_water_editor_add_point (self, -1, p, &error) < 0)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* ==========================================================================
 * Preview
 * ========================================================================== */

static void
preview_render (LrgViewport      *viewport,
                GrlRenderTexture *target,
                gfloat            width,
                gfloat            height,
                gpointer          user_data)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (user_data);
	LrgWorld3DView        rv;
	LrgRenderItem         ground;
	GrlColor              bg = { 30, 30, 46, 255 };
	gfloat                mn[3] = { -10.0f, 0.0f, -10.0f }, mx[3] = { 10.0f, 0.0f, 10.0f };
	gfloat                c[3], ext, cy = cosf (self->pitch);

	if (self->body == NULL || !IsWindowReady ())
		return;
	if (self->renderer == NULL)
	{
		self->renderer = lrg_world3d_renderer_new ();
		lrg_world3d_renderer_set_flags (self->renderer, LRG_WORLD3D_RENDER_FLAG_CLEAR | LRG_WORLD3D_RENDER_FLAG_SKY);
		self->list = lrg_render_list_new ();
		self->entity = lrg_entity3d_new_with_name ("water-preview");
		self->comp = lrg_water_component_new ();
		lrg_entity3d_add_component (self->entity, LRG_COMPONENT (self->comp));
		self->body_changed = TRUE;
	}
	if (self->body_changed)
	{
		/* the component shows its own copy (the parsed body is replaced
		 * on every document change) */
		g_autoptr(LrgWaterBody) copy = lrg_water_body_new (lrg_water_body_get_shape (self->body));

		editor_water_copy_into (copy, self->body);
		lrg_water_component_set_body (self->comp, copy);
		lrg_water_component_rebuild (self->comp);
		self->body_changed = FALSE;
	}
	lrg_water_component_set_time (self->comp, self->time);

	/* the camera orbits the body's centre */
	lrg_water_body_get_bounds (self->body, mn, mx);
	c[0] = (mn[0] + mx[0]) * 0.5f;
	c[1] = (mn[1] + mx[1]) * 0.5f;
	c[2] = (mn[2] + mx[2]) * 0.5f;
	ext = MAX (MAX (mx[0] - mn[0], mx[2] - mn[2]), 4.0f);
	lrg_world3d_view_init (&rv, MAX (width, 1.0f), MAX (height, 1.0f));
	rv.target[0] = c[0];
	rv.target[1] = c[1];
	rv.target[2] = c[2];
	rv.position[0] = c[0] + cosf (self->yaw) * cy * ext * self->distance;
	rv.position[1] = c[1] + sinf (self->pitch) * ext * self->distance;
	rv.position[2] = c[2] + sinf (self->yaw) * cy * ext * self->distance;
	rv.far_plane = MAX (ext * 8.0f, 200.0f);

	/* a sand plane under the water, then the surface */
	lrg_render_list_clear (self->list);
	lrg_render_list_set_view (self->list, rv.position, LRG_RENDER_LAYERS_MASK_ALL, NULL);
	lrg_render_item_init (&ground);
	memset (ground.world_matrix, 0, sizeof ground.world_matrix);
	ground.world_matrix[0] = ext * 2.0f;
	ground.world_matrix[5] = 1.0f;
	ground.world_matrix[10] = ext * 2.0f;
	ground.world_matrix[12] = c[0];
	ground.world_matrix[13] = mn[1] - 1.5f;
	ground.world_matrix[14] = c[2];
	ground.world_matrix[15] = 1.0f;
	ground.kind = LRG_RENDER_ITEM_PRIMITIVE;
	ground.primitive = LRG_PRIMITIVE_PLANE;
	ground.color[0] = 0.76f;
	ground.color[1] = 0.70f;
	ground.color[2] = 0.52f;
	ground.color[3] = 1.0f;
	ground.flags = LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW;
	lrg_render_list_add (self->list, &ground);
	lrg_component_collect_render (LRG_COMPONENT (self->comp), self->list);
	lrg_render_list_sort (self->list);
	lrg_world3d_renderer_set_background (self->renderer, &bg);
	lrg_world3d_renderer_render_list (self->renderer, self->list, &rv);
}

static void
on_preview_press (LrgViewport       *viewport,
                  gint               button,
                  gfloat             u,
                  gfloat             v,
                  LrgInputModifiers  mods,
                  gpointer           user_data)
{
	LRG_EDITOR_WATER_EDITOR (user_data)->orbiting = TRUE;
}

static void
on_preview_release (LrgViewport       *viewport,
                    gint               button,
                    gfloat             u,
                    gfloat             v,
                    LrgInputModifiers  mods,
                    gpointer           user_data)
{
	LRG_EDITOR_WATER_EDITOR (user_data)->orbiting = FALSE;
}

static void
on_preview_move (LrgViewport *viewport,
                 gfloat       u,
                 gfloat       v,
                 gfloat       dx,
                 gfloat       dy,
                 gpointer     user_data)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (user_data);

	if (!self->orbiting)
		return;
	self->yaw += dx * 0.01f;
	self->pitch = CLAMP (self->pitch + dy * 0.01f, 0.1f, 1.45f);
}

static void
on_preview_scroll (LrgViewport *viewport,
                   gfloat       u,
                   gfloat       v,
                   gfloat       sx,
                   gfloat       sy,
                   gpointer     user_data)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (user_data);

	self->distance = CLAMP (self->distance * (sy > 0.0f ? 0.9f : 1.1f), 0.3f, 6.0f);
}

static void
caption_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (user_data);
	LrgTheme             *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	g_autofree gchar     *text = lrg_editor_data_editor_get_state (LRG_EDITOR_DATA_EDITOR (self));

	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), self->load_error != NULL ? self->load_error : text,
	                     x + 6.0f, y + 4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL), 0.0f,
	                     lrg_theme_get_color (theme, self->load_error != NULL ? LRG_THEME_COLOR_DANGER
	                                                                          : LRG_THEME_COLOR_TEXT_MUTED));
}

/* ==========================================================================
 * Sync
 * ========================================================================== */

static void
reparse (LrgEditorWaterEditor *self)
{
	g_autoptr(GError) error = NULL;
	GObject          *obj = lrg_editor_document_dup_object (doc_of (self), &error);
	const gchar      *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	LrgEditorApp     *app = app_of (self);

	g_clear_object (&self->body);
	g_clear_pointer (&self->load_error, g_free);
	if (obj == NULL)
	{
		self->load_error = g_strdup (error != NULL ? error->message : "The document does not load");
		return;
	}
	self->body = LRG_WATER_BODY (obj);
	self->body_changed = TRUE;

	/* the level's water nodes follow the unsaved document */
	if (app != NULL && path != NULL)
		lrg_editor_terrain_store_set_water (lrg_editor_app_get_editor (app), path, self->body);
}

static void
sync_views (LrgEditorWaterEditor *self)
{
	g_autofree gchar *sig = NULL;
	guint             i;

	if (self->grid == NULL)
		return;
	if (self->body != NULL)
	{
		gint nw = (gint) lrg_water_body_get_wave_count (self->body);
		gint np = (gint) lrg_water_body_get_n_points (self->body);

		if (self->sel_wave >= nw)
			self->sel_wave = nw - 1;
		if (self->sel_point >= np)
			self->sel_point = np - 1;
	}
	else
		self->sel_wave = self->sel_point = -1;

	sig = g_strdup_printf ("%p/%d/%d/%d", (gpointer) (self->body != NULL ? (gpointer) self : NULL),
	                       self->body != NULL ? (gint) lrg_water_body_get_shape (self->body) : -1, self->sel_wave,
	                       self->sel_point);
	if (g_strcmp0 (sig, self->signature) != 0)
	{
		g_free (self->signature);
		self->signature = g_steal_pointer (&sig);
		for (i = 0; i < 3; i++)
			lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self->src[i]), NULL);
		lrg_property_grid_rebuild (self->grid);
		lrg_property_grid_rebuild (self->wave_grid);
		lrg_property_grid_rebuild (self->point_grid);
	}
	else if (self->gesture == 0)
	{
		lrg_property_grid_refresh (self->grid);
		lrg_property_grid_refresh (self->wave_grid);
		lrg_property_grid_refresh (self->point_grid);
	}
	lrg_widget_set_visible (LRG_WIDGET (self->wave_grid), self->sel_wave >= 0);
	lrg_widget_set_visible (LRG_WIDGET (self->point_grid), self->sel_point >= 0);
	{
		gboolean outline = self->body != NULL && lrg_water_body_get_shape (self->body) != LRG_WATER_BODY_PLANE;

		lrg_widget_set_visible (self->point_heading, outline);
		lrg_widget_set_visible (self->point_list, outline);
		if (!outline)
			lrg_widget_set_visible (LRG_WIDGET (self->point_grid), FALSE);
	}
	lrg_widget_set_visible (self->add_wave_chip, list_count (self, FALSE) < MAX_WAVES);
	lrg_widget_queue_layout (self->wave_list);
	lrg_widget_queue_layout (self->point_list);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* ==========================================================================
 * LrgEditorDataEditor vfuncs
 * ========================================================================== */

static void
water_refresh (LrgEditorDataEditor *editor,
               const gchar         *path)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (editor);

	reparse (self);
	sync_views (self);
}

static gchar *
water_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (editor);
	GEnumClass           *klass;
	GEnumValue           *ev;
	gchar                *s;

	if (self->body == NULL)
		return g_strdup ("not loaded");
	klass = g_type_class_ref (LRG_TYPE_WATER_BODY_SHAPE);
	ev = g_enum_get_value (klass, (gint) lrg_water_body_get_shape (self->body));
	s = g_strdup_printf ("%s - %u points - %u waves - level %.2f - wave %d - point %d",
	                     ev != NULL ? ev->value_nick : "?", lrg_water_body_get_n_points (self->body),
	                     lrg_water_body_get_wave_count (self->body), (gdouble) lrg_water_body_get_level (self->body),
	                     self->sel_wave, self->sel_point);
	g_type_class_unref (klass);
	return s;
}

static void
water_validate (LrgEditorDataEditor *editor,
                GPtrArray           *issues)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (editor);
	LrgWaterBodyShape     shape;
	guint                 n;

	if (self->body == NULL)
		return;
	shape = lrg_water_body_get_shape (self->body);
	n = lrg_water_body_get_n_points (self->body);
	if (shape == LRG_WATER_BODY_LAKE && n < 3)
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, "/points", 0, 0,
		                                                        "A lake needs at least three points"));
	if (shape == LRG_WATER_BODY_RIVER && n < 2)
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, "/points", 0, 0,
		                                                        "A river needs at least two points"));
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static gboolean
lrg_editor_water_editor_step (LrgWidget *widget,
                              gfloat     delta_time)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (widget);

	LRG_WIDGET_CLASS (lrg_editor_water_editor_parent_class)->step (widget, delta_time);
	self->time += delta_time;
	return TRUE;   /* the waves keep moving */
}

static gpointer
add_to (gpointer box,
        gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (child));
	g_object_unref (child);
	return child;
}

static LrgWidget *
heading_row (const gchar *name,
             const gchar *text)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());
	LrgWidget *label = LRG_WIDGET (lrg_label_new (text));

	lrg_widget_set_name (row, name);
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	lrg_widget_add_class (label, "heading");
	lrg_widget_set_valign (label, LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (label, 1.0f);
	add_to (row, label);
	return row;
}

static LrgPropertyGrid *
make_grid (LrgEditorWaterEditor *self,
           const gchar          *name,
           Part                  part)
{
	LrgPropertyGrid   *grid = lrg_property_grid_new ();
	EditorWaterSource *src = g_object_new (EDITOR_TYPE_WATER_SOURCE, NULL);

	g_set_weak_pointer (&src->editor, self);
	src->part = part;
	self->src[part] = src;
	lrg_widget_set_name (LRG_WIDGET (grid), name);
	lrg_property_grid_set_label_width (grid, 110.0f);
	g_signal_connect (grid, "edit-begin", G_CALLBACK (on_edit_begin), self);
	g_signal_connect (grid, "edit-end", G_CALLBACK (on_edit_end), self);
	lrg_property_grid_set_source (grid, LRG_PROPERTY_SOURCE (src));
	return grid;
}

static void
lrg_editor_water_editor_constructed (GObject *object)
{
	static const LrgCustomWidgetFuncs list_funcs = { list_measure, list_paint, NULL, list_event, { NULL } };
	static const LrgCustomWidgetFuncs caption_funcs = { NULL, caption_paint, NULL, NULL, { NULL } };
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (object);
	LrgEditorDataEditor  *base = LRG_EDITOR_DATA_EDITOR (object);
	const LrgInsets       pad = { 8.0f, 6.0f, 8.0f, 6.0f };
	LrgWidget            *left, *right, *scroll, *row;
	LrgSplitter          *split;
	LrgEditorApp         *app;

	G_OBJECT_CLASS (lrg_editor_water_editor_parent_class)->constructed (object);

	/* Left: the body form, waves, outline */
	left = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (left, "water-form");
	lrg_container_set_spacing (LRG_CONTAINER (left), 6.0f);
	lrg_widget_set_margin (left, &pad);
	self->grid = add_to (left, make_grid (self, "water-grid", PART_BODY));
	row = heading_row ("water-waves-heading", "Waves");
	self->add_wave_chip = editor_chip_new ("water-add-wave", LRG_ICON_PLUS, "Add", on_add_wave, self);
	add_to (row, self->add_wave_chip);
	add_to (left, row);
	self->wave_list = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->wave_list, "water-wave-list");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->wave_list), &list_funcs, self, NULL);
	add_to (left, self->wave_list);
	self->wave_grid = add_to (left, make_grid (self, "water-wave-grid", PART_WAVE));
	self->point_heading = heading_row ("water-points-heading", "Outline");
	add_to (self->point_heading, editor_chip_new ("water-add-point", LRG_ICON_PLUS, "Add", on_add_point, self));
	add_to (left, self->point_heading);
	self->point_list = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->point_list, "water-point-list");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->point_list), &list_funcs, self, NULL);
	add_to (left, self->point_list);
	self->point_grid = add_to (left, make_grid (self, "water-point-grid", PART_POINT));
	scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (scroll, "water-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), left);
	g_object_unref (left);

	/* Right: the preview and its caption */
	right = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (right, "water-preview-box");
	lrg_container_set_spacing (LRG_CONTAINER (right), 4.0f);
	lrg_widget_set_margin (right, &pad);
	self->preview = lrg_viewport_new ();
	lrg_widget_set_name (LRG_WIDGET (self->preview), "water-preview");
	lrg_viewport_set_auto_clear (self->preview, FALSE);
	lrg_viewport_set_continuous (self->preview, TRUE);
	lrg_viewport_set_render_func (self->preview, preview_render, self, NULL);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->preview), 1.0f);
	g_signal_connect (self->preview, "pointer-press", G_CALLBACK (on_preview_press), self);
	g_signal_connect (self->preview, "pointer-release", G_CALLBACK (on_preview_release), self);
	g_signal_connect (self->preview, "pointer-move", G_CALLBACK (on_preview_move), self);
	g_signal_connect (self->preview, "pointer-scroll", G_CALLBACK (on_preview_scroll), self);
	add_to (right, self->preview);
	self->caption = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->caption, "water-caption");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->caption), &caption_funcs, self, NULL);
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->caption), 22.0f);
	add_to (right, self->caption);

	split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_splitter_set_start_child (split, scroll);
	g_object_unref (scroll);
	lrg_splitter_set_end_child (split, right);
	g_object_unref (right);
	lrg_splitter_set_default_fraction (split, 0.45f);
	lrg_splitter_set_fraction (split, 0.45f, FALSE);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (split));

	/* A level water node may have changed this file without saving it
	 * (viewport handles, water.* on a node): the document starts from the
	 * store's state, as one undo step, so opening the editor loses
	 * nothing */
	app = app_of (self);
	if (app != NULL && lrg_editor_data_editor_get_path (base) != NULL &&
	    lrg_editor_terrain_store_is_dirty (lrg_editor_app_get_editor (app), lrg_editor_data_editor_get_path (base)))
	{
		LrgWaterBody     *stored = lrg_editor_terrain_store_get_water (lrg_editor_app_get_editor (app),
		                                                               lrg_editor_data_editor_get_path (base), NULL);
		g_autoptr(GError) error = NULL;

		if (stored != NULL &&
		    !lrg_editor_document_commit_object (doc_of (self), G_OBJECT (stored), "Water: unsaved level edits", 0, &error))
			g_message ("Water: %s", error->message);
	}

	reparse (self);
	self->sel_wave = (self->body != NULL && lrg_water_body_get_wave_count (self->body) > 0) ? 0 : -1;
	self->sel_point = (self->body != NULL && lrg_water_body_get_n_points (self->body) > 0) ? 0 : -1;
	sync_views (self);

	if (app != NULL)
		lrg_editor_water_editor_install (app);
}

static void
lrg_editor_water_editor_dispose (GObject *object)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (object);
	guint                 i;

	if (self->preview != NULL)
		lrg_viewport_set_render_func (self->preview, NULL, NULL, NULL);
	self->preview = NULL;
	g_clear_object (&self->body);
	g_clear_object (&self->entity);
	self->comp = NULL;
	g_clear_object (&self->list);
	g_clear_object (&self->renderer);
	for (i = 0; i < 3; i++)
		g_clear_object (&self->src[i]);
	G_OBJECT_CLASS (lrg_editor_water_editor_parent_class)->dispose (object);
}

static void
lrg_editor_water_editor_finalize (GObject *object)
{
	LrgEditorWaterEditor *self = LRG_EDITOR_WATER_EDITOR (object);

	g_free (self->signature);
	g_free (self->load_error);
	G_OBJECT_CLASS (lrg_editor_water_editor_parent_class)->finalize (object);
}

static void
lrg_editor_water_editor_class_init (LrgEditorWaterEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_water_editor_constructed;
	object_class->dispose = lrg_editor_water_editor_dispose;
	object_class->finalize = lrg_editor_water_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_water_editor_step;
	data_class->refresh = water_refresh;
	data_class->get_state = water_get_state;
	data_class->validate = water_validate;
}

static void
lrg_editor_water_editor_init (LrgEditorWaterEditor *self)
{
	self->sel_wave = -1;
	self->sel_point = -1;
	self->yaw = 0.8f;
	self->pitch = 0.55f;
	self->distance = 1.6f;
}

LrgEditorWaterEditor *
lrg_editor_water_editor_new (LrgEditorApp      *app,
                             LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_WATER_EDITOR, "app", app, "document", document, NULL);
}

/* ==========================================================================
 * IPC: water.*
 *
 * Every method takes {"path": "levels/lake.rwater"} (the file, through
 * its editor, opened when needed: a document undo step) or {"node": ...}
 * (a level water node: the shared store's body, a level undo step).
 * ========================================================================== */

static JsonNode *
obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static gboolean
p_has (JsonObject  *params,
       const gchar *name)
{
	return params != NULL && json_object_has_member (params, name) &&
	       !JSON_NODE_HOLDS_NULL (json_object_get_member (params, name));
}

static const gchar *
p_string (JsonObject  *params,
          const gchar *name)
{
	JsonNode *n;

	if (!p_has (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING) ? json_node_get_string (n)
	                                                                                    : NULL;
}

static gboolean
p_number (JsonObject  *params,
          const gchar *name,
          gdouble     *out)
{
	JsonNode *n;

	if (!p_has (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	if (json_node_get_value_type (n) == G_TYPE_INT64)
		*out = (gdouble) json_node_get_int (n);
	else if (json_node_get_value_type (n) == G_TYPE_DOUBLE)
		*out = json_node_get_double (n);
	else
		return FALSE;
	return TRUE;
}

static guint
p_floats (JsonObject  *params,
          const gchar *name,
          gfloat      *out,
          guint        max)
{
	JsonArray *a;
	guint      i, len;

	if (!p_has (params, name) || !JSON_NODE_HOLDS_ARRAY (json_object_get_member (params, name)))
		return 0;
	a = json_object_get_array_member (params, name);
	len = MIN (json_array_get_length (a), max);
	for (i = 0; i < len; i++)
		out[i] = (gfloat) json_array_get_double_element (a, i);
	return len;
}

static JsonArray *
floats_array (const gfloat *v,
              guint         n)
{
	JsonArray *a = json_array_new ();
	guint      i;

	for (i = 0; i < n; i++)
		json_array_add_double_element (a, (gdouble) v[i]);
	return a;
}

static JsonArray *
color_array (const GrlColor *c)
{
	gfloat v[4];

	v[0] = (gfloat) c->r / 255.0f;
	v[1] = (gfloat) c->g / 255.0f;
	v[2] = (gfloat) c->b / 255.0f;
	v[3] = (gfloat) c->a / 255.0f;
	return floats_array (v, 4);
}

static gboolean
bad_params (GError      **error,
            const gchar  *message)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, message);
	return FALSE;
}

/* What a water.* call works on. */
typedef struct
{
	LrgEditorApp         *app;
	LrgEditorWaterEditor *editor;   /* path mode */
	LrgNode              *node;     /* node mode */
	gchar                *path;     /* absolute */
	LrgWaterBody         *body;     /* the body shown (transfer none) */
} Target;

static void
target_clear (Target *t)
{
	g_clear_pointer (&t->path, g_free);
}

static gboolean
resolve_target (LrgEditorApp  *app,
                JsonObject    *params,
                Target        *t,
                GError       **error)
{
	LrgEditor *editor = lrg_editor_app_get_editor (app);

	memset (t, 0, sizeof *t);
	t->app = app;
	if (p_has (params, "node"))
	{
		const gchar        *ref = p_string (params, "node");
		LrgWorld3DRenderer *resolver = (app->renderer != NULL)
		                               ? lrg_editor_scene_renderer_get_runtime_renderer (app->renderer) : NULL;

		t->node = (ref != NULL) ? lrg_editor_find_node (editor, ref) : NULL;
		if (t->node == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No node '%s'",
			             ref != NULL ? ref : "(not a string)");
			return FALSE;
		}
		if (lrg_node_get_visual (t->node) == NULL ||
		    lrg_node_visual_get_kind (lrg_node_get_visual (t->node)) != LRG_NODE_VISUAL_WATER)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "'%s' is not a water node",
			             lrg_node_get_name (t->node));
			return FALSE;
		}
		t->path = lrg_editor_terrain_node_path (editor, resolver, t->node);
		t->body = (t->path != NULL) ? lrg_editor_terrain_store_get_water (editor, t->path, NULL) : NULL;
		if (t->body == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "The .rwater of '%s' does not load", lrg_node_get_name (t->node));
			target_clear (t);
			return FALSE;
		}
		return TRUE;
	}
	if (p_has (params, "path"))
	{
		g_autoptr(GError)    local = NULL;
		LrgEditorDataEditor *ed = lrg_editor_data_editor_open (app, p_string (params, "path"), &local);

		if (ed == NULL || !LRG_IS_EDITOR_WATER_EDITOR (ed))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "%s",
			             local != NULL ? local->message : "Not a .rwater file");
			return FALSE;
		}
		t->editor = LRG_EDITOR_WATER_EDITOR (ed);
		t->path = g_strdup (lrg_editor_data_editor_get_path (ed));
		t->body = t->editor->body;
		if (t->body == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s",
			             t->editor->load_error != NULL ? t->editor->load_error : "The document does not load");
			target_clear (t);
			return FALSE;
		}
		return TRUE;
	}
	return bad_params (error, "Give \"path\" (a .rwater file) or \"node\" (a water node)");
}

/* Node mode: a level undo step holding the body before and after. */
typedef struct
{
	LrgEditor    *editor;   /* weak */
	LrgEditorApp *app;      /* weak */
	LrgWaterBody *body;
	gchar        *path;
	gchar        *before;
	gchar        *after;
} NodeUndo;

static void
node_undo_free (gpointer data)
{
	NodeUndo *u = data;

	g_clear_weak_pointer (&u->editor);
	g_clear_weak_pointer (&u->app);
	g_object_unref (u->body);
	g_free (u->path);
	g_free (u->before);
	g_free (u->after);
	g_free (u);
}

static void
node_undo_restore (gpointer data,
                   gboolean after)
{
	NodeUndo                *u = data;
	g_autoptr(LrgWaterBody) parsed = lrg_water_body_new_from_data (after ? u->after : u->before, -1, NULL);

	if (parsed != NULL)
		editor_water_copy_into (u->body, parsed);
	if (u->editor != NULL)
		lrg_editor_terrain_store_set_dirty (u->editor, u->path, TRUE);
	if (u->app != NULL)
		lrg_editor_water_editor_sync_from_store (u->app, u->path);
}

/* Runs @change on the target: a document commit (path) or the store body
 * with a level undo step (node). */
static gboolean
target_edit (Target       *t,
             const gchar  *label,
             BodyChange    change,
             gpointer      data,
             GError      **error)
{
	g_autoptr(GError) local = NULL;
	gboolean          ok;

	if (t->editor != NULL)
		ok = edit_body (t->editor, label, 0, change, data, &local);
	else
	{
		LrgEditor *editor = lrg_editor_app_get_editor (t->app);
		NodeUndo  *u = g_new0 (NodeUndo, 1);

		u->before = lrg_water_body_to_data (t->body, NULL, NULL);
		ok = change (t->body, data, &local);
		if (ok)
		{
			g_set_weak_pointer (&u->editor, editor);
			g_set_weak_pointer (&u->app, t->app);
			u->body = g_object_ref (t->body);
			u->path = g_strdup (t->path);
			u->after = lrg_water_body_to_data (t->body, NULL, NULL);
			lrg_editor_terrain_store_set_dirty (editor, t->path, TRUE);
			editor_terrain_push_undo (editor, label, node_undo_restore, u, node_undo_free);
			lrg_editor_water_editor_sync_from_store (t->app, t->path);
		}
		else
		{
			g_free (u->before);
			g_free (u);
		}
	}
	if (!ok)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     local != NULL ? local->message : "Edit failed");
		return FALSE;
	}
	if (t->editor != NULL)
		t->body = t->editor->body;
	return TRUE;
}

static JsonNode *
body_json (Target *t)
{
	LrgWaterBody *body = t->body;
	JsonObject   *o = json_object_new ();
	JsonObject   *colors = json_object_new ();
	JsonObject   *optics = json_object_new ();
	JsonArray    *points = json_array_new ();
	JsonArray    *waves = json_array_new ();
	GEnumClass   *klass = g_type_class_ref (LRG_TYPE_WATER_BODY_SHAPE);
	GEnumValue   *ev = g_enum_get_value (klass, (gint) lrg_water_body_get_shape (body));
	GrlColor      shallow, deep, foam;
	gfloat        a, b, c, d, f2[2];
	guint         i;

	json_object_set_string_member (o, "path", t->path);
	if (t->node != NULL)
		json_object_set_string_member (o, "node", lrg_node_get_guid (t->node));
	json_object_set_string_member (o, "name", lrg_water_body_get_name (body) != NULL ? lrg_water_body_get_name (body) : "");
	json_object_set_string_member (o, "shape", ev != NULL ? ev->value_nick : "");
	g_type_class_unref (klass);
	json_object_set_double_member (o, "level", (gdouble) lrg_water_body_get_level (body));
	lrg_water_body_get_size (body, &f2[0], &f2[1]);
	json_object_set_array_member (o, "size", floats_array (f2, 2));
	for (i = 0; i < lrg_water_body_get_n_points (body); i++)
	{
		gfloat p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		lrg_water_body_get_point (body, i, p);
		json_array_add_array_element (points, floats_array (p, 4));
	}
	json_object_set_array_member (o, "points", points);
	json_object_set_double_member (o, "cell_size", (gdouble) lrg_water_body_get_cell_size (body));
	lrg_water_body_get_foam (body, &f2[0], &f2[1]);
	json_object_set_array_member (o, "foam", floats_array (f2, 2));
	for (i = 0; i < lrg_water_body_get_wave_count (body); i++)
	{
		LrgWaterWave w;
		JsonObject  *wo = json_object_new ();

		lrg_water_body_get_wave (body, i, &w);
		json_object_set_array_member (wo, "direction", floats_array (w.direction, 2));
		json_object_set_double_member (wo, "amplitude", (gdouble) w.amplitude);
		json_object_set_double_member (wo, "wavelength", (gdouble) w.wavelength);
		json_object_set_double_member (wo, "speed", (gdouble) w.speed);
		json_object_set_double_member (wo, "steepness", (gdouble) w.steepness);
		json_array_add_object_element (waves, wo);
	}
	json_object_set_array_member (o, "waves", waves);
	json_object_set_double_member (o, "wave_scale", (gdouble) lrg_water_body_get_wave_scale (body));
	lrg_water_body_get_colors (body, &shallow, &deep, &foam);
	json_object_set_array_member (colors, "shallow", color_array (&shallow));
	json_object_set_array_member (colors, "deep", color_array (&deep));
	json_object_set_array_member (colors, "foam", color_array (&foam));
	json_object_set_object_member (o, "colors", colors);
	lrg_water_body_get_optics (body, &a, &b, &c, &d);
	json_object_set_double_member (optics, "transparency", (gdouble) a);
	json_object_set_double_member (optics, "reflection", (gdouble) b);
	json_object_set_double_member (optics, "refraction", (gdouble) c);
	json_object_set_double_member (optics, "reflect_distance", (gdouble) d);
	json_object_set_object_member (o, "optics", optics);
	json_object_set_boolean_member (o, "dirty", t->editor != NULL
	                                ? lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (t->editor))
	                                : lrg_editor_terrain_store_is_dirty (lrg_editor_app_get_editor (t->app), t->path));
	return obj_node (o);
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	Target    t;
	JsonNode *r;

	if (!resolve_target (LRG_EDITOR_APP (user_data), params, &t, error))
		return NULL;
	r = body_json (&t);
	target_clear (&t);
	return r;
}

/* water.set: every body field given, one step. */
typedef struct
{
	JsonObject *params;
	GError     *bad;
} SetFields;

static gboolean
change_fields (LrgWaterBody  *body,
               gpointer       data,
               GError       **error)
{
	static const gchar * const fields[] = { "name", "shape", "level", "size", "cell_size", "wave_scale", "foam",
	                                        "transparency", "reflection", "refraction", "reflect_distance", NULL };
	SetFields *sf = data;
	guint      i;

	for (i = 0; fields[i] != NULL; i++)
	{
		g_auto(GValue)    v = G_VALUE_INIT;
		g_autofree gchar *canon = NULL;
		gfloat            pair[2];
		gdouble           d;
		const gchar      *s;

		if (!p_has (sf->params, fields[i]))
			continue;
		if ((s = p_string (sf->params, fields[i])) != NULL)
		{
			g_value_init (&v, G_TYPE_STRING);
			g_value_set_string (&v, s);
		}
		else if (p_number (sf->params, fields[i], &d))
		{
			g_value_init (&v, G_TYPE_DOUBLE);
			g_value_set_double (&v, d);
		}
		else if (p_floats (sf->params, fields[i], pair, 2) == 2)
		{
			GrlVector2 p;

			p.x = pair[0];
			p.y = pair[1];
			g_value_init (&v, GRL_TYPE_VECTOR2);
			g_value_set_boxed (&v, &p);
		}
		canon = g_strdelimit (g_strdup (fields[i]), "_", '-');
		if (!G_IS_VALUE (&v) || !lrg_editor_water_body_apply_field (body, canon, &v))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Bad \"%s\"", fields[i]);
			return FALSE;
		}
	}
	if (p_has (sf->params, "colors") && JSON_NODE_HOLDS_OBJECT (json_object_get_member (sf->params, "colors")))
	{
		JsonObject *co = json_object_get_object_member (sf->params, "colors");
		static const gchar * const names[] = { "shallow", "deep", "foam" };

		for (i = 0; i < G_N_ELEMENTS (names); i++)
		{
			gfloat            rgba[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			GrlColor          col;
			g_auto(GValue)    v = G_VALUE_INIT;
			g_autofree gchar *field = g_strdup_printf ("%s-color", names[i]);

			if (p_floats (co, names[i], rgba, 4) < 3)
				continue;
			col.r = (guint8) CLAMP (rgba[0] * 255.0f + 0.5f, 0.0f, 255.0f);
			col.g = (guint8) CLAMP (rgba[1] * 255.0f + 0.5f, 0.0f, 255.0f);
			col.b = (guint8) CLAMP (rgba[2] * 255.0f + 0.5f, 0.0f, 255.0f);
			col.a = (guint8) CLAMP (rgba[3] * 255.0f + 0.5f, 0.0f, 255.0f);
			g_value_init (&v, GRL_TYPE_COLOR);
			g_value_set_boxed (&v, &col);
			lrg_editor_water_body_apply_field (body, field, &v);
		}
	}
	return TRUE;
}

static JsonNode *
m_set (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	Target    t;
	SetFields sf = { params, NULL };
	JsonNode *r = NULL;

	if (!resolve_target (LRG_EDITOR_APP (user_data), params, &t, error))
		return NULL;
	if (target_edit (&t, "Water: set", change_fields, &sf, error))
		r = body_json (&t);
	target_clear (&t);
	return r;
}

/* water.set_wave {index, count?, direction?, amplitude?, ...} */
typedef struct
{
	JsonObject *params;
	gint        index;
	gint        count;
} WaveSet;

static gboolean
change_wave_params (LrgWaterBody  *body,
                    gpointer       data,
                    GError       **error)
{
	WaveSet     *ws = data;
	LrgWaterWave w = { { 1.0f, 0.0f }, 0.1f, 6.0f, 1.0f, 0.3f };
	gdouble      d;
	gfloat       dir[2];

	if (ws->count >= 0)
	{
		Change c = { 0 };

		c.index = (guint) ws->count;
		if (!change_wave_count (body, &c, error))
			return FALSE;
	}
	if (ws->index < 0)
		return TRUE;
	if (ws->index >= MAX_WAVES)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "\"index\" is 0 .. 3");
		return FALSE;
	}
	if ((guint) ws->index < lrg_water_body_get_wave_count (body))
		lrg_water_body_get_wave (body, (guint) ws->index, &w);
	if (p_floats (ws->params, "direction", dir, 2) == 2)
	{
		w.direction[0] = dir[0];
		w.direction[1] = dir[1];
	}
	if (p_number (ws->params, "amplitude", &d))
		w.amplitude = (gfloat) MAX (d, 0.0);
	if (p_number (ws->params, "wavelength", &d))
		w.wavelength = (gfloat) MAX (d, 0.01);
	if (p_number (ws->params, "speed", &d))
		w.speed = (gfloat) d;
	if (p_number (ws->params, "steepness", &d))
		w.steepness = (gfloat) CLAMP (d, 0.0, 1.0);
	{
		Change c = { 0 };

		c.index = (guint) ws->index;
		c.wave = &w;
		return change_wave (body, &c, error);
	}
}

static JsonNode *
m_set_wave (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	Target    t;
	WaveSet   ws;
	JsonNode *r = NULL;
	gdouble   d;

	ws.params = params;
	ws.index = p_number (params, "index", &d) ? (gint) d : -1;
	ws.count = p_number (params, "count", &d) ? (gint) d : -1;
	if (ws.index < 0 && ws.count < 0)
	{
		bad_params (error, "Give \"index\" (and wave fields) and/or \"count\"");
		return NULL;
	}
	if (!resolve_target (LRG_EDITOR_APP (user_data), params, &t, error))
		return NULL;
	if (target_edit (&t, "Water: wave", change_wave_params, &ws, error))
		r = body_json (&t);
	target_clear (&t);
	return r;
}

static JsonNode *
m_add_point (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	Target      t;
	Change      c = { 0 };
	gfloat      p[4] = { 0.0f, 0.0f, 0.0f, 4.0f };
	gdouble     d;
	JsonObject *o;

	if (!p_number (params, "x", &d))
	{
		bad_params (error, "Missing \"x\" / \"z\"");
		return NULL;
	}
	p[0] = (gfloat) d;
	if (!p_number (params, "z", &d))
	{
		bad_params (error, "Missing \"z\"");
		return NULL;
	}
	p[2] = (gfloat) d;
	if (p_number (params, "y", &d))
		p[1] = (gfloat) d;
	if (p_number (params, "width", &d))
		p[3] = (gfloat) MAX (d, 0.1);
	c.at = p_number (params, "index", &d) ? (gint) d : -1;
	c.point = p;
	c.result = -1;
	if (!resolve_target (LRG_EDITOR_APP (user_data), params, &t, error))
		return NULL;
	if (!target_edit (&t, "Water: add point", change_add_point, &c, error))
	{
		target_clear (&t);
		return NULL;
	}
	target_clear (&t);
	o = json_object_new ();
	json_object_set_int_member (o, "index", c.result);
	return obj_node (o);
}

static JsonNode *
m_set_point (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	Target      t;
	Change      c = { 0 };
	gfloat      p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	gdouble     d;
	JsonObject *o;

	if (!p_number (params, "index", &d) || d < 0.0)
	{
		bad_params (error, "Missing \"index\"");
		return NULL;
	}
	c.index = (guint) d;
	if (!resolve_target (LRG_EDITOR_APP (user_data), params, &t, error))
		return NULL;
	if (!lrg_water_body_get_point (t.body, c.index, p))
	{
		target_clear (&t);
		bad_params (error, "\"index\" out of range");
		return NULL;
	}
	if (p_number (params, "x", &d))
		p[0] = (gfloat) d;
	if (p_number (params, "y", &d))
		p[1] = (gfloat) d;
	if (p_number (params, "z", &d))
		p[2] = (gfloat) d;
	if (p_number (params, "width", &d))
		p[3] = (gfloat) MAX (d, 0.1);
	c.point = p;
	if (!target_edit (&t, "Water: point", change_set_point, &c, error))
	{
		target_clear (&t);
		return NULL;
	}
	target_clear (&t);
	o = json_object_new ();
	json_object_set_int_member (o, "index", (gint64) c.index);
	return obj_node (o);
}

static JsonNode *
m_remove_point (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	Target      t;
	Change      c = { 0 };
	gdouble     d;
	JsonObject *o;

	if (!p_number (params, "index", &d) || d < 0.0)
	{
		bad_params (error, "Missing \"index\"");
		return NULL;
	}
	c.index = (guint) d;
	if (!resolve_target (LRG_EDITOR_APP (user_data), params, &t, error))
		return NULL;
	if (!target_edit (&t, "Water: remove point", change_remove_point, &c, error))
	{
		target_clear (&t);
		return NULL;
	}
	target_clear (&t);
	o = json_object_new ();
	json_object_set_boolean_member (o, "removed", TRUE);
	return obj_node (o);
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	Target            t;
	g_autoptr(GError) local = NULL;
	JsonObject       *o;
	gboolean          ok;

	if (!resolve_target (LRG_EDITOR_APP (user_data), params, &t, error))
		return NULL;
	if (t.editor != NULL)
		ok = lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (t.editor), &local);
	else
		ok = lrg_editor_terrain_store_save (lrg_editor_app_get_editor (t.app), t.path, &local);
	if (!ok)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, local->message);
		target_clear (&t);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", t.path);
	target_clear (&t);
	return obj_node (o);
}

void
lrg_editor_water_editor_install (LrgEditorApp *app)
{
	LrgEditorServer *server;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	if (g_object_get_data (G_OBJECT (app), INSTALL_KEY) != NULL)
		return;
	g_object_set_data (G_OBJECT (app), INSTALL_KEY, GINT_TO_POINTER (1));
	lrg_editor_data_editor_install (app);
	server = lrg_editor_app_get_server (app);
	if (server == NULL)
		return;
	lrg_editor_server_register_method (server, "water.get", m_get, app, NULL);
	lrg_editor_server_register_method (server, "water.set", m_set, app, NULL);
	lrg_editor_server_register_method (server, "water.set_wave", m_set_wave, app, NULL);
	lrg_editor_server_register_method (server, "water.add_point", m_add_point, app, NULL);
	lrg_editor_server_register_method (server, "water.set_point", m_set_point, app, NULL);
	lrg_editor_server_register_method (server, "water.remove_point", m_remove_point, app, NULL);
	lrg_editor_server_register_method (server, "water.save", m_save, app, NULL);
}
