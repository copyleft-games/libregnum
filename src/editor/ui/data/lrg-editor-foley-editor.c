/* lrg-editor-foley-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Foley designer (see lrg-editor-foley-editor.h and
 * docs/editor/foley.org).
 *
 * Layout: on the left the patch settings (a property grid), the layer
 * list and the selected layer's parameters; on the right the waveform of
 * the render.  The toolbar adds layers (a menu of the six types), loads a
 * preset (a menu of the built-in kinds), moves and removes the selected
 * layer, makes another take (variation), plays and exports.
 *
 * The document text is the truth: every edit duplicates the document's
 * patch, changes the copy and commits it as one undo step (drags in the
 * property grid merge into one through their gesture).  After every
 * document change the patch is parsed again, and the render the waveform
 * shows is redone when it is next needed.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-foley-editor.h"
#include "lrg-editor-music-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-menu.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-property-source.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../audio/lrg-audio-encoder.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <errno.h>

#define INSTALL_KEY "lrg-editor-foley-installed"
#define ROW_H       (24.0f)
#define BTN_W       (22.0f)

enum
{
	PART_PATCH,
	PART_LAYER,
	N_PARTS
};

struct _LrgEditorFoleyEditor
{
	LrgEditorDataEditor parent_instance;

	LrgFoleyPatch      *patch;
	gchar              *load_error;
	gint                sel;
	guint               gesture;
	gchar              *signature;

	GObject            *src[N_PARTS];
	LrgPropertyGrid    *grid[N_PARTS];
	LrgWidget          *list;
	LrgWidget          *wave;
	LrgMenu            *add_menu;
	LrgMenu            *preset_menu;
	LrgWidget          *add_chip;
	LrgWidget          *preset_chip;

	gfloat             *pcm;
	gsize               n_pcm;
	gboolean            pcm_valid;
	GrlSound           *preview;
};

G_DEFINE_FINAL_TYPE (LrgEditorFoleyEditor, lrg_editor_foley_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void sync_views (LrgEditorFoleyEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorFoleyEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static const gchar *
type_nick (LrgFoleyLayerType type)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_FOLEY_LAYER_TYPE);
	GEnumValue           *ev = g_enum_get_value (klass, (gint) type);

	return ev != NULL ? g_intern_string (ev->value_nick) : "?";
}

/* ==========================================================================
 * Edits: duplicate, change, commit
 * ========================================================================== */

typedef gboolean (*PatchChange) (LrgFoleyPatch *patch,
                                 gpointer       data,
                                 GError       **error);

static gboolean
edit_patch (LrgEditorFoleyEditor  *self,
            const gchar           *label,
            guint                  gesture,
            PatchChange            change,
            gpointer               data,
            GError               **error)
{
	g_autoptr(GObject) obj = lrg_editor_document_dup_object (doc_of (self), error);

	if (obj == NULL)
		return FALSE;
	if (!change (LRG_FOLEY_PATCH (obj), data, error))
		return FALSE;
	return lrg_editor_document_commit_object (doc_of (self), obj, label, gesture, error);
}

typedef struct
{
	const gchar       *name;
	const gchar       *text;
	gdouble            value;
	guint              index;
	guint              to;
	LrgFoleyLayerType  type;
	LrgFoleyKind       kind;
	gint               result;
} Change;

static gboolean
change_global (LrgFoleyPatch  *patch,
               gpointer        data,
               GError        **error)
{
	Change *c = data;

	if (g_strcmp0 (c->name, "name") == 0)
	{
		g_object_set (patch, "name", c->text, NULL);
		return TRUE;
	}
	if (!lrg_foley_patch_set_global (patch, c->name, c->value))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "No patch setting '%s'", c->name);
		return FALSE;
	}
	return TRUE;
}

static gboolean
change_add (LrgFoleyPatch  *patch,
            gpointer        data,
            GError        **error)
{
	Change *c = data;

	c->result = lrg_foley_patch_add_layer (patch, c->type);
	if (c->result < 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "The patch has no room for a layer");
		return FALSE;
	}
	return TRUE;
}

static gboolean
change_remove (LrgFoleyPatch  *patch,
               gpointer        data,
               GError        **error)
{
	Change *c = data;

	if (!lrg_foley_patch_remove_layer (patch, c->index))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No layer %u", c->index);
		return FALSE;
	}
	return TRUE;
}

static gboolean
change_move (LrgFoleyPatch  *patch,
             gpointer        data,
             GError        **error)
{
	Change *c = data;

	if (!lrg_foley_patch_move_layer (patch, c->index, c->to))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No layer %u or %u", c->index, c->to);
		return FALSE;
	}
	return TRUE;
}

static gboolean
change_param (LrgFoleyPatch  *patch,
              gpointer        data,
              GError        **error)
{
	Change *c = data;

	if (!lrg_foley_patch_set_layer_param (patch, c->index, c->name, c->value))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Layer %u has no parameter '%s'", c->index,
		             c->name);
		return FALSE;
	}
	return TRUE;
}

/* swaps the document's patch for a kind's recipe (the object is replaced) */
static gboolean
replace_with_kind (LrgEditorFoleyEditor  *self,
                   LrgFoleyKind           kind,
                   guint                  variant,
                   GError               **error)
{
	g_autoptr(LrgFoleyPatch) fresh = NULL;
	guint                    rate = self->patch != NULL ? lrg_foley_patch_get_sample_rate (self->patch) : 0;

	fresh = lrg_foley_patch_new_from_kind (kind, variant, rate, error);
	if (fresh == NULL)
		return FALSE;
	return lrg_editor_document_commit_object (doc_of (self), G_OBJECT (fresh), "Foley: preset", 0, error);
}

/* ==========================================================================
 * Public edits
 * ========================================================================== */

LrgFoleyPatch *
lrg_editor_foley_editor_get_patch (LrgEditorFoleyEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self), NULL);
	return self->patch;
}

gint
lrg_editor_foley_editor_get_selected_layer (LrgEditorFoleyEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self), -1);
	return self->sel;
}

void
lrg_editor_foley_editor_select_layer (LrgEditorFoleyEditor *self,
                                      gint                  index)
{
	g_return_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self));
	self->sel = index;
	sync_views (self);
}

gboolean
lrg_editor_foley_editor_set_global (LrgEditorFoleyEditor  *self,
                                    const gchar           *name,
                                    gdouble                value,
                                    const gchar           *text,
                                    guint                  gesture,
                                    GError               **error)
{
	Change c = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self), FALSE);
	c.name = name;
	c.value = value;
	c.text = text;
	return edit_patch (self, "Foley: setting", gesture, change_global, &c, error);
}

gint
lrg_editor_foley_editor_add_layer (LrgEditorFoleyEditor  *self,
                                   LrgFoleyLayerType      type,
                                   GError               **error)
{
	Change c = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self), -1);
	c.type = type;
	if (!edit_patch (self, "Foley: add layer", 0, change_add, &c, error))
		return -1;
	lrg_editor_foley_editor_select_layer (self, c.result);
	return c.result;
}

