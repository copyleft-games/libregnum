/* lrg-editor-bone-map-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBoneMapEditor: the `.rbonemap` data editor (see the header).
 *
 * Layout:
 *
 *   +--------------------------------------+---------------------------+
 *   | Source [mocap.glb v]  Target [hero v]| RULES                     |
 *   | TARGET BONES            DRIVEN BY    |  ignore case  [x]         |
 *   |  Hips                  [Hips     v] = |  separators   [x]         |
 *   |    Spine               [Spine    v] ~ |  sides        [x]         |
 *   |      Head              [(none)   v] ! |  prefixes     mixamorig:  |
 *   |  ...                                 | [Auto map]                |
 *   |                                      | UNMAPPED  2 target, 1 src |
 *   +------------------+-------------------+---------------------------+
 *   |  source preview  |  target preview (live retarget)  [clip v]     |
 *   +------------------+-----------------------------------------------+
 *
 * The document is the truth: the explicit pairs live under `bones:`
 * (source: target), the rules under `rules:`, the editor's choices under
 * `editor:` (ignored by the engine). After every change the text is parsed
 * into an LrgBoneMap; with both skeletons an LrgRetargeter is made, which
 * the target preview runs every frame on the pose the source preview's
 * clip produced. Rows are rebuilt on the next step when the mapping
 * changes, never inside a combo's own signal.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-bone-map-editor.h"
#include "lrg-editor-anim-graph-editor.h"
#include "lrg-editor-material-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../lrg-editor-server.h"
#include "../../../animation/lrg-bone.h"
#include "../../../animation/lrg-gltf-animation.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-button.h"
#include "../../../ui/lrg-combo-box.h"
#include "../../../ui/lrg-scroll-box.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>

#define INSTALLED_KEY "lrg-editor-bonemap-installed"
#define NONE_LABEL    "(none)"

struct _LrgEditorBoneMapEditor
{
	LrgEditorDataEditor  parent_instance;

	LrgBoneMap          *map;
	gchar               *load_error;
	LrgRetargeter       *retargeter;
	gint                *mapping;        /* per target bone: source index or -1 */
	guint                n_mapping;

	LrgWidget           *src_view;
	LrgWidget           *tgt_view;
	LrgComboBox         *src_combo;
	LrgComboBox         *tgt_combo;
	LrgComboBox         *clip_combo;
	LrgWidget           *rows;
	LrgWidget           *unmapped;
	LrgPropertyGrid     *grid;
	LrgEditorDataSource *source;
	GPtrArray           *model_list;

	gboolean             syncing;
	gboolean             rows_stale;
	gboolean             combos_stale;
	gchar               *rows_sig;
};

G_DEFINE_FINAL_TYPE (LrgEditorBoneMapEditor, lrg_editor_bone_map_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static LrgEditorDocument *
doc_of (LrgEditorBoneMapEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorBoneMapEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

static gchar *
pj (const gchar *parent,
    const gchar *token)
{
	return lrg_editor_document_path_join (parent, token);
}

/* A YAML scalar for any string: quoted unless it is a plain identifier. */
static gchar *
yaml_str (const gchar *s)
{
	const gchar *p;
	GString     *out;
	gboolean     plain = s != NULL && *s != '\0' && (g_ascii_isalpha (*s) || *s == '_');

	for (p = s; plain && *p != '\0'; p++)
		if (!g_ascii_isalnum (*p) && *p != '_' && *p != '-' && *p != '.')
			plain = FALSE;
	if (plain && (g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "false") == 0 ||
	              g_ascii_strcasecmp (s, "yes") == 0 || g_ascii_strcasecmp (s, "no") == 0 ||
	              g_ascii_strcasecmp (s, "on") == 0 || g_ascii_strcasecmp (s, "off") == 0 ||
	              g_ascii_strcasecmp (s, "null") == 0))
		plain = FALSE;
	if (plain)
		return g_strdup (s);
	out = g_string_new ("\"");
	for (p = s != NULL ? s : ""; *p != '\0'; p++)
	{
		if (*p == '"' || *p == '\\')
			g_string_append_c (out, '\\');
		g_string_append_c (out, *p);
	}
	g_string_append_c (out, '"');
	return g_string_free (out, FALSE);
}

static gchar *
base_dir_of (LrgEditorBoneMapEditor *self)
{
	const gchar  *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	LrgEditorApp *app = app_of (self);

	if (path != NULL)
		return g_path_get_dirname (path);
	if (app != NULL && lrg_editor_app_get_project_dir (app) != NULL)
		return g_strdup (lrg_editor_app_get_project_dir (app));
	return g_get_current_dir ();
}

/* `editor/<key>` resolved against the bone map's folder, or NULL. */
static gchar *
editor_ref (LrgEditorBoneMapEditor *self,
            const gchar            *key)
{
	g_autofree gchar *p = pj ("/editor", key);
	const gchar      *ref = lrg_editor_document_get_scalar (doc_of (self), p);
	g_autofree gchar *dir = NULL;

	if (ref == NULL || *ref == '\0')
		return NULL;
	if (g_path_is_absolute (ref))
		return g_canonicalize_filename (ref, NULL);
	dir = base_dir_of (self);
	/* canonical: "chars/../anims/x.glb" must compare equal to the pickers */
	return g_canonicalize_filename (ref, dir);
}

static LrgSkeleton *
src_skeleton (LrgEditorBoneMapEditor *self)
{
	return editor_anim_view_get_skeleton (self->src_view);
}

static LrgSkeleton *
tgt_skeleton (LrgEditorBoneMapEditor *self)
{
	return editor_anim_view_get_skeleton (self->tgt_view);
}

/* ==========================================================================
 * The model
 * ========================================================================== */

/* Parses the map, resolves it and makes the preview's retargeter. */
static void
load_map (LrgEditorBoneMapEditor *self)
{
	g_autofree gchar     *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autoptr(GError)     error = NULL;
	g_autoptr(LrgBoneMap) map = lrg_bone_map_new_from_data (text, -1, &error);

	g_clear_pointer (&self->load_error, g_free);
	g_clear_object (&self->retargeter);
	g_clear_pointer (&self->mapping, g_free);
	self->n_mapping = 0;
	g_clear_object (&self->map);
	if (map == NULL)
	{
		self->load_error = g_strdup (error->message);
		return;
	}
	self->map = g_steal_pointer (&map);
	if (src_skeleton (self) != NULL && tgt_skeleton (self) != NULL)
	{
		self->mapping = lrg_bone_map_resolve (self->map, src_skeleton (self), tgt_skeleton (self), &self->n_mapping);
		self->retargeter = lrg_retargeter_new (src_skeleton (self), tgt_skeleton (self), self->map);
	}
}

/* Loads the models named under `editor:` into the two previews. */
static void
sync_models (LrgEditorBoneMapEditor *self)
{
	g_autofree gchar *src = editor_ref (self, "source");
	g_autofree gchar *tgt = editor_ref (self, "target");
	g_autofree gchar *clip = NULL;
	g_autofree gchar *cp = pj ("/editor", "clip");
	gboolean          changed = FALSE;

	if (g_strcmp0 (src, editor_anim_view_get_model (self->src_view)) != 0)
	{
		editor_anim_view_set_model (self->src_view, src != NULL && g_file_test (src, G_FILE_TEST_IS_REGULAR) ? src : NULL,
		                            NULL);
		changed = TRUE;
	}
	if (g_strcmp0 (tgt, editor_anim_view_get_model (self->tgt_view)) != 0)
	{
		editor_anim_view_set_model (self->tgt_view, tgt != NULL && g_file_test (tgt, G_FILE_TEST_IS_REGULAR) ? tgt : NULL,
		                            NULL);
		changed = TRUE;
	}
	/* the preview clip by name */
	clip = g_strdup (lrg_editor_document_get_scalar (doc_of (self), cp));
	{
		GPtrArray *clips = editor_anim_view_get_clips (self->src_view);
		gint       want = (clips != NULL && clips->len > 0) ? 0 : -1;
		guint      i;

		for (i = 0; clip != NULL && clips != NULL && i < clips->len; i++)
			if (g_strcmp0 (lrg_animation_clip_get_name (g_ptr_array_index (clips, i)), clip) == 0)
				want = (gint) i;
		if (want != editor_anim_view_get_clip (self->src_view))
		{
			editor_anim_view_set_clip (self->src_view, want);
			changed = TRUE;
		}
	}
	if (changed)
	{
		load_map (self);
		self->combos_stale = TRUE;
		self->rows_stale = TRUE;
	}
}

/* The target view follows the source skeleton through the retargeter. */
static void
pose_target (gpointer        user_data,
             LrgSkeleton    *skeleton,
             LrgSkinnedMesh *mesh,
             gfloat          dt)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (user_data);

	if (self->retargeter == NULL || !lrg_retargeter_retarget_skeleton (self->retargeter))
		lrg_skeleton_reset_to_bind (skeleton);
}

