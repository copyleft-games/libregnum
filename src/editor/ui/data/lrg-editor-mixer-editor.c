/* lrg-editor-mixer-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMixerEditor: the `.rmixer` data editor, its channel strips
 * (shared with the Mixer panel) and the mixer.* IPC methods.
 *
 * Model: the document text is the truth. load_asset() parses it into
 * self->asset after every document change; every edit copies that
 * asset, changes the copy through the LrgMixerAsset API (which checks
 * routing, names and ranges) and lrg_editor_mixer_editor_commit() writes
 * lrg_mixer_asset_to_data() of the copy over the whole document in one
 * undo group (gesture ids merge a fader drag into one step).
 *
 * Widgets:
 *
 *   toolbar      Preview (toggle), Base (preview without snapshot)
 *   strips       one channel strip per bus (MixerStrips, a custom widget
 *                drawn and hit-tested here; the Mixer panel embeds a
 *                compact one)
 *   left         heading + property grid of the selected bus (parent,
 *                volume, mute, solo, sends) or effect (enabled + every
 *                parameter of its type), then Add effect / Add send /
 *                new bus / move / remove controls
 *   middle       snapshot list (select, preview, remove), name input,
 *                Add / Capture, property grid of the selected snapshot
 *                (blend, default, then every volume, send level and
 *                numeric effect parameter, overridden ones marked)
 *   right        ducking rules list, trigger/target pickers + Add, the
 *                selected rule's threshold, depth, attack, release
 *
 * The three grids are fed by MixerSource, an LrgPropertySource whose
 * rows are built from the current layout: names are generated ("vol-3")
 * and map to a key table, labels carry the bus / send / parameter.
 *
 * Preview: an offline LrgAudioMixer built from the layout plays a looping
 * pulsed tone on every bus without child buses and renders the frame
 * time every step; the strips read its meters (or the game mixer's while
 * play-in-editor runs).
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-mixer-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../lrg-editor-server.h"
#include "../../lrg-project.h"
#include "../../lrg-editor-play-session.h"
#include "../../../audio/lrg-audio-manager.h"
#include "../../../audio/lrg-wave-data.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-combo-box.h"
#include "../../../ui/lrg-text-input.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-property-source.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../paint/lrg-paint-list.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <math.h>

#define INSTALL_KEY     "lrg-mixer-editor-installed"
#define MIXER_PANEL_ID  "mixer"

/* strip geometry */
#define STRIP_W         (118.0f)
#define STRIP_W_COMPACT (78.0f)
#define STRIP_GAP       (6.0f)
#define ROW_H           (18.0f)
#define HEADER_H        (40.0f)
#define BUTTON_H        (22.0f)
#define READOUT_H       (18.0f)

/* ==========================================================================
 * Enums and the fader law
 * ========================================================================== */

GType
lrg_editor_mixer_strip_part_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_MIXER_STRIP_HEADER, "LRG_EDITOR_MIXER_STRIP_HEADER", "header" },
			{ LRG_EDITOR_MIXER_STRIP_EFFECT, "LRG_EDITOR_MIXER_STRIP_EFFECT", "effect" },
			{ LRG_EDITOR_MIXER_STRIP_EFFECT_TOGGLE, "LRG_EDITOR_MIXER_STRIP_EFFECT_TOGGLE", "effect-toggle" },
			{ LRG_EDITOR_MIXER_STRIP_SEND, "LRG_EDITOR_MIXER_STRIP_SEND", "send" },
			{ LRG_EDITOR_MIXER_STRIP_FADER, "LRG_EDITOR_MIXER_STRIP_FADER", "fader" },
			{ LRG_EDITOR_MIXER_STRIP_METER, "LRG_EDITOR_MIXER_STRIP_METER", "meter" },
			{ LRG_EDITOR_MIXER_STRIP_MUTE, "LRG_EDITOR_MIXER_STRIP_MUTE", "mute" },
			{ LRG_EDITOR_MIXER_STRIP_SOLO, "LRG_EDITOR_MIXER_STRIP_SOLO", "solo" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorMixerStripPart"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* (dB, position) knots of the fader law, bottom to top */
static const gdouble fader_knots[][2] = {
	{ LRG_EDITOR_MIXER_FADER_MIN_DB, 0.05 },
	{ -12.0, 0.45 },
	{ 0.0, 0.75 },
	{ LRG_AUDIO_MIXER_MAX_GAIN_DB, 1.0 }
};

gdouble
lrg_editor_mixer_fader_db_to_position (gdouble db)
{
	guint i;

	if (isnan (db) || db < LRG_EDITOR_MIXER_FADER_MIN_DB)
		return 0.0;
	for (i = 1; i < G_N_ELEMENTS (fader_knots); i++)
	{
		if (db <= fader_knots[i][0])
		{
			gdouble t = (db - fader_knots[i - 1][0]) / (fader_knots[i][0] - fader_knots[i - 1][0]);

			return fader_knots[i - 1][1] + t * (fader_knots[i][1] - fader_knots[i - 1][1]);
		}
	}
	return 1.0;
}

gdouble
lrg_editor_mixer_fader_position_to_db (gdouble position)
{
	guint i;

	if (isnan (position) || position < fader_knots[0][1])
		return LRG_AUDIO_SILENCE_DB;
	for (i = 1; i < G_N_ELEMENTS (fader_knots); i++)
	{
		if (position <= fader_knots[i][1])
		{
			gdouble t = (position - fader_knots[i - 1][1]) / (fader_knots[i][1] - fader_knots[i - 1][1]);

			return fader_knots[i - 1][0] + t * (fader_knots[i][0] - fader_knots[i - 1][0]);
		}
	}
	return LRG_AUDIO_MIXER_MAX_GAIN_DB;
}

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static GrlColor
rgba (guint8 r,
      guint8 g,
      guint8 b,
      guint8 a)
{
	GrlColor c;

	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

/* A stable colour per bus name (catppuccin accents). */
static GrlColor
bus_color (const gchar *name)
{
	static const guint8 palette[][3] = {
		{ 0x89, 0xb4, 0xfa }, { 0xa6, 0xe3, 0xa1 }, { 0xf9, 0xe2, 0xaf }, { 0xf5, 0xc2, 0xe7 },
		{ 0x94, 0xe2, 0xd5 }, { 0xfa, 0xb3, 0x87 }, { 0xcb, 0xa6, 0xf7 }, { 0xf3, 0x8b, 0xa8 }
	};
	guint h = (g_strcmp0 (name, "master") == 0) ? 6u : g_str_hash (name != NULL ? name : "");

	h %= G_N_ELEMENTS (palette);
	return rgba (palette[h][0], palette[h][1], palette[h][2], 0xff);
}

static gboolean
strv_has (gchar       **strv,
          const gchar  *s)
{
	return strv != NULL && s != NULL && g_strv_contains ((const gchar * const *) strv, s);
}

static guint
strv_len (gchar **strv)
{
	return strv != NULL ? g_strv_length (strv) : 0u;
}

/* Whether @bus has child buses. */
static gboolean
bus_has_children (LrgMixerAsset *asset,
                  const gchar   *bus)
{
	g_auto(GStrv) names = lrg_mixer_asset_get_bus_names (asset);
	guint         i;

	for (i = 0; names != NULL && names[i] != NULL; i++)
		if (g_strcmp0 (lrg_mixer_asset_get_bus_parent (asset, names[i]), bus) == 0)
			return TRUE;
	return FALSE;
}

/* Whether @bus is @ancestor or below it. */
static gboolean
bus_is_below (LrgMixerAsset *asset,
              const gchar   *bus,
              const gchar   *ancestor)
{
	const gchar *b = bus;
	guint        guard = 0;

	while (b != NULL && guard++ < 256)
	{
		if (g_strcmp0 (b, ancestor) == 0)
			return TRUE;
		b = lrg_mixer_asset_get_bus_parent (asset, b);
	}
	return FALSE;
}

static gchar *
format_db (gdouble db)
{
	if (db <= LRG_AUDIO_SILENCE_DB + 0.01)
		return g_strdup ("-inf");
	return g_strdup_printf ("%+.1f", db);
}

/* The writable parameters of an effect type (borrowed pspecs). */
static GPtrArray *
effect_pspecs (GType type)
{
	GPtrArray     *out = g_ptr_array_new ();
	GObjectClass  *klass;
	GParamSpec   **specs;
	guint          n = 0, i;

	if (type == G_TYPE_INVALID || !g_type_is_a (type, LRG_TYPE_AUDIO_EFFECT))
		return out;
	klass = g_type_class_ref (type);
	specs = g_object_class_list_properties (klass, &n);
	for (i = 0; i < n; i++)
	{
		GParamSpec *p = specs[i];

		if (!(p->flags & G_PARAM_WRITABLE) || (p->flags & G_PARAM_CONSTRUCT_ONLY) ||
		    p->owner_type == LRG_TYPE_AUDIO_EFFECT || p->owner_type == G_TYPE_OBJECT)
			continue;
		g_ptr_array_add (out, p);
	}
	g_free (specs);
	/* registered effect classes live for the process: keep the ref */
	return out;
}

static gboolean
pspec_is_numeric (GParamSpec *p)
{
	GType t = G_PARAM_SPEC_VALUE_TYPE (p);

	return t == G_TYPE_FLOAT || t == G_TYPE_DOUBLE || t == G_TYPE_INT || t == G_TYPE_UINT || t == G_TYPE_INT64 ||
	       t == G_TYPE_UINT64 || t == G_TYPE_LONG || t == G_TYPE_ULONG;
}

/* ==========================================================================
 * The editor
 * ========================================================================== */

typedef struct _MixerSource MixerSource;

struct _LrgEditorMixerEditor
{
	LrgEditorDataEditor  parent_instance;

	LrgMixerAsset       *asset;          /* parsed document, nullable */
	gchar               *load_error;

	/* selection */
	gchar               *sel_bus;
	gchar               *sel_effect;
	gchar               *sel_snapshot;
	gint                 sel_duck;

	/* widgets (borrowed: in the tree) */
	LrgWidget           *strips;
	LrgLabel            *bus_heading;
	LrgPropertyGrid     *bus_grid;
	LrgPropertyGrid     *snap_grid;
	LrgPropertyGrid     *duck_grid;
	LrgComboBox         *add_effect;
	LrgComboBox         *add_send;
	LrgTextInput        *bus_name;
	LrgTextInput        *snap_name;
	LrgWidget           *snap_list;
	LrgWidget           *duck_list;
	LrgComboBox         *duck_trigger;
	LrgComboBox         *duck_target;
	LrgWidget           *preview_chip;
	LrgWidget           *move_up;
	LrgWidget           *move_down;
	LrgLabel            *status;

	/* grid sources (owned) */
	MixerSource         *bus_src;
	MixerSource         *snap_src;
	MixerSource         *duck_src;
	gboolean             grids_stale;
	gchar               *combo_sig;
	gchar              **effect_types;   /* the Add effect picker's items */

	/* preview */
	gboolean             previewing;
	LrgAudioMixer       *preview;
	gchar               *preview_snapshot;
	guint64              rendered_frame; /* app frame the preview last rendered */
	gboolean             syncing;
};

G_DEFINE_FINAL_TYPE (LrgEditorMixerEditor, lrg_editor_mixer_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void sync_widgets (LrgEditorMixerEditor *self);
static void rebuild_preview (LrgEditorMixerEditor *self);
static void pump_preview (LrgEditorMixerEditor *self,
                          gdouble               delta_time);

static LrgEditorDocument *
doc_of (LrgEditorMixerEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorMixerEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

/* Parses the document into self->asset (or load_error). */
static void
load_asset (LrgEditorMixerEditor *self)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = lrg_editor_document_to_data (doc_of (self), NULL);

	g_clear_object (&self->asset);
	g_clear_pointer (&self->load_error, g_free);
	self->asset = lrg_mixer_asset_new_from_data (text != NULL ? text : "", -1, &error);
	if (self->asset == NULL)
		self->load_error = g_strdup (error != NULL ? error->message : "not a mixer layout");

	/* drop a selection that no longer exists */
	if (self->asset == NULL || !lrg_mixer_asset_has_bus (self->asset, self->sel_bus))
	{
		g_clear_pointer (&self->sel_bus, g_free);
		g_clear_pointer (&self->sel_effect, g_free);
	}
	else if (self->sel_effect != NULL)
	{
		g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (self->asset, self->sel_bus);

		if (!strv_has (ids, self->sel_effect))
			g_clear_pointer (&self->sel_effect, g_free);
	}
	if (self->sel_snapshot != NULL)
	{
		g_auto(GStrv) snaps = self->asset != NULL ? lrg_mixer_asset_get_snapshot_names (self->asset) : NULL;

		if (!strv_has (snaps, self->sel_snapshot))
			g_clear_pointer (&self->sel_snapshot, g_free);
	}
	/* a rule stays selected: the last one when the selection is gone,
	 * the first when nothing was selected */
	if (self->asset == NULL || self->sel_duck >= (gint) lrg_mixer_asset_get_duck_count (self->asset))
		self->sel_duck = (self->asset != NULL && lrg_mixer_asset_get_duck_count (self->asset) > 0)
		                 ? (gint) lrg_mixer_asset_get_duck_count (self->asset) - 1 : -1;
	else if (self->sel_duck < 0 && lrg_mixer_asset_get_duck_count (self->asset) > 0)
		self->sel_duck = 0;
}

LrgMixerAsset *
lrg_editor_mixer_editor_get_asset (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), NULL);
	return self->asset;
}

const gchar *
lrg_editor_mixer_editor_get_load_error (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), NULL);
	return self->load_error;
}

gboolean
lrg_editor_mixer_editor_commit (LrgEditorMixerEditor  *self,
                                LrgMixerAsset         *edited,
                                const gchar           *label,
                                guint                  gesture,
                                GError               **error)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *now = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_MIXER_ASSET (edited), FALSE);

	if (!lrg_mixer_asset_validate (edited, error))
		return FALSE;
	text = lrg_mixer_asset_to_data (edited, NULL, error);
	if (text == NULL)
		return FALSE;
	now = lrg_editor_document_to_data (doc_of (self), NULL);
	if (g_strcmp0 (now, text) == 0)
		return TRUE;
	if (gesture != 0)
		ok = lrg_editor_document_replace_gesture (doc_of (self), "", text, gesture, error);
	else
	{
		lrg_editor_document_begin_group (doc_of (self), label != NULL ? label : "Edit Mixer");
		ok = lrg_editor_document_replace_gesture (doc_of (self), "", text, 0, error);
		lrg_editor_document_end_group (doc_of (self));
	}
	if (ok)
	{
		/* the refresh vfunc follows the document; reparse now so callers
		 * (IPC, strips) see the result immediately */
		load_asset (self);
		sync_widgets (self);
	}
	return ok;
}

/* A copy of the current layout to edit, or NULL with @error set. */
static LrgMixerAsset *
edit_copy (LrgEditorMixerEditor  *self,
           GError               **error)
{
	if (self->asset == NULL)
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_PARSE, "%s",
		             self->load_error != NULL ? self->load_error : "The document is not a mixer layout");
		return NULL;
	}
	return lrg_mixer_asset_copy (self->asset);
}

/* Reports a failed edit in the editor's banner and status line. */
static void
report (LrgEditorMixerEditor *self,
        const GError         *error)
{
	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* ==========================================================================
 * MixerSource: the three property grids
 * ========================================================================== */

typedef enum
{
	SRC_BUS,       /* the selected bus, or its selected effect */
	SRC_SNAPSHOT,
	SRC_DUCK
} SourceMode;

/* What a generated row name stands for. */
typedef enum
{
	KEY_PARENT,
	KEY_VOLUME,
	KEY_MUTE,
	KEY_SOLO,
	KEY_SEND,          /* a = target */
	KEY_ENABLED,
	KEY_PARAM,         /* effect parameter: a = param */
	KEY_BLEND,
	KEY_DEFAULT,
	KEY_SNAP_VOLUME,   /* a = bus */
	KEY_SNAP_SEND,     /* a = source, b = target */
	KEY_SNAP_PARAM,    /* a = bus, b = effect, c = param */
	KEY_THRESHOLD,
	KEY_DEPTH,
	KEY_ATTACK,
	KEY_RELEASE
} KeyKind;

typedef struct
{
	KeyKind  kind;
	gchar   *a;
	gchar   *b;
	gchar   *c;
} RowKey;

static void
row_key_free (gpointer data)
{
	RowKey *k = data;

	g_free (k->a);
	g_free (k->b);
	g_free (k->c);
	g_free (k);
}

#define MIXER_TYPE_SOURCE (mixer_source_get_type ())
G_DECLARE_FINAL_TYPE (MixerSource, mixer_source, MIXER, SOURCE, GObject)

struct _MixerSource
{
	GObject               parent_instance;

	LrgEditorMixerEditor *editor;   /* not owned: the editor owns us */
	SourceMode            mode;
	GPtrArray            *pspecs;   /* GParamSpec*, owned refs */
	GHashTable           *keys;     /* row name -> RowKey* */
	gchar                *sig;      /* what the rows were built for */
	guint                 gesture;  /* while a grid gesture runs */
};

static void mixer_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (MixerSource, mixer_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, mixer_source_iface_init))

static void
source_add (MixerSource *src,
            GParamSpec  *pspec,
            KeyKind      kind,
            const gchar *a,
            const gchar *b,
            const gchar *c)
{
	RowKey *k = g_new0 (RowKey, 1);

	k->kind = kind;
	k->a = g_strdup (a);
	k->b = g_strdup (b);
	k->c = g_strdup (c);
	g_hash_table_insert (src->keys, g_strdup (g_param_spec_get_name (pspec)), k);
	g_ptr_array_add (src->pspecs, g_param_spec_ref_sink (pspec));
}

/* A generated double row: name "<prefix>-<n>", label and group in the
 * nick and blurb. */
static GParamSpec *
gen_double (MixerSource *src,
            const gchar *prefix,
            const gchar *label,
            const gchar *group,
            gdouble      min,
            gdouble      max,
            gdouble      def,
            const gchar *hint)
{
	g_autofree gchar *name = g_strdup_printf ("%s-%u", prefix, src->pspecs->len);
	g_autofree gchar *blurb = g_strdup_printf ("[group:%s]%s%s%s", group, hint != NULL ? "[hint:" : "",
	                                           hint != NULL ? hint : "", hint != NULL ? "]" : "");

	return g_param_spec_double (g_intern_string (name), g_intern_string (label), g_intern_string (blurb), min, max,
	                            def, G_PARAM_READWRITE);
}

/* The signature of what the source's rows depend on (not the values). */
static gchar *
source_signature (MixerSource *src)
{
	LrgEditorMixerEditor *ed = src->editor;
	LrgMixerAsset        *a = ed != NULL ? ed->asset : NULL;
	GString              *s = g_string_new (NULL);
	g_auto(GStrv)         buses = NULL;
	guint                 i;

	if (a == NULL)
		return g_string_free (s, FALSE);
	buses = lrg_mixer_asset_get_bus_names (a);
	switch (src->mode)
	{
	case SRC_BUS:
		g_string_append_printf (s, "%s/%s:", ed->sel_bus != NULL ? ed->sel_bus : "",
		                        ed->sel_effect != NULL ? ed->sel_effect : "");
		if (ed->sel_effect != NULL && lrg_mixer_asset_get_effect_type (a, ed->sel_bus, ed->sel_effect) != NULL)
			g_string_append (s, lrg_mixer_asset_get_effect_type (a, ed->sel_bus, ed->sel_effect));
		else if (ed->sel_bus != NULL)
		{
			g_auto(GStrv) sends = lrg_mixer_asset_get_send_targets (a, ed->sel_bus);
			g_autofree gchar *b = g_strjoinv (",", buses);
			g_autofree gchar *t = sends != NULL ? g_strjoinv (",", sends) : g_strdup ("");

			g_string_append_printf (s, "%s|%s", b, t);
		}
		break;
	case SRC_SNAPSHOT:
		g_string_append_printf (s, "%s:", ed->sel_snapshot != NULL ? ed->sel_snapshot : "");
		for (i = 0; ed->sel_snapshot != NULL && buses[i] != NULL; i++)
		{
			g_auto(GStrv) sends = lrg_mixer_asset_get_send_targets (a, buses[i]);
			g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (a, buses[i]);
			g_autofree gchar *t = sends != NULL ? g_strjoinv (",", sends) : g_strdup ("");
			g_autofree gchar *e = ids != NULL ? g_strjoinv (",", ids) : g_strdup ("");
			guint j;

			g_string_append_printf (s, "%s[%s][%s", buses[i], t, e);
			for (j = 0; ids != NULL && ids[j] != NULL; j++)
				g_string_append_printf (s, ":%s", lrg_mixer_asset_get_effect_type (a, buses[i], ids[j]));
			g_string_append (s, "]");
		}
		break;
	case SRC_DUCK:
	default:
		g_string_append_printf (s, "%d", ed->sel_duck);
		break;
	}
	return g_string_free (s, FALSE);
}