gboolean
lrg_editor_foley_editor_remove_layer (LrgEditorFoleyEditor  *self,
                                      guint                  index,
                                      GError               **error)
{
	Change c = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self), FALSE);
	c.index = index;
	return edit_patch (self, "Foley: remove layer", 0, change_remove, &c, error);
}

gboolean
lrg_editor_foley_editor_move_layer (LrgEditorFoleyEditor  *self,
                                    guint                  from,
                                    guint                  to,
                                    GError               **error)
{
	Change c = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self), FALSE);
	c.index = from;
	c.to = to;
	if (!edit_patch (self, "Foley: move layer", 0, change_move, &c, error))
		return FALSE;
	if (self->sel == (gint) from)
		lrg_editor_foley_editor_select_layer (self, (gint) to);
	return TRUE;
}

gboolean
lrg_editor_foley_editor_set_layer_param (LrgEditorFoleyEditor  *self,
                                         guint                  index,
                                         const gchar           *name,
                                         gdouble                value,
                                         guint                  gesture,
                                         GError               **error)
{
	Change c = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self), FALSE);
	c.index = index;
	c.name = name;
	c.value = value;
	return edit_patch (self, "Foley: layer", gesture, change_param, &c, error);
}

gboolean
lrg_editor_foley_editor_load_kind (LrgEditorFoleyEditor  *self,
                                   LrgFoleyKind           kind,
                                   guint                  variant,
                                   GError               **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self), FALSE);
	if (!replace_with_kind (self, kind, variant, error))
		return FALSE;
	lrg_editor_foley_editor_select_layer (self, -1);
	return TRUE;
}

const gfloat *
lrg_editor_foley_editor_render (LrgEditorFoleyEditor  *self,
                                gsize                 *n_frames,
                                GError               **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self), NULL);
	g_return_val_if_fail (n_frames != NULL, NULL);

	*n_frames = 0;
	if (self->patch == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     self->load_error != NULL ? self->load_error : "The patch does not load");
		return NULL;
	}
	if (!self->pcm_valid)
	{
		g_clear_pointer (&self->pcm, g_free);
		self->pcm = lrg_foley_patch_render (self->patch, &self->n_pcm, error);
		if (self->pcm == NULL)
			return NULL;
		self->pcm_valid = TRUE;
	}
	*n_frames = self->n_pcm;
	return self->pcm;
}

gboolean
lrg_editor_foley_editor_export (LrgEditorFoleyEditor  *self,
                                const gchar           *path,
                                GError               **error)
{
	g_autoptr(LrgWaveData)           wave = NULL;
	g_autoptr(LrgAudioEncodeOptions) options = lrg_audio_encode_options_new ();
	LrgAudioFileFormat               format = LRG_AUDIO_FILE_FORMAT_WAV;

	g_return_val_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (self->patch == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "The patch does not load");
		return FALSE;
	}
	if (!lrg_audio_file_format_from_path (path, &format))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s: unknown audio extension", path);
		return FALSE;
	}
	wave = lrg_foley_patch_render_wave (self->patch, error);
	if (wave == NULL)
		return FALSE;
	options->title = g_strdup (lrg_foley_patch_get_name (self->patch));
	if (!music_ensure_parent (path, error))
		return FALSE;
	return lrg_audio_encode_wave (wave, format, options, path, NULL, error);
}

gboolean
lrg_editor_foley_editor_play (LrgEditorFoleyEditor  *self,
                              GError               **error)
{
	g_autoptr(LrgWaveData) wave = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_FOLEY_EDITOR (self), FALSE);
	if (self->patch == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "The patch does not load");
		return FALSE;
	}
	wave = lrg_foley_patch_render_wave (self->patch, error);
	if (wave == NULL)
		return FALSE;
	return music_play_wave (&self->preview, wave, error);
}

/* ==========================================================================
 * Property source: the patch settings and the selected layer
 * ========================================================================== */

#define EDITOR_TYPE_FOLEY_SOURCE (editor_foley_source_get_type ())
G_DECLARE_FINAL_TYPE (EditorFoleySource, editor_foley_source, EDITOR, FOLEY_SOURCE, GObject)

struct _EditorFoleySource
{
	GObject               parent_instance;
	LrgEditorFoleyEditor *editor;    /* weak */
	gint                  part;
};

static void editor_foley_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (EditorFoleySource, editor_foley_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, editor_foley_source_iface_init))

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
		pspec = g_param_spec_string (g_intern_string (name), NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	else if (type == G_TYPE_BOOLEAN)
		pspec = g_param_spec_boolean (g_intern_string (name), NULL, NULL, FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	else
		pspec = g_param_spec_double (g_intern_string (name), NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
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

/* a label from a dashed name: "freq-end" -> "Freq end" */
static gchar *
pretty (const gchar *name)
{
	gchar *s = g_strdelimit (g_strdup (name), "-_", ' ');

	s[0] = g_ascii_toupper (s[0]);
	return s;
}

static GPtrArray *
editor_foley_source_list_properties (LrgPropertySource *source)
{
	EditorFoleySource    *src = EDITOR_FOLEY_SOURCE (source);
	LrgEditorFoleyEditor *ed = src->editor;
	GPtrArray            *infos = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);

	if (ed == NULL || ed->patch == NULL)
		return infos;
	if (src->part == PART_PATCH)
	{
		add_row (infos, "name", G_TYPE_STRING, "Name", "Patch", NULL, NULL);
		add_row (infos, "duration", G_TYPE_DOUBLE, "Duration", "Patch", "range:0.01,60,0.01", "Seconds rendered");
		add_row (infos, "sample-rate", G_TYPE_DOUBLE, "Rate", "Patch", "range:8000,96000,100", "Hz");
		add_row (infos, "seed", G_TYPE_DOUBLE, "Seed", "Patch", "range:0,4294967295,1",
		         "Noise of the layers whose own seed is 0");
		add_row (infos, "variation", G_TYPE_DOUBLE, "Variation", "Patch", "range:0,4294967295,1",
		         "0: as designed; another value: another take of the same sound");
		add_row (infos, "loop", G_TYPE_BOOLEAN, "Loop", "Output", NULL, "The tail is crossfaded onto the head");
		add_row (infos, "loop-tail", G_TYPE_DOUBLE, "Loop tail", "Output", "range:0,10,0.01", "Seconds");
		add_row (infos, "fade", G_TYPE_DOUBLE, "Fade", "Output", "range:0,1,0.001", "Fade-in and -out seconds");
		add_row (infos, "normalize", G_TYPE_BOOLEAN, "Normalise", "Output", NULL, NULL);
		add_row (infos, "peak", G_TYPE_DOUBLE, "Peak", "Output", "range:0.01,1,0.01", NULL);
	}
	else if (ed->sel >= 0 && (guint) ed->sel < lrg_foley_patch_get_n_layers (ed->patch))
	{
		LrgFoleyLayerType type = lrg_foley_patch_get_layer_type (ed->patch, (guint) ed->sel);
		g_auto(GStrv)     names = lrg_foley_layer_type_list_params (type);
		guint             i;

		for (i = 0; names[i] != NULL; i++)
		{
			gdouble           min = 0, max = 0, def = 0;
			const gchar      *blurb = NULL;
			g_autofree gchar *hint = NULL;
			g_autofree gchar *label = pretty (names[i]);
			gdouble           step;

			lrg_foley_layer_type_get_param_info (type, names[i], &min, &max, &def, &blurb);
			step = (max - min) > 1000.0 ? 1.0 : (max - min) > 10.0 ? 0.1 : 0.001;
			hint = g_strdup_printf ("range:%g,%g,%g", min, max, step);
			add_row (infos, names[i], G_TYPE_DOUBLE, label, i < 7 ? "Layer" : type_nick (type), hint, blurb);
		}
	}
	return infos;
}

static gboolean
editor_foley_source_get_value (LrgPropertySource *source,
                               const gchar       *name,
                               GValue            *value)
{
	EditorFoleySource    *src = EDITOR_FOLEY_SOURCE (source);
	LrgEditorFoleyEditor *ed = src->editor;

	if (ed == NULL || ed->patch == NULL)
		return FALSE;
	if (src->part == PART_PATCH)
	{
		GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (ed->patch), name);
		GValue      v = G_VALUE_INIT;

		if (pspec == NULL)
			return FALSE;
		g_value_init (&v, pspec->value_type);
		g_object_get_property (G_OBJECT (ed->patch), name, &v);
		if (G_VALUE_HOLDS_STRING (&v))
		{
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, g_value_get_string (&v));
		}
		else if (G_VALUE_HOLDS_BOOLEAN (&v))
		{
			g_value_init (value, G_TYPE_BOOLEAN);
			g_value_set_boolean (value, g_value_get_boolean (&v));
		}
		else
		{
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, G_VALUE_HOLDS_UINT (&v) ? (gdouble) g_value_get_uint (&v)
			                                                   : g_value_get_double (&v));
		}
		g_value_unset (&v);
		return TRUE;
	}
	if (ed->sel < 0)
		return FALSE;
	g_value_init (value, G_TYPE_DOUBLE);
	g_value_set_double (value, lrg_foley_patch_get_layer_param (ed->patch, (guint) ed->sel, name));
	return TRUE;
}