/* ==========================================================================
 * Rows: target bones with their drivers
 * ========================================================================== */

static guint
bone_depth (LrgSkeleton *skeleton,
            gint         index)
{
	guint depth = 0;
	gint  p = lrg_bone_get_parent_index (lrg_skeleton_get_bone (skeleton, index));

	while (p >= 0 && depth < 32)
	{
		depth++;
		p = lrg_bone_get_parent_index (lrg_skeleton_get_bone (skeleton, p));
	}
	return depth;
}

/* Bones in tree order (parents first, children right after). */
static void
tree_order (LrgSkeleton *skeleton,
            gint         parent,
            GArray      *out)
{
	guint n = lrg_skeleton_get_bone_count (skeleton);
	guint i;

	for (i = 0; i < n; i++)
		if (lrg_bone_get_parent_index (lrg_skeleton_get_bone (skeleton, (gint) i)) == parent)
		{
			gint idx = (gint) i;

			g_array_append_val (out, idx);
			tree_order (skeleton, idx, out);
		}
}

static gchar *
rows_signature (LrgEditorBoneMapEditor *self)
{
	GString *s = g_string_new (self->load_error);
	guint    i;

	g_string_append_printf (s, "|%s|%s|", editor_anim_view_get_model (self->src_view),
	                        editor_anim_view_get_model (self->tgt_view));
	for (i = 0; self->mapping != NULL && i < self->n_mapping; i++)
	{
		const gchar *t = lrg_bone_get_name (lrg_skeleton_get_bone (tgt_skeleton (self), (gint) i));

		g_string_append_printf (s, "%d%s,", self->mapping[i],
		                        self->map != NULL && lrg_bone_map_get_source (self->map, t) != NULL ? "e" : "");
	}
	return g_string_free (s, FALSE);
}