/* Rebuilds the rows for the current layout and selection. */
static void
source_rebuild (MixerSource *src)
{
	LrgEditorMixerEditor *ed = src->editor;
	LrgMixerAsset        *a = ed != NULL ? ed->asset : NULL;
	g_auto(GStrv)         buses = NULL;
	guint                 i;

	g_ptr_array_set_size (src->pspecs, 0);
	g_hash_table_remove_all (src->keys);
	g_free (src->sig);
	src->sig = source_signature (src);
	if (a == NULL)
		return;
	buses = lrg_mixer_asset_get_bus_names (a);

	switch (src->mode)
	{
	case SRC_BUS:
		if (ed->sel_bus == NULL)
			break;
		if (ed->sel_effect != NULL)
		{
			const gchar         *type = lrg_mixer_asset_get_effect_type (a, ed->sel_bus, ed->sel_effect);
			g_autoptr(GPtrArray) specs = effect_pspecs (lrg_mixer_asset_lookup_effect_type (type));

			source_add (src, g_param_spec_boolean ("enabled", "Enabled", "[group:Effect] Unchecked: bypassed",
			                                       TRUE, G_PARAM_READWRITE),
			            KEY_ENABLED, NULL, NULL, NULL);
			for (i = 0; i < specs->len; i++)
			{
				GParamSpec *p = g_ptr_array_index (specs, i);
				RowKey     *k = g_new0 (RowKey, 1);

				/* the effect's own pspec: its type, range and default */
				k->kind = KEY_PARAM;
				k->a = g_strdup (p->name);
				g_hash_table_insert (src->keys, g_strdup (p->name), k);
				g_ptr_array_add (src->pspecs, g_param_spec_ref (p));
			}
		}
		else
		{
			GString       *parents = g_string_new ("enum-strings:");
			g_auto(GStrv)  sends = lrg_mixer_asset_get_send_targets (a, ed->sel_bus);
			gboolean       is_master = g_strcmp0 (ed->sel_bus, "master") == 0;
			gboolean       first = TRUE;
			g_autofree gchar *pblurb = NULL;

			for (i = 0; buses[i] != NULL; i++)
			{
				if (bus_is_below (a, buses[i], ed->sel_bus))
					continue;
				g_string_append_printf (parents, "%s%s", first ? "" : ",", buses[i]);
				first = FALSE;
			}
			pblurb = g_strdup_printf ("[group:Bus][hint:%s] The bus this one mixes into", parents->str);
			g_string_free (parents, TRUE);
			source_add (src, g_param_spec_string ("parent", "Parent", g_intern_string (pblurb), "master",
			                                      is_master ? G_PARAM_READABLE : G_PARAM_READWRITE),
			            KEY_PARENT, NULL, NULL, NULL);
			source_add (src, g_param_spec_double ("volume", "Volume (dB)",
			                                      "[group:Bus][hint:range:-144,12.04,0.1] Fader level in dB",
			                                      LRG_AUDIO_SILENCE_DB, LRG_AUDIO_MIXER_MAX_GAIN_DB, 0.0,
			                                      G_PARAM_READWRITE),
			            KEY_VOLUME, NULL, NULL, NULL);
			source_add (src, g_param_spec_boolean ("mute", "Mute", "[group:Bus] Silences the bus and its sends",
			                                       FALSE, G_PARAM_READWRITE),
			            KEY_MUTE, NULL, NULL, NULL);
			source_add (src, g_param_spec_boolean ("solo", "Solo",
			                                       "[group:Bus] While any bus is soloed only soloed buses are heard",
			                                       FALSE, G_PARAM_READWRITE),
			            KEY_SOLO, NULL, NULL, NULL);
			for (i = 0; sends != NULL && sends[i] != NULL; i++)
			{
				g_autofree gchar *label = g_strdup_printf ("To %s (dB)", sends[i]);

				source_add (src, gen_double (src, "send", label, "Sends", LRG_AUDIO_SILENCE_DB,
				                             LRG_AUDIO_MIXER_MAX_GAIN_DB, 0.0, "range:-144,12.04,0.1"),
				            KEY_SEND, sends[i], NULL, NULL);
			}
		}
		break;

	case SRC_SNAPSHOT:
		if (ed->sel_snapshot == NULL)
			break;
		source_add (src, g_param_spec_double ("blend", "Blend (s)",
		                                      "[group:Snapshot][hint:range:0,30,0.05] Default transition time into it",
		                                      0.0, 3600.0, 0.0, G_PARAM_READWRITE),
		            KEY_BLEND, NULL, NULL, NULL);
		source_add (src, g_param_spec_boolean ("default", "Default",
		                                       "[group:Snapshot] Applied with the layout (lrg_mixer_asset_apply())",
		                                       FALSE, G_PARAM_READWRITE),
		            KEY_DEFAULT, NULL, NULL, NULL);
		for (i = 0; buses[i] != NULL; i++)
			source_add (src, gen_double (src, "vol", buses[i], "Volumes (dB)", LRG_AUDIO_SILENCE_DB,
			                             LRG_AUDIO_MIXER_MAX_GAIN_DB, 0.0, "range:-144,12.04,0.1"),
			            KEY_SNAP_VOLUME, buses[i], NULL, NULL);
		for (i = 0; buses[i] != NULL; i++)
		{
			g_auto(GStrv) sends = lrg_mixer_asset_get_send_targets (a, buses[i]);
			guint         j;

			for (j = 0; sends != NULL && sends[j] != NULL; j++)
			{
				g_autofree gchar *label = g_strdup_printf ("%s > %s", buses[i], sends[j]);

				source_add (src, gen_double (src, "send", label, "Send levels (dB)", LRG_AUDIO_SILENCE_DB,
				                             LRG_AUDIO_MIXER_MAX_GAIN_DB, 0.0, "range:-144,12.04,0.1"),
				            KEY_SNAP_SEND, buses[i], sends[j], NULL);
			}
		}
		for (i = 0; buses[i] != NULL; i++)
		{
			g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (a, buses[i]);
			guint         j, k;

			for (j = 0; ids != NULL && ids[j] != NULL; j++)
			{
				const gchar         *type = lrg_mixer_asset_get_effect_type (a, buses[i], ids[j]);
				g_autoptr(GPtrArray) specs = effect_pspecs (lrg_mixer_asset_lookup_effect_type (type));

				for (k = 0; k < specs->len; k++)
				{
					GParamSpec       *p = g_ptr_array_index (specs, k);
					g_autofree gchar *label = NULL;
					gdouble           mn = -G_MAXDOUBLE, mx = G_MAXDOUBLE;
					g_auto(GValue)    dv = G_VALUE_INIT;
					g_auto(GValue)    dd = G_VALUE_INIT;
					g_autofree gchar *hint = NULL;

					if (!pspec_is_numeric (p))
						continue;
					label = g_strdup_printf ("%s/%s/%s", buses[i], ids[j], p->name);
					if (G_IS_PARAM_SPEC_FLOAT (p))
					{
						mn = G_PARAM_SPEC_FLOAT (p)->minimum;
						mx = G_PARAM_SPEC_FLOAT (p)->maximum;
					}
					else if (G_IS_PARAM_SPEC_DOUBLE (p))
					{
						mn = G_PARAM_SPEC_DOUBLE (p)->minimum;
						mx = G_PARAM_SPEC_DOUBLE (p)->maximum;
					}
					else if (G_IS_PARAM_SPEC_INT (p))
					{
						mn = G_PARAM_SPEC_INT (p)->minimum;
						mx = G_PARAM_SPEC_INT (p)->maximum;
					}
					else if (G_IS_PARAM_SPEC_UINT (p))
					{
						mn = G_PARAM_SPEC_UINT (p)->minimum;
						mx = G_PARAM_SPEC_UINT (p)->maximum;
					}
					g_value_init (&dv, G_PARAM_SPEC_VALUE_TYPE (p));
					g_param_value_set_default (p, &dv);
					g_value_init (&dd, G_TYPE_DOUBLE);
					if (!g_value_transform (&dv, &dd))
						g_value_set_double (&dd, 0.0);
					if (mx - mn < 1e9)
						hint = g_strdup_printf ("range:%g,%g,%g", mn, mx, (mx - mn) / 1000.0);
					source_add (src, gen_double (src, "param", label, "Parameters", mn, mx,
					                             CLAMP (g_value_get_double (&dd), mn, mx), hint),
					            KEY_SNAP_PARAM, buses[i], ids[j], p->name);
				}
			}
		}
		break;

	case SRC_DUCK:
	default:
		if (ed->sel_duck < 0 || ed->sel_duck >= (gint) lrg_mixer_asset_get_duck_count (a))
			break;
		source_add (src, g_param_spec_double ("threshold", "Threshold (dB)",
		                                      "[group:Rule][hint:range:-96,12,0.5] Trigger peak that starts the duck",
		                                      LRG_AUDIO_SILENCE_DB, LRG_AUDIO_MIXER_MAX_GAIN_DB, -30.0,
		                                      G_PARAM_READWRITE),
		            KEY_THRESHOLD, NULL, NULL, NULL);
		source_add (src, g_param_spec_double ("depth", "Depth (dB)",
		                                      "[group:Rule][hint:range:-60,0,0.5] Target gain while ducked",
		                                      LRG_AUDIO_SILENCE_DB, 0.0, -12.0, G_PARAM_READWRITE),
		            KEY_DEPTH, NULL, NULL, NULL);
		source_add (src, g_param_spec_double ("attack", "Attack (s)",
		                                      "[group:Rule][hint:range:0,2,0.005] Time to duck",
		                                      0.0, 10.0, 0.05, G_PARAM_READWRITE),
		            KEY_ATTACK, NULL, NULL, NULL);
		source_add (src, g_param_spec_double ("release", "Release (s)",
		                                      "[group:Rule][hint:range:0,5,0.01] Time to recover",
		                                      0.0, 10.0, 0.5, G_PARAM_READWRITE),
		            KEY_RELEASE, NULL, NULL, NULL);
		break;
	}
}

static GPtrArray *
mixer_source_list (LrgPropertySource *source)
{
	MixerSource *src = MIXER_SOURCE (source);
	GPtrArray   *infos = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	guint        i;

	for (i = 0; i < src->pspecs->len; i++)
	{
		GParamSpec      *p = g_ptr_array_index (src->pspecs, i);
		LrgPropertyInfo *info = lrg_property_info_new (p);
		RowKey          *k = g_hash_table_lookup (src->keys, p->name);

		/* effect parameters keep their own blurb: group them */
		if (k != NULL && k->kind == KEY_PARAM)
			lrg_property_info_set_group (info, "Parameters");
		g_ptr_array_add (infos, info);
	}
	return infos;
}

/* Duck rule @index of @a, as a struct. */
typedef struct
{
	const gchar *trigger;
	const gchar *target;
	gdouble      threshold, depth, attack, release;
} Duck;

static gboolean
get_duck (LrgMixerAsset *a,
          gint           index,
          Duck          *d)
{
	memset (d, 0, sizeof *d);
	return a != NULL && index >= 0 &&
	       lrg_mixer_asset_get_duck (a, (guint) index, &d->trigger, &d->target, &d->threshold, &d->depth, &d->attack,
	                                 &d->release);
}

static gboolean
mixer_source_get (LrgPropertySource *source,
                  const gchar       *name,
                  GValue            *value)
{
	MixerSource          *src = MIXER_SOURCE (source);
	LrgEditorMixerEditor *ed = src->editor;
	LrgMixerAsset        *a = ed != NULL ? ed->asset : NULL;
	RowKey               *k = g_hash_table_lookup (src->keys, name);
	gdouble               d = 0.0;
	Duck                  duck;
	GParamSpec           *p;

	if (k == NULL || a == NULL)
		return FALSE;
	switch (k->kind)
	{
	case KEY_PARENT:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, lrg_mixer_asset_get_bus_parent (a, ed->sel_bus));
		return TRUE;
	case KEY_VOLUME:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, lrg_mixer_asset_get_bus_volume (a, ed->sel_bus));
		return TRUE;
	case KEY_MUTE:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, lrg_mixer_asset_get_bus_mute (a, ed->sel_bus));
		return TRUE;
	case KEY_SOLO:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, lrg_mixer_asset_get_bus_solo (a, ed->sel_bus));
		return TRUE;
	case KEY_SEND:
		lrg_mixer_asset_get_send (a, ed->sel_bus, k->a, &d);
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, d);
		return TRUE;
	case KEY_ENABLED:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, lrg_mixer_asset_get_effect_enabled (a, ed->sel_bus, ed->sel_effect));
		return TRUE;
	case KEY_PARAM:
		p = NULL;
		{
			guint i;

			for (i = 0; i < src->pspecs->len; i++)
				if (g_strcmp0 (((GParamSpec *) g_ptr_array_index (src->pspecs, i))->name, name) == 0)
					p = g_ptr_array_index (src->pspecs, i);
		}
		if (p == NULL)
			return FALSE;
		g_value_init (value, G_PARAM_SPEC_VALUE_TYPE (p));
		{
			g_auto(GValue) stored = G_VALUE_INIT;

			if (lrg_mixer_asset_get_effect_param (a, ed->sel_bus, ed->sel_effect, k->a, &stored) &&
			    g_value_type_transformable (G_VALUE_TYPE (&stored), G_VALUE_TYPE (value)) &&
			    g_value_transform (&stored, value))
				return TRUE;
			if (G_VALUE_HOLDS_STRING (&stored) && G_IS_PARAM_SPEC_ENUM (p))
			{
				GEnumValue *ev = g_enum_get_value_by_nick (G_PARAM_SPEC_ENUM (p)->enum_class,
				                                           g_value_get_string (&stored));

				if (ev != NULL)
				{
					g_value_set_enum (value, ev->value);
					return TRUE;
				}
			}
		}
		g_param_value_set_default (p, value);
		return TRUE;
	case KEY_BLEND:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, lrg_mixer_asset_get_snapshot_blend (a, ed->sel_snapshot));
		return TRUE;
	case KEY_DEFAULT:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, g_strcmp0 (lrg_mixer_asset_get_default_snapshot (a), ed->sel_snapshot) == 0);
		return TRUE;
	case KEY_SNAP_VOLUME:
		if (!lrg_mixer_asset_get_snapshot_volume (a, ed->sel_snapshot, k->a, &d))
			d = lrg_mixer_asset_get_bus_volume (a, k->a);
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, d);
		return TRUE;
	case KEY_SNAP_SEND:
		if (!lrg_mixer_asset_get_snapshot_send (a, ed->sel_snapshot, k->a, k->b, &d) &&
		    !lrg_mixer_asset_get_send (a, k->a, k->b, &d))
			d = LRG_AUDIO_SILENCE_DB;
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, d);
		return TRUE;
	case KEY_SNAP_PARAM:
		if (!lrg_mixer_asset_get_snapshot_param (a, ed->sel_snapshot, k->a, k->b, k->c, &d))
		{
			g_auto(GValue) stored = G_VALUE_INIT;
			g_auto(GValue) dd = G_VALUE_INIT;
			guint          i;

			g_value_init (&dd, G_TYPE_DOUBLE);
			if (lrg_mixer_asset_get_effect_param (a, k->a, k->b, k->c, &stored) && g_value_transform (&stored, &dd))
				d = g_value_get_double (&dd);
			else
			{
				/* the row's default is the parameter's default */
				for (i = 0; i < src->pspecs->len; i++)
				{
					GParamSpec *ps = g_ptr_array_index (src->pspecs, i);

					if (g_strcmp0 (ps->name, name) == 0)
						d = G_PARAM_SPEC_DOUBLE (ps)->default_value;
				}
			}
		}
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, d);
		return TRUE;
	case KEY_THRESHOLD:
	case KEY_DEPTH:
	case KEY_ATTACK:
	case KEY_RELEASE:
		if (!get_duck (a, ed->sel_duck, &duck))
			return FALSE;
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, k->kind == KEY_THRESHOLD ? duck.threshold : k->kind == KEY_DEPTH ? duck.depth
		                           : k->kind == KEY_ATTACK ? duck.attack : duck.release);
		return TRUE;
	default:
		return FALSE;
	}
}

static gdouble
value_double (const GValue *v)
{
	g_auto(GValue) d = G_VALUE_INIT;

	g_value_init (&d, G_TYPE_DOUBLE);
	if (G_VALUE_HOLDS_DOUBLE (v))
		return g_value_get_double (v);
	if (g_value_transform (v, &d))
		return g_value_get_double (&d);
	return 0.0;
}

/*
 * Applies one row edit (@value, or "unset" when @value is NULL: the
 * row's reset) to a copy of the layout and commits it.
 */
static gboolean
source_write (MixerSource  *src,
              const gchar  *name,
              const GValue *value)
{
	LrgEditorMixerEditor     *ed = src->editor;
	RowKey                   *k = g_hash_table_lookup (src->keys, name);
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgMixerAsset)  c = NULL;
	g_autofree gchar         *label = NULL;
	gboolean                  ok = FALSE;
	Duck                      duck;

	if (k == NULL || ed == NULL)
		return FALSE;
	c = edit_copy (ed, &error);
	if (c == NULL)
	{
		report (ed, error);
		return FALSE;
	}

	switch (k->kind)
	{
	case KEY_PARENT:
		ok = value != NULL && lrg_mixer_asset_set_bus_parent (c, ed->sel_bus, g_value_get_string (value));
		label = g_strdup_printf ("Route %s", ed->sel_bus);
		break;
	case KEY_VOLUME:
		ok = lrg_mixer_asset_set_bus_volume (c, ed->sel_bus, value != NULL ? value_double (value) : 0.0);
		label = g_strdup_printf ("Set %s volume", ed->sel_bus);
		break;
	case KEY_MUTE:
		ok = lrg_mixer_asset_set_bus_mute (c, ed->sel_bus, value != NULL && g_value_get_boolean (value));
		label = g_strdup_printf ("Mute %s", ed->sel_bus);
		break;
	case KEY_SOLO:
		ok = lrg_mixer_asset_set_bus_solo (c, ed->sel_bus, value != NULL && g_value_get_boolean (value));
		label = g_strdup_printf ("Solo %s", ed->sel_bus);
		break;
	case KEY_SEND:
		ok = lrg_mixer_asset_set_send (c, ed->sel_bus, k->a, value != NULL ? value_double (value) : 0.0);
		label = g_strdup_printf ("Set send %s > %s", ed->sel_bus, k->a);
		break;
	case KEY_ENABLED:
		ok = lrg_mixer_asset_set_effect_enabled (c, ed->sel_bus, ed->sel_effect,
		                                         value == NULL || g_value_get_boolean (value));
		label = g_strdup_printf ("Bypass %s", ed->sel_effect);
		break;
	case KEY_PARAM:
		if (value == NULL)
			ok = lrg_mixer_asset_unset_effect_param (c, ed->sel_bus, ed->sel_effect, k->a);
		else
			ok = lrg_mixer_asset_set_effect_param (c, ed->sel_bus, ed->sel_effect, k->a, value, &error);
		label = g_strdup_printf ("Set %s %s", ed->sel_effect, k->a);
		break;
	case KEY_BLEND:
		ok = lrg_mixer_asset_set_snapshot_blend (c, ed->sel_snapshot, value != NULL ? value_double (value) : 0.0);
		label = g_strdup_printf ("Set %s blend", ed->sel_snapshot);
		break;
	case KEY_DEFAULT:
		if (value != NULL && g_value_get_boolean (value))
			ok = lrg_mixer_asset_set_default_snapshot (c, ed->sel_snapshot);
		else if (g_strcmp0 (lrg_mixer_asset_get_default_snapshot (c), ed->sel_snapshot) == 0)
			ok = lrg_mixer_asset_set_default_snapshot (c, NULL);
		else
			ok = TRUE;
		label = g_strdup ("Set default snapshot");
		break;
	case KEY_SNAP_VOLUME:
		ok = lrg_mixer_asset_set_snapshot_volume (c, ed->sel_snapshot, k->a, value != NULL ? value_double (value) : NAN);
		label = g_strdup_printf ("Snapshot %s: %s volume", ed->sel_snapshot, k->a);
		break;
	case KEY_SNAP_SEND:
		ok = lrg_mixer_asset_set_snapshot_send (c, ed->sel_snapshot, k->a, k->b,
		                                        value != NULL ? value_double (value) : NAN);
		label = g_strdup_printf ("Snapshot %s: send %s > %s", ed->sel_snapshot, k->a, k->b);
		break;
	case KEY_SNAP_PARAM:
		ok = lrg_mixer_asset_set_snapshot_param (c, ed->sel_snapshot, k->a, k->b, k->c,
		                                         value != NULL ? value_double (value) : NAN, &error);
		label = g_strdup_printf ("Snapshot %s: %s/%s/%s", ed->sel_snapshot, k->a, k->b, k->c);
		break;
	case KEY_THRESHOLD:
	case KEY_DEPTH:
	case KEY_ATTACK:
	case KEY_RELEASE:
		if (!get_duck (c, ed->sel_duck, &duck))
			break;
		{
			gdouble v = value != NULL ? value_double (value) : (k->kind == KEY_THRESHOLD ? -30.0
			                                                    : k->kind == KEY_DEPTH ? -12.0
			                                                    : k->kind == KEY_ATTACK ? 0.05 : 0.5);
			g_autofree gchar *trigger = g_strdup (duck.trigger);
			g_autofree gchar *target = g_strdup (duck.target);

			ok = lrg_mixer_asset_set_duck (c, trigger, target,
			                               k->kind == KEY_THRESHOLD ? v : duck.threshold,
			                               k->kind == KEY_DEPTH ? v : duck.depth,
			                               k->kind == KEY_ATTACK ? v : duck.attack,
			                               k->kind == KEY_RELEASE ? v : duck.release);
			label = g_strdup_printf ("Edit ducking %s > %s", trigger, target);
		}
		break;
	default:
		break;
	}

	if (!ok)
	{
		if (error == NULL)
			g_set_error (&error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_INVALID_VALUE,
			             "The mixer refused the value of %s", name);
		report (ed, error);
		return FALSE;
	}
	if (!lrg_editor_mixer_editor_commit (ed, c, label, src->gesture, &error))
	{
		report (ed, error);
		return FALSE;
	}
	return TRUE;
}

static gboolean
mixer_source_set (LrgPropertySource *source,
                  const gchar       *name,
                  const GValue      *value)
{
	return source_write (MIXER_SOURCE (source), name, value);
}