static gboolean
editor_foley_source_set_value (LrgPropertySource *source,
                               const gchar       *name,
                               const GValue      *value)
{
	EditorFoleySource    *src = EDITOR_FOLEY_SOURCE (source);
	LrgEditorFoleyEditor *ed = src->editor;
	g_autoptr(GError)     error = NULL;
	gboolean              ok = FALSE;
	gdouble               d = 0.0;

	if (ed == NULL || ed->patch == NULL)
		return FALSE;
	if (G_VALUE_HOLDS_DOUBLE (value))
		d = g_value_get_double (value);
	else if (G_VALUE_HOLDS_BOOLEAN (value))
		d = g_value_get_boolean (value) ? 1.0 : 0.0;
	if (src->part == PART_PATCH)
		ok = lrg_editor_foley_editor_set_global (ed, name, d, G_VALUE_HOLDS_STRING (value) ? g_value_get_string (value)
		                                                                                   : NULL, ed->gesture, &error);
	else if (ed->sel >= 0)
		ok = lrg_editor_foley_editor_set_layer_param (ed, (guint) ed->sel, name, d, ed->gesture, &error);
	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (ed), error);
	if (ok)
		lrg_property_source_emit_changed (source, name);
	return ok;
}

static void
editor_foley_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = editor_foley_source_list_properties;
	iface->get_value = editor_foley_source_get_value;
	iface->set_value = editor_foley_source_set_value;
}

static void
editor_foley_source_finalize (GObject *object)
{
	g_clear_weak_pointer (&EDITOR_FOLEY_SOURCE (object)->editor);
	G_OBJECT_CLASS (editor_foley_source_parent_class)->finalize (object);
}

static void
editor_foley_source_class_init (EditorFoleySourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = editor_foley_source_finalize;
}

static void
editor_foley_source_init (EditorFoleySource *self)
{
}

static void
on_edit_begin (LrgPropertyGrid *grid,
               const gchar     *name,
               guint            gesture_id,
               gpointer         user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);

	self->gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
}

static void
on_edit_end (LrgPropertyGrid *grid,
             const gchar     *name,
             guint            gesture_id,
             gboolean         commit_,
             gpointer         user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);

	self->gesture = 0;
	sync_views (self);
}

/* ==========================================================================
 * The layer list and the waveform
 * ========================================================================== */

static guint
n_layers (LrgEditorFoleyEditor *self)
{
	return self->patch != NULL ? lrg_foley_patch_get_n_layers (self->patch) : 0;
}

static void
list_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);

	size->width = 200.0f;
	size->height = (gfloat) MAX (MIN (n_layers (self), 14u), 1u) * ROW_H + 2.0f;
}