static void
on_row_combo (LrgComboBox *combo,
              gpointer     user_data)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (user_data);
	const gchar            *target = g_object_get_data (G_OBJECT (combo), "target");
	const gchar            *picked = lrg_combo_box_get_selected_text (combo);
	g_autoptr(GError)       error = NULL;

	if (self->syncing || target == NULL || picked == NULL)
		return;
	if (g_strcmp0 (picked, NONE_LABEL) == 0)
	{
		const gchar *explicit_src = self->map != NULL ? lrg_bone_map_get_source (self->map, target) : NULL;
		g_autofree gchar *dup = g_strdup (explicit_src);

		if (dup != NULL)
			lrg_editor_bone_map_editor_set_pair (self, dup, NULL, &error);
	}
	else if (!lrg_editor_bone_map_editor_set_pair (self, picked, target, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
rebuild_rows (LrgEditorBoneMapEditor *self)
{
	LrgTheme          *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	LrgSkeleton       *src = src_skeleton (self);
	LrgSkeleton       *tgt = tgt_skeleton (self);
	g_autoptr(GArray)  order = g_array_new (FALSE, FALSE, sizeof (gint));
	g_auto(GStrv)      un_t = NULL;
	g_auto(GStrv)      un_s = NULL;
	guint              i, n_src;

	self->rows_stale = FALSE;
	g_free (self->rows_sig);
	self->rows_sig = rows_signature (self);
	self->syncing = TRUE;
	lrg_container_remove_all (LRG_CONTAINER (self->rows));
	if (tgt == NULL || src == NULL)
	{
		LrgLabel *hint = lrg_label_new (self->load_error != NULL ? self->load_error
		                                : "Pick a source and a target model above to see the bones");

		lrg_widget_add_class (LRG_WIDGET (hint), "dim-label");
		lrg_container_add_child (LRG_CONTAINER (self->rows), LRG_WIDGET (hint));
		g_object_unref (hint);
		lrg_label_set_text (LRG_LABEL (self->unmapped), "");
		self->syncing = FALSE;
		return;
	}
	n_src = lrg_skeleton_get_bone_count (src);
	tree_order (tgt, -1, order);
	for (i = 0; i < order->len; i++)
	{
		gint              ti = g_array_index (order, gint, i);
		const gchar      *tname = lrg_bone_get_name (lrg_skeleton_get_bone (tgt, ti));
		gboolean          explicit_pair = FALSE;
		const gchar      *driver = lrg_editor_bone_map_editor_get_driver (self, tname, &explicit_pair);
		LrgWidget        *row = LRG_WIDGET (lrg_hbox_new ());
		g_autofree gchar *indent = g_strnfill (2 * bone_depth (tgt, ti), ' ');
		g_autofree gchar *label = g_strconcat (indent, tname, NULL);
		g_autofree gchar *rname = g_strdup_printf ("bonemap-row-%s", tname);
		g_autofree gchar *cname = g_strdup_printf ("bonemap-row-%s-source", tname);
		LrgLabel         *name_label = lrg_label_new (label);
		LrgLabel         *badge = lrg_label_new (driver == NULL ? "unmapped" : explicit_pair ? "pair" : "rule");
		LrgComboBox      *combo = lrg_combo_box_new ();
		guint             k;

		lrg_widget_set_name (row, rname);
		lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
		lrg_widget_set_fill_weight (LRG_WIDGET (name_label), 1.0f);
		lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (name_label));
		g_object_unref (name_label);
		lrg_widget_set_name (LRG_WIDGET (combo), cname);
		lrg_combo_box_append (combo, NONE_LABEL);
		for (k = 0; k < n_src; k++)
		{
			const gchar *sname = lrg_bone_get_name (lrg_skeleton_get_bone (src, (gint) k));

			lrg_combo_box_append (combo, sname);
			if (g_strcmp0 (sname, driver) == 0)
				lrg_combo_box_set_selected (combo, (gint) k + 1);
		}
		if (driver == NULL)
			lrg_combo_box_set_selected (combo, 0);
		g_object_set_data_full (G_OBJECT (combo), "target", g_strdup (tname), g_free);
		g_signal_connect (combo, "changed", G_CALLBACK (on_row_combo), self);
		lrg_widget_set_min_size (LRG_WIDGET (combo), 150.0f, 0.0f);
		lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (combo));
		g_object_unref (combo);
		lrg_label_set_color (badge, lrg_theme_get_color (theme, driver == NULL ? LRG_THEME_COLOR_WARNING
		                                                        : explicit_pair ? LRG_THEME_COLOR_ACCENT
		                                                                        : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_widget_set_min_size (LRG_WIDGET (badge), 64.0f, 0.0f);
		lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (badge));
		g_object_unref (badge);
		lrg_container_add_child (LRG_CONTAINER (self->rows), row);
		g_object_unref (row);
	}

	/* the summary under the rules */
	un_t = lrg_editor_bone_map_editor_get_unmapped_targets (self);
	un_s = lrg_editor_bone_map_editor_get_unmapped_sources (self);
	{
		g_autofree gchar *jt = g_strjoinv (", ", un_t);
		g_autofree gchar *js = g_strjoinv (", ", un_s);
		g_autofree gchar *text = g_strdup_printf ("\xe2\x9a\xa0 %u target bone(s) unmapped%s%s\n"
		                                          "%u source bone(s) drive nothing%s%s",
		                                          g_strv_length (un_t), un_t[0] != NULL ? ": " : "", jt,
		                                          g_strv_length (un_s), un_s[0] != NULL ? ": " : "", js);

		lrg_label_set_text (LRG_LABEL (self->unmapped), (un_t[0] == NULL && un_s[0] == NULL)
		                                                ? "Every bone is mapped" : text);
		lrg_label_set_color (LRG_LABEL (self->unmapped),
		                     lrg_theme_get_color (theme, un_t[0] != NULL ? LRG_THEME_COLOR_WARNING
		                                                                 : LRG_THEME_COLOR_SUCCESS));
	}
	self->syncing = FALSE;
}

/* ==========================================================================
 * Model and clip pickers
 * ========================================================================== */

static void
rebuild_combos (LrgEditorBoneMapEditor *self)
{
	LrgEditorApp *app = app_of (self);
	GPtrArray    *clips = editor_anim_view_get_clips (self->src_view);
	guint         i;
	gint          ss = 0, ts = 0;

	self->combos_stale = FALSE;
	g_clear_pointer (&self->model_list, g_ptr_array_unref);
	self->model_list = editor_anim_list_models (app);
	self->syncing = TRUE;
	lrg_combo_box_remove_all (self->src_combo);
	lrg_combo_box_remove_all (self->tgt_combo);
	lrg_combo_box_append (self->src_combo, "Source model\xe2\x80\xa6");
	lrg_combo_box_append (self->tgt_combo, "Target model\xe2\x80\xa6");
	for (i = 0; i < self->model_list->len; i++)
	{
		const gchar      *rel = g_ptr_array_index (self->model_list, i);
		g_autofree gchar *p = app != NULL ? editor_app_project_path (app, rel) : g_strdup (rel);
		g_autofree gchar *abs = g_canonicalize_filename (p, NULL);

		lrg_combo_box_append (self->src_combo, rel);
		lrg_combo_box_append (self->tgt_combo, rel);
		if (g_strcmp0 (abs, editor_anim_view_get_model (self->src_view)) == 0)
			ss = (gint) i + 1;
		if (g_strcmp0 (abs, editor_anim_view_get_model (self->tgt_view)) == 0)
			ts = (gint) i + 1;
	}
	lrg_combo_box_set_selected (self->src_combo, ss);
	lrg_combo_box_set_selected (self->tgt_combo, ts);
	lrg_combo_box_remove_all (self->clip_combo);
	for (i = 0; clips != NULL && i < clips->len; i++)
		lrg_combo_box_append (self->clip_combo, lrg_animation_clip_get_name (g_ptr_array_index (clips, i)));
	if (clips == NULL || clips->len == 0)
		lrg_combo_box_append (self->clip_combo, "(no clips)");
	lrg_combo_box_set_selected (self->clip_combo, MAX (editor_anim_view_get_clip (self->src_view), 0));
	self->syncing = FALSE;
}

static void
on_model_combo (LrgComboBox *combo,
                gpointer     user_data)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (user_data);
	gint                    i = lrg_combo_box_get_selected (combo);
	const gchar            *rel;
	g_autoptr(GError)       error = NULL;

	if (self->syncing || i <= 0 || self->model_list == NULL || (guint) i > self->model_list->len)
		return;
	rel = g_ptr_array_index (self->model_list, i - 1);
	if (!lrg_editor_bone_map_editor_set_models (self, combo == self->src_combo ? rel : NULL,
	                                            combo == self->tgt_combo ? rel : NULL, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_clip_combo (LrgComboBox *combo,
               gpointer     user_data)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (user_data);

	if (!self->syncing)
		lrg_editor_bone_map_editor_set_preview_clip (self, lrg_combo_box_get_selected_text (combo));
}

static void
on_auto_map (LrgButton *button,
             gpointer   user_data)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (user_data);
	guint                   n = lrg_editor_bone_map_editor_auto_fill (self);

	if (app_of (self) != NULL)
		editor_app_notify (app_of (self), EDITOR_NOTIFY_INFO, "Auto map added %u pair(s)", n);
}

/* ==========================================================================
 * Rules grid (strip_prefixes as one comma-separated field)
 * ========================================================================== */

static gboolean
source_read (LrgEditorDataSource *source,
             const gchar         *name,
             GValue              *value,
             gpointer             user_data)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (user_data);
	gint                    n, i;
	GString                *s;

	if (g_strcmp0 (name, "rules-prefixes") != 0)
		return FALSE;
	n = lrg_editor_document_get_length (doc_of (self), "/rules/strip_prefixes");
	s = g_string_new (NULL);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *ip = g_strdup_printf ("/rules/strip_prefixes/%d", i);

		g_string_append_printf (s, "%s%s", i > 0 ? ", " : "", lrg_editor_document_get_scalar (doc_of (self), ip));
	}
	g_value_take_string (value, g_string_free (s, FALSE));
	return TRUE;
}

/* Makes sure `rules:` is a mapping before a rule is written. */
static void
ensure_rules (LrgEditorBoneMapEditor *self)
{
	if (lrg_editor_document_get_kind (doc_of (self), "/rules") != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
	{
		if (lrg_editor_document_has_path (doc_of (self), "/rules"))
			lrg_editor_document_replace (doc_of (self), "/rules", "{}", NULL);
		else
			lrg_editor_document_add_key (doc_of (self), "", "rules", "{}", 1, NULL);
	}
}

static gboolean
source_write (LrgEditorDataSource *source,
              const gchar         *name,
              GValue              *value,
              gpointer             user_data)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (user_data);
	g_auto(GStrv)           parts = NULL;
	GString                *s;
	guint                   i, k = 0;

	ensure_rules (self);
	if (g_strcmp0 (name, "rules-prefixes") != 0)
		return TRUE;
	parts = g_strsplit (g_value_get_string (value) != NULL ? g_value_get_string (value) : "", ",", -1);
	s = g_string_new ("[");
	for (i = 0; parts[i] != NULL; i++)
	{
		g_autofree gchar *p = g_strstrip (g_strdup (parts[i]));
		g_autofree gchar *y = NULL;

		if (*p == '\0')
			continue;
		y = yaml_str (p);
		g_string_append_printf (s, "%s%s", k++ > 0 ? ", " : "", y);
	}
	g_string_append_c (s, ']');
	if (lrg_editor_document_has_path (doc_of (self), "/rules/strip_prefixes"))
	{
		if (k == 0)
			lrg_editor_document_remove (doc_of (self), "/rules/strip_prefixes", NULL);
		else
			lrg_editor_document_replace (doc_of (self), "/rules/strip_prefixes", s->str, NULL);
	}
	else if (k > 0)
		lrg_editor_document_add_key (doc_of (self), "/rules", "strip_prefixes", s->str, -1, NULL);
	g_string_free (s, TRUE);
	return FALSE;
}