static LrgPropertyState
mixer_source_get_state (LrgPropertySource *source,
                        const gchar       *name)
{
	MixerSource          *src = MIXER_SOURCE (source);
	LrgEditorMixerEditor *ed = src->editor;
	LrgMixerAsset        *a = ed != NULL ? ed->asset : NULL;
	RowKey               *k = g_hash_table_lookup (src->keys, name);
	gdouble               d;

	if (k == NULL || a == NULL)
		return LRG_PROPERTY_STATE_NONE;
	switch (k->kind)
	{
	case KEY_PARAM:
		{
			g_auto(GValue) stored = G_VALUE_INIT;

			return lrg_mixer_asset_get_effect_param (a, ed->sel_bus, ed->sel_effect, k->a, &stored)
			       ? LRG_PROPERTY_STATE_OVERRIDDEN : LRG_PROPERTY_STATE_DEFAULT;
		}
	case KEY_SNAP_VOLUME:
		return lrg_mixer_asset_get_snapshot_volume (a, ed->sel_snapshot, k->a, &d) ? LRG_PROPERTY_STATE_OVERRIDDEN
		                                                                            : LRG_PROPERTY_STATE_DEFAULT;
	case KEY_SNAP_SEND:
		return lrg_mixer_asset_get_snapshot_send (a, ed->sel_snapshot, k->a, k->b, &d)
		       ? LRG_PROPERTY_STATE_OVERRIDDEN : LRG_PROPERTY_STATE_DEFAULT;
	case KEY_SNAP_PARAM:
		return lrg_mixer_asset_get_snapshot_param (a, ed->sel_snapshot, k->a, k->b, k->c, &d)
		       ? LRG_PROPERTY_STATE_OVERRIDDEN : LRG_PROPERTY_STATE_DEFAULT;
	default:
		return LRG_PROPERTY_STATE_NONE;
	}
}

/* Reset: an override or a stored parameter goes away; other rows go back
 * to the format's default. */
static gboolean
mixer_source_reset (LrgPropertySource *source,
                    const gchar       *name)
{
	return source_write (MIXER_SOURCE (source), name, NULL);
}

static void
mixer_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = mixer_source_list;
	iface->get_value = mixer_source_get;
	iface->set_value = mixer_source_set;
	iface->get_state = mixer_source_get_state;
	iface->reset = mixer_source_reset;
}

static void
mixer_source_finalize (GObject *object)
{
	MixerSource *src = MIXER_SOURCE (object);

	g_ptr_array_unref (src->pspecs);
	g_hash_table_unref (src->keys);
	g_free (src->sig);
	G_OBJECT_CLASS (mixer_source_parent_class)->finalize (object);
}

static void
mixer_source_class_init (MixerSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = mixer_source_finalize;
}

static void
mixer_source_init (MixerSource *src)
{
	src->pspecs = g_ptr_array_new_with_free_func ((GDestroyNotify) g_param_spec_unref);
	src->keys = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, row_key_free);
}

static MixerSource *
mixer_source_new (LrgEditorMixerEditor *editor,
                  SourceMode            mode)
{
	MixerSource *src = g_object_new (MIXER_TYPE_SOURCE, NULL);

	src->editor = editor;
	src->mode = mode;
	return src;
}

/* Grid gestures: one undo step per drag of a spin box. */
static void
on_grid_edit_begin (LrgPropertyGrid *grid,
                    const gchar     *name,
                    guint            gesture_id,
                    gpointer         user_data)
{
	MixerSource *src = MIXER_SOURCE (user_data);

	if (src->editor != NULL)
		src->gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (src->editor));
}

static void
on_grid_edit_end (LrgPropertyGrid *grid,
                  const gchar     *name,
                  guint            gesture_id,
                  gboolean         commit_,
                  gpointer         user_data)
{
	MIXER_SOURCE (user_data)->gesture = 0;
}

/* Brings a grid in line with its source: rebuilds the rows when what
 * they depend on changed, else refreshes the values. */
static void
sync_grid (LrgPropertyGrid *grid,
           MixerSource     *src,
           gboolean         allow_rebuild)
{
	g_autofree gchar *sig = source_signature (src);

	if (g_strcmp0 (sig, src->sig) != 0)
	{
		if (!allow_rebuild)
		{
			src->editor->grids_stale = TRUE;
			return;
		}
		source_rebuild (src);
		lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (src), NULL);
		lrg_property_grid_rebuild (grid);
		return;
	}
	lrg_property_grid_refresh (grid);
}

/* ==========================================================================
 * Channel strips
 * ========================================================================== */

typedef struct
{
	gchar                   *bus;
	LrgEditorMixerStripPart  part;
	guint                    index;
	gchar                   *extra;   /* effect id / send target */
	gfloat                   x, y, w, h;
} StripHit;

static void
strip_hit_free (gpointer data)
{
	StripHit *h = data;

	g_free (h->bus);
	g_free (h->extra);
	g_free (h);
}

typedef struct
{
	GWeakRef                        app;
	gboolean                        compact;
	LrgEditorMixerStripsAssetFunc   asset_func;
	LrgEditorMixerStripsEditorFunc  editor_func;
	gpointer                        user_data;
	GDestroyNotify                  destroy;

	GPtrArray                      *hits;      /* StripHit*, from the last paint */
	GHashTable                     *levels;    /* bus -> gdouble[4] smoothed peak L/R, rms L/R (dB) */
	gint64                          last_paint;

	/* drag */
	gchar                          *drag_bus;
	LrgEditorMixerStripPart         drag_part;
	gchar                          *drag_extra;
	gfloat                          drag_x0;
	gdouble                         drag_v0;
	guint                           gesture;
	gint64                          last_click;
	gchar                          *last_click_bus;
} Strips;

static void
strips_free (gpointer data)
{
	Strips *s = data;

	if (s->destroy != NULL)
		s->destroy (s->user_data);
	g_weak_ref_clear (&s->app);
	g_ptr_array_unref (s->hits);
	g_hash_table_unref (s->levels);
	g_free (s->drag_bus);
	g_free (s->drag_extra);
	g_free (s->last_click_bus);
	g_free (s);
}

#define STRIPS_KEY "lrg-mixer-strips"

static Strips *
strips_of (LrgWidget *widget)
{
	return widget != NULL ? g_object_get_data (G_OBJECT (widget), STRIPS_KEY) : NULL;
}

/* Rows of effects and sends every strip reserves (the largest bus). */
static void
strip_rows (LrgMixerAsset *a,
            guint         *fx,
            guint         *sends)
{
	g_auto(GStrv) buses = lrg_mixer_asset_get_bus_names (a);
	guint         i;

	*fx = 1;
	*sends = 1;
	for (i = 0; buses != NULL && buses[i] != NULL; i++)
	{
		g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (a, buses[i]);
		g_auto(GStrv) t = lrg_mixer_asset_get_send_targets (a, buses[i]);

		*fx = MAX (*fx, strv_len (ids) + 0u);
		*sends = MAX (*sends, strv_len (t));
	}
}

static void
strips_measure (LrgCustomWidget     *widget,
                gfloat               avail_width,
                gfloat               avail_height,
                LrgCustomWidgetSize *size,
                gpointer             user_data)
{
	Strips        *s = user_data;
	LrgMixerAsset *a = s->asset_func (s->user_data);
	g_auto(GStrv)  buses = a != NULL ? lrg_mixer_asset_get_bus_names (a) : NULL;
	guint          n = MAX (strv_len (buses), 1u);
	gfloat         w = s->compact ? STRIP_W_COMPACT : STRIP_W;
	guint          fx = 0, sends = 0;

	size->width = (gfloat) n * (w + STRIP_GAP) + STRIP_GAP;
	if (s->compact || a == NULL)
		size->height = 210.0f;
	else
	{
		/* what strips_paint() stacks, with a 100 px fader; the scroll box
		 * stretches the strips to its height, so faders grow with it */
		strip_rows (a, &fx, &sends);
		size->height = 4.0f + HEADER_H + 16.0f + (gfloat) fx * ROW_H + 2.0f + 16.0f + (gfloat) sends * ROW_H + 4.0f +
		               8.0f + 100.0f + READOUT_H + BUTTON_H + 12.0f + 4.0f;
	}
}

static void
add_hit (Strips                  *s,
         const gchar             *bus,
         LrgEditorMixerStripPart  part,
         guint                    index,
         const gchar             *extra,
         gfloat                   x,
         gfloat                   y,
         gfloat                   w,
         gfloat                   h)
{
	StripHit *hit = g_new0 (StripHit, 1);

	hit->bus = g_strdup (bus);
	hit->part = part;
	hit->index = index;
	hit->extra = g_strdup (extra);
	hit->x = x;
	hit->y = y;
	hit->w = w;
	hit->h = h;
	g_ptr_array_add (s->hits, hit);
}

/* Meter levels of @bus (smoothed: instant attack, 20 dB/s fall). */
static void
meter_levels (Strips        *s,
              LrgAudioMixer *mixer,
              const gchar   *bus,
              gdouble        dt,
              gdouble       *out)
{
	gdouble *lv = g_hash_table_lookup (s->levels, bus);
	guint    ch;

	if (lv == NULL)
	{
		lv = g_new (gdouble, 4);
		lv[0] = lv[1] = lv[2] = lv[3] = LRG_AUDIO_SILENCE_DB;
		g_hash_table_insert (s->levels, g_strdup (bus), lv);
	}
	for (ch = 0; ch < 2; ch++)
	{
		gdouble peak = 0.0, rms = 0.0;
		gdouble pdb, rdb;

		if (mixer != NULL)
			lrg_audio_mixer_get_bus_meter (mixer, bus, lrg_audio_mixer_get_channels (mixer) > 1 ? (gint) ch : 0,
			                               &peak, &rms);
		pdb = lrg_audio_gain_to_db (peak);
		rdb = lrg_audio_gain_to_db (rms);
		lv[ch] = MAX (pdb, lv[ch] - 20.0 * dt);
		lv[2 + ch] = MAX (rdb, lv[2 + ch] - 20.0 * dt);
		if (mixer == NULL)
			lv[ch] = lv[2 + ch] = LRG_AUDIO_SILENCE_DB;
	}
	memcpy (out, lv, 4 * sizeof (gdouble));
}

static void
paint_button (LrgPaintList   *list,
              LrgTheme       *theme,
              GrlFont        *font,
              const gchar    *text,
              gfloat          x,
              gfloat          y,
              gfloat          w,
              gfloat          h,
              gboolean        on,
              const GrlColor *on_color)
{
	gfloat          fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat          r = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	const GrlColor *bg = on ? on_color : lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL);
	const GrlColor *fg = on ? lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW)
	                        : lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	gfloat          tw = 0.0f, th = 0.0f;

	lrg_paint_list_rect (list, x, y, w, h, r, bg);
	lrg_paint_measure_text (font, text, fs, 0.0f, &tw, &th);
	lrg_paint_list_text (list, font, text, roundf (x + (w - tw) * 0.5f), roundf (y + (h - fs) * 0.5f), fs, 0.0f, fg);
}

static void
strips_paint (LrgCustomWidget *widget,
              LrgPaintList    *list,
              gfloat           x,
              gfloat           y,
              gfloat           width,
              gfloat           height,
              gpointer         user_data)
{
	Strips               *s = user_data;
	LrgTheme             *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgMixerAsset        *a = s->asset_func (s->user_data);
	LrgEditorMixerEditor *ed = s->editor_func (FALSE, s->user_data);
	g_autoptr(LrgEditorApp) app = g_weak_ref_get (&s->app);
	LrgAudioMixer        *meters = NULL;
	g_auto(GStrv)         buses = NULL;
	gfloat                fs, r, sw;
	GrlFont              *font;
	const GrlColor       *text, *muted, *panel, *border, *accent, *warn, *danger, *ok;
	GrlColor              track;
	guint                 fx = 1, nsends = 1, i;
	gint64                now = g_get_monotonic_time ();
	gdouble               dt = s->last_paint > 0 ? MIN ((gdouble) (now - s->last_paint) / 1e6, 0.5) : 0.0;
	gboolean              any_solo = FALSE;

	s->last_paint = now;
	g_ptr_array_set_size (s->hits, 0);
	if (theme == NULL)
		return;
	fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	r = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	font = lrg_theme_get_ui_font (theme);
	text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	panel = lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE);
	border = lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER);
	accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	warn = lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING);
	danger = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);
	ok = lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS);
	track = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);

	if (a == NULL)
	{
		lrg_paint_list_text (list, font, ed != NULL && ed->load_error != NULL ? ed->load_error : "No mixer layout",
		                     x + 10.0f, y + 10.0f, fs, 0.0f, muted);
		return;
	}
	buses = lrg_mixer_asset_get_bus_names (a);
	if (!s->compact)
		strip_rows (a, &fx, &nsends);
	if (app != NULL)
		meters = lrg_editor_mixer_editor_get_meter_mixer (app, ed, NULL);
	for (i = 0; buses[i] != NULL; i++)
		any_solo = any_solo || lrg_mixer_asset_get_bus_solo (a, buses[i]);

	sw = s->compact ? STRIP_W_COMPACT : STRIP_W;
	for (i = 0; buses[i] != NULL; i++)
	{
		const gchar *bus = buses[i];
		gfloat       sx = x + STRIP_GAP + (gfloat) i * (sw + STRIP_GAP);
		gfloat       cy = y + 4.0f;
		gfloat       bottom = y + height - 4.0f;
		GrlColor     bc = bus_color (bus);
		gboolean     selected = ed != NULL && g_strcmp0 (ed->sel_bus, bus) == 0;
		gboolean     mute = lrg_mixer_asset_get_bus_mute (a, bus);
		gboolean     solo = lrg_mixer_asset_get_bus_solo (a, bus);
		gboolean     audible = meters != NULL ? lrg_audio_mixer_is_bus_audible (meters, bus) : (!mute && (!any_solo || solo));
		gdouble      vol = lrg_mixer_asset_get_bus_volume (a, bus);
		gdouble      lv[4];
		gfloat       fy0, fy1, fxl, pos;
		g_autofree gchar *db = format_db (vol);
		const gchar *parent = lrg_mixer_asset_get_bus_parent (a, bus);

		/* the strip card */
		lrg_paint_list_rect (list, sx, cy, sw, bottom - cy, r + 2.0f, panel);
		if (selected)
			lrg_paint_list_border (list, sx, cy, sw, bottom - cy, r + 2.0f, 2.0f, accent);
		else
			lrg_paint_list_border (list, sx, cy, sw, bottom - cy, r + 2.0f, 1.0f, border);
		lrg_paint_list_rect (list, sx + 6.0f, cy + 4.0f, sw - 12.0f, 3.0f, 1.5f, &bc);

		/* header: name and parent */
		lrg_paint_list_push_clip (list, sx + 4.0f, cy, sw - 8.0f, HEADER_H);
		lrg_paint_list_text (list, font, bus, sx + 8.0f, cy + 11.0f, fs + 1.0f, 0.0f, audible ? text : muted);
		if (parent != NULL)
		{
			g_autofree gchar *p = g_strdup_printf ("> %s", parent);

			lrg_paint_list_text (list, font, p, sx + 8.0f, cy + 13.0f + fs + 2.0f, fs - 1.0f, 0.0f, muted);
		}
		lrg_paint_list_pop_clip (list);
		add_hit (s, bus, LRG_EDITOR_MIXER_STRIP_HEADER, 0, NULL, sx, cy, sw, HEADER_H);
		cy += HEADER_H;

		if (!s->compact)
		{
			g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (a, bus);
			g_auto(GStrv) sends = lrg_mixer_asset_get_send_targets (a, bus);
			guint         j;

			/* effects */
			lrg_paint_list_text (list, font, "FX", sx + 8.0f, cy + 2.0f, fs - 2.0f, 0.0f, muted);
			cy += 16.0f;
			for (j = 0; j < fx; j++)
			{
				gfloat ry = cy + (gfloat) j * ROW_H;

				if (ids == NULL || ids[j] == NULL || j >= strv_len (ids))
				{
					if (j == 0)
						lrg_paint_list_text (list, font, "none", sx + 10.0f, ry + 2.0f, fs - 1.0f, 0.0f, muted);
					continue;
				}
				{
					gboolean en = lrg_mixer_asset_get_effect_enabled (a, bus, ids[j]);
					gboolean fsel = selected && ed != NULL && g_strcmp0 (ed->sel_effect, ids[j]) == 0;
					GrlColor well = track;

					well.a = (guint8) (well.a * 0.6f);
					lrg_paint_list_rect (list, sx + 5.0f, ry + 1.0f, sw - 10.0f, ROW_H - 2.0f, r,
					                     fsel ? lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION) : &well);
					lrg_paint_list_circle (list, sx + 13.0f, ry + ROW_H * 0.5f, 4.0f, en ? ok : muted);
					lrg_paint_list_push_clip (list, sx + 20.0f, ry, sw - 26.0f, ROW_H);
					lrg_paint_list_text (list, font, ids[j], sx + 22.0f, ry + (ROW_H - fs) * 0.5f, fs - 1.0f, 0.0f,
					                     en ? text : muted);
					lrg_paint_list_pop_clip (list);
					add_hit (s, bus, LRG_EDITOR_MIXER_STRIP_EFFECT, j, ids[j], sx + 20.0f, ry, sw - 25.0f, ROW_H);
					add_hit (s, bus, LRG_EDITOR_MIXER_STRIP_EFFECT_TOGGLE, j, ids[j], sx + 5.0f, ry, 15.0f, ROW_H);
				}
			}
			cy += (gfloat) fx * ROW_H + 2.0f;

			/* sends */
			lrg_paint_list_text (list, font, "SENDS", sx + 8.0f, cy + 2.0f, fs - 2.0f, 0.0f, muted);
			cy += 16.0f;
			for (j = 0; j < nsends; j++)
			{
				gfloat  ry = cy + (gfloat) j * ROW_H;
				gdouble lvl = LRG_AUDIO_SILENCE_DB;
				gfloat  bw = sw - 12.0f;
				gfloat  fill;
				g_autofree gchar *t = NULL;

				if (sends == NULL || j >= strv_len (sends))
				{
					if (j == 0)
						lrg_paint_list_text (list, font, "none", sx + 10.0f, ry + 2.0f, fs - 1.0f, 0.0f, muted);
					continue;
				}
				lrg_mixer_asset_get_send (a, bus, sends[j], &lvl);
				fill = (gfloat) lrg_editor_mixer_fader_db_to_position (lvl);
				lrg_paint_list_rect (list, sx + 6.0f, ry + 1.0f, bw, ROW_H - 2.0f, r, &track);
				{
					GrlColor tc = bus_color (sends[j]);

					tc.a = 0x90;
					lrg_paint_list_rect (list, sx + 6.0f, ry + 1.0f, MAX (bw * fill, 2.0f), ROW_H - 2.0f, r, &tc);
				}
				t = g_strdup_printf ("> %s", sends[j]);
				lrg_paint_list_push_clip (list, sx + 8.0f, ry, bw - 4.0f, ROW_H);
				lrg_paint_list_text (list, font, t, sx + 10.0f, ry + (ROW_H - fs) * 0.5f, fs - 1.0f, 0.0f, text);
				lrg_paint_list_pop_clip (list);
				add_hit (s, bus, LRG_EDITOR_MIXER_STRIP_SEND, j, sends[j], sx + 6.0f, ry, bw, ROW_H);
			}
			cy += (gfloat) nsends * ROW_H + 4.0f;
		}

		/* meter + fader */
		fy0 = cy + 8.0f;
		fy1 = bottom - BUTTON_H - READOUT_H - 12.0f;
		fxl = sx + 10.0f;
		meter_levels (s, meters, bus, dt, lv);
		{
			gfloat mw = 6.0f;
			guint  ch;
			gfloat duck = meters != NULL ? (gfloat) lrg_audio_mixer_get_bus_duck_gain (meters, bus) : 1.0f;

			/* scale ticks */
			static const gdouble ticks[] = { 12.0, 0.0, -12.0, -24.0, -48.0 };
			guint t;

			for (t = 0; t < G_N_ELEMENTS (ticks); t++)
			{
				gfloat ty = fy1 - (fy1 - fy0) * (gfloat) lrg_editor_mixer_fader_db_to_position (ticks[t]);
				g_autofree gchar *lab = g_strdup_printf ("%g", ticks[t]);

				lrg_paint_list_line (list, sx + sw - 30.0f, ty, sx + sw - 24.0f, ty, 1.0f, border);
				if (!s->compact)
					lrg_paint_list_text (list, font, lab, sx + sw - 22.0f, ty - (fs - 2.0f) * 0.5f, fs - 3.0f, 0.0f,
					                     muted);
			}
			for (ch = 0; ch < 2; ch++)
			{
				gfloat mx = fxl + (gfloat) ch * (mw + 2.0f);
				gfloat pk = (gfloat) lrg_editor_mixer_fader_db_to_position (lv[ch]);
				gfloat rm = (gfloat) lrg_editor_mixer_fader_db_to_position (lv[2 + ch]);
				GrlColor rc = *ok;

				lrg_paint_list_rect (list, mx, fy0, mw, fy1 - fy0, 2.0f, &track);
				if (lv[2 + ch] > -6.0)
					rc = *warn;
				if (lv[ch] > 0.0)
					rc = *danger;
				rc.a = 0xc0;
				lrg_paint_list_rect (list, mx, fy1 - (fy1 - fy0) * rm, mw, (fy1 - fy0) * rm, 2.0f, &rc);
				if (pk > 0.0f)
					lrg_paint_list_rect (list, mx, fy1 - (fy1 - fy0) * pk - 1.0f, mw, 2.0f, 0.0f,
					                     lv[ch] > 0.0 ? danger : text);
			}
			add_hit (s, bus, LRG_EDITOR_MIXER_STRIP_METER, 0, NULL, fxl, fy0, 2.0f * mw + 2.0f, fy1 - fy0);
			/* gain reduction: from the top down */
			if (duck < 0.999f)
			{
				gfloat gr = (gfloat) (lrg_editor_mixer_fader_db_to_position (0.0) -
				                      lrg_editor_mixer_fader_db_to_position (lrg_audio_gain_to_db (duck)));

				lrg_paint_list_rect (list, fxl + 2.0f * mw + 4.0f, fy0, 3.0f, (fy1 - fy0) * CLAMP (gr, 0.0f, 1.0f),
				                     1.5f, warn);
			}
		}
		{
			gfloat tx = sx + sw * 0.5f + (s->compact ? 2.0f : 0.0f);
			gfloat tw = 6.0f;
			gfloat zero_y = fy1 - (fy1 - fy0) * 0.75f;

			pos = (gfloat) lrg_editor_mixer_fader_db_to_position (vol);
			lrg_paint_list_rect (list, tx - tw * 0.5f, fy0, tw, fy1 - fy0, 3.0f, &track);
			lrg_paint_list_line (list, tx - 9.0f, zero_y, tx + 9.0f, zero_y, 1.0f, border);
			{
				GrlColor fillc = bc;

				fillc.a = mute ? 0x50 : 0xb0;
				lrg_paint_list_rect (list, tx - tw * 0.5f, fy1 - (fy1 - fy0) * pos, tw, (fy1 - fy0) * pos, 3.0f,
				                     &fillc);
			}
			/* the cap */
			{
				gfloat capy = fy1 - (fy1 - fy0) * pos;
				GrlColor cap = *lrg_theme_get_color (theme, LRG_THEME_COLOR_HANDLE);

				lrg_paint_list_rect (list, tx - 13.0f, capy - 6.0f, 26.0f, 12.0f, 3.0f, &cap);
				lrg_paint_list_line (list, tx - 9.0f, capy, tx + 9.0f, capy, 2.0f, &bc);
			}
			add_hit (s, bus, LRG_EDITOR_MIXER_STRIP_FADER, 0, NULL, tx - 14.0f, fy0, 28.0f, fy1 - fy0);
		}

		/* readout */
		{
			gfloat tw = 0.0f, th = 0.0f;

			lrg_paint_measure_text (font, db, fs, 0.0f, &tw, &th);
			lrg_paint_list_text (list, font, db, roundf (sx + (sw - tw) * 0.5f), fy1 + 6.0f, fs, 0.0f,
			                     audible ? text : muted);
		}

		/* mute / solo */
		{
			gfloat by = bottom - BUTTON_H - 5.0f;
			gfloat bw = (sw - 15.0f) * 0.5f;

			paint_button (list, theme, font, "M", sx + 5.0f, by, bw, BUTTON_H, mute, danger);
			add_hit (s, bus, LRG_EDITOR_MIXER_STRIP_MUTE, 0, NULL, sx + 5.0f, by, bw, BUTTON_H);
			paint_button (list, theme, font, "S", sx + 10.0f + bw, by, bw, BUTTON_H, solo, warn);
			add_hit (s, bus, LRG_EDITOR_MIXER_STRIP_SOLO, 0, NULL, sx + 10.0f + bw, by, bw, BUTTON_H);
		}
	}
}