/* the one-line summary of a layer in the list */
static gchar *
layer_summary (LrgFoleyPatch *patch,
               guint          i)
{
	LrgFoleyLayerType type = lrg_foley_patch_get_layer_type (patch, i);
	gdouble           at = lrg_foley_patch_get_layer_param (patch, i, "at");
	gboolean          mute = lrg_foley_patch_get_layer_param (patch, i, "mute") >= 0.5;
	gdouble           count = lrg_foley_patch_get_layer_param (patch, i, "count");
	g_autofree gchar *main = NULL;

	switch (type)
	{
	case LRG_FOLEY_LAYER_BODY:
		main = g_strdup_printf ("%.0f Hz", lrg_foley_patch_get_layer_param (patch, i, "freq"));
		break;
	case LRG_FOLEY_LAYER_TONE:
		main = g_strdup_printf ("%.0f > %.0f Hz", lrg_foley_patch_get_layer_param (patch, i, "freq"),
		                        lrg_foley_patch_get_layer_param (patch, i, "freq-end"));
		break;
	case LRG_FOLEY_LAYER_NOISE:
		main = g_strdup_printf ("%.0f > %.0f Hz", lrg_foley_patch_get_layer_param (patch, i, "cutoff"),
		                        lrg_foley_patch_get_layer_param (patch, i, "cutoff-end"));
		break;
	case LRG_FOLEY_LAYER_GRAIN:
		main = g_strdup_printf ("%.0f-%.0f Hz", lrg_foley_patch_get_layer_param (patch, i, "low"),
		                        lrg_foley_patch_get_layer_param (patch, i, "high"));
		break;
	case LRG_FOLEY_LAYER_WASH:
		main = g_strdup_printf ("%.0f Hz", lrg_foley_patch_get_layer_param (patch, i, "cutoff"));
		break;
	case LRG_FOLEY_LAYER_BED:
	default:
		main = g_strdup ("whole clip");
		break;
	}
	return g_strdup_printf ("%u  %s  %s  @%.3gs%s%s", i, type_nick (type), main, at,
	                        count > 1.0 ? "  x" : "", mute ? "  (muted)" : "");
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
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);
	LrgTheme             *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	guint                 n = n_layers (self), i;
	gfloat                fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlFont              *font = lrg_theme_get_ui_font (theme);
	GrlColor              track = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);

	track.a = (guint8) (track.a / 2);
	lrg_paint_list_rect (list, x, y, width, height, radius, &track);
	if (n == 0)
	{
		lrg_paint_list_text (list, font, "No layers: add one from the toolbar", x + 10.0f,
		                     roundf (y + (ROW_H - fs) * 0.5f), fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		return;
	}
	lrg_paint_list_push_clip (list, x, y, width, height);
	for (i = 0; i < n; i++)
	{
		gfloat            ry = y + 1.0f + (gfloat) i * ROW_H;
		g_autofree gchar *label = layer_summary (self->patch, i);
		gfloat            is = 12.0f;
		gfloat            bx = x + width - BTN_W - 2.0f;

		if (ry > y + height)
			break;
		if ((gint) i == self->sel)
			lrg_paint_list_rect (list, x + 2.0f, ry, width - 4.0f, ROW_H - 1.0f, radius,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION));
		lrg_paint_list_push_clip (list, x + 8.0f, ry, MAX (width - 8.0f - BTN_W - 6.0f, 0.0f), ROW_H);
		lrg_paint_list_text (list, font, label, x + 8.0f, roundf (ry + (ROW_H - fs) * 0.5f - 1.0f), fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_paint_list_pop_clip (list);
		lrg_paint_list_icon (list, LRG_ICON_TRASH, bx + (BTN_W - is) * 0.5f, ry + (ROW_H - is) * 0.5f, is, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
	}
	lrg_paint_list_pop_clip (list);
}

static gboolean
list_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);
	gfloat                x = lrg_widget_get_world_x (LRG_WIDGET (widget));
	gfloat                y = lrg_widget_get_world_y (LRG_WIDGET (widget));
	gfloat                w = lrg_widget_get_width (LRG_WIDGET (widget));
	g_autoptr(GError)     error = NULL;
	guint                 row;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || lrg_ui_event_get_button (event) != 0)
		return FALSE;
	if (lrg_ui_event_get_y (event) < y + 1.0f || n_layers (self) == 0)
		return FALSE;
	row = (guint) ((lrg_ui_event_get_y (event) - y - 1.0f) / ROW_H);
	if (row >= n_layers (self))
		return TRUE;
	if (lrg_ui_event_get_x (event) >= x + w - BTN_W - 2.0f)
	{
		if (!lrg_editor_foley_editor_remove_layer (self, row, &error))
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return TRUE;
	}
	lrg_editor_foley_editor_select_layer (self, (gint) row);
	return TRUE;
}

static void
wave_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	size->width = 240.0f;
	size->height = 180.0f;
}

/* the render as min/max columns, with a centre line and the length */
static void
wave_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);
	LrgTheme             *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont              *font = lrg_theme_get_ui_font (theme);
	gfloat                fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor       *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK);
	const GrlColor       *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	GrlColor              track = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);
	g_autoptr(GError)     error = NULL;
	const gfloat         *pcm;
	gsize                 n = 0;
	gfloat                mid = y + height * 0.5f;
	guint                 col, cols = (guint) MAX (width - 4.0f, 1.0f);
	g_autofree gchar     *caption = NULL;

	track.a = (guint8) (track.a / 2);
	lrg_paint_list_rect (list, x, y, width, height, 4.0f, &track);
	pcm = lrg_editor_foley_editor_render (self, &n, &error);
	if (pcm == NULL || n == 0)
	{
		lrg_paint_list_text (list, font, error != NULL ? error->message : "Nothing to render", x + 10.0f, y + 10.0f, fs,
		                     0.0f, muted);
		return;
	}
	lrg_paint_list_line (list, x + 2.0f, mid, x + width - 2.0f, mid, 1.0f, muted);
	for (col = 0; col < cols; col++)
	{
		gsize  from = n * col / cols;
		gsize  to = MAX (n * (col + 1) / cols, from + 1);
		gfloat lo = 0.0f, hi = 0.0f;
		gsize  i;

		for (i = from; i < to && i < n; i++)
		{
			lo = MIN (lo, pcm[i]);
			hi = MAX (hi, pcm[i]);
		}
		lrg_paint_list_line (list, x + 2.0f + (gfloat) col, mid - hi * (height * 0.45f), x + 2.0f + (gfloat) col,
		                     mid - lo * (height * 0.45f) + 1.0f, 1.0f, accent);
	}
	caption = g_strdup_printf ("%.3f s  %u Hz  %u layers", (gdouble) n / lrg_foley_patch_get_sample_rate (self->patch),
	                           lrg_foley_patch_get_sample_rate (self->patch), n_layers (self));
	lrg_paint_list_text (list, font, caption, x + 8.0f, y + 6.0f, fs, 0.0f, muted);
}

/* ==========================================================================
 * Toolbar
 * ========================================================================== */

static void
on_add_layer_action (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_FOLEY_LAYER_TYPE);
	GEnumValue           *ev = g_enum_get_value_by_nick (klass, g_variant_get_string (parameter, NULL));
	g_autoptr(GError)     error = NULL;

	if (ev != NULL && lrg_editor_foley_editor_add_layer (self, (LrgFoleyLayerType) ev->value, &error) < 0)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_preset_action (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_FOLEY_KIND);
	GEnumValue           *ev = g_enum_get_value_by_nick (klass, g_variant_get_string (parameter, NULL));
	g_autoptr(GError)     error = NULL;

	if (ev != NULL && !lrg_editor_foley_editor_load_kind (self, (LrgFoleyKind) ev->value, 0, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static GMenuModel *
enum_menu (GType        type,
           const gchar *action)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	GMenu                *menu = g_menu_new ();
	guint                 i;

	for (i = 0; i < klass->n_values; i++)
	{
		g_autofree gchar *detailed = g_action_print_detailed_name (action,
		                                                           g_variant_new_string (klass->values[i].value_nick));
		g_autofree gchar *label = pretty (klass->values[i].value_nick);

		g_menu_append (menu, label, detailed);
	}
	return G_MENU_MODEL (menu);
}

static void
on_add_chip (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);

	lrg_menu_popup_at_widget (self->add_menu, chip, LRG_POPUP_PLACEMENT_BELOW);
}

static void
on_preset_chip (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);

	lrg_menu_popup_at_widget (self->preset_menu, chip, LRG_POPUP_PLACEMENT_BELOW);
}