/* ==========================================================================
 * LrgEditorDataEditor
 * ========================================================================== */

static void
bonemap_refresh (LrgEditorDataEditor *editor,
                 const gchar         *path)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (editor);
	g_autofree gchar       *sig = NULL;

	if (self->rows == NULL)
		return;
	sync_models (self);
	load_map (self);
	sig = rows_signature (self);
	if (g_strcmp0 (sig, self->rows_sig) != 0)
		self->rows_stale = TRUE;
	lrg_editor_data_source_document_changed (self->source, path);
	lrg_property_grid_refresh (self->grid);
}

static void
bonemap_validate (LrgEditorDataEditor *editor,
                  GPtrArray           *issues)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (editor);
	g_auto(GStrv)           un = NULL;
	guint                   i;

	if (self->load_error != NULL)
	{
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, "", 1, 1, self->load_error));
		return;
	}
	if (src_skeleton (self) == NULL || tgt_skeleton (self) == NULL)
		return;
	/* explicit pairs naming bones the skeletons do not have */
	{
		g_auto(GStrv) sources = lrg_bone_map_get_sources (self->map);

		for (i = 0; sources != NULL && sources[i] != NULL; i++)
		{
			const gchar      *t = lrg_bone_map_get_target (self->map, sources[i]);
			g_autofree gchar *path = pj ("/bones", sources[i]);
			guint             line = 0, col = 0;

			lrg_editor_document_get_line (doc_of (self), path, &line, &col);
			if (lrg_skeleton_get_bone_by_name (src_skeleton (self), sources[i]) == NULL)
			{
				g_autofree gchar *msg = g_strdup_printf ("source bone '%s' is not in the source skeleton", sources[i]);

				g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path, line, col,
				                                                        msg));
			}
			if (t != NULL && lrg_skeleton_get_bone_by_name (tgt_skeleton (self), t) == NULL)
			{
				g_autofree gchar *msg = g_strdup_printf ("target bone '%s' is not in the target skeleton", t);

				g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path, line, col,
				                                                        msg));
			}
		}
	}
	un = lrg_editor_bone_map_editor_get_unmapped_targets (self);
	for (i = 0; un[i] != NULL; i++)
	{
		g_autofree gchar *msg = g_strdup_printf ("target bone '%s' is unmapped (keeps its bind pose)", un[i]);

		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_INFO, "/bones", 0, 0, msg));
	}
}

static gchar *
bonemap_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (editor);
	g_auto(GStrv)           un = lrg_editor_bone_map_editor_get_unmapped_targets (self);

	return g_strdup_printf ("pairs=%u unmapped=%u", self->map != NULL ? lrg_bone_map_get_size (self->map) : 0u,
	                        g_strv_length (un));
}

static gboolean
lrg_editor_bone_map_editor_step (LrgWidget *widget,
                                 gfloat     delta_time)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (widget);
	gboolean                busy;

	/* the target follows the source: keep both views on one clock */
	editor_anim_view_set_playing (self->tgt_view, editor_anim_view_get_playing (self->src_view));
	busy = LRG_WIDGET_CLASS (lrg_editor_bone_map_editor_parent_class)->step (widget, delta_time);
	if (self->combos_stale)
		rebuild_combos (self);
	if (self->rows_stale)
		rebuild_rows (self);
	return busy;
}

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static LrgWidget *
heading (const gchar *text)
{
	LrgLabel *l = lrg_label_new (text);

	lrg_widget_add_class (LRG_WIDGET (l), "dim-label");
	return LRG_WIDGET (l);
}

static void
lrg_editor_bone_map_editor_constructed (GObject *object)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (object);
	LrgEditorDataEditor    *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorApp           *app;
	LrgWidget              *left, *pickers, *scroll, *right, *auto_btn, *bottom, *previews, *clipbar;
	LrgSplitter            *top, *outer;

	G_OBJECT_CLASS (lrg_editor_bone_map_editor_parent_class)->constructed (object);
	app = app_of (self);

	/* --- left: pickers and the target bone rows --- */
	left = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (left), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (left), 8.0f);
	pickers = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (pickers), 8.0f);
	self->src_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->src_combo), "bonemap-source");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->src_combo), 1.0f);
	g_signal_connect_object (self->src_combo, "changed", G_CALLBACK (on_model_combo), self, 0);
	lrg_container_add_child (LRG_CONTAINER (pickers), LRG_WIDGET (self->src_combo));
	g_object_unref (self->src_combo);
	self->tgt_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->tgt_combo), "bonemap-target");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->tgt_combo), 1.0f);
	g_signal_connect_object (self->tgt_combo, "changed", G_CALLBACK (on_model_combo), self, 0);
	lrg_container_add_child (LRG_CONTAINER (pickers), LRG_WIDGET (self->tgt_combo));
	g_object_unref (self->tgt_combo);
	add_to (left, pickers);
	add_to (left, heading ("TARGET BONE  \xe2\x86\x90  DRIVEN BY SOURCE BONE"));
	self->rows = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->rows, "bonemap-rows");
	lrg_container_set_spacing (LRG_CONTAINER (self->rows), 2.0f);
	scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (scroll, "bonemap-rows-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), self->rows);
	g_object_unref (self->rows);
	lrg_widget_set_fill_weight (scroll, 1.0f);
	add_to (left, scroll);

	/* --- right: rules, auto map, unmapped summary --- */
	right = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (right), 8.0f);
	lrg_container_set_padding (LRG_CONTAINER (right), 8.0f);
	add_to (right, heading ("NAME RULES"));
	self->source = lrg_editor_data_source_new (base);
	lrg_editor_data_source_set_read_func (self->source, source_read, self, NULL);
	lrg_editor_data_source_set_write_func (self->source, source_write, self, NULL);
	lrg_editor_data_source_add_field (self->source, "rules-case", "/rules/ignore_case", G_TYPE_BOOLEAN, "Ignore case",
	                                  NULL, NULL, "true");
	lrg_editor_data_source_add_field (self->source, "rules-separators", "/rules/normalize_separators", G_TYPE_BOOLEAN,
	                                  "Split words", NULL, NULL, "true");
	lrg_editor_data_source_add_field (self->source, "rules-sides", "/rules/normalize_sides", G_TYPE_BOOLEAN,
	                                  "Left / right", NULL, NULL, "true");
	lrg_editor_data_source_add_field (self->source, "rules-prefixes", "/rules/strip_prefixes", G_TYPE_STRING,
	                                  "Strip prefixes", NULL, NULL, "");
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "bonemap-rules");
	lrg_property_grid_set_label_width (self->grid, 110.0f);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_editor_data_source_bind_grid (self->source, self->grid);
	lrg_container_add_child (LRG_CONTAINER (right), LRG_WIDGET (self->grid));
	g_object_unref (self->grid);
	auto_btn = LRG_WIDGET (lrg_button_new ("Auto map from the rules"));
	lrg_widget_set_name (auto_btn, "bonemap-auto");
	g_signal_connect (auto_btn, "clicked", G_CALLBACK (on_auto_map), self);
	add_to (right, auto_btn);
	add_to (right, heading ("UNMAPPED"));
	self->unmapped = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->unmapped, "bonemap-unmapped");
	lrg_container_add_child (LRG_CONTAINER (right), self->unmapped);
	g_object_unref (self->unmapped);

	top = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (top), "bonemap-top");
	lrg_splitter_set_start_child (top, left);
	g_object_unref (left);
	lrg_splitter_set_end_child (top, right);
	g_object_unref (right);
	lrg_splitter_set_default_fraction (top, 0.62f);
	lrg_splitter_set_fraction (top, 0.62f, FALSE);
	lrg_splitter_set_min_sizes (top, 300.0f, 220.0f);

	/* --- bottom: source and target previews --- */
	bottom = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (bottom), 4.0f);
	previews = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (previews), 4.0f);
	lrg_widget_set_fill_weight (previews, 1.0f);
	self->src_view = editor_anim_view_new (app, "bonemap-source-preview");
	lrg_widget_set_fill_weight (self->src_view, 1.0f);
	editor_anim_view_set_show_bones (self->src_view, TRUE);
	lrg_container_add_child (LRG_CONTAINER (previews), self->src_view);
	g_object_unref (self->src_view);
	self->tgt_view = editor_anim_view_new (app, "bonemap-target-preview");
	lrg_widget_set_fill_weight (self->tgt_view, 1.0f);
	editor_anim_view_set_show_bones (self->tgt_view, TRUE);
	editor_anim_view_set_tint (self->tgt_view, 120, 180, 230);
	editor_anim_view_set_pose_func (self->tgt_view, pose_target, self);
	lrg_container_add_child (LRG_CONTAINER (previews), self->tgt_view);
	g_object_unref (self->tgt_view);
	add_to (bottom, previews);
	clipbar = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (clipbar), 8.0f);
	lrg_container_set_padding (LRG_CONTAINER (clipbar), 4.0f);
	add_to (clipbar, heading ("Preview clip"));
	self->clip_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->clip_combo), "bonemap-clip");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->clip_combo), 1.0f);
	g_signal_connect_object (self->clip_combo, "changed", G_CALLBACK (on_clip_combo), self, 0);
	lrg_container_add_child (LRG_CONTAINER (clipbar), LRG_WIDGET (self->clip_combo));
	g_object_unref (self->clip_combo);
	add_to (bottom, clipbar);

	outer = lrg_splitter_new (LRG_ORIENTATION_VERTICAL);
	lrg_widget_set_name (LRG_WIDGET (outer), "bonemap-split");
	lrg_splitter_set_start_child (outer, LRG_WIDGET (top));
	g_object_unref (top);
	lrg_splitter_set_end_child (outer, bottom);
	g_object_unref (bottom);
	lrg_splitter_set_default_fraction (outer, 0.56f);
	lrg_splitter_set_fraction (outer, 0.56f, FALSE);
	lrg_splitter_set_min_sizes (outer, 200.0f, 180.0f);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (outer));
	g_object_unref (outer);

	sync_models (self);
	load_map (self);
	rebuild_combos (self);
	rebuild_rows (self);
	if (app != NULL)
		lrg_editor_bone_map_editor_install (app);
}