/* Repaints every frame while a meter source runs. */
static gboolean
strips_step (LrgCustomWidget *widget,
             gfloat           delta_time,
             gpointer         user_data)
{
	Strips                 *s = user_data;
	g_autoptr(LrgEditorApp) app = g_weak_ref_get (&s->app);
	LrgEditorMixerEditor   *ed = s->editor_func (FALSE, s->user_data);

	/* keep a hidden editor's preview running for these meters */
	if (ed != NULL)
		pump_preview (ed, (gdouble) delta_time);
	return app != NULL && lrg_editor_mixer_editor_get_meter_mixer (app, ed, NULL) != NULL;
}

static StripHit *
hit_at (Strips *s,
        gfloat  px,
        gfloat  py)
{
	guint i;

	/* toggles sit inside effect rows: test them first */
	for (i = 0; i < s->hits->len; i++)
	{
		StripHit *h = g_ptr_array_index (s->hits, i);

		if (h->part == LRG_EDITOR_MIXER_STRIP_EFFECT_TOGGLE && px >= h->x && px < h->x + h->w && py >= h->y &&
		    py < h->y + h->h)
			return h;
	}
	for (i = 0; i < s->hits->len; i++)
	{
		StripHit *h = g_ptr_array_index (s->hits, i);

		if (h->part != LRG_EDITOR_MIXER_STRIP_METER && px >= h->x && px < h->x + h->w && py >= h->y &&
		    py < h->y + h->h)
			return h;
	}
	return NULL;
}

/* Commits one strip edit (mute, solo, fader, send, bypass). */
static void
strips_edit (Strips                  *s,
             const gchar             *bus,
             LrgEditorMixerStripPart  part,
             const gchar             *extra,
             gdouble                  value,
             guint                    gesture)
{
	LrgEditorMixerEditor     *ed = s->editor_func (TRUE, s->user_data);
	g_autoptr(LrgMixerAsset)  c = NULL;
	g_autoptr(GError)         error = NULL;
	g_autofree gchar         *label = NULL;
	gboolean                  ok = FALSE;

	if (ed == NULL || ed->asset == NULL)
		return;
	c = lrg_mixer_asset_copy (ed->asset);
	switch (part)
	{
	case LRG_EDITOR_MIXER_STRIP_MUTE:
		ok = lrg_mixer_asset_set_bus_mute (c, bus, !lrg_mixer_asset_get_bus_mute (c, bus));
		label = g_strdup_printf ("Mute %s", bus);
		break;
	case LRG_EDITOR_MIXER_STRIP_SOLO:
		ok = lrg_mixer_asset_set_bus_solo (c, bus, !lrg_mixer_asset_get_bus_solo (c, bus));
		label = g_strdup_printf ("Solo %s", bus);
		break;
	case LRG_EDITOR_MIXER_STRIP_FADER:
		ok = lrg_mixer_asset_set_bus_volume (c, bus, value);
		label = g_strdup_printf ("Set %s volume", bus);
		break;
	case LRG_EDITOR_MIXER_STRIP_SEND:
		ok = lrg_mixer_asset_set_send (c, bus, extra, value);
		label = g_strdup_printf ("Set send %s > %s", bus, extra);
		break;
	case LRG_EDITOR_MIXER_STRIP_EFFECT_TOGGLE:
		ok = lrg_mixer_asset_set_effect_enabled (c, bus, extra, !lrg_mixer_asset_get_effect_enabled (c, bus, extra));
		label = g_strdup_printf ("Bypass %s", extra);
		break;
	default:
		break;
	}
	if (ok && !lrg_editor_mixer_editor_commit (ed, c, label, gesture, &error))
		report (ed, error);
}

/* Fader position of a pointer y inside a fader hit. */
static gdouble
fader_db_at (StripHit *h,
             gfloat    py)
{
	gdouble pos = 1.0 - (gdouble) ((py - h->y) / MAX (h->h, 1.0f));

	pos = CLAMP (pos, 0.0, 1.0);
	return lrg_editor_mixer_fader_position_to_db (pos);
}

static StripHit *
find_hit (Strips                  *s,
          const gchar             *bus,
          LrgEditorMixerStripPart  part,
          const gchar             *extra)
{
	guint i;

	for (i = 0; i < s->hits->len; i++)
	{
		StripHit *h = g_ptr_array_index (s->hits, i);

		if (h->part == part && g_strcmp0 (h->bus, bus) == 0 && (extra == NULL || g_strcmp0 (h->extra, extra) == 0))
			return h;
	}
	return NULL;
}

static gboolean
strips_event (LrgCustomWidget  *widget,
              const LrgUIEvent *event,
              gpointer          user_data)
{
	Strips         *s = user_data;
	LrgUIEventType  type = lrg_ui_event_get_event_type (event);
	gfloat          px = lrg_ui_event_get_x (event);
	gfloat          py = lrg_ui_event_get_y (event);
	StripHit       *h;

	if (type == LRG_UI_EVENT_MOUSE_BUTTON_DOWN && lrg_ui_event_get_button (event) == 0)
	{
		LrgEditorMixerEditor *ed;
		gint64                now = g_get_monotonic_time ();
		gboolean              dbl;

		h = hit_at (s, px, py);
		if (h == NULL)
			return FALSE;
		dbl = h->part == LRG_EDITOR_MIXER_STRIP_FADER && g_strcmp0 (s->last_click_bus, h->bus) == 0 &&
		      now - s->last_click < 400000;
		s->last_click = now;
		g_free (s->last_click_bus);
		s->last_click_bus = g_strdup (h->bus);

		switch (h->part)
		{
		case LRG_EDITOR_MIXER_STRIP_HEADER:
		case LRG_EDITOR_MIXER_STRIP_EFFECT:
			ed = s->editor_func (!s->compact, s->user_data);
			if (ed != NULL)
				lrg_editor_mixer_editor_select (ed, h->bus,
				                                h->part == LRG_EDITOR_MIXER_STRIP_EFFECT ? h->extra : NULL);
			return TRUE;
		case LRG_EDITOR_MIXER_STRIP_MUTE:
		case LRG_EDITOR_MIXER_STRIP_SOLO:
		case LRG_EDITOR_MIXER_STRIP_EFFECT_TOGGLE:
			strips_edit (s, h->bus, h->part, h->extra, 0.0, 0);
			return TRUE;
		case LRG_EDITOR_MIXER_STRIP_FADER:
		case LRG_EDITOR_MIXER_STRIP_SEND:
			ed = s->editor_func (TRUE, s->user_data);
			if (ed == NULL || ed->asset == NULL)
				return TRUE;
			g_free (s->drag_bus);
			g_free (s->drag_extra);
			s->drag_bus = g_strdup (h->bus);
			s->drag_extra = g_strdup (h->extra);
			s->drag_part = h->part;
			s->drag_x0 = px;
			s->gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (ed));
			if (h->part == LRG_EDITOR_MIXER_STRIP_SEND)
			{
				s->drag_v0 = LRG_AUDIO_SILENCE_DB;
				lrg_mixer_asset_get_send (ed->asset, h->bus, h->extra, &s->drag_v0);
			}
			else
			{
				/* a double-click resets to unity; a click jumps there */
				strips_edit (s, h->bus, h->part, NULL, dbl ? 0.0 : fader_db_at (h, py), s->gesture);
				if (dbl)
				{
					g_clear_pointer (&s->drag_bus, g_free);
					s->gesture = 0;
				}
			}
			return TRUE;
		case LRG_EDITOR_MIXER_STRIP_METER:
		default:
			return FALSE;
		}
	}
	if (type == LRG_UI_EVENT_MOUSE_MOVE && s->drag_bus != NULL)
	{
		h = find_hit (s, s->drag_bus, s->drag_part, s->drag_part == LRG_EDITOR_MIXER_STRIP_SEND ? s->drag_extra : NULL);
		if (h == NULL)
			return TRUE;
		if (s->drag_part == LRG_EDITOR_MIXER_STRIP_FADER)
			strips_edit (s, s->drag_bus, s->drag_part, NULL, fader_db_at (h, py), s->gesture);
		else
		{
			/* sideways along the send bar, in fader-law positions */
			gdouble p0 = lrg_editor_mixer_fader_db_to_position (s->drag_v0);
			gdouble p = p0 + (gdouble) ((px - s->drag_x0) / MAX (h->w, 1.0f));

			strips_edit (s, s->drag_bus, s->drag_part, s->drag_extra,
			             lrg_editor_mixer_fader_position_to_db (CLAMP (p, 0.0, 1.0)), s->gesture);
		}
		return TRUE;
	}
	if (type == LRG_UI_EVENT_MOUSE_BUTTON_UP && s->drag_bus != NULL)
	{
		g_clear_pointer (&s->drag_bus, g_free);
		g_clear_pointer (&s->drag_extra, g_free);
		s->gesture = 0;
		return TRUE;
	}
	return FALSE;
}

LrgWidget *
lrg_editor_mixer_strips_new (LrgEditorApp                   *app,
                             gboolean                        compact,
                             LrgEditorMixerStripsAssetFunc   asset_func,
                             LrgEditorMixerStripsEditorFunc  editor_func,
                             gpointer                        user_data,
                             GDestroyNotify                  destroy)
{
	static const LrgCustomWidgetFuncs funcs = { strips_measure, strips_paint, strips_step, strips_event, { NULL } };
	LrgCustomWidget *w;
	Strips          *s;

	g_return_val_if_fail (asset_func != NULL, NULL);
	g_return_val_if_fail (editor_func != NULL, NULL);

	s = g_new0 (Strips, 1);
	g_weak_ref_init (&s->app, app);
	s->compact = compact;
	s->asset_func = asset_func;
	s->editor_func = editor_func;
	s->user_data = user_data;
	s->destroy = destroy;
	s->hits = g_ptr_array_new_with_free_func (strip_hit_free);
	s->levels = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	w = lrg_custom_widget_new ();
	lrg_widget_set_name (LRG_WIDGET (w), compact ? "mixer-strips-compact" : "mixer-strips");
	/* the data lives as long as the widget; the funcs only borrow it */
	g_object_set_data_full (G_OBJECT (w), STRIPS_KEY, s, strips_free);
	lrg_custom_widget_set_funcs (w, &funcs, s, NULL);
	return LRG_WIDGET (w);
}

gboolean
lrg_editor_mixer_strips_get_part_rect (LrgWidget               *strips,
                                       const gchar             *bus,
                                       LrgEditorMixerStripPart  part,
                                       guint                    index,
                                       gfloat                  *x,
                                       gfloat                  *y,
                                       gfloat                  *width,
                                       gfloat                  *height)
{
	Strips *s = strips_of (strips);
	guint   i;

	g_return_val_if_fail (LRG_IS_WIDGET (strips), FALSE);
	g_return_val_if_fail (x != NULL && y != NULL && width != NULL && height != NULL, FALSE);
	if (s == NULL)
		return FALSE;
	for (i = 0; i < s->hits->len; i++)
	{
		StripHit *h = g_ptr_array_index (s->hits, i);

		if (h->part == part && g_strcmp0 (h->bus, bus) == 0 &&
		    (h->index == index || (part != LRG_EDITOR_MIXER_STRIP_EFFECT && part != LRG_EDITOR_MIXER_STRIP_SEND &&
		                           part != LRG_EDITOR_MIXER_STRIP_EFFECT_TOGGLE)))
		{
			*x = h->x;
			*y = h->y;
			*width = h->w;
			*height = h->h;
			return TRUE;
		}
	}
	return FALSE;
}

/* The editor's own strips: its asset, itself. */
static LrgMixerAsset *
own_asset (gpointer user_data)
{
	LrgEditorMixerEditor *self = user_data;

	return self->asset;
}

static LrgEditorMixerEditor *
own_editor (gboolean open,
            gpointer user_data)
{
	return LRG_EDITOR_MIXER_EDITOR (user_data);
}

/* ==========================================================================
 * Snapshot and ducking lists
 * ========================================================================== */

#define LIST_BTN_W (22.0f)

static void
list_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (user_data);
	gboolean              snaps = LRG_WIDGET (widget) == self->snap_list;
	guint                 n = 0;

	if (self->asset != NULL)
	{
		if (snaps)
		{
			g_auto(GStrv) names = lrg_mixer_asset_get_snapshot_names (self->asset);

			n = strv_len (names);
		}
		else
			n = lrg_mixer_asset_get_duck_count (self->asset);
	}
	size->width = 160.0f;
	size->height = (gfloat) MAX (n, 1u) * 22.0f + 2.0f;
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
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (user_data);
	LrgTheme             *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gboolean              snaps = LRG_WIDGET (widget) == self->snap_list;
	gfloat                fs, r;
	GrlFont              *font;
	GrlColor              track;
	const GrlColor       *text, *muted;
	guint                 n = 0, i;
	g_auto(GStrv)         names = NULL;

	if (theme == NULL)
		return;
	fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	r = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	font = lrg_theme_get_ui_font (theme);
	text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	track = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);
	track.a = (guint8) (track.a / 2);
	lrg_paint_list_rect (list, x, y, width, height, r, &track);
	if (self->asset != NULL)
	{
		if (snaps)
		{
			names = lrg_mixer_asset_get_snapshot_names (self->asset);
			n = strv_len (names);
		}
		else
			n = lrg_mixer_asset_get_duck_count (self->asset);
	}
	if (n == 0)
	{
		lrg_paint_list_text (list, font, snaps ? "No snapshots" : "No ducking rules", x + 10.0f, y + 5.0f, fs, 0.0f,
		                     muted);
		return;
	}
	for (i = 0; i < n; i++)
	{
		gfloat            ry = y + 1.0f + (gfloat) i * 22.0f;
		g_autofree gchar *label = NULL;
		gboolean          sel;

		if (snaps)
		{
			const gchar *def = lrg_mixer_asset_get_default_snapshot (self->asset);

			sel = g_strcmp0 (self->sel_snapshot, names[i]) == 0;
			label = g_strdup_printf ("%s%s   %.2g s%s", names[i], g_strcmp0 (def, names[i]) == 0 ? " (default)" : "",
			                         lrg_mixer_asset_get_snapshot_blend (self->asset, names[i]),
			                         g_strcmp0 (self->preview_snapshot, names[i]) == 0 && self->previewing
			                         ? "   previewing" : "");
		}
		else
		{
			Duck d;

			sel = (gint) i == self->sel_duck;
			get_duck (self->asset, (gint) i, &d);
			label = g_strdup_printf ("%s > %s   %.0f dB / %.0f dB", d.trigger, d.target, d.threshold, d.depth);
		}
		if (sel)
			lrg_paint_list_rect (list, x + 2.0f, ry, width - 4.0f, 21.0f, r,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION));
		lrg_paint_list_push_clip (list, x + 8.0f, ry, MAX (width - 8.0f - LIST_BTN_W * (snaps ? 2.0f : 1.0f), 0.0f),
		                          22.0f);
		lrg_paint_list_text (list, font, label, x + 10.0f, roundf (ry + (21.0f - fs) * 0.5f), fs, 0.0f, text);
		lrg_paint_list_pop_clip (list);
		if (snaps)
			lrg_paint_list_icon (list, LRG_ICON_PLAY, x + width - 2.0f * LIST_BTN_W + 5.0f, ry + 5.0f, 12.0f, 0.0f,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_paint_list_icon (list, LRG_ICON_TRASH, x + width - LIST_BTN_W + 5.0f, ry + 5.0f, 12.0f, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
	}
}

static gboolean
list_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorMixerEditor     *self = LRG_EDITOR_MIXER_EDITOR (user_data);
	gboolean                  snaps = LRG_WIDGET (widget) == self->snap_list;
	gfloat                    x = lrg_widget_get_world_x (LRG_WIDGET (widget));
	gfloat                    y = lrg_widget_get_world_y (LRG_WIDGET (widget));
	gfloat                    w = lrg_widget_get_width (LRG_WIDGET (widget));
	gfloat                    px, py;
	guint                     row, n;
	g_auto(GStrv)             names = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgMixerAsset)  c = NULL;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || lrg_ui_event_get_button (event) != 0 ||
	    self->asset == NULL)
		return FALSE;
	px = lrg_ui_event_get_x (event);
	py = lrg_ui_event_get_y (event);
	if (snaps)
	{
		names = lrg_mixer_asset_get_snapshot_names (self->asset);
		n = strv_len (names);
	}
	else
		n = lrg_mixer_asset_get_duck_count (self->asset);
	if (py < y + 1.0f || n == 0)
		return FALSE;
	row = (guint) ((py - y - 1.0f) / 22.0f);
	if (row >= n)
		return TRUE;

	/* trash */
	if (px >= x + w - LIST_BTN_W)
	{
		c = lrg_mixer_asset_copy (self->asset);
		if (snaps)
		{
			g_autofree gchar *label = g_strdup_printf ("Remove snapshot %s", names[row]);

			lrg_mixer_asset_remove_snapshot (c, names[row]);
			if (!lrg_editor_mixer_editor_commit (self, c, label, 0, &error))
				report (self, error);
		}
		else
		{
			Duck              d;
			g_autofree gchar *trigger = NULL;
			g_autofree gchar *target = NULL;

			get_duck (c, (gint) row, &d);
			trigger = g_strdup (d.trigger);
			target = g_strdup (d.target);
			lrg_mixer_asset_remove_duck (c, trigger, target);
			if (!lrg_editor_mixer_editor_commit (self, c, "Remove ducking rule", 0, &error))
				report (self, error);
		}
		return TRUE;
	}
	/* preview (snapshots) */
	if (snaps && px >= x + w - 2.0f * LIST_BTN_W)
	{
		lrg_editor_mixer_editor_select_snapshot (self, names[row]);
		if (!lrg_editor_mixer_editor_preview_snapshot (self, names[row], -1.0, &error))
			report (self, error);
		return TRUE;
	}
	if (snaps)
		lrg_editor_mixer_editor_select_snapshot (self, names[row]);
	else
	{
		self->sel_duck = (gint) row;
		sync_widgets (self);
	}
	return TRUE;
}

/* ==========================================================================
 * Preview
 * ========================================================================== */

/* A 2 s stereo float loop for the @index-th previewed bus: a tone gated
 * on for the first 60% of a per-bus period, with short fades. */
static LrgWaveData *
test_signal (guint index)
{
	const guint       rate = LRG_EDITOR_MIXER_PREVIEW_RATE;
	const guint       frames = rate * 2;
	g_autofree gfloat *pcm = g_new (gfloat, frames * 2);
	gdouble           freq = 196.0 * pow (1.2599, (gdouble) (index % 7));
	gdouble           period = 0.5 + 0.25 * (gdouble) (index % 4);
	guint             i;

	for (i = 0; i < frames; i++)
	{
		gdouble t = (gdouble) i / (gdouble) rate;
		gdouble ph = fmod (t, period) / period;
		gdouble env = ph < 0.6 ? MIN (ph / 0.02, 1.0) * MIN ((0.6 - ph) / 0.02, 1.0) : 0.0;
		gfloat  v = (gfloat) (0.35 * env * sin (2.0 * G_PI * freq * t));

		pcm[i * 2] = v;
		pcm[i * 2 + 1] = v;
	}
	return lrg_wave_data_new_from_samples (rate, 32, 2, (const guint8 *) pcm, frames * 2 * sizeof (gfloat));
}