static void
on_move (LrgEditorFoleyEditor *self,
         gint                  delta)
{
	g_autoptr(GError) error = NULL;
	gint              to = self->sel + delta;

	if (self->sel < 0 || to < 0 || (guint) to >= n_layers (self))
		return;
	if (!lrg_editor_foley_editor_move_layer (self, (guint) self->sel, (guint) to, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_up_chip (LrgWidget *chip,
            gpointer   user_data)
{
	on_move (LRG_EDITOR_FOLEY_EDITOR (user_data), -1);
}

static void
on_down_chip (LrgWidget *chip,
              gpointer   user_data)
{
	on_move (LRG_EDITOR_FOLEY_EDITOR (user_data), 1);
}

static void
on_take_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);
	g_autoptr(GError)     error = NULL;
	guint                 variation = 0;

	if (self->patch == NULL)
		return;
	g_object_get (self->patch, "variation", &variation, NULL);
	if (!lrg_editor_foley_editor_set_global (self, "variation", (gdouble) variation + 1.0, NULL, 0, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_play_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);
	g_autoptr(GError)     error = NULL;

	if (!lrg_editor_foley_editor_play (self, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* exports next to the .rfoley: the same name with .wav */
static void
on_export_chip (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (user_data);
	const gchar          *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *stem = NULL;
	g_autofree gchar     *out = NULL;

	if (path == NULL)
		return;
	stem = g_str_has_suffix (path, ".rfoley") ? g_strndup (path, strlen (path) - 7) : g_strdup (path);
	out = g_strconcat (stem, ".wav", NULL);
	if (!lrg_editor_foley_editor_export (self, out, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	else
		g_message ("Foley: exported %s", out);
}

/* ==========================================================================
 * Sync
 * ========================================================================== */

static void
reparse (LrgEditorFoleyEditor *self)
{
	g_autoptr(GError) error = NULL;
	GObject          *obj = lrg_editor_document_dup_object (doc_of (self), &error);

	g_clear_object (&self->patch);
	g_clear_pointer (&self->load_error, g_free);
	self->pcm_valid = FALSE;
	if (obj == NULL)
	{
		self->load_error = g_strdup (error != NULL ? error->message : "The document does not load");
		return;
	}
	self->patch = LRG_FOLEY_PATCH (obj);
}

static void
sync_views (LrgEditorFoleyEditor *self)
{
	g_autofree gchar *sig = NULL;
	guint             i;

	if (self->grid[PART_PATCH] == NULL)
		return;
	if (self->sel >= (gint) n_layers (self))
		self->sel = (gint) n_layers (self) - 1;
	sig = g_strdup_printf ("%d/%d", self->sel,
	                       self->sel >= 0 ? (gint) lrg_foley_patch_get_layer_type (self->patch, (guint) self->sel) : -1);
	if (g_strcmp0 (sig, self->signature) != 0)
	{
		g_free (self->signature);
		self->signature = g_steal_pointer (&sig);
		for (i = 0; i < N_PARTS; i++)
		{
			lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self->src[i]), NULL);
			lrg_property_grid_rebuild (self->grid[i]);
		}
	}
	else if (self->gesture == 0)
		for (i = 0; i < N_PARTS; i++)
			lrg_property_grid_refresh (self->grid[i]);
	lrg_widget_set_visible (LRG_WIDGET (self->grid[PART_LAYER]), self->sel >= 0);
	lrg_widget_queue_layout (self->list);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
foley_refresh (LrgEditorDataEditor *editor,
               const gchar         *path)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (editor);

	reparse (self);
	sync_views (self);
}

static gchar *
foley_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (editor);

	if (self->patch == NULL)
		return g_strdup ("not loaded");
	return g_strdup_printf ("%u layers - %.3f s - layer %d", n_layers (self),
	                        lrg_foley_patch_get_duration (self->patch), self->sel);
}

static void
foley_validate (LrgEditorDataEditor *editor,
                GPtrArray           *issues)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (editor);

	if (self->patch != NULL && n_layers (self) == 0)
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, "/layers", 0, 0,
		                                                        "The patch has no layers: it renders silence"));
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static gpointer
add_to (gpointer box,
        gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (child));
	g_object_unref (child);
	return child;
}

static LrgPropertyGrid *
make_grid (LrgEditorFoleyEditor *self,
           const gchar          *name,
           gint                  part)
{
	LrgPropertyGrid   *grid = lrg_property_grid_new ();
	EditorFoleySource *src = g_object_new (EDITOR_TYPE_FOLEY_SOURCE, NULL);

	g_set_weak_pointer (&src->editor, self);
	src->part = part;
	self->src[part] = G_OBJECT (src);
	lrg_widget_set_name (LRG_WIDGET (grid), name);
	lrg_property_grid_set_label_width (grid, 110.0f);
	g_signal_connect (grid, "edit-begin", G_CALLBACK (on_edit_begin), self);
	g_signal_connect (grid, "edit-end", G_CALLBACK (on_edit_end), self);
	lrg_property_grid_set_source (grid, LRG_PROPERTY_SOURCE (src));
	self->grid[part] = grid;
	return grid;
}

static void
tool (LrgEditorFoleyEditor *self,
      const gchar          *name,
      LrgIcon               icon,
      const gchar          *label,
      EditorChipFunc        func,
      LrgWidget           **keep)
{
	LrgWidget *chip = editor_chip_new (name, icon, label, func, self);

	lrg_editor_data_editor_add_tool (LRG_EDITOR_DATA_EDITOR (self), chip);
	if (keep != NULL)
		*keep = chip;
	g_object_unref (chip);
}

static void
lrg_editor_foley_editor_constructed (GObject *object)
{
	static const LrgCustomWidgetFuncs list_funcs = { list_measure, list_paint, NULL, list_event, { NULL } };
	static const LrgCustomWidgetFuncs wave_funcs = { wave_measure, wave_paint, NULL, NULL, { NULL } };
	static const GActionEntry         entries[] = {
		{ "add-layer", on_add_layer_action, "s", NULL, NULL, { 0 } },
		{ "preset", on_preset_action, "s", NULL, NULL, { 0 } },
	};
	LrgEditorFoleyEditor          *self = LRG_EDITOR_FOLEY_EDITOR (object);
	const LrgInsets                pad = { 8.0f, 6.0f, 8.0f, 6.0f };
	g_autoptr(GSimpleActionGroup)  group = g_simple_action_group_new ();
	g_autoptr(GMenuModel)          add_model = enum_menu (LRG_TYPE_FOLEY_LAYER_TYPE, "foley.add-layer");
	g_autoptr(GMenuModel)          preset_model = enum_menu (LRG_TYPE_FOLEY_KIND, "foley.preset");
	LrgWidget                     *left, *right, *scroll, *heading;
	LrgSplitter                   *split;
	LrgEditorApp                  *app;

	G_OBJECT_CLASS (lrg_editor_foley_editor_parent_class)->constructed (object);

	/* the toolbar and its menus */
	g_action_map_add_action_entries (G_ACTION_MAP (group), entries, G_N_ELEMENTS (entries), self);
	lrg_widget_insert_action_group (LRG_WIDGET (self), "foley", G_ACTION_GROUP (group));
	self->add_menu = lrg_menu_new_from_model (add_model);
	self->preset_menu = lrg_menu_new_from_model (preset_model);
	tool (self, "foley-add", LRG_ICON_PLUS, "Layer", on_add_chip, &self->add_chip);
	tool (self, "foley-preset", LRG_ICON_STAR, "Preset", on_preset_chip, &self->preset_chip);
	tool (self, "foley-up", LRG_ICON_CHEVRON_UP, "Up", on_up_chip, NULL);
	tool (self, "foley-down", LRG_ICON_CHEVRON_DOWN, "Down", on_down_chip, NULL);
	tool (self, "foley-take", LRG_ICON_REFRESH, "Another take", on_take_chip, NULL);
	tool (self, "foley-play", LRG_ICON_PLAY, "Play", on_play_chip, NULL);
	tool (self, "foley-export", LRG_ICON_DOWNLOAD, "Export WAV", on_export_chip, NULL);

	/* left: layers, the selected layer, the patch settings */
	left = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (left, "foley-form");
	lrg_container_set_spacing (LRG_CONTAINER (left), 6.0f);
	lrg_widget_set_margin (left, &pad);
	heading = LRG_WIDGET (lrg_label_new ("Layers"));
	lrg_widget_add_class (heading, "heading");
	add_to (left, heading);
	self->list = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->list, "foley-layer-list");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->list), &list_funcs, self, NULL);
	add_to (left, self->list);
	add_to (left, make_grid (self, "foley-layer-grid", PART_LAYER));
	heading = LRG_WIDGET (lrg_label_new ("Patch"));
	lrg_widget_add_class (heading, "heading");
	add_to (left, heading);
	add_to (left, make_grid (self, "foley-patch-grid", PART_PATCH));
	scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (scroll, "foley-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), left);
	g_object_unref (left);

	/* right: the waveform */
	right = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (right, "foley-wave-box");
	lrg_widget_set_margin (right, &pad);
	self->wave = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->wave, "foley-waveform");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->wave), &wave_funcs, self, NULL);
	lrg_widget_set_fill_weight (self->wave, 1.0f);
	add_to (right, self->wave);

	split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_splitter_set_start_child (split, scroll);
	g_object_unref (scroll);
	lrg_splitter_set_end_child (split, right);
	g_object_unref (right);
	lrg_splitter_set_default_fraction (split, 0.42f);
	lrg_splitter_set_fraction (split, 0.42f, FALSE);
	lrg_editor_data_editor_set_content (LRG_EDITOR_DATA_EDITOR (self), LRG_WIDGET (split));

	reparse (self);
	self->sel = n_layers (self) > 0 ? 0 : -1;
	sync_views (self);
	app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
	if (app != NULL)
		lrg_editor_foley_editor_install (app);
}