static void
lrg_editor_bone_map_editor_dispose (GObject *object)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (object);

	if (self->tgt_view != NULL)
		editor_anim_view_set_pose_func (self->tgt_view, NULL, NULL);
	g_clear_object (&self->retargeter);
	G_OBJECT_CLASS (lrg_editor_bone_map_editor_parent_class)->dispose (object);
}

static void
lrg_editor_bone_map_editor_finalize (GObject *object)
{
	LrgEditorBoneMapEditor *self = LRG_EDITOR_BONE_MAP_EDITOR (object);

	g_clear_object (&self->map);
	g_clear_object (&self->source);
	g_clear_pointer (&self->model_list, g_ptr_array_unref);
	g_free (self->mapping);
	g_free (self->load_error);
	g_free (self->rows_sig);
	G_OBJECT_CLASS (lrg_editor_bone_map_editor_parent_class)->finalize (object);
}

static void
lrg_editor_bone_map_editor_class_init (LrgEditorBoneMapEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_bone_map_editor_constructed;
	object_class->dispose = lrg_editor_bone_map_editor_dispose;
	object_class->finalize = lrg_editor_bone_map_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_bone_map_editor_step;
	data_class->refresh = bonemap_refresh;
	data_class->validate = bonemap_validate;
	data_class->get_state = bonemap_get_state;
}

static void
lrg_editor_bone_map_editor_init (LrgEditorBoneMapEditor *self)
{
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorBoneMapEditor *
lrg_editor_bone_map_editor_new (LrgEditorApp      *app,
                                LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_BONE_MAP_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorBoneMapEditor *
lrg_editor_bone_map_editor_open (LrgEditorApp  *app,
                                 const gchar   *path,
                                 GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_BONE_MAP_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a bone map", path);
		return NULL;
	}
	return LRG_EDITOR_BONE_MAP_EDITOR (e);
}

gboolean
lrg_editor_bone_map_editor_create (LrgEditorApp  *app,
                                   const gchar   *path,
                                   const gchar   *source,
                                   const gchar   *target,
                                   gboolean       auto_fill,
                                   guint         *n_added,
                                   GError       **error)
{
	g_autofree gchar     *abs = NULL;
	g_autofree gchar     *dir = NULL;
	g_autofree gchar     *sabs = NULL;
	g_autofree gchar     *tabs = NULL;
	g_autoptr(LrgBoneMap) map = lrg_bone_map_new ();
	g_autofree gchar     *body = NULL;
	GString              *text;
	guint                 added = 0;

	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (n_added != NULL)
		*n_added = 0;
	abs = (app != NULL && !g_path_is_absolute (path)) ? editor_app_project_path (app, path) : g_strdup (path);
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
	if (source != NULL)
		sabs = (app != NULL && !g_path_is_absolute (source)) ? editor_app_project_path (app, source) : g_strdup (source);
	if (target != NULL)
		tabs = (app != NULL && !g_path_is_absolute (target)) ? editor_app_project_path (app, target) : g_strdup (target);

	/* the pairs: an auto fill between the two skeletons */
	if (auto_fill && sabs != NULL && tabs != NULL)
	{
		g_autoptr(LrgSkeleton) s = lrg_gltf_load_skeleton (sabs, 0, error);
		g_autoptr(LrgSkeleton) t = NULL;

		if (s == NULL)
			return FALSE;
		t = lrg_gltf_load_skeleton (tabs, 0, error);
		if (t == NULL)
			return FALSE;
		added = lrg_bone_map_auto_fill (map, s, t);
	}
	body = lrg_bone_map_to_data (map, error);
	if (body == NULL)
		return FALSE;
	text = g_string_new (body);
	if (strstr (body, "rules:") == NULL)
		g_string_append (text, "rules:\n  ignore_case: true\n  normalize_separators: true\n  normalize_sides: true\n");
	if (strstr (body, "bones:") == NULL)
		g_string_append (text, "bones: {}\n");
	if (sabs != NULL || tabs != NULL)
	{
		g_string_append (text, "editor:\n");
		if (sabs != NULL)
		{
			g_autofree gchar *rel = editor_mat_relative_to (dir, sabs);
			g_autofree gchar *y = yaml_str (rel);

			g_string_append_printf (text, "  source: %s\n", y);
		}
		if (tabs != NULL)
		{
			g_autofree gchar *rel = editor_mat_relative_to (dir, tabs);
			g_autofree gchar *y = yaml_str (rel);

			g_string_append_printf (text, "  target: %s\n", y);
		}
	}
	if (!g_file_set_contents (abs, text->str, -1, error))
	{
		g_string_free (text, TRUE);
		return FALSE;
	}
	g_string_free (text, TRUE);
	if (n_added != NULL)
		*n_added = added;
	return TRUE;
}

LrgBoneMap *
lrg_editor_bone_map_editor_get_map (LrgEditorBoneMapEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), NULL);
	return self->map;
}