/* Builds the preview mixer from the layout and starts the test signal. */
static void
rebuild_preview (LrgEditorMixerEditor *self)
{
	g_autoptr(GError) error = NULL;
	g_auto(GStrv)     buses = NULL;
	guint             i, k = 0;

	if (!self->previewing || self->asset == NULL)
	{
		g_clear_object (&self->preview);
		return;
	}
	/* an existing mixer follows edits (voices keep playing); a changed
	 * bus tree needs a new one */
	if (self->preview != NULL && lrg_mixer_asset_apply (self->asset, self->preview, &error))
	{
		if (self->preview_snapshot != NULL)
			lrg_mixer_asset_apply_snapshot (self->asset, self->preview, self->preview_snapshot, 0.0, NULL);
		return;
	}
	g_clear_error (&error);
	g_clear_object (&self->preview);
	self->preview = lrg_mixer_asset_create_mixer (self->asset, LRG_EDITOR_MIXER_PREVIEW_RATE, 2, &error);
	if (self->preview == NULL)
	{
		report (self, error);
		return;
	}
	buses = lrg_mixer_asset_get_bus_names (self->asset);
	for (i = 0; buses[i] != NULL; i++)
	{
		g_autoptr(LrgWaveData) wave = NULL;

		if (g_strcmp0 (buses[i], "master") == 0 || bus_has_children (self->asset, buses[i]))
			continue;
		wave = test_signal (k++);
		if (wave != NULL)
			lrg_audio_mixer_play (self->preview, wave, buses[i], 1.0, TRUE);
	}
	if (self->preview_snapshot != NULL &&
	    !lrg_mixer_asset_apply_snapshot (self->asset, self->preview, self->preview_snapshot, 0.0, NULL))
		g_clear_pointer (&self->preview_snapshot, g_free);
}

void
lrg_editor_mixer_editor_set_previewing (LrgEditorMixerEditor *self,
                                        gboolean              previewing)
{
	g_return_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self));

	previewing = !!previewing;
	if (self->previewing == previewing)
		return;
	self->previewing = previewing;
	if (!previewing)
		g_clear_pointer (&self->preview_snapshot, g_free);
	rebuild_preview (self);
	if (self->preview_chip != NULL)
		editor_chip_set_checked (self->preview_chip, previewing);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_editor_mixer_editor_get_previewing (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), FALSE);
	return self->previewing;
}

LrgAudioMixer *
lrg_editor_mixer_editor_get_preview_mixer (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), NULL);
	return self->preview;
}

void
lrg_editor_mixer_editor_preview_render (LrgEditorMixerEditor *self,
                                        gdouble               seconds)
{
	g_autofree gfloat *pcm = NULL;
	gsize              n = 0;
	guint              frames;

	g_return_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self));
	if (self->preview == NULL || !(seconds > 0.0))
		return;
	frames = (guint) CLAMP (seconds * LRG_EDITOR_MIXER_PREVIEW_RATE, 1.0, 48000.0);
	pcm = lrg_audio_mixer_render (self->preview, frames, &n);
}

/*
 * Renders one frame of preview audio, at most once per app frame: the
 * editor tab steps it while shown and the strips of the Mixer panel step
 * it while the tab is hidden behind another one.
 */
static void
pump_preview (LrgEditorMixerEditor *self,
              gdouble               delta_time)
{
	LrgEditorApp *app = app_of (self);

	if (self->preview == NULL)
		return;
	if (app != NULL)
	{
		if (app->frames == self->rendered_frame)
			return;
		self->rendered_frame = app->frames;
	}
	lrg_editor_mixer_editor_preview_render (self, MIN (delta_time, 0.1));
}

gboolean
lrg_editor_mixer_editor_preview_snapshot (LrgEditorMixerEditor  *self,
                                          const gchar           *name,
                                          gdouble                blend,
                                          GError               **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), FALSE);

	if (self->asset == NULL)
		return edit_copy (self, error) != NULL;
	if (!self->previewing)
		lrg_editor_mixer_editor_set_previewing (self, TRUE);
	if (self->preview == NULL)
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_ROUTING, "No preview mixer");
		return FALSE;
	}
	if (!lrg_mixer_asset_apply_snapshot (self->asset, self->preview, name, blend, error))
		return FALSE;
	{
		gchar *n = g_strdup (name);

		g_free (self->preview_snapshot);
		self->preview_snapshot = n;
	}
	return TRUE;
}

gboolean
lrg_editor_mixer_editor_preview_blend (LrgEditorMixerEditor  *self,
                                       const gchar * const   *names,
                                       const gdouble         *weights,
                                       guint                  n_snapshots,
                                       gdouble                blend,
                                       GError               **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), FALSE);

	if (self->asset == NULL)
		return edit_copy (self, error) != NULL;
	if (!self->previewing)
		lrg_editor_mixer_editor_set_previewing (self, TRUE);
	if (self->preview == NULL)
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_ROUTING, "No preview mixer");
		return FALSE;
	}
	if (!lrg_mixer_asset_blend_snapshots (self->asset, self->preview, names, weights, n_snapshots, blend, error))
		return FALSE;
	g_clear_pointer (&self->preview_snapshot, g_free);
	return TRUE;
}

const gchar *
lrg_editor_mixer_editor_get_preview_snapshot (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), NULL);
	return self->preview_snapshot;
}

LrgAudioMixer *
lrg_editor_mixer_editor_get_meter_mixer (LrgEditorApp          *app,
                                         LrgEditorMixerEditor  *fallback,
                                         const gchar          **out_source)
{
	LrgAudioMixer *game;

	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);

	if (app != NULL && app->play != NULL && lrg_editor_play_session_is_playing (app->play))
	{
		game = lrg_audio_manager_get_mixer (lrg_audio_manager_get_default ());
		if (game != NULL)
		{
			if (out_source != NULL)
				*out_source = "play";
			return game;
		}
	}
	if (fallback != NULL && fallback->preview != NULL)
	{
		if (out_source != NULL)
			*out_source = "preview";
		return fallback->preview;
	}
	if (out_source != NULL)
		*out_source = "none";
	return NULL;
}

/* ==========================================================================
 * Selection and snapshots
 * ========================================================================== */

const gchar *
lrg_editor_mixer_editor_get_selected_bus (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), NULL);
	return self->sel_bus;
}

const gchar *
lrg_editor_mixer_editor_get_selected_effect (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), NULL);
	return self->sel_effect;
}

gboolean
lrg_editor_mixer_editor_select (LrgEditorMixerEditor *self,
                                const gchar          *bus,
                                const gchar          *effect)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), FALSE);

	if (bus != NULL && (self->asset == NULL || !lrg_mixer_asset_has_bus (self->asset, bus)))
		return FALSE;
	if (effect != NULL)
	{
		g_auto(GStrv) ids = bus != NULL ? lrg_mixer_asset_get_effect_ids (self->asset, bus) : NULL;

		if (!strv_has (ids, effect))
			return FALSE;
	}
	{
		/* the arguments may be the current strings: copy first */
		gchar *b = g_strdup (bus);
		gchar *e = g_strdup (effect);

		g_free (self->sel_bus);
		g_free (self->sel_effect);
		self->sel_bus = b;
		self->sel_effect = e;
	}
	sync_widgets (self);
	return TRUE;
}

const gchar *
lrg_editor_mixer_editor_get_selected_snapshot (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), NULL);
	return self->sel_snapshot;
}

gboolean
lrg_editor_mixer_editor_select_snapshot (LrgEditorMixerEditor *self,
                                         const gchar          *name)
{
	g_auto(GStrv) names = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), FALSE);

	if (name != NULL)
	{
		names = self->asset != NULL ? lrg_mixer_asset_get_snapshot_names (self->asset) : NULL;
		if (!strv_has (names, name))
			return FALSE;
	}
	{
		gchar *n = g_strdup (name);

		g_free (self->sel_snapshot);
		self->sel_snapshot = n;
	}
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_mixer_editor_capture_snapshot (LrgEditorMixerEditor  *self,
                                          const gchar           *name,
                                          GError               **error)
{
	g_autoptr(LrgMixerAsset) c = NULL;
	g_auto(GStrv)            names = NULL;
	g_auto(GStrv)            buses = NULL;
	g_autofree gchar        *label = NULL;
	guint                    i;

	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), FALSE);
	g_return_val_if_fail (name != NULL && *name != '\0', FALSE);

	c = edit_copy (self, error);
	if (c == NULL)
		return FALSE;
	names = lrg_mixer_asset_get_snapshot_names (c);
	if (!strv_has (names, name) && !lrg_mixer_asset_add_snapshot (c, name, 0.25))
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, "Bad snapshot name '%s'", name);
		return FALSE;
	}
	buses = lrg_mixer_asset_get_bus_names (c);
	for (i = 0; buses[i] != NULL; i++)
	{
		g_auto(GStrv) sends = lrg_mixer_asset_get_send_targets (c, buses[i]);
		guint         j;

		lrg_mixer_asset_set_snapshot_volume (c, name, buses[i], lrg_mixer_asset_get_bus_volume (c, buses[i]));
		for (j = 0; sends != NULL && sends[j] != NULL; j++)
		{
			gdouble lvl = LRG_AUDIO_SILENCE_DB;

			lrg_mixer_asset_get_send (c, buses[i], sends[j], &lvl);
			lrg_mixer_asset_set_snapshot_send (c, name, buses[i], sends[j], lvl);
		}
	}
	label = g_strdup_printf ("Capture snapshot %s", name);
	if (!lrg_editor_mixer_editor_commit (self, c, label, 0, error))
		return FALSE;
	lrg_editor_mixer_editor_select_snapshot (self, name);
	return TRUE;
}

LrgWidget *
lrg_editor_mixer_editor_get_strips (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), NULL);
	return self->strips;
}

LrgPropertyGrid *
lrg_editor_mixer_editor_get_bus_grid (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), NULL);
	return self->bus_grid;
}

LrgPropertyGrid *
lrg_editor_mixer_editor_get_snapshot_grid (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), NULL);
	return self->snap_grid;
}

LrgPropertyGrid *
lrg_editor_mixer_editor_get_duck_grid (LrgEditorMixerEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_EDITOR (self), NULL);
	return self->duck_grid;
}

/* ==========================================================================
 * Widgets: sync and the controls under the grids
 * ========================================================================== */

/* Refills the bus / send / trigger pickers when the bus list changed. */
static void
sync_combos (LrgEditorMixerEditor *self)
{
	g_auto(GStrv)     buses = self->asset != NULL ? lrg_mixer_asset_get_bus_names (self->asset) : NULL;
	g_autofree gchar *joined = buses != NULL ? g_strjoinv (",", buses) : g_strdup ("");
	g_autofree gchar *sig = g_strdup_printf ("%s|%s", joined, self->sel_bus != NULL ? self->sel_bus : "");
	guint             i;

	if (g_strcmp0 (sig, self->combo_sig) == 0)
		return;
	g_free (self->combo_sig);
	self->combo_sig = g_steal_pointer (&sig);
	self->syncing = TRUE;
	lrg_combo_box_remove_all (self->add_send);
	lrg_combo_box_append (self->add_send, "+ Send to...");
	lrg_combo_box_remove_all (self->duck_trigger);
	lrg_combo_box_remove_all (self->duck_target);
	for (i = 0; buses != NULL && buses[i] != NULL; i++)
	{
		lrg_combo_box_append (self->add_send, buses[i]);
		lrg_combo_box_append (self->duck_trigger, buses[i]);
		lrg_combo_box_append (self->duck_target, buses[i]);
	}
	lrg_combo_box_set_selected (self->add_send, 0);
	lrg_combo_box_set_selected (self->duck_trigger, strv_len (buses) > 1 ? 1 : 0);
	lrg_combo_box_set_selected (self->duck_target, strv_len (buses) > 2 ? 2 : 0);
	self->syncing = FALSE;
}

static void
sync_widgets (LrgEditorMixerEditor *self)
{
	g_autofree gchar *heading = NULL;

	if (self->bus_grid == NULL)
		return;
	if (self->sel_bus == NULL)
		heading = g_strdup (self->asset != NULL ? "Select a strip to edit its bus or effect" : "");
	else if (self->sel_effect != NULL)
		heading = g_strdup_printf ("%s / %s (%s)", self->sel_bus, self->sel_effect,
		                           lrg_mixer_asset_get_effect_type (self->asset, self->sel_bus, self->sel_effect));
	else
		heading = g_strdup_printf ("Bus %s", self->sel_bus);
	lrg_label_set_text (self->bus_heading, heading);
	lrg_widget_set_visible (self->move_up, self->sel_effect != NULL);
	lrg_widget_set_visible (self->move_down, self->sel_effect != NULL);
	sync_combos (self);
	sync_grid (self->bus_grid, self->bus_src, FALSE);
	sync_grid (self->snap_grid, self->snap_src, FALSE);
	sync_grid (self->duck_grid, self->duck_src, FALSE);
	if (self->status != NULL)
	{
		g_autofree gchar *st = NULL;

		if (self->asset == NULL)
			st = g_strdup_printf ("Cannot load: %s", self->load_error != NULL ? self->load_error : "?");
		else
		{
			g_auto(GStrv) b = lrg_mixer_asset_get_bus_names (self->asset);
			g_auto(GStrv) sn = lrg_mixer_asset_get_snapshot_names (self->asset);

			st = g_strdup_printf ("%u buses, %u snapshots, %u ducking rules%s", strv_len (b), strv_len (sn),
			                      lrg_mixer_asset_get_duck_count (self->asset),
			                      self->previewing ? (self->preview_snapshot != NULL ? " - previewing snapshot"
			                                                                         : " - previewing")
			                                       : "");
		}
		lrg_label_set_text (self->status, st);
	}
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_add_effect_changed (LrgComboBox *combo,
                       gpointer     user_data)
{
	LrgEditorMixerEditor     *self = LRG_EDITOR_MIXER_EDITOR (user_data);
	gint                      i = lrg_combo_box_get_selected (combo);
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgMixerAsset)  c = NULL;
	const gchar              *type;
	g_autofree gchar         *id = NULL;
	g_autofree gchar         *label = NULL;
	guint                     n = 2;

	if (self->syncing || i <= 0 || self->effect_types == NULL || (guint) i > strv_len (self->effect_types))
		return;
	type = self->effect_types[i - 1];
	self->syncing = TRUE;
	lrg_combo_box_set_selected (combo, 0);
	self->syncing = FALSE;
	if (self->sel_bus == NULL)
	{
		editor_app_set_status (app_of (self), "Select a bus first");
		return;
	}
	c = edit_copy (self, &error);
	if (c == NULL)
	{
		report (self, error);
		return;
	}
	/* a unique id: the type, then type-2, type-3 ... */
	id = g_strdup (type);
	{
		g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (c, self->sel_bus);

		while (strv_has (ids, id))
		{
			g_free (id);
			id = g_strdup_printf ("%s-%u", type, n++);
		}
	}
	label = g_strdup_printf ("Add %s to %s", type, self->sel_bus);
	if (!lrg_mixer_asset_add_effect (c, self->sel_bus, type, id, &error) ||
	    !lrg_editor_mixer_editor_commit (self, c, label, 0, &error))
	{
		report (self, error);
		return;
	}
	lrg_editor_mixer_editor_select (self, self->sel_bus, id);
}

static void
on_add_send_changed (LrgComboBox *combo,
                     gpointer     user_data)
{
	LrgEditorMixerEditor     *self = LRG_EDITOR_MIXER_EDITOR (user_data);
	g_autofree gchar         *target = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgMixerAsset)  c = NULL;
	g_autofree gchar         *label = NULL;

	if (self->syncing || lrg_combo_box_get_selected (combo) <= 0)
		return;
	target = g_strdup (lrg_combo_box_get_selected_text (combo));
	self->syncing = TRUE;
	lrg_combo_box_set_selected (combo, 0);
	self->syncing = FALSE;
	if (self->sel_bus == NULL)
		return;
	c = edit_copy (self, &error);
	if (c == NULL || !lrg_mixer_asset_set_send (c, self->sel_bus, target, -12.0))
	{
		if (error == NULL)
			g_set_error (&error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_ROUTING,
			             "A send from %s to %s is not allowed (itself, its parent, master or a loop)",
			             self->sel_bus, target);
		report (self, error);
		return;
	}
	label = g_strdup_printf ("Add send %s > %s", self->sel_bus, target);
	if (!lrg_editor_mixer_editor_commit (self, c, label, 0, &error))
		report (self, error);
}

static void
on_add_bus (LrgWidget *chip,
            gpointer   user_data)
{
	LrgEditorMixerEditor     *self = LRG_EDITOR_MIXER_EDITOR (user_data);
	const gchar              *text = lrg_text_input_get_text (self->bus_name);
	g_autofree gchar         *name = g_strstrip (g_strdup (text != NULL ? text : ""));
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgMixerAsset)  c = NULL;
	g_autofree gchar         *label = NULL;
	guint                     n = 1;

	c = edit_copy (self, &error);
	if (c == NULL)
	{
		report (self, error);
		return;
	}
	if (*name == '\0')
	{
		g_free (name);
		name = g_strdup ("bus");
		while (lrg_mixer_asset_has_bus (c, name))
		{
			g_free (name);
			name = g_strdup_printf ("bus%u", n++);
		}
	}
	/* a child of the selected bus, else of master */
	if (!lrg_mixer_asset_add_bus (c, name, self->sel_bus != NULL ? self->sel_bus : "master"))
	{
		g_set_error (&error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS,
		             "Cannot add bus '%s' (taken or invalid name)", name);
		report (self, error);
		return;
	}
	label = g_strdup_printf ("Add bus %s", name);
	if (!lrg_editor_mixer_editor_commit (self, c, label, 0, &error))
	{
		report (self, error);
		return;
	}
	lrg_text_input_set_text (self->bus_name, "");
	lrg_editor_mixer_editor_select (self, name, NULL);
}

static void
on_remove (LrgWidget *chip,
           gpointer   user_data)
{
	LrgEditorMixerEditor     *self = LRG_EDITOR_MIXER_EDITOR (user_data);
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgMixerAsset)  c = NULL;
	g_autofree gchar         *label = NULL;
	gboolean                  ok;

	if (self->sel_bus == NULL)
		return;
	c = edit_copy (self, &error);
	if (c == NULL)
	{
		report (self, error);
		return;
	}
	if (self->sel_effect != NULL)
	{
		ok = lrg_mixer_asset_remove_effect (c, self->sel_bus, self->sel_effect);
		label = g_strdup_printf ("Remove %s from %s", self->sel_effect, self->sel_bus);
	}
	else
	{
		ok = lrg_mixer_asset_remove_bus (c, self->sel_bus);
		label = g_strdup_printf ("Remove bus %s", self->sel_bus);
	}
	if (!ok)
	{
		g_set_error (&error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_ROUTING,
		             "Cannot remove %s (master and buses with child buses stay)", self->sel_bus);
		report (self, error);
		return;
	}
	if (!lrg_editor_mixer_editor_commit (self, c, label, 0, &error))
		report (self, error);
}

static void
move_effect (LrgEditorMixerEditor *self,
             gint                  delta)
{
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgMixerAsset)  c = NULL;
	g_auto(GStrv)             ids = NULL;
	gint                      at = -1, i;

	if (self->sel_bus == NULL || self->sel_effect == NULL || self->asset == NULL)
		return;
	ids = lrg_mixer_asset_get_effect_ids (self->asset, self->sel_bus);
	for (i = 0; ids != NULL && ids[i] != NULL; i++)
		if (g_strcmp0 (ids[i], self->sel_effect) == 0)
			at = i;
	if (at < 0 || at + delta < 0 || at + delta >= (gint) strv_len (ids))
		return;
	c = lrg_mixer_asset_copy (self->asset);
	lrg_mixer_asset_move_effect (c, self->sel_bus, self->sel_effect, (guint) (at + delta));
	if (!lrg_editor_mixer_editor_commit (self, c, "Reorder effects", 0, &error))
		report (self, error);
}

static void
on_move_up (LrgWidget *chip,
            gpointer   user_data)
{
	move_effect (LRG_EDITOR_MIXER_EDITOR (user_data), -1);
}

static void
on_move_down (LrgWidget *chip,
              gpointer   user_data)
{
	move_effect (LRG_EDITOR_MIXER_EDITOR (user_data), 1);
}

static void
on_add_snapshot (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorMixerEditor     *self = LRG_EDITOR_MIXER_EDITOR (user_data);
	const gchar              *text = lrg_text_input_get_text (self->snap_name);
	g_autofree gchar         *name = g_strstrip (g_strdup (text != NULL ? text : ""));
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgMixerAsset)  c = NULL;
	g_autofree gchar         *label = NULL;
	guint                     n = 1;

	c = edit_copy (self, &error);
	if (c == NULL)
	{
		report (self, error);
		return;
	}
	if (*name == '\0')
	{
		g_auto(GStrv) names = lrg_mixer_asset_get_snapshot_names (c);

		g_free (name);
		name = g_strdup ("snapshot");
		while (strv_has (names, name))
		{
			g_free (name);
			name = g_strdup_printf ("snapshot%u", n++);
		}
	}
	if (!lrg_mixer_asset_add_snapshot (c, name, 0.25))
	{
		g_set_error (&error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_INVALID_VALUE,
		             "Cannot add snapshot '%s' (taken?)", name);
		report (self, error);
		return;
	}
	label = g_strdup_printf ("Add snapshot %s", name);
	if (!lrg_editor_mixer_editor_commit (self, c, label, 0, &error))
	{
		report (self, error);
		return;
	}
	lrg_text_input_set_text (self->snap_name, "");
	lrg_editor_mixer_editor_select_snapshot (self, name);
}