static void
lrg_editor_foley_editor_dispose (GObject *object)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (object);
	guint                 i;

	if (self->add_menu != NULL)
		lrg_menu_close (self->add_menu);
	if (self->preset_menu != NULL)
		lrg_menu_close (self->preset_menu);
	g_clear_object (&self->add_menu);
	g_clear_object (&self->preset_menu);
	if (self->preview != NULL)
		grl_sound_stop (self->preview);
	g_clear_object (&self->preview);
	g_clear_object (&self->patch);
	for (i = 0; i < N_PARTS; i++)
		g_clear_object (&self->src[i]);
	G_OBJECT_CLASS (lrg_editor_foley_editor_parent_class)->dispose (object);
}

static void
lrg_editor_foley_editor_finalize (GObject *object)
{
	LrgEditorFoleyEditor *self = LRG_EDITOR_FOLEY_EDITOR (object);

	g_free (self->signature);
	g_free (self->load_error);
	g_free (self->pcm);
	G_OBJECT_CLASS (lrg_editor_foley_editor_parent_class)->finalize (object);
}

static void
lrg_editor_foley_editor_class_init (LrgEditorFoleyEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_foley_editor_constructed;
	object_class->dispose = lrg_editor_foley_editor_dispose;
	object_class->finalize = lrg_editor_foley_editor_finalize;
	data_class->refresh = foley_refresh;
	data_class->get_state = foley_get_state;
	data_class->validate = foley_validate;
}

static void
lrg_editor_foley_editor_init (LrgEditorFoleyEditor *self)
{
	self->sel = -1;
}

LrgEditorFoleyEditor *
lrg_editor_foley_editor_new (LrgEditorApp      *app,
                             LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_FOLEY_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorFoleyEditor *
lrg_editor_foley_editor_open (LrgEditorApp  *app,
                              const gchar   *path,
                              GError       **error)
{
	LrgEditorDataEditor *editor;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	editor = lrg_editor_data_editor_open (app, path, error);
	if (editor == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_FOLEY_EDITOR (editor))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "%s is not a .rfoley patch",
		             path);
		return NULL;
	}
	return LRG_EDITOR_FOLEY_EDITOR (editor);
}

gboolean
lrg_editor_foley_editor_create (LrgEditorApp  *app,
                                const gchar   *path,
                                const gchar   *kind,
                                GError       **error)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autofree gchar            *abs = NULL;
	g_autofree gchar            *dir = NULL;

	g_return_val_if_fail (path != NULL, FALSE);
	abs = music_abs_path (app, path);
	if (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", abs);
		return FALSE;
	}
	dir = g_path_get_dirname (abs);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Could not create %s", dir);
		return FALSE;
	}
	doc = lrg_editor_document_new (LRG_EDITOR_DOCUMENT_FORMAT_FOLEY);
	if (kind != NULL)
	{
		g_autoptr(GEnumClass)    klass = g_type_class_ref (LRG_TYPE_FOLEY_KIND);
		GEnumValue              *ev = g_enum_get_value_by_nick (klass, kind);
		g_autoptr(LrgFoleyPatch) patch = NULL;

		if (ev == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "No foley kind '%s'", kind);
			return FALSE;
		}
		patch = lrg_foley_patch_new_from_kind ((LrgFoleyKind) ev->value, 0, 0, error);
		if (patch == NULL || !lrg_editor_document_commit_object (doc, G_OBJECT (patch), "Kind", 0, error))
			return FALSE;
	}
	return lrg_editor_document_save_as (doc, abs, error);
}

/* ==========================================================================
 * IPC: foley.*
 *
 * Every method takes an optional "path" (a .rfoley file, opened when
 * needed); without it the focused Foley designer is used.  Edits are one
 * undo step each.
 * ========================================================================== */

static LrgEditorFoleyEditor *
need_editor (LrgEditorApp  *app,
             JsonObject    *params,
             GError       **error)
{
	const gchar         *path = music_param_string (params, "path");
	LrgEditorDataEditor *editor;
	g_autoptr(GError)    err = NULL;

	if (path == NULL)
	{
		editor = lrg_editor_data_editor_find (app, NULL);
		if (!LRG_IS_EDITOR_FOLEY_EDITOR (editor))
			return music_missing (error, "No Foley patch is open");
		return LRG_EDITOR_FOLEY_EDITOR (editor);
	}
	editor = LRG_EDITOR_DATA_EDITOR (lrg_editor_foley_editor_open (app, path, &err));
	if (editor == NULL)
		return music_fail (error, err);
	return LRG_EDITOR_FOLEY_EDITOR (editor);
}