/* Writes `editor/<key>` = @abs made relative to the bone map. */
static gboolean
write_editor_ref (LrgEditorBoneMapEditor  *self,
                  const gchar             *key,
                  const gchar             *value,
                  GError                 **error)
{
	g_autofree gchar *p = pj ("/editor", key);
	g_autofree gchar *y = yaml_str (value);

	if (lrg_editor_document_get_kind (doc_of (self), "/editor") != LRG_EDITOR_DOCUMENT_NODE_MAPPING &&
	    !lrg_editor_document_add_key (doc_of (self), "", "editor", "{}", -1, error))
		return FALSE;
	if (lrg_editor_document_has_path (doc_of (self), p))
		return lrg_editor_document_set_string (doc_of (self), p, value, 0, error);
	return lrg_editor_document_add_key (doc_of (self), "/editor", key, y, -1, error);
}

gboolean
lrg_editor_bone_map_editor_set_models (LrgEditorBoneMapEditor  *self,
                                       const gchar             *source,
                                       const gchar             *target,
                                       GError                 **error)
{
	LrgEditorApp     *app;
	g_autofree gchar *dir = NULL;
	gboolean          ok = TRUE;
	const gchar      *paths[2] = { source, target };
	const gchar      *keys[2] = { "source", "target" };
	guint             i;

	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), FALSE);
	app = app_of (self);
	dir = base_dir_of (self);
	lrg_editor_document_begin_group (doc_of (self), "Pick Skeletons");
	for (i = 0; ok && i < 2; i++)
	{
		g_autofree gchar *abs = NULL;
		g_autofree gchar *rel = NULL;

		if (paths[i] == NULL)
			continue;
		abs = (app != NULL && !g_path_is_absolute (paths[i])) ? editor_app_project_path (app, paths[i])
		                                                       : g_strdup (paths[i]);
		if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No model at %s", abs);
			ok = FALSE;
			break;
		}
		rel = editor_mat_relative_to (dir, abs);
		ok = write_editor_ref (self, keys[i], rel, error);
	}
	lrg_editor_document_end_group (doc_of (self));
	if (!ok)
		return FALSE;
	sync_models (self);
	self->combos_stale = TRUE;
	if ((source != NULL && src_skeleton (self) == NULL) || (target != NULL && tgt_skeleton (self) == NULL))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "The model has no skeleton (no skin)");
		return FALSE;
	}
	return TRUE;
}

const gchar *
lrg_editor_bone_map_editor_get_source_model (LrgEditorBoneMapEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), NULL);
	return editor_anim_view_get_model (self->src_view);
}

const gchar *
lrg_editor_bone_map_editor_get_target_model (LrgEditorBoneMapEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), NULL);
	return editor_anim_view_get_model (self->tgt_view);
}

LrgSkeleton *
lrg_editor_bone_map_editor_get_source_skeleton (LrgEditorBoneMapEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), NULL);
	return src_skeleton (self);
}

LrgSkeleton *
lrg_editor_bone_map_editor_get_target_skeleton (LrgEditorBoneMapEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), NULL);
	return tgt_skeleton (self);
}

/* Makes sure `bones:` is a mapping. */
static gboolean
ensure_bones (LrgEditorBoneMapEditor  *self,
              GError                 **error)
{
	if (lrg_editor_document_get_kind (doc_of (self), "/bones") == LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		return TRUE;
	if (lrg_editor_document_has_path (doc_of (self), "/bones"))
		return lrg_editor_document_replace (doc_of (self), "/bones", "{}", error);
	return lrg_editor_document_add_key (doc_of (self), "", "bones", "{}", -1, error);
}

gboolean
lrg_editor_bone_map_editor_set_pair (LrgEditorBoneMapEditor  *self,
                                     const gchar             *source,
                                     const gchar             *target,
                                     GError                 **error)
{
	g_auto(GStrv)     keys = NULL;
	g_autofree gchar *sp = NULL;
	gboolean          ok = TRUE;
	guint             i;

	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), FALSE);
	g_return_val_if_fail (source != NULL, FALSE);
	sp = pj ("/bones", source);
	lrg_editor_document_begin_group (doc_of (self), target != NULL ? "Map Bone" : "Unmap Bone");
	if (target == NULL)
	{
		if (lrg_editor_document_has_path (doc_of (self), sp))
			ok = lrg_editor_document_remove (doc_of (self), sp, error);
		lrg_editor_document_end_group (doc_of (self));
		return ok;
	}
	ok = ensure_bones (self, error);
	/* one driver per target bone: drop other explicit pairs onto it */
	keys = lrg_editor_document_get_keys (doc_of (self), "/bones");
	for (i = 0; ok && keys != NULL && keys[i] != NULL; i++)
	{
		g_autofree gchar *kp = pj ("/bones", keys[i]);

		if (g_strcmp0 (keys[i], source) != 0 &&
		    g_strcmp0 (lrg_editor_document_get_scalar (doc_of (self), kp), target) == 0)
			ok = lrg_editor_document_remove (doc_of (self), kp, error);
	}
	if (ok)
	{
		if (lrg_editor_document_has_path (doc_of (self), sp))
			ok = lrg_editor_document_set_string (doc_of (self), sp, target, 0, error);
		else
		{
			g_autofree gchar *y = yaml_str (target);

			ok = lrg_editor_document_add_key (doc_of (self), "/bones", source, y, -1, error);
		}
	}
	lrg_editor_document_end_group (doc_of (self));
	return ok;
}

guint
lrg_editor_bone_map_editor_auto_fill (LrgEditorBoneMapEditor *self)
{
	g_auto(GStrv)         before = NULL;
	g_auto(GStrv)         after = NULL;
	g_autoptr(LrgBoneMap) map = NULL;
	guint                 added = 0, i;

	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), 0);
	if (self->map == NULL || src_skeleton (self) == NULL || tgt_skeleton (self) == NULL)
		return 0;
	/* our own reference: every document write below replaces self->map */
	map = g_object_ref (self->map);
	before = lrg_bone_map_get_sources (map);
	lrg_bone_map_auto_fill (map, src_skeleton (self), tgt_skeleton (self));
	after = lrg_bone_map_get_sources (map);
	lrg_editor_document_begin_group (doc_of (self), "Auto Map");
	ensure_bones (self, NULL);
	for (i = 0; after != NULL && after[i] != NULL; i++)
	{
		const gchar      *t = lrg_bone_map_get_target (map, after[i]);
		g_autofree gchar *y = NULL;

		if (before != NULL && g_strv_contains ((const gchar * const *) before, after[i]))
			continue;
		y = yaml_str (t);
		if (lrg_editor_document_add_key (doc_of (self), "/bones", after[i], y, -1, NULL))
			added++;
	}
	lrg_editor_document_end_group (doc_of (self));
	return added;
}

const gchar *
lrg_editor_bone_map_editor_get_driver (LrgEditorBoneMapEditor *self,
                                       const gchar            *target,
                                       gboolean               *explicit_pair)
{
	LrgBone *bone;
	gint     idx, s;

	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), NULL);
	if (explicit_pair != NULL)
		*explicit_pair = FALSE;
	if (self->mapping == NULL || target == NULL)
		return NULL;
	bone = lrg_skeleton_get_bone_by_name (tgt_skeleton (self), target);
	if (bone == NULL)
		return NULL;
	idx = lrg_bone_get_index (bone);
	if (idx < 0 || (guint) idx >= self->n_mapping || (s = self->mapping[idx]) < 0)
		return NULL;
	if (explicit_pair != NULL)
		*explicit_pair = lrg_bone_map_get_source (self->map, target) != NULL;
	return lrg_bone_get_name (lrg_skeleton_get_bone (src_skeleton (self), s));
}