static void
on_capture (LrgWidget *chip,
            gpointer   user_data)
{
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (user_data);
	const gchar          *text = lrg_text_input_get_text (self->snap_name);
	g_autofree gchar     *name = g_strstrip (g_strdup (text != NULL ? text : ""));
	g_autoptr(GError)     error = NULL;

	/* the typed name, else the selected snapshot, else a new one */
	if (*name == '\0' && self->sel_snapshot != NULL)
	{
		g_free (name);
		name = g_strdup (self->sel_snapshot);
	}
	if (*name == '\0')
	{
		g_free (name);
		name = g_strdup ("captured");
	}
	if (!lrg_editor_mixer_editor_capture_snapshot (self, name, &error))
		report (self, error);
	else
		lrg_text_input_set_text (self->snap_name, "");
}

static void
on_add_duck (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorMixerEditor     *self = LRG_EDITOR_MIXER_EDITOR (user_data);
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgMixerAsset)  c = NULL;
	const gchar              *trigger = lrg_combo_box_get_selected_text (self->duck_trigger);
	const gchar              *target = lrg_combo_box_get_selected_text (self->duck_target);
	g_autofree gchar         *label = NULL;

	c = edit_copy (self, &error);
	if (c == NULL)
	{
		report (self, error);
		return;
	}
	if (!lrg_mixer_asset_set_duck (c, trigger, target, -30.0, -12.0, 0.05, 0.5))
	{
		g_set_error (&error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_ROUTING,
		             "%s cannot duck %s (same bus, master or a bus that feeds the trigger)",
		             trigger != NULL ? trigger : "?", target != NULL ? target : "?");
		report (self, error);
		return;
	}
	label = g_strdup_printf ("Duck %s under %s", target, trigger);
	if (!lrg_editor_mixer_editor_commit (self, c, label, 0, &error))
	{
		report (self, error);
		return;
	}
	self->sel_duck = (gint) lrg_mixer_asset_get_duck_count (self->asset) - 1;
	sync_widgets (self);
}

static void
on_preview_chip (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (user_data);

	lrg_editor_mixer_editor_set_previewing (self, !self->previewing);
	sync_widgets (self);
}

static void
on_base_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (user_data);
	g_autoptr(GError)     error = NULL;

	if (!lrg_editor_mixer_editor_preview_snapshot (self, NULL, 0.25, &error))
		report (self, error);
	sync_widgets (self);
}

/* ==========================================================================
 * Data editor vfuncs
 * ========================================================================== */

static void
mixer_refresh (LrgEditorDataEditor *editor,
               const gchar         *path)
{
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (editor);

	load_asset (self);
	rebuild_preview (self);
	sync_widgets (self);
}

static void
mixer_reveal (LrgEditorDataEditor *editor,
              const gchar         *path,
              guint                line)
{
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (editor);
	g_auto(GStrv)         parts = lrg_editor_document_path_split (path, NULL);
	g_auto(GStrv)         buses = NULL;
	g_auto(GStrv)         snaps = NULL;
	guint                 i;

	if (parts == NULL || parts[0] == NULL || parts[1] == NULL || self->asset == NULL)
		return;
	i = (guint) g_ascii_strtoull (parts[1], NULL, 10);
	if (g_strcmp0 (parts[0], "buses") == 0)
	{
		buses = lrg_mixer_asset_get_bus_names (self->asset);
		if (i < strv_len (buses))
		{
			const gchar *effect = NULL;
			g_auto(GStrv) ids = NULL;

			if (parts[2] != NULL && g_strcmp0 (parts[2], "effects") == 0 && parts[3] != NULL)
			{
				guint e = (guint) g_ascii_strtoull (parts[3], NULL, 10);

				ids = lrg_mixer_asset_get_effect_ids (self->asset, buses[i]);
				if (e < strv_len (ids))
					effect = ids[e];
			}
			lrg_editor_mixer_editor_select (self, buses[i], effect);
		}
	}
	else if (g_strcmp0 (parts[0], "snapshots") == 0)
	{
		snaps = lrg_mixer_asset_get_snapshot_names (self->asset);
		if (i < strv_len (snaps))
			lrg_editor_mixer_editor_select_snapshot (self, snaps[i]);
	}
	else if (g_strcmp0 (parts[0], "ducking") == 0)
	{
		self->sel_duck = (gint) i;
		sync_widgets (self);
	}
}

/* The engine's own validation as a problem. */
static void
mixer_validate (LrgEditorDataEditor *editor,
                GPtrArray           *issues)
{
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (editor);
	g_autoptr(GError)     error = NULL;

	load_asset (self);
	if (self->asset == NULL)
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, "", 0, 0,
		                                                        self->load_error != NULL ? self->load_error : "?"));
	else if (!lrg_mixer_asset_validate (self->asset, &error))
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, "", 0, 0, error->message));
}

static gchar *
mixer_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (editor);
	g_auto(GStrv)         b = self->asset != NULL ? lrg_mixer_asset_get_bus_names (self->asset) : NULL;

	return g_strdup_printf ("buses=%u selected=%s/%s snapshot=%s preview=%s", strv_len (b),
	                        self->sel_bus != NULL ? self->sel_bus : "-", self->sel_effect != NULL ? self->sel_effect : "-",
	                        self->sel_snapshot != NULL ? self->sel_snapshot : "-", self->previewing ? "on" : "off");
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static gboolean
lrg_editor_mixer_editor_step (LrgWidget *widget,
                              gfloat     delta_time)
{
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (widget);
	LrgWidgetClass       *parent = LRG_WIDGET_CLASS (lrg_editor_mixer_editor_parent_class);
	gboolean              busy = parent->step != NULL && parent->step (widget, delta_time);

	if (self->grids_stale)
	{
		self->grids_stale = FALSE;
		sync_grid (self->bus_grid, self->bus_src, TRUE);
		sync_grid (self->snap_grid, self->snap_src, TRUE);
		sync_grid (self->duck_grid, self->duck_src, TRUE);
		lrg_widget_queue_layout (widget);
	}
	if (self->preview != NULL)
	{
		pump_preview (self, (gdouble) delta_time);
		busy = TRUE;
	}
	return busy;
}

static gpointer
add_to (gpointer box,
        gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (child));
	g_object_unref (child);
	return child;
}

static LrgPropertyGrid *
make_grid (const gchar *name,
           MixerSource *src)
{
	LrgPropertyGrid *grid = lrg_property_grid_new ();

	lrg_widget_set_name (LRG_WIDGET (grid), name);
	lrg_property_grid_set_label_width (grid, 104.0f);
	source_rebuild (src);
	lrg_property_grid_set_source (grid, LRG_PROPERTY_SOURCE (src));
	g_signal_connect (grid, "edit-begin", G_CALLBACK (on_grid_edit_begin), src);
	g_signal_connect (grid, "edit-end", G_CALLBACK (on_grid_edit_end), src);
	return grid;
}

static LrgWidget *
heading (const gchar *text)
{
	LrgWidget *label = LRG_WIDGET (lrg_label_new (text));

	lrg_widget_add_class (label, "heading");
	return label;
}

static LrgWidget *
row_box (const gchar *name)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());

	lrg_widget_set_name (row, name);
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	return row;
}

/* A column: a vbox in a vertical scroll box, filling its share. */
static LrgWidget *
column (const gchar *name,
        LrgWidget  **out_box)
{
	LrgWidget *box = LRG_WIDGET (lrg_vbox_new ());
	LrgWidget *scroll = LRG_WIDGET (lrg_scroll_box_new ());
	g_autofree gchar *sname = g_strdup_printf ("%s-scroll", name);

	lrg_widget_set_name (box, name);
	lrg_container_set_spacing (LRG_CONTAINER (box), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (box), 8.0f);
	lrg_widget_set_name (scroll, sname);
	/* three columns share the tab's width: let them shrink (and scroll
	 * sideways) rather than push the row past the panel */
	lrg_scroll_box_set_hscroll (LRG_SCROLL_BOX (scroll), TRUE);
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), box);
	g_object_unref (box);
	lrg_widget_set_fill_weight (scroll, 1.0f);
	*out_box = box;
	return scroll;
}

static void
lrg_editor_mixer_editor_constructed (GObject *object)
{
	static const LrgCustomWidgetFuncs list_funcs = { list_measure, list_paint, NULL, list_event, { NULL } };
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (object);
	LrgEditorDataEditor  *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorApp         *app;
	LrgSplitter          *split;
	LrgWidget            *strips_scroll, *bottom, *col, *box, *row, *w;
	g_auto(GStrv)         buses = NULL;
	guint                 i;

	G_OBJECT_CLASS (lrg_editor_mixer_editor_parent_class)->constructed (object);

	load_asset (self);
	self->bus_src = mixer_source_new (self, SRC_BUS);
	self->snap_src = mixer_source_new (self, SRC_SNAPSHOT);
	self->duck_src = mixer_source_new (self, SRC_DUCK);
	/* start on the first bus below master, so the grid has something */
	if (self->asset != NULL)
	{
		buses = lrg_mixer_asset_get_bus_names (self->asset);
		self->sel_bus = g_strdup (strv_len (buses) > 1 ? buses[1] : buses[0]);
	}

	/* Top: the strips, scrolling sideways */
	self->strips = lrg_editor_mixer_strips_new (lrg_editor_data_editor_get_app (base), FALSE, own_asset, own_editor,
	                                            self, NULL);
	strips_scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (strips_scroll, "mixer-strips-scroll");
	lrg_scroll_box_set_hscroll (LRG_SCROLL_BOX (strips_scroll), TRUE);
	lrg_scroll_box_set_vscroll (LRG_SCROLL_BOX (strips_scroll), TRUE);
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (strips_scroll), self->strips);
	g_object_unref (self->strips);

	/* Bottom: three columns */
	bottom = row_box ("mixer-details");
	lrg_container_set_spacing (LRG_CONTAINER (bottom), 2.0f);

	/* -- selection column -- */
	col = column ("mixer-bus-column", &box);
	self->bus_heading = add_to (box, lrg_label_new (""));
	lrg_widget_set_name (LRG_WIDGET (self->bus_heading), "mixer-bus-heading");
	lrg_widget_add_class (LRG_WIDGET (self->bus_heading), "heading");
	row = row_box ("mixer-bus-actions");
	self->add_effect = add_to (row, lrg_combo_box_new ());
	lrg_widget_set_name (LRG_WIDGET (self->add_effect), "mixer-add-effect");
	lrg_widget_set_max_size (LRG_WIDGET (self->add_effect), 140.0f, 0.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->add_effect), 1.0f);
	lrg_combo_box_append (self->add_effect, "+ Add effect...");
	self->effect_types = lrg_mixer_asset_list_effect_types ();
	for (i = 0; self->effect_types != NULL && self->effect_types[i] != NULL; i++)
		lrg_combo_box_append (self->add_effect, self->effect_types[i]);
	lrg_combo_box_set_selected (self->add_effect, 0);
	g_signal_connect (self->add_effect, "changed", G_CALLBACK (on_add_effect_changed), self);
	self->add_send = add_to (row, lrg_combo_box_new ());
	lrg_widget_set_name (LRG_WIDGET (self->add_send), "mixer-add-send");
	lrg_widget_set_max_size (LRG_WIDGET (self->add_send), 140.0f, 0.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->add_send), 1.0f);
	g_signal_connect (self->add_send, "changed", G_CALLBACK (on_add_send_changed), self);
	add_to (box, row);
	row = row_box ("mixer-bus-edit");
	self->bus_name = add_to (row, lrg_text_input_new_with_placeholder ("new bus name"));
	lrg_widget_set_name (LRG_WIDGET (self->bus_name), "mixer-bus-name");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->bus_name), 1.0f);
	add_to (row, editor_chip_new ("mixer-add-bus", LRG_ICON_PLUS, "Bus", on_add_bus, self));
	add_to (box, row);
	row = row_box ("mixer-bus-order");
	self->move_up = add_to (row, editor_chip_new ("mixer-effect-up", LRG_ICON_CHEVRON_UP, "Earlier", on_move_up, self));
	self->move_down = add_to (row, editor_chip_new ("mixer-effect-down", LRG_ICON_CHEVRON_DOWN, "Later", on_move_down,
	                                                self));
	w = add_to (row, editor_chip_new ("mixer-remove", LRG_ICON_TRASH, "Remove", on_remove, self));
	editor_chip_set_tint (w, LRG_THEME_COLOR_DANGER);
	add_to (box, row);
	/* the grid last: the controls stay in view when the grid scrolls */
	self->bus_grid = add_to (box, make_grid ("mixer-bus-grid", self->bus_src));
	add_to (bottom, col);

	/* -- snapshots column -- */
	col = column ("mixer-snapshot-column", &box);
	add_to (box, heading ("Snapshots"));
	self->snap_list = add_to (box, lrg_custom_widget_new ());
	lrg_widget_set_name (self->snap_list, "mixer-snapshots");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->snap_list), &list_funcs, self, NULL);
	row = row_box ("mixer-snapshot-actions");
	self->snap_name = add_to (row, lrg_text_input_new_with_placeholder ("snapshot name"));
	lrg_widget_set_name (LRG_WIDGET (self->snap_name), "mixer-snapshot-name");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->snap_name), 1.0f);
	add_to (row, editor_chip_new ("mixer-add-snapshot", LRG_ICON_PLUS, "Add", on_add_snapshot, self));
	add_to (box, row);
	row = row_box ("mixer-snapshot-capture");
	w = add_to (row, editor_chip_new ("mixer-capture", LRG_ICON_DOWNLOAD, "Capture faders", on_capture, self));
	editor_chip_set_tint (w, LRG_THEME_COLOR_ACCENT);
	add_to (box, row);
	self->snap_grid = add_to (box, make_grid ("mixer-snapshot-grid", self->snap_src));
	add_to (bottom, col);

	/* -- ducking column -- */
	col = column ("mixer-duck-column", &box);
	add_to (box, heading ("Ducking"));
	self->duck_list = add_to (box, lrg_custom_widget_new ());
	lrg_widget_set_name (self->duck_list, "mixer-ducks");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->duck_list), &list_funcs, self, NULL);
	row = row_box ("mixer-duck-actions");
	self->duck_trigger = add_to (row, lrg_combo_box_new ());
	lrg_widget_set_name (LRG_WIDGET (self->duck_trigger), "mixer-duck-trigger");
	lrg_widget_set_max_size (LRG_WIDGET (self->duck_trigger), 110.0f, 0.0f);
	w = add_to (row, lrg_label_new ("ducks"));
	lrg_widget_add_class (w, "dim");
	lrg_widget_set_valign (w, LRG_ALIGN_CENTER);
	self->duck_target = add_to (row, lrg_combo_box_new ());
	lrg_widget_set_name (LRG_WIDGET (self->duck_target), "mixer-duck-target");
	lrg_widget_set_max_size (LRG_WIDGET (self->duck_target), 110.0f, 0.0f);
	add_to (box, row);
	row = row_box ("mixer-duck-add-row");
	add_to (row, editor_chip_new ("mixer-add-duck", LRG_ICON_PLUS, "Add rule", on_add_duck, self));
	add_to (box, row);
	self->duck_grid = add_to (box, make_grid ("mixer-duck-grid", self->duck_src));
	self->status = add_to (box, lrg_label_new (""));
	lrg_widget_set_name (LRG_WIDGET (self->status), "mixer-status");
	lrg_widget_add_class (LRG_WIDGET (self->status), "dim");
	add_to (bottom, col);

	split = lrg_splitter_new (LRG_ORIENTATION_VERTICAL);
	lrg_widget_set_name (LRG_WIDGET (split), "mixer-split");
	lrg_splitter_set_start_child (split, strips_scroll);
	g_object_unref (strips_scroll);
	lrg_splitter_set_end_child (split, bottom);
	g_object_unref (bottom);
	lrg_splitter_set_default_fraction (split, 0.56f);
	lrg_splitter_set_fraction (split, 0.56f, FALSE);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (split));
	g_object_unref (split);

	/* Toolbar */
	self->preview_chip = editor_chip_new ("mixer-preview", LRG_ICON_PLAY, "Preview", on_preview_chip, self);
	editor_chip_set_tint (self->preview_chip, LRG_THEME_COLOR_ACCENT);
	lrg_editor_data_editor_add_tool (base, self->preview_chip);
	g_object_unref (self->preview_chip);
	w = editor_chip_new ("mixer-base", LRG_ICON_REFRESH, "Base", on_base_chip, self);
	lrg_editor_data_editor_add_tool (base, w);
	g_object_unref (w);

	sync_widgets (self);
	self->grids_stale = TRUE;

	app = app_of (self);
	if (app != NULL)
		lrg_editor_mixer_editor_install (app);
}

static void
lrg_editor_mixer_editor_dispose (GObject *object)
{
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (object);

	g_clear_object (&self->preview);
	if (self->bus_src != NULL)
		self->bus_src->editor = NULL;
	if (self->snap_src != NULL)
		self->snap_src->editor = NULL;
	if (self->duck_src != NULL)
		self->duck_src->editor = NULL;
	self->strips = NULL;
	self->snap_list = NULL;
	self->duck_list = NULL;
	self->preview_chip = NULL;
	self->bus_grid = NULL;
	self->snap_grid = NULL;
	self->duck_grid = NULL;
	G_OBJECT_CLASS (lrg_editor_mixer_editor_parent_class)->dispose (object);
	g_clear_object (&self->bus_src);
	g_clear_object (&self->snap_src);
	g_clear_object (&self->duck_src);
}

static void
lrg_editor_mixer_editor_finalize (GObject *object)
{
	LrgEditorMixerEditor *self = LRG_EDITOR_MIXER_EDITOR (object);

	g_clear_object (&self->asset);
	g_free (self->load_error);
	g_free (self->sel_bus);
	g_free (self->sel_effect);
	g_free (self->sel_snapshot);
	g_free (self->combo_sig);
	g_free (self->preview_snapshot);
	g_strfreev (self->effect_types);
	G_OBJECT_CLASS (lrg_editor_mixer_editor_parent_class)->finalize (object);
}

static void
lrg_editor_mixer_editor_class_init (LrgEditorMixerEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_mixer_editor_constructed;
	object_class->dispose = lrg_editor_mixer_editor_dispose;
	object_class->finalize = lrg_editor_mixer_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_mixer_editor_step;
	data_class->refresh = mixer_refresh;
	data_class->reveal = mixer_reveal;
	data_class->validate = mixer_validate;
	data_class->get_state = mixer_get_state;
}

static void
lrg_editor_mixer_editor_init (LrgEditorMixerEditor *self)
{
	self->sel_duck = -1;
}

LrgEditorMixerEditor *
lrg_editor_mixer_editor_new (LrgEditorApp      *app,
                             LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_MIXER_EDITOR, "app", app, "document", document, NULL);
}

/* ==========================================================================
 * Finding editors
 * ========================================================================== */

gchar *
lrg_editor_mixer_editor_project_mixer_path (LrgEditorApp *app)
{
	const gchar *rel;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	if (app->project == NULL)
		return NULL;
	rel = lrg_project_get_audio_mixer (app->project);
	if (rel == NULL || *rel == '\0')
		return NULL;
	return editor_app_project_path (app, rel);
}

LrgEditorMixerEditor *
lrg_editor_mixer_editor_find_for_path (LrgEditorApp  *app,
                                       const gchar   *path,
                                       gboolean       open,
                                       GError       **error)
{
	LrgEditorDataEditor *ed;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	ed = lrg_editor_data_editor_find (app, path);
	if (ed == NULL && open)
		ed = lrg_editor_data_editor_open (app, path, error);
	else if (ed == NULL)
		return NULL;
	if (ed == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_MIXER_EDITOR (ed))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a .rmixer mixer layout", path);
		return NULL;
	}
	return LRG_EDITOR_MIXER_EDITOR (ed);
}

/* ==========================================================================
 * IPC: mixer.*
 * ========================================================================== */

static JsonNode *
obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static const gchar *
p_string (JsonObject  *params,
          const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
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

	if (params == NULL || !json_object_has_member (params, name))
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

static gboolean
p_bool (JsonObject  *params,
        const gchar *name,
        gboolean    *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_BOOLEAN)
		return FALSE;
	*out = json_node_get_boolean (n);
	return TRUE;
}

static gboolean
p_is_null (JsonObject  *params,
           const gchar *name)
{
	return params != NULL && json_object_has_member (params, name) &&
	       JSON_NODE_HOLDS_NULL (json_object_get_member (params, name));
}

static void
bad_params (GError      **error,
            const gchar  *message)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, message);
}

static void
not_found (GError      **error,
           const gchar  *what)
{
	g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "%s", what);
}

static void
failed (GError       **error,
        const GError  *local,
        const gchar   *fallback)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
	                     local != NULL ? local->message : fallback);
}

/* The absolute path of params.path, or of the project's default mixer. */
static gchar *
ipc_path (LrgEditorApp  *app,
          JsonObject    *params,
          GError       **error)
{
	const gchar *path = p_string (params, "path");
	gchar       *abs;

	if (path != NULL)
		return g_path_is_absolute (path) ? g_strdup (path) : editor_app_project_path (app, path);
	abs = lrg_editor_mixer_editor_project_mixer_path (app);
	if (abs == NULL)
		not_found (error, "No mixer: pass \"path\" or set the project's default mixer");
	return abs;
}

static LrgEditorMixerEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	g_autofree gchar     *path = ipc_path (app, params, error);
	g_autoptr(GError)     local = NULL;
	LrgEditorMixerEditor *ed;

	if (path == NULL)
		return NULL;
	ed = lrg_editor_mixer_editor_find_for_path (app, path, TRUE, &local);
	if (ed == NULL)
	{
		not_found (error, local != NULL ? local->message : "Cannot open the mixer");
		return NULL;
	}
	if (ed->asset == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s does not load: %s", path,
		             ed->load_error != NULL ? ed->load_error : "?");
		return NULL;
	}
	return ed;
}

/* A GValue as JSON (numbers, booleans, strings, enums by nick). */
static JsonNode *
value_json (const GValue *v)
{
	JsonNode *n = json_node_new (JSON_NODE_VALUE);

	if (G_VALUE_HOLDS_BOOLEAN (v))
		json_node_set_boolean (n, g_value_get_boolean (v));
	else if (G_VALUE_HOLDS_STRING (v))
		json_node_set_string (n, g_value_get_string (v));
	else if (G_VALUE_HOLDS_ENUM (v))
	{
		g_autoptr(GEnumClass) ec = g_type_class_ref (G_VALUE_TYPE (v));
		GEnumValue           *ev = g_enum_get_value (ec, g_value_get_enum (v));

		json_node_set_string (n, ev != NULL ? ev->value_nick : "");
	}
	else
	{
		g_auto(GValue) d = G_VALUE_INIT;

		g_value_init (&d, G_TYPE_DOUBLE);
		if (g_value_transform (v, &d))
			json_node_set_double (n, g_value_get_double (&d));
		else
			json_node_init_null (n);
	}
	return n;
}

/* The layout as JSON (see the IPC docs). */
static JsonNode *
mixer_json (LrgEditorMixerEditor *ed)
{
	LrgMixerAsset *a = ed->asset;
	JsonObject    *o = json_object_new ();
	JsonArray     *arr;
	g_auto(GStrv)  buses = lrg_mixer_asset_get_bus_names (a);
	g_auto(GStrv)  snaps = lrg_mixer_asset_get_snapshot_names (a);
	guint          i, j, k;

	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (ed)));
	json_object_set_string_member (o, "name", lrg_mixer_asset_get_name (a));
	if (lrg_mixer_asset_get_default_snapshot (a) != NULL)
		json_object_set_string_member (o, "default_snapshot", lrg_mixer_asset_get_default_snapshot (a));
	else
		json_object_set_null_member (o, "default_snapshot");
	json_object_set_boolean_member (o, "dirty", lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (ed)));

	arr = json_array_new ();
	for (i = 0; buses[i] != NULL; i++)
	{
		JsonObject   *b = json_object_new ();
		JsonArray    *sa = json_array_new ();
		JsonArray    *ea = json_array_new ();
		g_auto(GStrv) sends = lrg_mixer_asset_get_send_targets (a, buses[i]);
		g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (a, buses[i]);
		const gchar  *parent = lrg_mixer_asset_get_bus_parent (a, buses[i]);

		json_object_set_string_member (b, "name", buses[i]);
		if (parent != NULL)
			json_object_set_string_member (b, "parent", parent);
		else
			json_object_set_null_member (b, "parent");
		json_object_set_double_member (b, "volume", lrg_mixer_asset_get_bus_volume (a, buses[i]));
		json_object_set_boolean_member (b, "mute", lrg_mixer_asset_get_bus_mute (a, buses[i]));
		json_object_set_boolean_member (b, "solo", lrg_mixer_asset_get_bus_solo (a, buses[i]));
		for (j = 0; sends != NULL && sends[j] != NULL; j++)
		{
			JsonObject *so = json_object_new ();
			gdouble     lvl = 0.0;

			lrg_mixer_asset_get_send (a, buses[i], sends[j], &lvl);
			json_object_set_string_member (so, "target", sends[j]);
			json_object_set_double_member (so, "level", lvl);
			json_array_add_object_element (sa, so);
		}
		json_object_set_array_member (b, "sends", sa);
		for (j = 0; ids != NULL && ids[j] != NULL; j++)
		{
			JsonObject          *eo = json_object_new ();
			JsonObject          *po = json_object_new ();
			const gchar         *type = lrg_mixer_asset_get_effect_type (a, buses[i], ids[j]);
			g_autoptr(GPtrArray) specs = effect_pspecs (lrg_mixer_asset_lookup_effect_type (type));

			json_object_set_string_member (eo, "id", ids[j]);
			json_object_set_string_member (eo, "type", type);
			json_object_set_boolean_member (eo, "enabled", lrg_mixer_asset_get_effect_enabled (a, buses[i], ids[j]));
			for (k = 0; k < specs->len; k++)
			{
				GParamSpec    *p = g_ptr_array_index (specs, k);
				g_auto(GValue) v = G_VALUE_INIT;

				if (lrg_mixer_asset_get_effect_param (a, buses[i], ids[j], p->name, &v))
					json_object_set_member (po, p->name, value_json (&v));
			}
			json_object_set_object_member (eo, "params", po);
			json_array_add_object_element (ea, eo);
		}
		json_object_set_array_member (b, "effects", ea);
		json_array_add_object_element (arr, b);
	}
	json_object_set_array_member (o, "buses", arr);

	arr = json_array_new ();
	for (i = 0; snaps != NULL && snaps[i] != NULL; i++)
	{
		JsonObject *so = json_object_new ();
		JsonObject *vols = json_object_new ();
		JsonArray  *ss = json_array_new ();
		JsonObject *ps = json_object_new ();

		json_object_set_string_member (so, "name", snaps[i]);
		json_object_set_double_member (so, "blend", lrg_mixer_asset_get_snapshot_blend (a, snaps[i]));
		for (j = 0; buses[j] != NULL; j++)
		{
			gdouble       d;
			g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (a, buses[j]);

			if (lrg_mixer_asset_get_snapshot_volume (a, snaps[i], buses[j], &d))
				json_object_set_double_member (vols, buses[j], d);
			for (k = 0; buses[k] != NULL; k++)
			{
				if (lrg_mixer_asset_get_snapshot_send (a, snaps[i], buses[j], buses[k], &d))
				{
					JsonObject *e = json_object_new ();

					json_object_set_string_member (e, "source", buses[j]);
					json_object_set_string_member (e, "target", buses[k]);
					json_object_set_double_member (e, "level", d);
					json_array_add_object_element (ss, e);
				}
			}
			for (k = 0; ids != NULL && ids[k] != NULL; k++)
			{
				const gchar         *type = lrg_mixer_asset_get_effect_type (a, buses[j], ids[k]);
				g_autoptr(GPtrArray) specs = effect_pspecs (lrg_mixer_asset_lookup_effect_type (type));
				guint                m;

				for (m = 0; m < specs->len; m++)
				{
					GParamSpec *p = g_ptr_array_index (specs, m);

					if (lrg_mixer_asset_get_snapshot_param (a, snaps[i], buses[j], ids[k], p->name, &d))
					{
						g_autofree gchar *key = g_strdup_printf ("%s/%s/%s", buses[j], ids[k], p->name);

						json_object_set_double_member (ps, key, d);
					}
				}
			}
		}
		json_object_set_object_member (so, "volumes", vols);
		json_object_set_array_member (so, "sends", ss);
		json_object_set_object_member (so, "params", ps);
		json_array_add_object_element (arr, so);
	}
	json_object_set_array_member (o, "snapshots", arr);

	arr = json_array_new ();
	for (i = 0; i < lrg_mixer_asset_get_duck_count (a); i++)
	{
		JsonObject *d = json_object_new ();
		Duck        duck;

		get_duck (a, (gint) i, &duck);
		json_object_set_string_member (d, "trigger", duck.trigger);
		json_object_set_string_member (d, "target", duck.target);
		json_object_set_double_member (d, "threshold", duck.threshold);
		json_object_set_double_member (d, "depth", duck.depth);
		json_object_set_double_member (d, "attack", duck.attack);
		json_object_set_double_member (d, "release", duck.release);
		json_array_add_object_element (arr, d);
	}
	json_object_set_array_member (o, "ducking", arr);
	return obj_node (o);
}

/* Commits @c for an IPC method and returns the layout JSON. */
static JsonNode *
ipc_commit (LrgEditorMixerEditor  *ed,
            LrgMixerAsset         *c,
            const gchar           *label,
            GError               **error)
{
	g_autoptr(GError) local = NULL;

	if (!lrg_editor_mixer_editor_commit (ed, c, label, 0, &local))
	{
		failed (error, local, "The mixer refused the edit");
		return NULL;
	}
	return mixer_json (ed);
}

/* The common prologue of an editing method: the editor and a copy. */
#define IPC_EDIT_PROLOGUE                                                         \
	LrgEditorApp             *app = LRG_EDITOR_APP (user_data);               \
	LrgEditorMixerEditor     *ed = ipc_editor (app, params, error);           \
	g_autoptr(LrgMixerAsset)  c = NULL;                                       \
	if (ed == NULL)                                                           \
		return NULL;                                                      \
	c = lrg_mixer_asset_copy (ed->asset)

static JsonNode *
m_show (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	JsonObject   *o = json_object_new ();

	editor_app_show_panel (app, MIXER_PANEL_ID);
	json_object_set_string_member (o, "panel", MIXER_PANEL_ID);
	json_object_set_boolean_member (o, "shown", editor_app_panel_is_shown (app, MIXER_PANEL_ID));
	return obj_node (o);
}

static JsonNode *
m_open (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorApp         *app = LRG_EDITOR_APP (user_data);
	LrgEditorMixerEditor *ed = ipc_editor (app, params, error);
	JsonObject           *o;

	if (ed == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (ed)));
	json_object_set_string_member (o, "panel", lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (ed)));
	return obj_node (o);
}

/* Writes project.ryaml audio.mixer = @rel (NULL clears) and saves. */
static gboolean
set_project_mixer (LrgEditorApp  *app,
                   const gchar   *rel,
                   GError       **error)
{
	if (app->project == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No project is open");
		return FALSE;
	}
	lrg_project_set_audio_mixer (app->project, rel);
	return lrg_project_save (app->project, error);
}

static JsonNode *
m_create (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp             *app = LRG_EDITOR_APP (user_data);
	const gchar              *path = p_string (params, "path");
	g_autofree gchar         *abs = NULL;
	g_autofree gchar         *dir = NULL;
	g_autoptr(LrgMixerAsset)  mix = NULL;
	g_autoptr(GError)         local = NULL;
	gboolean                  set_default = FALSE;
	LrgEditorMixerEditor     *ed;

	if (path == NULL)
	{
		bad_params (error, "Missing \"path\"");
		return NULL;
	}
	abs = g_path_is_absolute (path) ? g_strdup (path) : editor_app_project_path (app, path);
	if (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s already exists", abs);
		return NULL;
	}
	mix = lrg_mixer_asset_new ();
	if (params != NULL && json_object_has_member (params, "buses"))
	{
		JsonNode *bn = json_object_get_member (params, "buses");
		guint     i;

		if (!JSON_NODE_HOLDS_ARRAY (bn))
		{
			bad_params (error, "\"buses\" must be an array of names");
			return NULL;
		}
		for (i = 0; i < json_array_get_length (json_node_get_array (bn)); i++)
		{
			const gchar *b = json_array_get_string_element (json_node_get_array (bn), i);

			if (b == NULL || !lrg_mixer_asset_add_bus (mix, b, NULL))
			{
				bad_params (error, "Bad or duplicate bus name in \"buses\"");
				return NULL;
			}
		}
	}
	dir = g_path_get_dirname (abs);
	g_mkdir_with_parents (dir, 0755);
	if (!lrg_mixer_asset_save_to_file (mix, abs, &local))
	{
		failed (error, local, "Cannot write the mixer");
		return NULL;
	}
	p_bool (params, "set_default", &set_default);
	if (set_default)
	{
		g_autofree gchar *rel = editor_app_relative_path (app, abs);

		if (!set_project_mixer (app, rel, &local))
		{
			failed (error, local, "Cannot save project.ryaml");
			return NULL;
		}
	}
	ed = lrg_editor_mixer_editor_find_for_path (app, abs, TRUE, &local);
	if (ed == NULL || ed->asset == NULL)
	{
		failed (error, local, "Cannot open the new mixer");
		return NULL;
	}
	return mixer_json (ed);
}

static JsonNode *
m_set_default (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	const gchar      *path = p_string (params, "path");
	g_autofree gchar *rel = NULL;
	g_autoptr(GError) local = NULL;
	JsonObject       *o;

	if (path == NULL && !p_is_null (params, "path"))
	{
		bad_params (error, "\"path\" must be a .rmixer path or null");
		return NULL;
	}
	if (path != NULL)
	{
		g_autofree gchar *abs = g_path_is_absolute (path) ? g_strdup (path) : editor_app_project_path (app, path);

		if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
		{
			not_found (error, "No such .rmixer file");
			return NULL;
		}
		rel = editor_app_relative_path (app, abs);
	}
	if (!set_project_mixer (app, rel, &local))
	{
		failed (error, local, "Cannot save project.ryaml");
		return NULL;
	}
	o = json_object_new ();
	if (rel != NULL)
		json_object_set_string_member (o, "mixer", rel);
	else
		json_object_set_null_member (o, "mixer");
	return obj_node (o);
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorMixerEditor *ed = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return ed != NULL ? mixer_json (ed) : NULL;
}

static JsonNode *
m_effect_types (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	g_auto(GStrv) types = lrg_mixer_asset_list_effect_types ();
	JsonObject   *o = json_object_new ();
	JsonArray    *arr = json_array_new ();
	guint         i, k;

	for (i = 0; types != NULL && types[i] != NULL; i++)
	{
		JsonObject          *t = json_object_new ();
		JsonArray           *pa = json_array_new ();
		GType                gt = lrg_mixer_asset_lookup_effect_type (types[i]);
		g_autoptr(GPtrArray) specs = effect_pspecs (gt);

		json_object_set_string_member (t, "name", types[i]);
		json_object_set_string_member (t, "gtype", g_type_name (gt));
		for (k = 0; k < specs->len; k++)
		{
			GParamSpec    *p = g_ptr_array_index (specs, k);
			JsonObject    *po = json_object_new ();
			g_auto(GValue) dv = G_VALUE_INIT;

			json_object_set_string_member (po, "name", p->name);
			json_object_set_string_member (po, "type", g_type_name (G_PARAM_SPEC_VALUE_TYPE (p)));
			g_value_init (&dv, G_PARAM_SPEC_VALUE_TYPE (p));
			g_param_value_set_default (p, &dv);
			json_object_set_member (po, "default", value_json (&dv));
			if (G_IS_PARAM_SPEC_FLOAT (p))
			{
				json_object_set_double_member (po, "min", G_PARAM_SPEC_FLOAT (p)->minimum);
				json_object_set_double_member (po, "max", G_PARAM_SPEC_FLOAT (p)->maximum);
			}
			else if (G_IS_PARAM_SPEC_DOUBLE (p))
			{
				json_object_set_double_member (po, "min", G_PARAM_SPEC_DOUBLE (p)->minimum);
				json_object_set_double_member (po, "max", G_PARAM_SPEC_DOUBLE (p)->maximum);
			}
			else if (G_IS_PARAM_SPEC_INT (p))
			{
				json_object_set_int_member (po, "min", G_PARAM_SPEC_INT (p)->minimum);
				json_object_set_int_member (po, "max", G_PARAM_SPEC_INT (p)->maximum);
			}
			else if (G_IS_PARAM_SPEC_UINT (p))
			{
				json_object_set_int_member (po, "min", G_PARAM_SPEC_UINT (p)->minimum);
				json_object_set_int_member (po, "max", G_PARAM_SPEC_UINT (p)->maximum);
			}
			else if (G_IS_PARAM_SPEC_ENUM (p))
			{
				JsonArray  *va = json_array_new ();
				GEnumClass *ec = G_PARAM_SPEC_ENUM (p)->enum_class;
				guint       e;

				for (e = 0; e < ec->n_values; e++)
					json_array_add_string_element (va, ec->values[e].value_nick);
				json_object_set_array_member (po, "values", va);
			}
			json_array_add_object_element (pa, po);
		}
		json_object_set_array_member (t, "params", pa);
		json_array_add_object_element (arr, t);
	}
	json_object_set_array_member (o, "types", arr);
	return obj_node (o);
}

static JsonNode *
m_add_bus (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	const gchar      *name = p_string (params, "name");
	g_autofree gchar *label = NULL;
	IPC_EDIT_PROLOGUE;

	if (name == NULL)
	{
		bad_params (error, "Missing \"name\"");
		return NULL;
	}
	if (!lrg_mixer_asset_add_bus (c, name, p_string (params, "parent")))
	{
		failed (error, NULL, "Cannot add the bus (name taken or invalid, or no such parent)");
		return NULL;
	}
	label = g_strdup_printf ("Add bus %s", name);
	return ipc_commit (ed, c, label, error);
}

static JsonNode *
m_remove_bus (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	const gchar      *name = p_string (params, "name");
	g_autofree gchar *label = NULL;
	IPC_EDIT_PROLOGUE;

	if (name == NULL)
	{
		bad_params (error, "Missing \"name\"");
		return NULL;
	}
	if (!lrg_mixer_asset_has_bus (c, name))
	{
		not_found (error, "No such bus");
		return NULL;
	}
	if (!lrg_mixer_asset_remove_bus (c, name))
	{
		failed (error, NULL, "Master and buses with child buses cannot be removed");
		return NULL;
	}
	label = g_strdup_printf ("Remove bus %s", name);
	return ipc_commit (ed, c, label, error);
}

static JsonNode *
m_set_bus (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	const gchar      *name = p_string (params, "name");
	const gchar      *parent = p_string (params, "parent");
	gdouble           vol;
	gboolean          b;
	g_autofree gchar *label = NULL;
	IPC_EDIT_PROLOGUE;

	if (name == NULL)
	{
		bad_params (error, "Missing \"name\"");
		return NULL;
	}
	if (!lrg_mixer_asset_has_bus (c, name))
	{
		not_found (error, "No such bus");
		return NULL;
	}
	if (parent != NULL && !lrg_mixer_asset_set_bus_parent (c, name, parent))
	{
		failed (error, NULL, "Cannot route the bus there (master, itself, below itself or unknown)");
		return NULL;
	}
	if (p_number (params, "volume", &vol))
		lrg_mixer_asset_set_bus_volume (c, name, vol);
	else if (p_is_null (params, "volume"))
		lrg_mixer_asset_set_bus_volume (c, name, LRG_AUDIO_SILENCE_DB);
	if (p_bool (params, "mute", &b))
		lrg_mixer_asset_set_bus_mute (c, name, b);
	if (p_bool (params, "solo", &b))
		lrg_mixer_asset_set_bus_solo (c, name, b);
	label = g_strdup_printf ("Edit bus %s", name);
	return ipc_commit (ed, c, label, error);
}

static JsonNode *
m_set_send (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	const gchar      *source = p_string (params, "source");
	const gchar      *target = p_string (params, "target");
	gdouble           level = 0.0;
	g_autofree gchar *label = NULL;
	IPC_EDIT_PROLOGUE;

	if (source == NULL || target == NULL || !p_number (params, "level", &level))
	{
		bad_params (error, "Needs \"source\", \"target\" and \"level\" (dB)");
		return NULL;
	}
	if (!lrg_mixer_asset_has_bus (c, source) || !lrg_mixer_asset_has_bus (c, target))
	{
		not_found (error, "No such bus");
		return NULL;
	}
	if (!lrg_mixer_asset_set_send (c, source, target, level))
	{
		failed (error, NULL, "Send refused (to itself, its parent, from master, or a loop)");
		return NULL;
	}
	label = g_strdup_printf ("Set send %s > %s", source, target);
	return ipc_commit (ed, c, label, error);
}

static JsonNode *
m_remove_send (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	const gchar *source = p_string (params, "source");
	const gchar *target = p_string (params, "target");
	IPC_EDIT_PROLOGUE;

	if (source == NULL || target == NULL)
	{
		bad_params (error, "Needs \"source\" and \"target\"");
		return NULL;
	}
	if (!lrg_mixer_asset_remove_send (c, source, target))
	{
		not_found (error, "No such send");
		return NULL;
	}
	return ipc_commit (ed, c, "Remove send", error);
}

static JsonNode *
m_add_effect (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	const gchar       *bus = p_string (params, "bus");
	const gchar       *type = p_string (params, "type");
	g_autoptr(GError)  local = NULL;
	g_autofree gchar  *label = NULL;
	IPC_EDIT_PROLOGUE;

	if (bus == NULL || type == NULL)
	{
		bad_params (error, "Needs \"bus\" and \"type\"");
		return NULL;
	}
	if (!lrg_mixer_asset_has_bus (c, bus))
	{
		not_found (error, "No such bus");
		return NULL;
	}
	if (!lrg_mixer_asset_add_effect (c, bus, type, p_string (params, "id"), &local))
	{
		failed (error, local, "Cannot add the effect");
		return NULL;
	}
	label = g_strdup_printf ("Add %s to %s", type, bus);
	return ipc_commit (ed, c, label, error);
}

static JsonNode *
m_remove_effect (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	const gchar *bus = p_string (params, "bus");
	const gchar *id = p_string (params, "id");
	IPC_EDIT_PROLOGUE;

	if (bus == NULL || id == NULL)
	{
		bad_params (error, "Needs \"bus\" and \"id\"");
		return NULL;
	}
	if (!lrg_mixer_asset_remove_effect (c, bus, id))
	{
		not_found (error, "No such effect");
		return NULL;
	}
	return ipc_commit (ed, c, "Remove effect", error);
}