/* the patch as JSON: settings and every layer with all its parameters */
static JsonNode *
patch_json (LrgEditorFoleyEditor *self)
{
	JsonObject *o = json_object_new ();
	JsonArray  *layers = json_array_new ();
	guint       seed = 0, variation = 0, rate = 0;
	gboolean    loop = FALSE, normalize = TRUE;
	gdouble     tail = 0, fade = 0, peak = 0;
	guint       i, j;

	g_object_get (self->patch, "seed", &seed, "variation", &variation, "sample-rate", &rate, "loop", &loop,
	              "loop-tail", &tail, "fade", &fade, "peak", &peak, "normalize", &normalize, NULL);
	json_object_set_string_member (o, "name", lrg_foley_patch_get_name (self->patch));
	json_object_set_double_member (o, "duration", lrg_foley_patch_get_duration (self->patch));
	json_object_set_int_member (o, "sample_rate", rate);
	json_object_set_int_member (o, "seed", seed);
	json_object_set_int_member (o, "variation", variation);
	json_object_set_boolean_member (o, "loop", loop);
	json_object_set_double_member (o, "loop_tail", tail);
	json_object_set_double_member (o, "fade", fade);
	json_object_set_double_member (o, "peak", peak);
	json_object_set_boolean_member (o, "normalize", normalize);
	for (i = 0; i < n_layers (self); i++)
	{
		LrgFoleyLayerType type = lrg_foley_patch_get_layer_type (self->patch, i);
		g_auto(GStrv)     names = lrg_foley_layer_type_list_params (type);
		JsonObject       *l = json_object_new ();
		JsonObject       *params = json_object_new ();

		json_object_set_int_member (l, "index", i);
		json_object_set_string_member (l, "type", type_nick (type));
		for (j = 0; names[j] != NULL; j++)
			json_object_set_double_member (params, names[j], lrg_foley_patch_get_layer_param (self->patch, i, names[j]));
		json_object_set_object_member (l, "params", params);
		json_array_add_object_element (layers, l);
	}
	json_object_set_array_member (o, "layers", layers);
	json_object_set_int_member (o, "selected", self->sel);
	return music_object_node (o);
}

static JsonNode *
m_new (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = music_param_string (params, "path");
	g_autoptr(GError)  err = NULL;
	JsonObject        *o;

	if (path == NULL)
		return music_bad (error, "Missing string parameter 'path'");
	if (!lrg_editor_foley_editor_create (app, path, music_param_string (params, "kind"), &err))
		return music_fail (error, err);
	if (lrg_editor_foley_editor_open (app, path, &err) == NULL)
		return music_fail (error, err);
	o = json_object_new ();
	json_object_set_string_member (o, "path", path);
	return music_object_node (o);
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorFoleyEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);

	if (self == NULL)
		return NULL;
	if (self->patch == NULL)
		return music_bad (error, self->load_error != NULL ? self->load_error : "The patch does not load");
	return patch_json (self);
}

/* "settings": { name: value } on the patch, one undo step */
static gboolean
change_settings (LrgFoleyPatch  *patch,
                 gpointer        data,
                 GError        **error)
{
	JsonObject *settings = data;
	GList      *keys = json_object_get_members (settings);
	GList      *l;
	gboolean    ok = TRUE;

	for (l = keys; l != NULL && ok; l = l->next)
	{
		const gchar *key = l->data;
		JsonNode    *n = json_object_get_member (settings, key);
		gdouble      v = 0.0;

		if (g_str_equal (key, "name") && JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
			g_object_set (patch, "name", json_node_get_string (n), NULL);
		else if (!music_param_double (settings, key, &v) || !lrg_foley_patch_set_global (patch, key, v))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "No patch setting '%s' (or not a number)", key);
			ok = FALSE;
		}
	}
	g_list_free (keys);
	return ok;
}

static JsonNode *
m_set (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorFoleyEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject           *settings = music_param_object (params, "settings");
	g_autoptr(GError)     err = NULL;

	if (self == NULL)
		return NULL;
	if (settings == NULL)
		return music_bad (error, "Missing object parameter 'settings'");
	if (!edit_patch (self, "Foley: settings", 0, change_settings, settings, &err))
		return music_fail (error, err);
	return patch_json (self);
}

typedef struct
{
	guint       index;
	JsonObject *params;
} LayerParams;

static gboolean
change_layer_params (LrgFoleyPatch  *patch,
                     gpointer        data,
                     GError        **error)
{
	LayerParams *lp = data;
	GList       *keys;
	GList       *l;
	gboolean     ok = TRUE;

	if (lp->index >= lrg_foley_patch_get_n_layers (patch))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No layer %u", lp->index);
		return FALSE;
	}
	if (lp->params == NULL)
		return TRUE;
	keys = json_object_get_members (lp->params);
	for (l = keys; l != NULL && ok; l = l->next)
	{
		gdouble v = 0.0;

		if (!music_param_double (lp->params, l->data, &v)
		    || !lrg_foley_patch_set_layer_param (patch, lp->index, l->data, v))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Layer %u has no parameter '%s'",
			             lp->index, (const gchar *) l->data);
			ok = FALSE;
		}
	}
	g_list_free (keys);
	return ok;
}

typedef struct
{
	LrgFoleyLayerType type;
	JsonObject       *params;
	gint              result;
} AddLayer;

static gboolean
change_add_with_params (LrgFoleyPatch  *patch,
                        gpointer        data,
                        GError        **error)
{
	AddLayer    *a = data;
	LayerParams  lp;

	a->result = lrg_foley_patch_add_layer (patch, a->type);
	if (a->result < 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "The patch has no room for a layer");
		return FALSE;
	}
	lp.index = (guint) a->result;
	lp.params = a->params;
	return change_layer_params (patch, &lp, error);
}

static JsonNode *
m_add_layer (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorFoleyEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar          *type = music_param_string (params, "type");
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_FOLEY_LAYER_TYPE);
	g_autoptr(GError)     err = NULL;
	GEnumValue           *ev;
	AddLayer              a;
	JsonObject           *o;

	if (self == NULL)
		return NULL;
	ev = type != NULL ? g_enum_get_value_by_nick (klass, type) : NULL;
	if (ev == NULL)
		return music_bad (error, "Parameter 'type' must be body, grain, wash, bed, tone or noise");
	a.type = (LrgFoleyLayerType) ev->value;
	a.params = music_param_object (params, "params");
	if (!edit_patch (self, "Foley: add layer", 0, change_add_with_params, &a, &err))
		return music_fail (error, err);
	lrg_editor_foley_editor_select_layer (self, a.result);
	o = json_object_new ();
	json_object_set_int_member (o, "index", a.result);
	return music_object_node (o);
}

static JsonNode *
m_set_layer (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorFoleyEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)     err = NULL;
	LayerParams           lp;
	gdouble               index = -1;

	if (self == NULL)
		return NULL;
	if (!music_param_double (params, "index", &index) || index < 0)
		return music_bad (error, "Missing integer parameter 'index'");
	lp.index = (guint) index;
	lp.params = music_param_object (params, "params");
	if (lp.params == NULL)
		return music_bad (error, "Missing object parameter 'params'");
	if (!edit_patch (self, "Foley: layer", 0, change_layer_params, &lp, &err))
		return music_fail (error, err);
	return patch_json (self);
}