GStrv
lrg_editor_bone_map_editor_get_unmapped_targets (LrgEditorBoneMapEditor *self)
{
	GPtrArray *out = g_ptr_array_new ();
	guint      i;

	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), NULL);
	for (i = 0; self->mapping != NULL && i < self->n_mapping; i++)
		if (self->mapping[i] < 0)
			g_ptr_array_add (out, g_strdup (lrg_bone_get_name (lrg_skeleton_get_bone (tgt_skeleton (self), (gint) i))));
	g_ptr_array_add (out, NULL);
	return (GStrv) g_ptr_array_free (out, FALSE);
}

GStrv
lrg_editor_bone_map_editor_get_unmapped_sources (LrgEditorBoneMapEditor *self)
{
	GPtrArray *out = g_ptr_array_new ();
	guint      i, k, n;

	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), NULL);
	if (self->mapping != NULL && src_skeleton (self) != NULL)
	{
		n = lrg_skeleton_get_bone_count (src_skeleton (self));
		for (i = 0; i < n; i++)
		{
			gboolean used = FALSE;

			for (k = 0; k < self->n_mapping && !used; k++)
				used = self->mapping[k] == (gint) i;
			if (!used)
				g_ptr_array_add (out, g_strdup (lrg_bone_get_name (lrg_skeleton_get_bone (src_skeleton (self), (gint) i))));
		}
	}
	g_ptr_array_add (out, NULL);
	return (GStrv) g_ptr_array_free (out, FALSE);
}

LrgRetargeter *
lrg_editor_bone_map_editor_get_retargeter (LrgEditorBoneMapEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), NULL);
	return self->retargeter;
}

gboolean
lrg_editor_bone_map_editor_set_preview_clip (LrgEditorBoneMapEditor *self,
                                             const gchar            *clip)
{
	GPtrArray *clips;
	guint      i;

	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), FALSE);
	clips = editor_anim_view_get_clips (self->src_view);
	if (clips == NULL || clips->len == 0)
		return FALSE;
	if (clip == NULL)
		clip = lrg_animation_clip_get_name (g_ptr_array_index (clips, 0));
	for (i = 0; i < clips->len; i++)
	{
		if (g_strcmp0 (lrg_animation_clip_get_name (g_ptr_array_index (clips, i)), clip) != 0)
			continue;
		editor_anim_view_set_clip (self->src_view, (gint) i);
		lrg_editor_document_begin_group (doc_of (self), "Preview Clip");
		write_editor_ref (self, "clip", clip, NULL);
		lrg_editor_document_end_group (doc_of (self));
		return TRUE;
	}
	return FALSE;
}

const gchar *
lrg_editor_bone_map_editor_get_preview_clip (LrgEditorBoneMapEditor *self)
{
	GPtrArray *clips;
	gint       i;

	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), NULL);
	clips = editor_anim_view_get_clips (self->src_view);
	i = editor_anim_view_get_clip (self->src_view);
	return (clips != NULL && i >= 0 && (guint) i < clips->len)
	       ? lrg_animation_clip_get_name (g_ptr_array_index (clips, i)) : NULL;
}

void
lrg_editor_bone_map_editor_set_preview_time (LrgEditorBoneMapEditor *self,
                                             gfloat                  time)
{
	g_return_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self));
	lrg_editor_bone_map_editor_set_playing (self, FALSE);
	editor_anim_view_set_time (self->src_view, time);
	editor_anim_view_pose_now (self->tgt_view, 0.0f);
}

void
lrg_editor_bone_map_editor_set_playing (LrgEditorBoneMapEditor *self,
                                        gboolean                playing)
{
	g_return_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self));
	editor_anim_view_set_playing (self->src_view, playing);
	editor_anim_view_set_playing (self->tgt_view, playing);
}

gboolean
lrg_editor_bone_map_editor_get_playing (LrgEditorBoneMapEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BONE_MAP_EDITOR (self), FALSE);
	return editor_anim_view_get_playing (self->src_view);
}

/* ==========================================================================
 * IPC: bonemap.*
 * ========================================================================== */

static const gchar *
param_string (JsonObject  *params,
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
param_bool (JsonObject  *params,
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

static const gchar *
require_string (JsonObject   *params,
                const gchar  *name,
                GError      **error)
{
	const gchar *s = param_string (params, name);

	if (s == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing string parameter '%s'", name);
	return s;
}

static void
fail (GError       **error,
      const GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	                      g_error_matches (cause, G_FILE_ERROR, G_FILE_ERROR_NOENT)))
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause != NULL ? cause->message : "failed");
}

static JsonNode *
object_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static LrgEditorBoneMapEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar            *path = require_string (params, "path", error);
	LrgEditorBoneMapEditor *self;
	g_autoptr(GError)       err = NULL;

	if (path == NULL)
		return NULL;
	self = lrg_editor_bone_map_editor_open (app, path, &err);
	if (self == NULL)
		fail (error, err);
	return self;
}

static JsonArray *
strv_array (GStrv v)
{
	JsonArray *a = json_array_new ();
	guint      i;

	for (i = 0; v != NULL && v[i] != NULL; i++)
		json_array_add_string_element (a, v[i]);
	return a;
}

static JsonObject *
rules_json (LrgEditorBoneMapEditor *self)
{
	JsonObject  *r = json_object_new ();
	JsonArray   *a = json_array_new ();
	const gchar * const *p = self->map != NULL ? lrg_bone_map_get_strip_prefixes (self->map) : NULL;
	guint        i;

	json_object_set_boolean_member (r, "ignore_case", self->map == NULL || lrg_bone_map_get_ignore_case (self->map));
	json_object_set_boolean_member (r, "normalize_separators",
	                                self->map == NULL || lrg_bone_map_get_normalize_separators (self->map));
	json_object_set_boolean_member (r, "normalize_sides",
	                                self->map == NULL || lrg_bone_map_get_normalize_sides (self->map));
	for (i = 0; p != NULL && p[i] != NULL; i++)
		json_array_add_string_element (a, p[i]);
	json_object_set_array_member (r, "strip_prefixes", a);
	return r;
}