static JsonNode *
m_move_effect (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	const gchar *bus = p_string (params, "bus");
	const gchar *id = p_string (params, "id");
	gdouble      index = 0.0;
	IPC_EDIT_PROLOGUE;

	if (bus == NULL || id == NULL || !p_number (params, "index", &index) || index < 0.0)
	{
		bad_params (error, "Needs \"bus\", \"id\" and \"index\"");
		return NULL;
	}
	if (!lrg_mixer_asset_move_effect (c, bus, id, (guint) index))
	{
		not_found (error, "No such effect");
		return NULL;
	}
	return ipc_commit (ed, c, "Reorder effects", error);
}

static JsonNode *
m_set_effect (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	const gchar *bus = p_string (params, "bus");
	const gchar *id = p_string (params, "id");
	gboolean     en;
	g_autofree gchar *label = NULL;
	IPC_EDIT_PROLOGUE;

	if (bus == NULL || id == NULL)
	{
		bad_params (error, "Needs \"bus\" and \"id\"");
		return NULL;
	}
	if (lrg_mixer_asset_get_effect_type (c, bus, id) == NULL)
	{
		not_found (error, "No such effect");
		return NULL;
	}
	if (p_bool (params, "enabled", &en))
		lrg_mixer_asset_set_effect_enabled (c, bus, id, en);
	if (params != NULL && json_object_has_member (params, "params"))
	{
		JsonNode   *pn = json_object_get_member (params, "params");
		JsonObject *po;
		GList      *members, *l;

		if (!JSON_NODE_HOLDS_OBJECT (pn))
		{
			bad_params (error, "\"params\" must be an object");
			return NULL;
		}
		po = json_node_get_object (pn);
		members = json_object_get_members (po);
		for (l = members; l != NULL; l = l->next)
		{
			const gchar       *pname = l->data;
			JsonNode          *v = json_object_get_member (po, pname);
			g_auto(GValue)     gv = G_VALUE_INIT;
			g_autoptr(GError)  local = NULL;

			if (JSON_NODE_HOLDS_NULL (v))
			{
				lrg_mixer_asset_unset_effect_param (c, bus, id, pname);
				continue;
			}
			if (!JSON_NODE_HOLDS_VALUE (v))
			{
				g_list_free (members);
				bad_params (error, "Parameter values must be numbers, booleans, strings or null");
				return NULL;
			}
			json_node_get_value (v, &gv);
			if (G_VALUE_HOLDS_INT64 (&gv))
			{
				gint64 i64 = g_value_get_int64 (&gv);

				g_value_unset (&gv);
				g_value_init (&gv, G_TYPE_DOUBLE);
				g_value_set_double (&gv, (gdouble) i64);
			}
			if (!lrg_mixer_asset_set_effect_param (c, bus, id, pname, &gv, &local))
			{
				g_list_free (members);
				failed (error, local, "Bad parameter");
				return NULL;
			}
		}
		g_list_free (members);
	}
	label = g_strdup_printf ("Edit %s", id);
	return ipc_commit (ed, c, label, error);
}

static JsonNode *
m_add_snapshot (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	const gchar       *name = p_string (params, "name");
	gdouble            blend = 0.25;
	gboolean           capture = FALSE;
	g_autoptr(GError)  local = NULL;
	IPC_EDIT_PROLOGUE;

	if (name == NULL)
	{
		bad_params (error, "Missing \"name\"");
		return NULL;
	}
	p_number (params, "blend", &blend);
	if (!lrg_mixer_asset_add_snapshot (c, name, blend))
	{
		failed (error, NULL, "Cannot add the snapshot (name taken or bad blend)");
		return NULL;
	}
	if (!lrg_editor_mixer_editor_commit (ed, c, "Add snapshot", 0, &local))
	{
		failed (error, local, "The mixer refused the edit");
		return NULL;
	}
	p_bool (params, "capture", &capture);
	if (capture && !lrg_editor_mixer_editor_capture_snapshot (ed, name, &local))
	{
		failed (error, local, "Cannot capture");
		return NULL;
	}
	lrg_editor_mixer_editor_select_snapshot (ed, name);
	return mixer_json (ed);
}

static JsonNode *
m_remove_snapshot (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	const gchar *name = p_string (params, "name");
	IPC_EDIT_PROLOGUE;

	if (name == NULL)
	{
		bad_params (error, "Missing \"name\"");
		return NULL;
	}
	if (!lrg_mixer_asset_remove_snapshot (c, name))
	{
		not_found (error, "No such snapshot");
		return NULL;
	}
	return ipc_commit (ed, c, "Remove snapshot", error);
}

static JsonNode *
m_set_snapshot (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	const gchar   *name = p_string (params, "name");
	g_auto(GStrv)  snaps = NULL;
	gdouble        d;
	gboolean       b;
	IPC_EDIT_PROLOGUE;

	snaps = lrg_mixer_asset_get_snapshot_names (c);
	if (name == NULL)
	{
		bad_params (error, "Missing \"name\"");
		return NULL;
	}
	if (!strv_has (snaps, name))
	{
		not_found (error, "No such snapshot");
		return NULL;
	}
	if (p_number (params, "blend", &d) && !lrg_mixer_asset_set_snapshot_blend (c, name, d))
	{
		bad_params (error, "\"blend\" must be 0 .. 3600 seconds");
		return NULL;
	}
	if (p_bool (params, "default", &b))
	{
		if (b)
			lrg_mixer_asset_set_default_snapshot (c, name);
		else if (g_strcmp0 (lrg_mixer_asset_get_default_snapshot (c), name) == 0)
			lrg_mixer_asset_set_default_snapshot (c, NULL);
	}
	if (params != NULL && json_object_has_member (params, "volumes"))
	{
		JsonNode *vn = json_object_get_member (params, "volumes");
		GList    *members, *l;

		if (!JSON_NODE_HOLDS_OBJECT (vn))
		{
			bad_params (error, "\"volumes\" must be an object bus -> dB|null");
			return NULL;
		}
		members = json_object_get_members (json_node_get_object (vn));
		for (l = members; l != NULL; l = l->next)
		{
			JsonNode *v = json_object_get_member (json_node_get_object (vn), l->data);
			gdouble   db = JSON_NODE_HOLDS_NULL (v) ? NAN : json_node_get_double (v);

			if (!lrg_mixer_asset_set_snapshot_volume (c, name, l->data, db))
			{
				g_list_free (members);
				not_found (error, "No such bus in \"volumes\"");
				return NULL;
			}
		}
		g_list_free (members);
	}
	if (params != NULL && json_object_has_member (params, "sends"))
	{
		JsonNode *sn = json_object_get_member (params, "sends");
		guint     i;

		if (!JSON_NODE_HOLDS_ARRAY (sn))
		{
			bad_params (error, "\"sends\" must be an array of {source, target, level|null}");
			return NULL;
		}
		for (i = 0; i < json_array_get_length (json_node_get_array (sn)); i++)
		{
			JsonObject  *so = json_array_get_object_element (json_node_get_array (sn), i);
			const gchar *src = so != NULL ? p_string (so, "source") : NULL;
			const gchar *tgt = so != NULL ? p_string (so, "target") : NULL;
			gdouble      lvl = NAN;

			if (src == NULL || tgt == NULL)
			{
				bad_params (error, "Each send needs \"source\" and \"target\"");
				return NULL;
			}
			p_number (so, "level", &lvl);
			if (!lrg_mixer_asset_set_snapshot_send (c, name, src, tgt, lvl))
			{
				not_found (error, "No such bus in \"sends\"");
				return NULL;
			}
		}
	}
	if (params != NULL && json_object_has_member (params, "params"))
	{
		JsonNode *pn = json_object_get_member (params, "params");
		GList    *members, *l;

		if (!JSON_NODE_HOLDS_OBJECT (pn))
		{
			bad_params (error, "\"params\" must be an object \"bus/effect/param\" -> number|null");
			return NULL;
		}
		members = json_object_get_members (json_node_get_object (pn));
		for (l = members; l != NULL; l = l->next)
		{
			JsonNode          *v = json_object_get_member (json_node_get_object (pn), l->data);
			g_auto(GStrv)      parts = g_strsplit (l->data, "/", 3);
			g_autoptr(GError)  local = NULL;
			gdouble            val = JSON_NODE_HOLDS_NULL (v) ? NAN : json_node_get_double (v);

			if (strv_len (parts) != 3 ||
			    !lrg_mixer_asset_set_snapshot_param (c, name, parts[0], parts[1], parts[2], val, &local))
			{
				g_list_free (members);
				failed (error, local, "Bad \"bus/effect/param\" key");
				return NULL;
			}
		}
		g_list_free (members);
	}
	return ipc_commit (ed, c, "Edit snapshot", error);
}

static JsonNode *
m_capture_snapshot (LrgEditorServer  *server,
                    JsonObject       *params,
                    gpointer          user_data,
                    GError          **error)
{
	LrgEditorApp         *app = LRG_EDITOR_APP (user_data);
	LrgEditorMixerEditor *ed = ipc_editor (app, params, error);
	const gchar          *name = p_string (params, "name");
	g_autoptr(GError)     local = NULL;

	if (ed == NULL)
		return NULL;
	if (name == NULL)
	{
		bad_params (error, "Missing \"name\"");
		return NULL;
	}
	if (!lrg_editor_mixer_editor_capture_snapshot (ed, name, &local))
	{
		failed (error, local, "Cannot capture");
		return NULL;
	}
	return mixer_json (ed);
}

static JsonNode *
m_set_duck (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	const gchar *trigger = p_string (params, "trigger");
	const gchar *target = p_string (params, "target");
	gdouble      threshold = -30.0, depth = -12.0, attack = 0.05, release = 0.5;
	guint        i;
	IPC_EDIT_PROLOGUE;

	if (trigger == NULL || target == NULL)
	{
		bad_params (error, "Needs \"trigger\" and \"target\"");
		return NULL;
	}
	/* an existing rule keeps the values not given */
	for (i = 0; i < lrg_mixer_asset_get_duck_count (c); i++)
	{
		Duck d;

		get_duck (c, (gint) i, &d);
		if (g_strcmp0 (d.trigger, trigger) == 0 && g_strcmp0 (d.target, target) == 0)
		{
			threshold = d.threshold;
			depth = d.depth;
			attack = d.attack;
			release = d.release;
		}
	}
	p_number (params, "threshold", &threshold);
	p_number (params, "depth", &depth);
	p_number (params, "attack", &attack);
	p_number (params, "release", &release);
	if (!lrg_mixer_asset_set_duck (c, trigger, target, threshold, depth, attack, release))
	{
		failed (error, NULL, "Ducking rule refused (unknown or same buses, master, a loop or values out of range)");
		return NULL;
	}
	return ipc_commit (ed, c, "Edit ducking rule", error);
}

static JsonNode *
m_remove_duck (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	const gchar *trigger = p_string (params, "trigger");
	const gchar *target = p_string (params, "target");
	IPC_EDIT_PROLOGUE;

	if (trigger == NULL || target == NULL)
	{
		bad_params (error, "Needs \"trigger\" and \"target\"");
		return NULL;
	}
	if (!lrg_mixer_asset_remove_duck (c, trigger, target))
	{
		not_found (error, "No such ducking rule");
		return NULL;
	}
	return ipc_commit (ed, c, "Remove ducking rule", error);
}

static JsonNode *
m_preview (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorApp         *app = LRG_EDITOR_APP (user_data);
	LrgEditorMixerEditor *ed = ipc_editor (app, params, error);
	gboolean              playing;
	gdouble               blend = -1.0;
	g_autoptr(GError)     local = NULL;
	const gchar          *source = "none";
	JsonObject           *o;

	if (ed == NULL)
		return NULL;
	if (p_bool (params, "playing", &playing))
		lrg_editor_mixer_editor_set_previewing (ed, playing);
	p_number (params, "blend", &blend);
	if (params != NULL && json_object_has_member (params, "snapshots"))
	{
		JsonNode         *sn = json_object_get_member (params, "snapshots");
		JsonNode         *wn = json_object_get_member (params, "weights");
		guint             n, i;
		g_autofree const gchar **names = NULL;
		g_autofree gdouble *weights = NULL;

		if (!JSON_NODE_HOLDS_ARRAY (sn) || wn == NULL || !JSON_NODE_HOLDS_ARRAY (wn) ||
		    json_array_get_length (json_node_get_array (sn)) != json_array_get_length (json_node_get_array (wn)))
		{
			bad_params (error, "\"snapshots\" and \"weights\" must be arrays of the same length");
			return NULL;
		}
		n = json_array_get_length (json_node_get_array (sn));
		names = g_new0 (const gchar *, n + 1);
		weights = g_new0 (gdouble, n + 1);
		for (i = 0; i < n; i++)
		{
			names[i] = json_array_get_string_element (json_node_get_array (sn), i);
			weights[i] = json_array_get_double_element (json_node_get_array (wn), i);
		}
		if (!lrg_editor_mixer_editor_preview_blend (ed, names, weights, n, MAX (blend, 0.0), &local))
		{
			failed (error, local, "Cannot blend");
			return NULL;
		}
	}
	else if (params != NULL && json_object_has_member (params, "snapshot"))
	{
		if (!lrg_editor_mixer_editor_preview_snapshot (ed, p_string (params, "snapshot"), blend, &local))
		{
			failed (error, local, "Cannot preview the snapshot");
			return NULL;
		}
	}
	sync_widgets (ed);
	lrg_editor_mixer_editor_get_meter_mixer (app, ed, &source);
	o = json_object_new ();
	json_object_set_boolean_member (o, "playing", ed->previewing);
	if (ed->preview_snapshot != NULL)
		json_object_set_string_member (o, "snapshot", ed->preview_snapshot);
	else
		json_object_set_null_member (o, "snapshot");
	json_object_set_string_member (o, "source", source);
	return obj_node (o);
}

static JsonNode *
m_meters (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp         *app = LRG_EDITOR_APP (user_data);
	LrgEditorMixerEditor *ed = NULL;
	g_autofree gchar     *path = NULL;
	LrgAudioMixer        *m;
	const gchar          *source = "none";
	JsonObject           *o = json_object_new ();
	JsonArray            *arr = json_array_new ();
	g_auto(GStrv)         buses = NULL;
	guint                 i;

	/* the editor of the given (or default) mixer, only when open */
	path = p_string (params, "path") != NULL ? ipc_path (app, params, NULL) : lrg_editor_mixer_editor_project_mixer_path (app);
	if (path != NULL)
		ed = lrg_editor_mixer_editor_find_for_path (app, path, FALSE, NULL);
	m = lrg_editor_mixer_editor_get_meter_mixer (app, ed, &source);
	if (ed != NULL && ed->asset != NULL)
		buses = lrg_mixer_asset_get_bus_names (ed->asset);
	for (i = 0; m != NULL && buses != NULL && buses[i] != NULL; i++)
	{
		JsonObject *b = json_object_new ();
		gdouble     peak = 0.0, rms = 0.0;

		if (!lrg_audio_mixer_get_bus_meter (m, buses[i], -1, &peak, &rms))
		{
			json_object_unref (b);
			continue;
		}
		json_object_set_string_member (b, "name", buses[i]);
		json_object_set_double_member (b, "peak", peak);
		json_object_set_double_member (b, "rms", rms);
		json_object_set_double_member (b, "peak_db", lrg_audio_gain_to_db (peak));
		json_object_set_double_member (b, "rms_db", lrg_audio_gain_to_db (rms));
		json_object_set_double_member (b, "duck_gain", lrg_audio_mixer_get_bus_duck_gain (m, buses[i]));
		json_object_set_boolean_member (b, "audible", lrg_audio_mixer_is_bus_audible (m, buses[i]));
		json_array_add_object_element (arr, b);
	}
	json_object_set_string_member (o, "source", source);
	json_object_set_array_member (o, "buses", arr);
	return obj_node (o);
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorApp         *app = LRG_EDITOR_APP (user_data);
	LrgEditorMixerEditor *ed = ipc_editor (app, params, error);
	g_autoptr(GError)     local = NULL;
	JsonObject           *o;

	if (ed == NULL)
		return NULL;
	if (!lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (ed), &local))
	{
		failed (error, local, "Cannot save");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (ed)));
	json_object_set_boolean_member (o, "saved", TRUE);
	return obj_node (o);
}

/* ==========================================================================
 * Actions and install
 * ========================================================================== */

/* "New Mixer": audio/main.rmixer with the usual buses, as the project's
 * default when it has none. */
static void
a_new_mixer (gpointer data)
{
	LrgEditorApp             *app = LRG_EDITOR_APP (data);
	g_autofree gchar         *abs = NULL;
	g_autofree gchar         *dir = NULL;
	g_autoptr(LrgMixerAsset)  mix = NULL;
	g_autoptr(GError)         error = NULL;
	g_autofree gchar         *existing = lrg_editor_mixer_editor_project_mixer_path (app);
	guint                     n = 2;

	if (lrg_editor_app_get_project_dir (app) == NULL)
	{
		editor_app_set_status (app, "Open a project first");
		return;
	}
	abs = editor_app_project_path (app, "audio/main.rmixer");
	while (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_autofree gchar *rel = g_strdup_printf ("audio/main-%u.rmixer", n++);

		g_free (abs);
		abs = editor_app_project_path (app, rel);
	}
	mix = lrg_mixer_asset_new ();
	lrg_mixer_asset_add_bus (mix, "music", NULL);
	lrg_mixer_asset_add_bus (mix, "sfx", NULL);
	lrg_mixer_asset_add_bus (mix, "dialogue", NULL);
	lrg_mixer_asset_add_bus (mix, "ambience", NULL);
	lrg_mixer_asset_set_bus_volume (mix, "music", -6.0);
	dir = g_path_get_dirname (abs);
	g_mkdir_with_parents (dir, 0755);
	if (!lrg_mixer_asset_save_to_file (mix, abs, &error))
	{
		editor_app_set_status (app, "New mixer: %s", error->message);
		return;
	}
	if (existing == NULL)
	{
		g_autofree gchar *rel = editor_app_relative_path (app, abs);

		if (!set_project_mixer (app, rel, &error))
			editor_app_set_status (app, "New mixer: %s", error->message);
	}
	if (lrg_editor_mixer_editor_find_for_path (app, abs, TRUE, &error) == NULL)
		editor_app_set_status (app, "New mixer: %s", error != NULL ? error->message : "cannot open");
}

static void
a_open_mixer (gpointer data)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (data);
	g_autofree gchar *path = lrg_editor_mixer_editor_project_mixer_path (app);
	g_autoptr(GError) error = NULL;

	if (path == NULL)
	{
		editor_app_set_status (app, "The project has no default mixer (Tools > New Mixer)");
		return;
	}
	if (lrg_editor_mixer_editor_find_for_path (app, path, TRUE, &error) == NULL)
		editor_app_set_status (app, "Mixer: %s", error != NULL ? error->message : "cannot open");
}

void
lrg_editor_mixer_editor_install (LrgEditorApp *app)
{
	LrgEditorServer  *server;
	LrgEditorActions *actions;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	if (g_object_get_data (G_OBJECT (app), INSTALL_KEY) != NULL)
		return;
	g_object_set_data (G_OBJECT (app), INSTALL_KEY, GINT_TO_POINTER (1));

	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "mixer.new", "New Mixer", "Audio", NULL, a_new_mixer, NULL, NULL, app);
	lrg_editor_actions_add (actions, "mixer.open", "Open Project Mixer", "Audio", NULL, a_open_mixer, NULL, NULL, app);

	server = lrg_editor_app_get_server (app);
	if (server == NULL)
		return;
	lrg_editor_server_register_method (server, "mixer.show", m_show, app, NULL);
	lrg_editor_server_register_method (server, "mixer.open", m_open, app, NULL);
	lrg_editor_server_register_method (server, "mixer.create", m_create, app, NULL);
	lrg_editor_server_register_method (server, "mixer.set_default", m_set_default, app, NULL);
	lrg_editor_server_register_method (server, "mixer.get", m_get, app, NULL);
	lrg_editor_server_register_method (server, "mixer.effect_types", m_effect_types, app, NULL);
	lrg_editor_server_register_method (server, "mixer.add_bus", m_add_bus, app, NULL);
	lrg_editor_server_register_method (server, "mixer.remove_bus", m_remove_bus, app, NULL);
	lrg_editor_server_register_method (server, "mixer.set_bus", m_set_bus, app, NULL);
	lrg_editor_server_register_method (server, "mixer.set_send", m_set_send, app, NULL);
	lrg_editor_server_register_method (server, "mixer.remove_send", m_remove_send, app, NULL);
	lrg_editor_server_register_method (server, "mixer.add_effect", m_add_effect, app, NULL);
	lrg_editor_server_register_method (server, "mixer.remove_effect", m_remove_effect, app, NULL);
	lrg_editor_server_register_method (server, "mixer.move_effect", m_move_effect, app, NULL);
	lrg_editor_server_register_method (server, "mixer.set_effect", m_set_effect, app, NULL);
	lrg_editor_server_register_method (server, "mixer.add_snapshot", m_add_snapshot, app, NULL);
	lrg_editor_server_register_method (server, "mixer.remove_snapshot", m_remove_snapshot, app, NULL);
	lrg_editor_server_register_method (server, "mixer.set_snapshot", m_set_snapshot, app, NULL);
	lrg_editor_server_register_method (server, "mixer.capture_snapshot", m_capture_snapshot, app, NULL);
	lrg_editor_server_register_method (server, "mixer.set_duck", m_set_duck, app, NULL);
	lrg_editor_server_register_method (server, "mixer.remove_duck", m_remove_duck, app, NULL);
	lrg_editor_server_register_method (server, "mixer.preview", m_preview, app, NULL);
	lrg_editor_server_register_method (server, "mixer.meters", m_meters, app, NULL);
	lrg_editor_server_register_method (server, "mixer.save", m_save, app, NULL);
}