static JsonNode *
m_remove_layer (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorFoleyEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)     err = NULL;
	gdouble               index = -1;
	JsonObject           *o;

	if (self == NULL)
		return NULL;
	if (!music_param_double (params, "index", &index) || index < 0)
		return music_bad (error, "Missing integer parameter 'index'");
	if (!lrg_editor_foley_editor_remove_layer (self, (guint) index, &err))
		return music_fail (error, err);
	o = json_object_new ();
	json_object_set_boolean_member (o, "removed", TRUE);
	return music_object_node (o);
}

static JsonNode *
m_move_layer (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorFoleyEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)     err = NULL;
	gdouble               from = -1, to = -1;

	if (self == NULL)
		return NULL;
	if (!music_param_double (params, "from", &from) || !music_param_double (params, "to", &to) || from < 0 || to < 0)
		return music_bad (error, "Missing integer parameters 'from' and 'to'");
	if (!lrg_editor_foley_editor_move_layer (self, (guint) from, (guint) to, &err))
		return music_fail (error, err);
	return patch_json (self);
}

static JsonNode *
m_from_kind (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorFoleyEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar          *kind = music_param_string (params, "kind");
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_FOLEY_KIND);
	g_autoptr(GError)     err = NULL;
	GEnumValue           *ev;
	gdouble               variant = 0;

	if (self == NULL)
		return NULL;
	ev = kind != NULL ? g_enum_get_value_by_nick (klass, kind) : NULL;
	if (ev == NULL)
		return music_bad (error, "Parameter 'kind' must be a foley kind such as footstep-stone");
	music_param_double (params, "variant", &variant);
	if (!lrg_editor_foley_editor_load_kind (self, (LrgFoleyKind) ev->value, (guint) MAX (variant, 0.0), &err))
		return music_fail (error, err);
	return patch_json (self);
}

static JsonNode *
m_render (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorFoleyEditor        *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)            err = NULL;
	g_autoptr(LrgAudioAnalysis)  a = NULL;
	const gfloat                *pcm;
	gsize                        n = 0;
	gdouble                      bins = 48;
	JsonObject                  *o;

	if (self == NULL)
		return NULL;
	pcm = lrg_editor_foley_editor_render (self, &n, &err);
	if (pcm == NULL)
		return music_fail (error, err);
	music_param_double (params, "bins", &bins);
	a = lrg_audio_analyze (pcm, n, 1, lrg_foley_patch_get_sample_rate (self->patch));
	if (a == NULL)
		return music_bad (error, "The render could not be measured");
	o = music_analysis_json (a);
	json_object_set_int_member (o, "frames", (gint64) n);
	json_object_set_int_member (o, "sample_rate", lrg_foley_patch_get_sample_rate (self->patch));
	json_object_set_array_member (o, "envelope", music_envelope (pcm, n, 1, (guint) CLAMP (bins, 1.0, 1024.0)));
	return music_object_node (o);
}

static JsonNode *
m_export (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp         *app = LRG_EDITOR_APP (user_data);
	LrgEditorFoleyEditor *self = need_editor (app, params, error);
	const gchar          *file = music_param_string (params, "file");
	g_autofree gchar     *abs = NULL;
	g_autoptr(GError)     err = NULL;
	JsonObject           *o;
	GStatBuf              st;

	if (self == NULL)
		return NULL;
	if (file == NULL)
		return music_bad (error, "Missing string parameter 'file' (its extension picks the format)");
	abs = music_abs_path (app, file);
	if (!music_ensure_parent (abs, &err))
		return music_fail (error, err);
	if (!lrg_editor_foley_editor_export (self, abs, &err))
		return music_fail (error, err);
	o = json_object_new ();
	json_object_set_string_member (o, "file", abs);
	if (g_stat (abs, &st) == 0)
		json_object_set_int_member (o, "bytes", st.st_size);
	return music_object_node (o);
}

static JsonNode *
m_play (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorFoleyEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)     err = NULL;
	JsonObject           *o;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_foley_editor_play (self, &err))
		return music_fail (error, err);
	o = json_object_new ();
	json_object_set_boolean_member (o, "playing", TRUE);
	return music_object_node (o);
}

/* every layer type with its parameters (name, range, default, meaning) */
static JsonNode *
m_layer_types (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_FOLEY_LAYER_TYPE);
	g_autoptr(GEnumClass) kinds = g_type_class_ref (LRG_TYPE_FOLEY_KIND);
	JsonObject           *o = json_object_new ();
	JsonObject           *types = json_object_new ();
	JsonArray            *kind_list = json_array_new ();
	guint                 t, i;

	for (t = 0; t < klass->n_values; t++)
	{
		g_auto(GStrv) names = lrg_foley_layer_type_list_params ((LrgFoleyLayerType) klass->values[t].value);
		JsonArray    *list = json_array_new ();

		for (i = 0; names[i] != NULL; i++)
		{
			JsonObject  *p = json_object_new ();
			gdouble      min = 0, max = 0, def = 0;
			const gchar *blurb = NULL;

			lrg_foley_layer_type_get_param_info ((LrgFoleyLayerType) klass->values[t].value, names[i], &min, &max, &def,
			                                     &blurb);
			json_object_set_string_member (p, "name", names[i]);
			json_object_set_double_member (p, "min", min);
			json_object_set_double_member (p, "max", max);
			json_object_set_double_member (p, "default", def);
			json_object_set_string_member (p, "description", blurb);
			json_array_add_object_element (list, p);
		}
		json_object_set_array_member (types, klass->values[t].value_nick, list);
	}
	for (i = 0; i < kinds->n_values; i++)
		json_array_add_string_element (kind_list, kinds->values[i].value_nick);
	json_object_set_object_member (o, "layer_types", types);
	json_object_set_array_member (o, "kinds", kind_list);
	return music_object_node (o);
}

void
lrg_editor_foley_editor_install (LrgEditorApp *app)
{
	static const struct
	{
		const gchar           *name;
		LrgEditorServerMethod  func;
	} methods[] = {
		{ "foley.new", m_new },
		{ "foley.get", m_get },
		{ "foley.set", m_set },
		{ "foley.add_layer", m_add_layer },
		{ "foley.set_layer", m_set_layer },
		{ "foley.remove_layer", m_remove_layer },
		{ "foley.move_layer", m_move_layer },
		{ "foley.from_kind", m_from_kind },
		{ "foley.render", m_render },
		{ "foley.export", m_export },
		{ "foley.play", m_play },
		{ "foley.layer_types", m_layer_types },
	};
	LrgEditorServer *server;
	guint            i;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));
	if (g_object_get_data (G_OBJECT (app), INSTALL_KEY) != NULL)
		return;
	server = lrg_editor_app_get_server (app);
	if (server == NULL)
		return;
	g_object_set_data (G_OBJECT (app), INSTALL_KEY, GINT_TO_POINTER (1));
	lrg_editor_data_editor_install (app);
	for (i = 0; i < G_N_ELEMENTS (methods); i++)
		lrg_editor_server_register_method (server, methods[i].name, methods[i].func, app, NULL);
}