static JsonNode *
m_bm_open (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorBoneMapEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject             *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "panel", lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

static JsonNode *
m_bm_create (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = require_string (params, "path", error);
	g_autoptr(GError)  err = NULL;
	gboolean           auto_fill = FALSE;
	guint              added = 0;
	g_autofree gchar  *abs = NULL;
	JsonObject        *o;

	if (path == NULL)
		return NULL;
	param_bool (params, "auto_fill", &auto_fill);
	if (!lrg_editor_bone_map_editor_create (app, path, param_string (params, "source"), param_string (params, "target"),
	                                        auto_fill, &added, &err))
	{
		fail (error, err);
		return NULL;
	}
	abs = g_path_is_absolute (path) ? g_strdup (path) : editor_app_project_path (app, path);
	o = json_object_new ();
	json_object_set_string_member (o, "path", abs);
	json_object_set_int_member (o, "added", added);
	return object_node (o);
}

static JsonNode *
m_bm_get (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorBoneMapEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject             *o;
	JsonArray              *pairs, *mapping;
	g_auto(GStrv)           sources = NULL;
	g_auto(GStrv)           un_t = NULL;
	g_auto(GStrv)           un_s = NULL;
	guint                   i;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	if (self->load_error != NULL)
		json_object_set_string_member (o, "load_error", self->load_error);
	json_object_set_object_member (o, "rules", rules_json (self));
	pairs = json_array_new ();
	sources = self->map != NULL ? lrg_bone_map_get_sources (self->map) : NULL;
	for (i = 0; sources != NULL && sources[i] != NULL; i++)
	{
		JsonObject *p = json_object_new ();

		json_object_set_string_member (p, "source", sources[i]);
		json_object_set_string_member (p, "target", lrg_bone_map_get_target (self->map, sources[i]));
		json_array_add_object_element (pairs, p);
	}
	json_object_set_array_member (o, "pairs", pairs);
	json_object_set_string_member (o, "source_model", lrg_editor_bone_map_editor_get_source_model (self));
	json_object_set_string_member (o, "target_model", lrg_editor_bone_map_editor_get_target_model (self));
	json_object_set_string_member (o, "clip", lrg_editor_bone_map_editor_get_preview_clip (self));
	mapping = json_array_new ();
	for (i = 0; self->mapping != NULL && i < self->n_mapping; i++)
	{
		JsonObject  *m = json_object_new ();
		const gchar *t = lrg_bone_get_name (lrg_skeleton_get_bone (tgt_skeleton (self), (gint) i));
		gboolean     exp = FALSE;
		const gchar *s = lrg_editor_bone_map_editor_get_driver (self, t, &exp);

		json_object_set_string_member (m, "target", t);
		if (s != NULL)
			json_object_set_string_member (m, "source", s);
		else
			json_object_set_null_member (m, "source");
		json_object_set_boolean_member (m, "explicit", exp);
		json_array_add_object_element (mapping, m);
	}
	json_object_set_array_member (o, "mapping", mapping);
	un_t = lrg_editor_bone_map_editor_get_unmapped_targets (self);
	un_s = lrg_editor_bone_map_editor_get_unmapped_sources (self);
	json_object_set_array_member (o, "unmapped_target", strv_array (un_t));
	json_object_set_array_member (o, "unmapped_source", strv_array (un_s));
	return object_node (o);
}

static JsonNode *
m_bm_set (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorBoneMapEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar            *source;
	const gchar            *target;
	g_autoptr(GError)       err = NULL;
	JsonObject             *o;

	if (self == NULL || (source = require_string (params, "source", error)) == NULL)
		return NULL;
	target = param_string (params, "target");
	if (target == NULL && !(json_object_has_member (params, "target") &&
	                        JSON_NODE_HOLDS_NULL (json_object_get_member (params, "target"))))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "'target' must be a bone name or null");
		return NULL;
	}
	if (!lrg_editor_bone_map_editor_set_pair (self, source, target, &err))
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "source", source);
	if (target != NULL)
		json_object_set_string_member (o, "target", target);
	else
		json_object_set_null_member (o, "target");
	return object_node (o);
}

static JsonNode *
m_bm_auto_fill (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorBoneMapEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject             *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_int_member (o, "added", lrg_editor_bone_map_editor_auto_fill (self));
	return object_node (o);
}

static JsonNode *
m_bm_set_rules (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorBoneMapEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	static const gchar * const flags[] = { "ignore_case", "normalize_separators", "normalize_sides" };
	JsonObject             *o;
	guint                   i;

	if (self == NULL)
		return NULL;
	lrg_editor_document_begin_group (doc_of (self), "Edit Rules");
	ensure_rules (self);
	for (i = 0; i < G_N_ELEMENTS (flags); i++)
	{
		gboolean          v;
		g_autofree gchar *p = pj ("/rules", flags[i]);

		if (!param_bool (params, flags[i], &v))
			continue;
		if (lrg_editor_document_has_path (doc_of (self), p))
			lrg_editor_document_set_boolean (doc_of (self), p, v, 0, NULL);
		else
			lrg_editor_document_add_key (doc_of (self), "/rules", flags[i], v ? "true" : "false", -1, NULL);
	}
	if (json_object_has_member (params, "strip_prefixes") &&
	    JSON_NODE_HOLDS_ARRAY (json_object_get_member (params, "strip_prefixes")))
	{
		JsonArray *a = json_object_get_array_member (params, "strip_prefixes");
		GString   *s = g_string_new (NULL);
		GValue     v = G_VALUE_INIT;

		for (i = 0; i < json_array_get_length (a); i++)
			g_string_append_printf (s, "%s%s", i > 0 ? "," : "", json_array_get_string_element (a, i));
		g_value_init (&v, G_TYPE_STRING);
		g_value_take_string (&v, g_string_free (s, FALSE));
		source_write (self->source, "rules-prefixes", &v, self);
		g_value_unset (&v);
	}
	lrg_editor_document_end_group (doc_of (self));
	o = json_object_new ();
	json_object_set_object_member (o, "rules", rules_json (self));
	return object_node (o);
}

static JsonNode *
m_bm_set_models (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorBoneMapEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	JsonObject             *o;

	if (self == NULL)
		return NULL;
	if ((param_string (params, "source") != NULL || param_string (params, "target") != NULL) &&
	    !lrg_editor_bone_map_editor_set_models (self, param_string (params, "source"), param_string (params, "target"),
	                                            &err))
	{
		fail (error, err);
		return NULL;
	}
	if (param_string (params, "clip") != NULL &&
	    !lrg_editor_bone_map_editor_set_preview_clip (self, param_string (params, "clip")))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "The source model has no clip named '%s'", param_string (params, "clip"));
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "source_model", lrg_editor_bone_map_editor_get_source_model (self));
	json_object_set_string_member (o, "target_model", lrg_editor_bone_map_editor_get_target_model (self));
	json_object_set_string_member (o, "clip", lrg_editor_bone_map_editor_get_preview_clip (self));
	return object_node (o);
}

static JsonNode *
m_bm_preview (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorBoneMapEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	gboolean                play;
	JsonObject             *o;

	if (self == NULL)
		return NULL;
	if (param_string (params, "clip") != NULL &&
	    !lrg_editor_bone_map_editor_set_preview_clip (self, param_string (params, "clip")))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "The source model has no clip named '%s'", param_string (params, "clip"));
		return NULL;
	}
	if (json_object_has_member (params, "time"))
		lrg_editor_bone_map_editor_set_preview_time (self, (gfloat) json_object_get_double_member (params, "time"));
	if (param_bool (params, "play", &play))
		lrg_editor_bone_map_editor_set_playing (self, play);
	o = json_object_new ();
	json_object_set_string_member (o, "clip", lrg_editor_bone_map_editor_get_preview_clip (self));
	json_object_set_double_member (o, "time", editor_anim_view_get_time (self->src_view));
	json_object_set_boolean_member (o, "playing", lrg_editor_bone_map_editor_get_playing (self));
	return object_node (o);
}

static JsonNode *
m_bm_save (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorBoneMapEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	JsonObject             *o;

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
lrg_editor_bone_map_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "bonemap.open", m_bm_open },
		{ "bonemap.create", m_bm_create },
		{ "bonemap.get", m_bm_get },
		{ "bonemap.set", m_bm_set },
		{ "bonemap.auto_fill", m_bm_auto_fill },
		{ "bonemap.set_rules", m_bm_set_rules },
		{ "bonemap.set_models", m_bm_set_models },
		{ "bonemap.preview", m_bm_preview },
		{ "bonemap.save", m_bm_save }
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
	lrg_editor_data_editor_install (app);
	for (i = 0; i < G_N_ELEMENTS (methods); i++)
		lrg_editor_server_register_method (server, methods[i].name, methods[i].func, app, NULL);
}
