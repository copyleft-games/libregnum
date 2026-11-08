/* lrg-editor-skeleton2d-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSkeleton2DEditor: the 2D rigging and animation editor of
 * lrgeditor (".rskel2d", see the header).
 *
 * Model. The document (YAML) is the source of truth. After every change
 * the editor loads it into a working LrgSkeleton2D (self->skel) with
 * lrg_skeleton2d_new_from_data() and keeps a "canonical" document of what
 * the engine would write for it (self->canon). An edit is made one of two
 * ways and always ends in apply_text():
 *
 *  - on the working skeleton through the engine API (bones, slots,
 *    attachments, meshes, weights, clips and keys), then serialised with
 *    lrg_skeleton2d_save_to_data();
 *  - on a scratch copy of the canonical document for what the engine has
 *    no API for (removing or renaming bones and slots, removing skins,
 *    moving keys in time with their Bézier control points).
 *
 * apply_text() diffs the canonical document against the new text node by
 * node and replays only the differences on the real document inside one
 * undo group: unchanged sections keep their comments and formatting, and
 * one user gesture is one undo step. A drag edits the working skeleton
 * live and commits once at release; Escape reloads from the document.
 *
 * Canvas. The rig is drawn into the UI paint list with a paint callback
 * (immediate-mode rlgl, skinned meshes with their textures) and the
 * overlays (grid, bones, joints, wireframes, vertices, weight heat,
 * brush, hints) with paint primitives, so everything is themed and
 * recorded headless. The skeleton's root transform IS the view transform
 * (translate to the canvas origin, scale by the zoom): world positions
 * and deformed vertices come out in canvas coordinates, and picking is
 * lrg_skeleton2d_pick_bone() at the pointer.
 *
 * Animation. In animation mode the selected clip poses the rig at the
 * playhead; dragging a bone on the canvas keys its translate or rotate
 * offset at the playhead (auto-key). The dope sheet is an LrgTimelineView
 * (a group track per bone or slot, a child track per channel, a keyframe
 * per key, events as markers); its playback drives the preview. The
 * curve view is an LrgCurveEdit of the first value of the selected
 * timeline over the clip's normalised time; easings map to the engine's
 * curves (linear, stepped and Bézier presets).
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-skeleton2d-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../../graphics/lrg-skinned-sprite.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-property-source.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-timeline-view.h"
#include "../../../ui/lrg-curve-edit.h"
#include "../../../ui/lrg-drop-target.h"
#include "../../../ui/lrg-drag.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../tween/lrg-keyframe-curve.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#define INSTALLED_KEY       "lrg-editor-skeleton2d-installed"
#define ASSET_MIME          "application/x-lrg-asset"
#define DEG                 (180.0f / (gfloat) G_PI)
#define RAD                 ((gfloat) G_PI / 180.0f)
#define JOINT_RADIUS        (7.0f)    /* canvas px: a press this close to a joint moves the bone */
#define PICK_RADIUS         (8.0f)    /* canvas px: bone picking tolerance */
#define VERTEX_RADIUS       (6.0f)    /* canvas px: vertex handle */
#define CLICK_SLOP          (4.0f)    /* canvas px: a press that moved less is a click */
#define ZOOM_MIN            (0.05f)
#define ZOOM_MAX            (40.0f)
#define OUTLINE_ROW         (22.0f)
#define OUTLINE_HEADER      (26.0f)
#define DOPE_HEIGHT         (230.0f)
#define MOD_MASK            (LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL | \
                             LRG_INPUT_MODIFIER_ALT | LRG_INPUT_MODIFIER_SUPER)

/* ==========================================================================
 * Enums
 * ========================================================================== */

GType
lrg_editor_skeleton2d_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_SKELETON2D_MODE_SETUP, "LRG_EDITOR_SKELETON2D_MODE_SETUP", "setup" },
			{ LRG_EDITOR_SKELETON2D_MODE_ANIMATE, "LRG_EDITOR_SKELETON2D_MODE_ANIMATE", "animate" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorSkeleton2DMode"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_editor_skeleton2d_tool_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_SKELETON2D_TOOL_SELECT, "LRG_EDITOR_SKELETON2D_TOOL_SELECT", "select" },
			{ LRG_EDITOR_SKELETON2D_TOOL_BONE, "LRG_EDITOR_SKELETON2D_TOOL_BONE", "bone" },
			{ LRG_EDITOR_SKELETON2D_TOOL_MESH, "LRG_EDITOR_SKELETON2D_TOOL_MESH", "mesh" },
			{ LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS, "LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS", "weights" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorSkeleton2DTool"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

static const gchar * const mode_nicks[] = { "setup", "animate" };
static const gchar * const tool_nicks[] = { "select", "bone", "mesh", "weights" };
static const gchar * const channel_nicks[] = { "translate", "rotate", "scale", "color", "attachment" };

/* ==========================================================================
 * Instance
 * ========================================================================== */

/* What a canvas drag is doing */
typedef enum
{
	DRAG_NONE,
	DRAG_PAN,          /* middle button: pan the view */
	DRAG_PENDING,      /* select tool pressed on empty space */
	DRAG_MOVE_BONE,    /* select tool on a joint */
	DRAG_ROTATE_BONE,  /* select tool on a bone body */
	DRAG_CREATE_BONE,  /* bone tool */
	DRAG_VERTEX,       /* mesh tool on a vertex */
	DRAG_PAINT         /* weight brush stroke */
} DragKind;

/* A dope-sheet keyframe: which key of the selected clip it shows */
typedef struct
{
	gchar               *target;
	LrgSkeleton2DChannel channel;
	guint                index;
} KeyRef;

/* Outline rows */
typedef enum
{
	ROW_HEADER,
	ROW_BONE,
	ROW_SLOT,
	ROW_ATTACHMENT,
	ROW_SKIN,
	ROW_CLIP
} RowKind;

typedef struct
{
	RowKind  kind;
	gint     index;   /* bone/slot/skin/clip index; slot of an attachment */
	gint     depth;   /* indentation */
	gchar   *label;
	gchar   *extra;   /* attachment name, header id */
} OutlineRow;

struct _LrgEditorSkeleton2DEditor
{
	LrgEditorDataEditor      parent_instance;

	/* model */
	LrgSkeleton2D           *skel;          /* working skeleton (NULL while unparsable) */
	LrgEditorDocument       *canon;         /* what the engine writes for the document */
	gchar                   *parse_error;
	GHashTable              *textures;      /* absolute image path -> GrlTexture */
	gboolean                 committing;    /* inside apply_text(): ignore refreshes */
	gboolean                 live;          /* the working skeleton differs from the document (a drag) */

	/* selection and view state (names survive reloads) */
	gchar                   *sel_bone;
	gchar                   *sel_slot;
	gchar                   *sel_attachment;
	gchar                   *skin;          /* active skin, NULL = default */
	gchar                   *clip;          /* selected clip */
	gint                     sel_vertex;
	LrgEditorSkeleton2DMode  mode;
	LrgEditorSkeleton2DTool  tool;
	gfloat                   zoom;
	gfloat                   pan_x;
	gfloat                   pan_y;
	gboolean                 framed;        /* the view was fitted once */
	gboolean                 user_view;     /* the user panned or zoomed: no more auto-fitting */
	gfloat                   time;
	gboolean                 playing;
	gboolean                 loop;
	gfloat                   brush_radius;  /* setup units */
	gfloat                   brush_strength;

	/* canvas interaction */
	DragKind                 drag;
	gfloat                   press_x;
	gfloat                   press_y;
	gfloat                   ptr_x;
	gfloat                   ptr_y;
	gboolean                 ptr_inside;
	gfloat                   grab_dx;       /* joint grab offset (canvas px) */
	gfloat                   grab_dy;
	gfloat                   grab_angle;    /* pointer angle at the press (degrees) */
	gfloat                   grab_rot;      /* bone local rotation at the press */
	gboolean                 erase;
	gboolean                 drop_hover;
	gchar                   *drop_label;
	gint                     hover_bone;

	/* widgets */
	LrgWidget               *canvas;
	LrgWidget               *outline;
	LrgScrollBox            *outline_scroll;
	LrgPropertyGrid         *grid;
	LrgEditorDataSource     *source;
	gchar                   *grid_sig;
	gboolean                 grid_stale;
	LrgWidget               *mode_chips[2];
	LrgWidget               *tool_chips[4];
	LrgWidget               *weights_chip;
	LrgWidget               *triangulate_chip;
	LrgWidget               *anim_box;      /* transport + dope sheet + curves */
	LrgWidget               *canvas_bar;    /* modes, tools, mesh commands */
	LrgWidget               *side_bar;      /* add / order / delete */
	LrgWidget               *play_chip;
	LrgWidget               *loop_chip;
	LrgWidget               *key_chip;
	LrgTimelineView         *dope;
	LrgCurveEdit            *curves;
	LrgSplitter             *main_split;    /* canvas | side bar */
	LrgSplitter             *anim_split;    /* upper | dope sheet */
	GPtrArray               *rows;          /* OutlineRow */

	/* dope sheet state */
	GHashTable              *keyrefs;       /* keyframe id -> KeyRef */
	gboolean                 syncing;       /* rebuilding the dope sheet: ignore its signals */
	GArray                  *moved_keys;    /* keyframe ids moved by a drag, applied in step() */
	GArray                  *marker_ids;    /* dope-sheet markers (events, draw-order keys) */
	gboolean                 dope_stale;
	gchar                   *curve_target;  /* the timeline the curve view shows */
	LrgSkeleton2DChannel     curve_channel;
	LrgEasingType           *curve_easings; /* easing shown per key (to detect changes) */
	guint                    n_curve_easings;
};

G_DEFINE_FINAL_TYPE (LrgEditorSkeleton2DEditor, lrg_editor_skeleton2d_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void reload (LrgEditorSkeleton2DEditor *self);
static void sync_widgets (LrgEditorSkeleton2DEditor *self);
static void rebuild_outline (LrgEditorSkeleton2DEditor *self);
static void rebuild_dope (LrgEditorSkeleton2DEditor *self);
static void sync_curve (LrgEditorSkeleton2DEditor *self);
static void pose (LrgEditorSkeleton2DEditor *self);
static gboolean rig_in_view (LrgEditorSkeleton2DEditor *self, gfloat x, gfloat y, gfloat w, gfloat h);

static LrgEditorDocument *
doc_of (LrgEditorSkeleton2DEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorSkeleton2DEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

/* The folder images are relative to: the rig's own folder. */
static gchar *
rig_dir (LrgEditorSkeleton2DEditor *self)
{
	const gchar *path = lrg_editor_document_get_path (doc_of (self));

	if (path != NULL)
		return g_path_get_dirname (path);
	if (app_of (self) != NULL && lrg_editor_app_get_project_dir (app_of (self)) != NULL)
		return g_strdup (lrg_editor_app_get_project_dir (app_of (self)));
	return g_get_current_dir ();
}

static void
set_error (GError      **error,
           gint          code,
           const gchar  *format,
           ...) G_GNUC_PRINTF (3, 4);

static void
set_error (GError      **error,
           gint          code,
           const gchar  *format,
           ...)
{
	va_list           args;
	g_autofree gchar *msg = NULL;

	va_start (args, format);
	msg = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, code, msg);
}

/* Whether a skeleton is loaded; sets @error otherwise. */
static gboolean
require_skel (LrgEditorSkeleton2DEditor  *self,
              GError                    **error)
{
	if (self->skel != NULL && self->parse_error == NULL)
		return TRUE;
	set_error (error, LRG_EDITOR_DOCUMENT_ERROR_PARSE, "the rig does not load: %s",
	           self->parse_error != NULL ? self->parse_error : "unknown error");
	return FALSE;
}

static gboolean
bone_ok (LrgEditorSkeleton2DEditor  *self,
         gint                        bone,
         GError                    **error)
{
	if (!require_skel (self, error))
		return FALSE;
	if (bone < 0 || bone >= (gint) lrg_skeleton2d_get_bone_count (self->skel))
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no bone %d", bone);
		return FALSE;
	}
	return TRUE;
}

static gboolean
slot_ok (LrgEditorSkeleton2DEditor  *self,
         gint                        slot,
         GError                    **error)
{
	if (!require_skel (self, error))
		return FALSE;
	if (slot < 0 || slot >= (gint) lrg_skeleton2d_get_slot_count (self->skel))
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no slot %d", slot);
		return FALSE;
	}
	return TRUE;
}

static gint
sel_bone_index (LrgEditorSkeleton2DEditor *self)
{
	return (self->skel != NULL && self->sel_bone != NULL) ? lrg_skeleton2d_find_bone (self->skel, self->sel_bone)
	                                                      : -1;
}

static gint
sel_slot_index (LrgEditorSkeleton2DEditor *self)
{
	return (self->skel != NULL && self->sel_slot != NULL) ? lrg_skeleton2d_find_slot (self->skel, self->sel_slot)
	                                                      : -1;
}

static LrgSkeleton2DClip *
current_clip (LrgEditorSkeleton2DEditor *self)
{
	return (self->skel != NULL && self->clip != NULL) ? lrg_skeleton2d_get_clip (self->skel, self->clip) : NULL;
}

static gint
clip_index (LrgEditorSkeleton2DEditor *self,
            const gchar               *name)
{
	guint i;

	for (i = 0; self->skel != NULL && i < lrg_skeleton2d_get_clip_count (self->skel); i++)
		if (g_strcmp0 (lrg_skeleton2d_clip_get_name (lrg_skeleton2d_get_clip_at (self->skel, i)), name) == 0)
			return (gint) i;
	return -1;
}

/* The selected attachment and the skin it lives in (the active skin,
 * else "default"); NULL when nothing is selected. */
static LrgSkinnedSprite *
sel_sprite (LrgEditorSkeleton2DEditor  *self,
            const gchar               **skin_out)
{
	gint              slot = sel_slot_index (self);
	LrgSkinnedSprite *s;

	if (skin_out != NULL)
		*skin_out = NULL;
	if (slot < 0 || self->sel_attachment == NULL)
		return NULL;
	if (self->skin != NULL)
	{
		s = lrg_skeleton2d_get_skin_attachment (self->skel, self->skin, slot, self->sel_attachment);
		if (s != NULL)
		{
			if (skin_out != NULL)
				*skin_out = self->skin;
			return s;
		}
	}
	s = lrg_skeleton2d_get_skin_attachment (self->skel, LRG_SKELETON2D_DEFAULT_SKIN, slot, self->sel_attachment);
	if (s != NULL && skin_out != NULL)
		*skin_out = LRG_SKELETON2D_DEFAULT_SKIN;
	return s;
}

static gboolean
require_sprite (LrgEditorSkeleton2DEditor  *self,
                LrgSkinnedSprite          **sprite,
                GError                    **error)
{
	if (!require_skel (self, error))
		return FALSE;
	*sprite = sel_sprite (self, NULL);
	if (*sprite == NULL)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no attachment selected");
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Committing: canonical diff applied to the document
 * ========================================================================== */

static gboolean
same_yaml (LrgEditorDocument *a,
           LrgEditorDocument *b,
           const gchar       *path)
{
	g_autofree gchar *ya = lrg_editor_document_get_yaml (a, path);
	g_autofree gchar *yb = lrg_editor_document_get_yaml (b, path);

	return g_strcmp0 (ya, yb) == 0;
}

/*
 * Replays the difference between @old (canonical) and @neu at @path on
 * the real document @real. Mappings recurse per key (removed keys are
 * removed, new keys appended), sequences recurse per item when the item
 * count is unchanged, take a pure append or a single removal as such,
 * and anything else replaces the node.
 */
static gboolean
diff_apply (LrgEditorDocument  *real,
            LrgEditorDocument  *old,
            LrgEditorDocument  *neu,
            const gchar        *path,
            GError            **error)
{
	LrgEditorDocumentNodeKind ko = lrg_editor_document_get_kind (old, path);
	LrgEditorDocumentNodeKind kn = lrg_editor_document_get_kind (neu, path);
	LrgEditorDocumentNodeKind kr = lrg_editor_document_get_kind (real, path);

	if (same_yaml (old, neu, path))
		return TRUE;

	/* mappings: per key */
	if (ko == LRG_EDITOR_DOCUMENT_NODE_MAPPING && kn == ko && kr == ko)
	{
		g_auto(GStrv) okeys = lrg_editor_document_get_keys (old, path);
		g_auto(GStrv) nkeys = lrg_editor_document_get_keys (neu, path);
		guint         i;

		for (i = 0; okeys != NULL && okeys[i] != NULL; i++)
		{
			g_autofree gchar *p = lrg_editor_document_path_join (path, okeys[i]);

			if (lrg_editor_document_has_path (neu, p) || !lrg_editor_document_has_path (real, p))
				continue;
			if (!lrg_editor_document_remove (real, p, error))
				return FALSE;
		}
		for (i = 0; nkeys != NULL && nkeys[i] != NULL; i++)
		{
			g_autofree gchar *p = lrg_editor_document_path_join (path, nkeys[i]);

			if (!lrg_editor_document_has_path (real, p))
			{
				g_autofree gchar *y = lrg_editor_document_get_yaml (neu, p);

				if (!lrg_editor_document_replace (real, p, y, error))
					return FALSE;
				continue;
			}
			if (!diff_apply (real, old, neu, p, error))
				return FALSE;
		}
		return TRUE;
	}

	/* sequences: per item, an append, or one removal */
	if (ko == LRG_EDITOR_DOCUMENT_NODE_SEQUENCE && kn == ko && kr == ko &&
	    lrg_editor_document_get_length (real, path) == lrg_editor_document_get_length (old, path))
	{
		gint lo = lrg_editor_document_get_length (old, path);
		gint ln = lrg_editor_document_get_length (neu, path);
		gint i;

		if (lo == ln)
		{
			for (i = 0; i < lo; i++)
			{
				g_autofree gchar *idx = g_strdup_printf ("%d", i);
				g_autofree gchar *p = lrg_editor_document_path_join (path, idx);

				if (!diff_apply (real, old, neu, p, error))
					return FALSE;
			}
			return TRUE;
		}
		if (ln > lo)
		{
			gboolean prefix = TRUE;

			for (i = 0; i < lo && prefix; i++)
			{
				g_autofree gchar *idx = g_strdup_printf ("%d", i);
				g_autofree gchar *p = lrg_editor_document_path_join (path, idx);

				prefix = same_yaml (old, neu, p);
			}
			if (prefix)
			{
				for (i = lo; i < ln; i++)
				{
					g_autofree gchar *idx = g_strdup_printf ("%d", i);
					g_autofree gchar *p = lrg_editor_document_path_join (path, idx);
					g_autofree gchar *y = lrg_editor_document_get_yaml (neu, p);

					if (!lrg_editor_document_insert_item (real, path, -1, y, error))
						return FALSE;
				}
				return TRUE;
			}
		}
		if (ln == lo - 1)
		{
			gint gap = -1, j;

			/* the first item that differs is the removed one if the rest shifted */
			for (i = 0; i < lo && gap < 0; i++)
			{
				g_autofree gchar *a = g_strdup_printf ("%s/%d", path, i);

				if (i >= ln || !same_yaml (old, neu, a))
					gap = i;
			}
			for (j = gap + 1; gap >= 0 && j < lo; j++)
			{
				g_autofree gchar *a = g_strdup_printf ("%s/%d", path, j);
				g_autofree gchar *b = g_strdup_printf ("%s/%d", path, j - 1);
				g_autofree gchar *ya = lrg_editor_document_get_yaml (old, a);
				g_autofree gchar *yb = lrg_editor_document_get_yaml (neu, b);

				if (g_strcmp0 (ya, yb) != 0)
					gap = -1;
			}
			if (gap >= 0)
			{
				g_autofree gchar *p = g_strdup_printf ("%s/%d", path, gap);

				return lrg_editor_document_remove (real, p, error);
			}
		}
	}

	/* anything else: the node goes */
	if (kn == LRG_EDITOR_DOCUMENT_NODE_MISSING)
		return (*path == '\0' || !lrg_editor_document_has_path (real, path))
		       ? TRUE : lrg_editor_document_remove (real, path, error);
	{
		g_autofree gchar *y = lrg_editor_document_get_yaml (neu, path);

		return lrg_editor_document_replace (real, path, y, error);
	}
}

/*
 * Applies @text (a whole .rskel2d) to the document as one undo step
 * labelled @label. The text must load (validated first, so a broken
 * edit never reaches the document). Reloads the working skeleton.
 */
static gboolean
apply_text (LrgEditorSkeleton2DEditor  *self,
            const gchar                *label,
            const gchar                *text,
            GError                    **error)
{
	LrgEditorDocument           *doc = doc_of (self);
	g_autoptr(LrgSkeleton2D)     probe = NULL;
	g_autoptr(LrgEditorDocument) neu = NULL;
	g_autoptr(GError)            local = NULL;
	gboolean                     ok;

	probe = lrg_skeleton2d_new_from_data (text, -1, &local);
	if (probe == NULL)
	{
		g_propagate_error (error, g_steal_pointer (&local));
		self->live = FALSE;
		reload (self);
		return FALSE;
	}
	neu = lrg_editor_document_new_from_data (text, -1, LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, error);
	if (neu == NULL)
		return FALSE;

	self->committing = TRUE;
	lrg_editor_document_begin_group (doc, label);
	if (self->canon != NULL)
		ok = diff_apply (doc, self->canon, neu, "", error);
	else
		ok = lrg_editor_document_replace (doc, "", text, error);
	lrg_editor_document_end_group (doc);
	self->committing = FALSE;
	self->live = FALSE;
	reload (self);
	return ok;
}

/* Serialises the working skeleton and applies it. */
static gboolean
commit (LrgEditorSkeleton2DEditor  *self,
        const gchar                *label,
        GError                    **error)
{
	g_autofree gchar *text = NULL;

	if (!require_skel (self, error))
		return FALSE;
	text = lrg_skeleton2d_save_to_data (self->skel, NULL);
	return apply_text (self, label, text, error);
}

/* Reports an edit failure on the canvas (toast). */
static void
report (LrgEditorSkeleton2DEditor *self,
        const GError              *error)
{
	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* A scratch copy of the canonical document for YAML surgery. */
static LrgEditorDocument *
scratch_doc (LrgEditorSkeleton2DEditor *self)
{
	g_autofree gchar *text = NULL;

	if (self->canon == NULL)
		return NULL;
	text = lrg_editor_document_to_data (self->canon, NULL);
	return lrg_editor_document_new_from_data (text, -1, LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, NULL);
}

static gboolean
apply_scratch (LrgEditorSkeleton2DEditor  *self,
               const gchar                *label,
               LrgEditorDocument          *scratch,
               GError                    **error)
{
	g_autofree gchar *text = lrg_editor_document_to_data (scratch, NULL);

	return apply_text (self, label, text, error);
}

/* ==========================================================================
 * Loading the working skeleton
 * ========================================================================== */

/* Gives every attachment with an image its texture: shared per file and
 * cached across reloads; only with a window. */
static void
attach_textures (LrgEditorSkeleton2DEditor *self)
{
	g_autofree gchar *dir = NULL;
	guint             s, k, n_skins;

	if (self->skel == NULL || !IsWindowReady ())
		return;
	dir = rig_dir (self);
	n_skins = lrg_skeleton2d_get_skin_count (self->skel);
	for (k = 0; k < n_skins; k++)
	{
		const gchar *skin = lrg_skeleton2d_get_skin_name (self->skel, k);

		for (s = 0; s < lrg_skeleton2d_get_slot_count (self->skel); s++)
		{
			g_auto(GStrv) names = lrg_skeleton2d_list_attachments (self->skel, skin, (gint) s);
			guint         i;

			for (i = 0; names != NULL && names[i] != NULL; i++)
			{
				LrgSkinnedSprite *sp = lrg_skeleton2d_get_skin_attachment (self->skel, skin, (gint) s, names[i]);
				const gchar      *image = lrg_skinned_sprite_get_image (sp);
				g_autofree gchar *abs = NULL;
				GrlTexture       *tex;

				if (image == NULL || *image == '\0' || lrg_skinned_sprite_get_texture (sp) != NULL)
					continue;
				abs = g_path_is_absolute (image) ? g_strdup (image) : g_build_filename (dir, image, NULL);
				tex = g_hash_table_lookup (self->textures, abs);
				if (tex == NULL)
				{
					g_autoptr(GError) error = NULL;

					if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR) ||
					    !lrg_skinned_sprite_load_texture (sp, dir, &error))
						continue;
					tex = lrg_skinned_sprite_get_texture (sp);
					if (tex != NULL)
						g_hash_table_insert (self->textures, g_strdup (abs), g_object_ref (tex));
					continue;
				}
				lrg_skinned_sprite_set_texture (sp, tex);
			}
		}
	}
}

/* Keeps the selection pointing at things that exist. */
static void
validate_selection (LrgEditorSkeleton2DEditor *self)
{
	if (self->skel == NULL)
		return;
	if (self->sel_bone != NULL && lrg_skeleton2d_find_bone (self->skel, self->sel_bone) < 0)
		g_clear_pointer (&self->sel_bone, g_free);
	if (self->sel_slot != NULL && lrg_skeleton2d_find_slot (self->skel, self->sel_slot) < 0)
		g_clear_pointer (&self->sel_slot, g_free);
	if (self->sel_attachment != NULL && sel_sprite (self, NULL) == NULL)
		g_clear_pointer (&self->sel_attachment, g_free);
	if (self->clip != NULL && lrg_skeleton2d_get_clip (self->skel, self->clip) == NULL)
		g_clear_pointer (&self->clip, g_free);
	if (self->skin != NULL && !lrg_skeleton2d_set_skin (self->skel, self->skin))
		g_clear_pointer (&self->skin, g_free);
	if (self->skin == NULL)
		lrg_skeleton2d_set_skin (self->skel, NULL);
	if (self->sel_attachment == NULL)
		self->sel_vertex = -1;
	{
		LrgSkinnedSprite *sp = sel_sprite (self, NULL);

		if (sp == NULL || self->sel_vertex >= (gint) lrg_skinned_sprite_get_vertex_count (sp))
			self->sel_vertex = -1;
	}
}

/* Loads the document into the working skeleton and refreshes the canonical
 * copy, the textures, the selection and every view. */
static void
reload (LrgEditorSkeleton2DEditor *self)
{
	g_autofree gchar         *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autoptr(GError)         error = NULL;
	LrgSkeleton2D            *skel;

	skel = lrg_skeleton2d_new_from_data (text, -1, &error);
	g_clear_pointer (&self->parse_error, g_free);
	if (skel == NULL)
	{
		/* keep the last good rig on screen, report why this one fails */
		self->parse_error = g_strdup (error->message);
		g_clear_object (&self->canon);
	}
	else
	{
		g_autofree gchar *dir = rig_dir (self);
		g_autofree gchar *canon = NULL;

		g_clear_object (&self->skel);
		self->skel = skel;
		lrg_skeleton2d_set_base_dir (self->skel, dir);
		canon = lrg_skeleton2d_save_to_data (self->skel, NULL);
		g_clear_object (&self->canon);
		self->canon = lrg_editor_document_new_from_data (canon, -1, LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, NULL);
		attach_textures (self);
		validate_selection (self);
	}
	self->live = FALSE;
	sync_widgets (self);
}

/* ==========================================================================
 * View and pose
 * ========================================================================== */

/* The canvas point the setup origin is drawn at. */
static void
view_origin (LrgEditorSkeleton2DEditor *self,
             gfloat                    *ox,
             gfloat                    *oy)
{
	gfloat w = 0.0f, h = 0.0f, x = 0.0f, y = 0.0f;

	if (self->canvas != NULL)
	{
		x = lrg_widget_get_world_x (self->canvas);
		y = lrg_widget_get_world_y (self->canvas);
		w = lrg_widget_get_width (self->canvas);
		h = lrg_widget_get_height (self->canvas);
	}
	*ox = x + w * 0.5f + self->pan_x;
	*oy = y + h * 0.5f + self->pan_y;
}

/*
 * Poses the working skeleton for display: the setup pose, or the selected
 * clip at the playhead in animation mode; the root transform is the view
 * so world transforms and deformed meshes are in canvas coordinates.
 */
static void
pose (LrgEditorSkeleton2DEditor *self)
{
	LrgSkeleton2DClip *clip = current_clip (self);
	gfloat             ox, oy;

	if (self->skel == NULL)
		return;
	view_origin (self, &ox, &oy);
	lrg_skeleton2d_reset_pose (self->skel);
	if (self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE && clip != NULL)
		lrg_skeleton2d_clip_apply (clip, self->skel, self->time, 1.0f);
	lrg_skeleton2d_set_root_transform (self->skel, ox, oy, 0.0f, self->zoom, self->zoom);
	lrg_skeleton2d_update_world (self->skel);
}

/* 2D affine helpers on (a, b, c, d, tx, ty) */
static gboolean
affine_invert (const gfloat *m,
               gfloat       *out)
{
	gfloat det = m[0] * m[3] - m[2] * m[1];

	if (fabsf (det) < 1e-12f)
		return FALSE;
	out[0] = m[3] / det;
	out[1] = -m[1] / det;
	out[2] = -m[2] / det;
	out[3] = m[0] / det;
	out[4] = -(out[0] * m[4] + out[2] * m[5]);
	out[5] = -(out[1] * m[4] + out[3] * m[5]);
	return TRUE;
}

static void
affine_apply (const gfloat *m,
              gfloat        x,
              gfloat        y,
              gfloat       *ox,
              gfloat       *oy)
{
	*ox = m[0] * x + m[2] * y + m[4];
	*oy = m[1] * x + m[3] * y + m[5];
}

/* The world matrix of @bone's parent (the view for root bones), as of the
 * last pose(). */
static void
parent_world (LrgEditorSkeleton2DEditor *self,
              gint                       bone,
              gfloat                    *m)
{
	gint parent = bone >= 0 ? lrg_skeleton2d_get_bone_parent (self->skel, bone) : -1;

	if (parent >= 0 && lrg_skeleton2d_get_bone_world_matrix (self->skel, parent, m))
		return;
	view_origin (self, &m[4], &m[5]);
	m[0] = self->zoom;
	m[1] = 0.0f;
	m[2] = 0.0f;
	m[3] = self->zoom;
}

/* Canvas point -> the local space of @bone's parent. */
static void
canvas_to_parent (LrgEditorSkeleton2DEditor *self,
                  gint                       bone,
                  gfloat                     cx,
                  gfloat                     cy,
                  gfloat                    *lx,
                  gfloat                    *ly)
{
	gfloat m[6], inv[6];

	parent_world (self, bone, m);
	if (!affine_invert (m, inv))
	{
		*lx = 0.0f;
		*ly = 0.0f;
		return;
	}
	affine_apply (inv, cx, cy, lx, ly);
}

static gchar *
unique_name (const gchar *prefix,
             gboolean   (*taken) (gpointer data, const gchar *name),
             gpointer     data)
{
	guint i;

	for (i = 1; ; i++)
	{
		gchar *name = g_strdup_printf ("%s%u", prefix, i);

		if (!taken (data, name))
			return name;
		g_free (name);
	}
}

static gboolean
bone_taken (gpointer     data,
            const gchar *name)
{
	return lrg_skeleton2d_find_bone (LRG_SKELETON2D (data), name) >= 0;
}

static gboolean
slot_taken (gpointer     data,
            const gchar *name)
{
	return lrg_skeleton2d_find_slot (LRG_SKELETON2D (data), name) >= 0;
}

/* ==========================================================================
 * YAML surgery on a scratch document
 * ========================================================================== */

static gint
seq_len (LrgEditorDocument *doc,
         const gchar       *path)
{
	return lrg_editor_document_get_kind (doc, path) == LRG_EDITOR_DOCUMENT_NODE_SEQUENCE
	       ? lrg_editor_document_get_length (doc, path) : 0;
}

static const gchar *
scalar_at (LrgEditorDocument *doc,
           const gchar       *format,
           ...) G_GNUC_PRINTF (2, 3);

static const gchar *
scalar_at (LrgEditorDocument *doc,
           const gchar       *format,
           ...)
{
	va_list           args;
	g_autofree gchar *path = NULL;

	va_start (args, format);
	path = g_strdup_vprintf (format, args);
	va_end (args);
	return lrg_editor_document_get_scalar (doc, path);
}

/*
 * Rewrites the weight rows of every attachment: names in @renames (old ->
 * new) are renamed, names in @drop are removed with their weight.
 */
static void
surgery_weights (LrgEditorDocument *s,
                 GHashTable        *drop,
                 GHashTable        *renames)
{
	gint k, a, r, n_skins = seq_len (s, "/skins");

	for (k = 0; k < n_skins; k++)
	{
		g_autofree gchar *ap = g_strdup_printf ("/skins/%d/attachments", k);
		gint              n_att = seq_len (s, ap);

		for (a = 0; a < n_att; a++)
		{
			g_autofree gchar *wp = g_strdup_printf ("%s/%d/weights", ap, a);
			gint              n_rows = seq_len (s, wp);

			for (r = 0; r < n_rows; r++)
			{
				g_autofree gchar *rp = g_strdup_printf ("%s/%d", wp, r);
				gint              n = seq_len (s, rp), j;
				GString          *row = g_string_new ("[");
				g_autofree gchar *text = NULL;
				gboolean          changed = FALSE;

				for (j = 0; j + 1 < n; j += 2)
				{
					const gchar *bone = scalar_at (s, "%s/%d", rp, j);
					const gchar *w = scalar_at (s, "%s/%d", rp, j + 1);
					const gchar *to;

					if (bone == NULL || w == NULL)
						continue;
					if (drop != NULL && g_hash_table_contains (drop, bone))
					{
						changed = TRUE;
						continue;
					}
					to = renames != NULL ? g_hash_table_lookup (renames, bone) : NULL;
					if (to != NULL)
						changed = TRUE;
					if (row->len > 1)
						g_string_append (row, ", ");
					g_string_append_printf (row, "\"%s\", %s", to != NULL ? to : bone, w);
				}
				g_string_append (row, "]");
				text = g_string_free (row, FALSE);
				if (changed)
					lrg_editor_document_replace (s, rp, text, NULL);
			}
		}
	}
}

/* Removes slots named in @slots everywhere (slot list, attachments,
 * timelines, draw-order keys). */
static void
surgery_drop_slots (LrgEditorDocument *s,
                    GHashTable        *slots)
{
	gint i, k, c, n;

	/* attachments of the dropped slots, in every skin */
	for (k = 0; k < seq_len (s, "/skins"); k++)
	{
		g_autofree gchar *ap = g_strdup_printf ("/skins/%d/attachments", k);

		for (i = seq_len (s, ap) - 1; i >= 0; i--)
		{
			const gchar *slot = scalar_at (s, "%s/%d/slot", ap, i);

			if (slot != NULL && g_hash_table_contains (slots, slot))
			{
				g_autofree gchar *p = g_strdup_printf ("%s/%d", ap, i);

				lrg_editor_document_remove (s, p, NULL);
			}
		}
	}
	/* slot timelines and draw-order keys */
	for (c = 0; c < seq_len (s, "/animations"); c++)
	{
		g_autofree gchar *sp = g_strdup_printf ("/animations/%d/slots", c);
		g_autofree gchar *dp = g_strdup_printf ("/animations/%d/draw_order", c);

		for (i = seq_len (s, sp) - 1; i >= 0; i--)
		{
			const gchar *slot = scalar_at (s, "%s/%d/slot", sp, i);

			if (slot != NULL && g_hash_table_contains (slots, slot))
			{
				g_autofree gchar *p = g_strdup_printf ("%s/%d", sp, i);

				lrg_editor_document_remove (s, p, NULL);
			}
		}
		if (seq_len (s, sp) == 0 && lrg_editor_document_has_path (s, sp))
			lrg_editor_document_remove (s, sp, NULL);
		n = seq_len (s, dp);
		for (k = 0; k < n; k++)
		{
			g_autofree gchar *op = g_strdup_printf ("%s/%d/order", dp, k);

			for (i = seq_len (s, op) - 1; i >= 0; i--)
			{
				const gchar *slot = scalar_at (s, "%s/%d", op, i);

				if (slot != NULL && g_hash_table_contains (slots, slot))
				{
					g_autofree gchar *p = g_strdup_printf ("%s/%d", op, i);

					lrg_editor_document_remove (s, p, NULL);
				}
			}
		}
	}
	/* the slots themselves */
	for (i = seq_len (s, "/slots") - 1; i >= 0; i--)
	{
		const gchar *name = scalar_at (s, "/slots/%d/name", i);

		if (name != NULL && g_hash_table_contains (slots, name))
		{
			g_autofree gchar *p = g_strdup_printf ("/slots/%d", i);

			lrg_editor_document_remove (s, p, NULL);
		}
	}
	if (seq_len (s, "/slots") == 0 && lrg_editor_document_has_path (s, "/slots"))
		lrg_editor_document_remove (s, "/slots", NULL);
}

/* Sets every scalar at "<seq>/<i>/<key>" equal to @from to @to. */
static void
surgery_rename_field (LrgEditorDocument *s,
                      const gchar       *seq,
                      const gchar       *key,
                      const gchar       *from,
                      const gchar       *to)
{
	gint i, n = seq_len (s, seq);

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *p = g_strdup_printf ("%s/%d/%s", seq, i, key);

		if (g_strcmp0 (lrg_editor_document_get_scalar (s, p), from) == 0)
			lrg_editor_document_set_string (s, p, to, 0, NULL);
	}
}

/* The entry of a timeline target in the canonical document:
 * "/animations/<c>/<bones|slots>/<e>", or NULL. */
static gchar *
timeline_path (LrgEditorDocument    *doc,
               gint                  clip,
               const gchar          *target,
               LrgSkeleton2DChannel  channel)
{
	gboolean          slots = channel == LRG_SKELETON2D_CHANNEL_COLOR || channel == LRG_SKELETON2D_CHANNEL_ATTACHMENT;
	g_autofree gchar *base = g_strdup_printf ("/animations/%d/%s", clip, slots ? "slots" : "bones");
	gint              e, n = seq_len (doc, base);

	for (e = 0; e < n; e++)
		if (g_strcmp0 (scalar_at (doc, "%s/%d/%s", base, e, slots ? "slot" : "bone"), target) == 0)
			return g_strdup_printf ("%s/%d/%s", base, e, channel_nicks[channel]);
	return NULL;
}

/* The Bézier control points of a key, read from the canonical document. */
static gboolean
key_bezier (LrgEditorSkeleton2DEditor *self,
            const gchar               *target,
            LrgSkeleton2DChannel       channel,
            guint                      index,
            gfloat                    *cp)
{
	g_autofree gchar *tp = NULL;
	g_autofree gchar *cpath = NULL;
	gint              i;

	if (self->canon == NULL || self->clip == NULL)
		return FALSE;
	tp = timeline_path (self->canon, clip_index (self, self->clip), target, channel);
	if (tp == NULL)
		return FALSE;
	cpath = g_strdup_printf ("%s/%u/curve", tp, index);
	if (seq_len (self->canon, cpath) != 4)
		return FALSE;
	for (i = 0; i < 4; i++)
	{
		const gchar *v = scalar_at (self->canon, "%s/%d", cpath, i);

		cp[i] = v != NULL ? (gfloat) g_ascii_strtod (v, NULL) : 0.0f;
	}
	return TRUE;
}

/* ==========================================================================
 * Mesh helpers
 * ========================================================================== */

/*
 * Least-squares affine map from setup position to texture coordinate
 * fitted to the mesh's vertices: u = c[0] x + c[1] y + c[2],
 * v = c[3] x + c[4] y + c[5]. FALSE with fewer than three non-collinear
 * vertices.
 */
static gboolean
uv_fit (LrgSkinnedSprite *sprite,
        gfloat           *c)
{
	gdouble sxx = 0, sxy = 0, syy = 0, sx = 0, sy = 0, n = 0;
	gdouble sxu = 0, syu = 0, su = 0, sxv = 0, syv = 0, sv = 0;
	gdouble det;
	guint   i, count = lrg_skinned_sprite_get_vertex_count (sprite);

	for (i = 0; i < count; i++)
	{
		gfloat x, y, u, v;

		lrg_skinned_sprite_get_vertex (sprite, i, &x, &y, &u, &v);
		sxx += (gdouble) x * x;
		sxy += (gdouble) x * y;
		syy += (gdouble) y * y;
		sx += x;
		sy += y;
		n += 1.0;
		sxu += (gdouble) x * u;
		syu += (gdouble) y * u;
		su += u;
		sxv += (gdouble) x * v;
		syv += (gdouble) y * v;
		sv += v;
	}
	/* normal equations [sxx sxy sx; sxy syy sy; sx sy n] p = rhs, Cramer */
	det = sxx * (syy * n - sy * sy) - sxy * (sxy * n - sy * sx) + sx * (sxy * sy - syy * sx);
	if (count < 3 || fabs (det) < 1e-9)
		return FALSE;
#define SOLVE(r0, r1, r2, out) \
	G_STMT_START { \
		gdouble a_ = (r0) * (syy * n - sy * sy) - sxy * ((r1) * n - sy * (r2)) + sx * ((r1) * sy - syy * (r2)); \
		gdouble b_ = sxx * ((r1) * n - sy * (r2)) - (r0) * (sxy * n - sy * sx) + sx * (sxy * (r2) - (r1) * sx); \
		gdouble c_ = sxx * (syy * (r2) - (r1) * sy) - sxy * (sxy * (r2) - (r1) * sx) + (r0) * (sxy * sy - syy * sx); \
		(out)[0] = (gfloat) (a_ / det); \
		(out)[1] = (gfloat) (b_ / det); \
		(out)[2] = (gfloat) (c_ / det); \
	} G_STMT_END
	SOLVE (sxu, syu, su, c);
	SOLVE (sxv, syv, sv, c + 3);
#undef SOLVE
	return TRUE;
}

/*
 * A copy of @src without vertex @skip (-1 keeps all). With @tris (index
 * triples in the new numbering) those become the triangles; otherwise the
 * old triangles are kept, minus those using @skip.
 */
static LrgSkinnedSprite *
sprite_rebuild (LrgSkinnedSprite *src,
                gint              skip,
                GArray           *tris)
{
	LrgSkinnedSprite *dst = lrg_skinned_sprite_new ();
	gfloat            rx, ry, rw, rh;
	guint             i, n = lrg_skinned_sprite_get_vertex_count (src);

	lrg_skinned_sprite_set_image (dst, lrg_skinned_sprite_get_image (src));
	lrg_skinned_sprite_get_region (src, &rx, &ry, &rw, &rh);
	lrg_skinned_sprite_set_region (dst, rx, ry, rw, rh);
	lrg_skinned_sprite_set_texture (dst, lrg_skinned_sprite_get_texture (src));
	for (i = 0; i < n; i++)
	{
		gfloat x, y, u, v, w[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
		gint   b[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
		guint  nw;
		gint   idx;

		if ((gint) i == skip)
			continue;
		lrg_skinned_sprite_get_vertex (src, i, &x, &y, &u, &v);
		idx = lrg_skinned_sprite_add_vertex (dst, x, y, u, v);
		nw = lrg_skinned_sprite_get_weights (src, i, b, w);
		if (nw > 0 && idx >= 0)
			lrg_skinned_sprite_set_weights (dst, (guint) idx, nw, b, w);
	}
	if (tris != NULL)
	{
		for (i = 0; i + 2 < tris->len; i += 3)
			lrg_skinned_sprite_add_triangle (dst, g_array_index (tris, guint, i), g_array_index (tris, guint, i + 1),
			                                 g_array_index (tris, guint, i + 2));
		return dst;
	}
	for (i = 0; i < lrg_skinned_sprite_get_triangle_count (src); i++)
	{
		guint a, b, c;

		lrg_skinned_sprite_get_triangle (src, i, &a, &b, &c);
		if (skip >= 0 && ((gint) a == skip || (gint) b == skip || (gint) c == skip))
			continue;
		if (skip >= 0)
		{
			a -= (gint) a > skip ? 1 : 0;
			b -= (gint) b > skip ? 1 : 0;
			c -= (gint) c > skip ? 1 : 0;
		}
		lrg_skinned_sprite_add_triangle (dst, a, b, c);
	}
	return dst;
}

typedef struct
{
	guint   v[3];
	gdouble cx, cy, r2;   /* circumcircle */
} DTri;

static gboolean
dtri_make (const gdouble *px,
           const gdouble *py,
           guint          a,
           guint          b,
           guint          c,
           DTri          *t)
{
	gdouble ax = px[a], ay = py[a], bx = px[b], by = py[b], cx = px[c], cy = py[c];
	gdouble d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));

	t->v[0] = a;
	t->v[1] = b;
	t->v[2] = c;
	if (fabs (d) < 1e-12)
	{
		t->cx = t->cy = 0.0;
		t->r2 = -1.0;     /* degenerate: never contains anything */
		return FALSE;
	}
	t->cx = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d;
	t->cy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d;
	t->r2 = (ax - t->cx) * (ax - t->cx) + (ay - t->cy) * (ay - t->cy);
	return TRUE;
}

/*
 * Bowyer-Watson Delaunay triangulation of the sprite's vertices. Triangles
 * whose centroid falls outside the image (texture coordinate outside
 * 0 .. 1, by the fitted UV map) are dropped, so a mesh traced around a
 * concave silhouette with points outside the picture does not grow skin
 * over the gaps. Returns index triples.
 */
static GArray *
delaunay (LrgSkinnedSprite *sprite)
{
	guint            n = lrg_skinned_sprite_get_vertex_count (sprite), i, j, k;
	g_autofree gdouble *px = g_new0 (gdouble, n + 3);
	g_autofree gdouble *py = g_new0 (gdouble, n + 3);
	g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (DTri));
	GArray           *out = g_array_new (FALSE, FALSE, sizeof (guint));
	gdouble          minx = G_MAXDOUBLE, miny = G_MAXDOUBLE, maxx = -G_MAXDOUBLE, maxy = -G_MAXDOUBLE, span, mx, my;
	gfloat           fit[6];
	gboolean         have_fit = uv_fit (sprite, fit);
	DTri             t;

	if (n < 3)
		return out;
	for (i = 0; i < n; i++)
	{
		gfloat x, y;

		lrg_skinned_sprite_get_vertex (sprite, i, &x, &y, NULL, NULL);
		px[i] = x;
		py[i] = y;
		minx = MIN (minx, x);
		miny = MIN (miny, y);
		maxx = MAX (maxx, x);
		maxy = MAX (maxy, y);
	}
	/* work in a unit box around the points (precision), inside a super
	 * triangle a few boxes wide */
	span = MAX (maxx - minx, maxy - miny);
	if (span <= 0.0)
		span = 1.0;
	mx = (minx + maxx) * 0.5;
	my = (miny + maxy) * 0.5;
	for (i = 0; i < n; i++)
	{
		px[i] = (px[i] - mx) / span;
		py[i] = (py[i] - my) / span;
	}
	px[n] = -10.0;
	py[n] = -10.0;
	px[n + 1] = 10.0;
	py[n + 1] = -10.0;
	px[n + 2] = 0.0;
	py[n + 2] = 10.0;
	dtri_make (px, py, n, n + 1, n + 2, &t);
	g_array_append_val (tris, t);

	for (i = 0; i < n; i++)
	{
		g_autoptr(GArray) edges = g_array_new (FALSE, FALSE, sizeof (guint) * 2);

		/* remove the triangles whose circumcircle holds the point, keeping
		 * the edges of the hole they leave */
		for (j = tris->len; j-- > 0;)
		{
			DTri   *d = &g_array_index (tris, DTri, j);
			gdouble dx = px[i] - d->cx, dy = py[i] - d->cy;

			if (d->r2 >= 0.0 && dx * dx + dy * dy < d->r2 - 1e-12)
			{
				for (k = 0; k < 3; k++)
				{
					guint e[2] = { d->v[k], d->v[(k + 1) % 3] };

					g_array_append_vals (edges, e, 1);
				}
				g_array_remove_index_fast (tris, j);
			}
		}
		/* edges shared by two removed triangles are inside the hole */
		for (j = 0; j < edges->len; j++)
		{
			guint   *e = (guint *) (edges->data + j * sizeof (guint) * 2);
			gboolean shared = FALSE;

			for (k = 0; k < edges->len && !shared; k++)
			{
				guint *f = (guint *) (edges->data + k * sizeof (guint) * 2);

				shared = k != j && ((e[0] == f[0] && e[1] == f[1]) || (e[0] == f[1] && e[1] == f[0]));
			}
			if (shared)
				continue;
			if (dtri_make (px, py, e[0], e[1], i, &t))
				g_array_append_val (tris, t);
		}
	}

	for (j = 0; j < tris->len; j++)
	{
		DTri   *d = &g_array_index (tris, DTri, j);
		gdouble cx, cy;

		if (d->v[0] >= n || d->v[1] >= n || d->v[2] >= n)
			continue;
		cx = (px[d->v[0]] + px[d->v[1]] + px[d->v[2]]) / 3.0 * span + mx;
		cy = (py[d->v[0]] + py[d->v[1]] + py[d->v[2]]) / 3.0 * span + my;
		if (have_fit)
		{
			gdouble u = fit[0] * cx + fit[1] * cy + fit[2];
			gdouble v = fit[3] * cx + fit[4] * cy + fit[5];

			if (u < -0.02 || u > 1.02 || v < -0.02 || v > 1.02)
				continue;
		}
		g_array_append_vals (out, d->v, 3);
	}
	return out;
}

/* Replaces the selected attachment with @sprite (one model edit). */
static void
replace_sprite (LrgEditorSkeleton2DEditor *self,
                LrgSkinnedSprite          *sprite)
{
	const gchar *skin = NULL;

	sel_sprite (self, &skin);
	lrg_skeleton2d_set_attachment (self->skel, skin, sel_slot_index (self), self->sel_attachment, sprite);
}

/*
 * One weight-brush dab on @sprite (setup space). Returns the number of
 * vertices changed.
 */
static guint
brush_dab (LrgSkinnedSprite *sprite,
           gint              bone,
           gint              slot_bone,
           gfloat            x,
           gfloat            y,
           gfloat            radius,
           gfloat            strength)
{
	guint i, changed = 0, n = lrg_skinned_sprite_get_vertex_count (sprite);
	gboolean erase = strength < 0.0f;

	strength = CLAMP (fabsf (strength), 0.0f, 1.0f);
	for (i = 0; i < n && radius > 0.0f; i++)
	{
		gfloat vx, vy, d, f, cur = 0.0f, others = 0.0f, nw;
		gint   b[LRG_SKINNED_SPRITE_MAX_WEIGHTS + 1];
		gfloat w[LRG_SKINNED_SPRITE_MAX_WEIGHTS + 1];
		guint  nb, k, at = G_MAXUINT;

		lrg_skinned_sprite_get_vertex (sprite, i, &vx, &vy, NULL, NULL);
		d = hypotf (vx - x, vy - y);
		if (d >= radius)
			continue;
		f = (1.0f - d / radius);
		f = f * f * strength;
		nb = lrg_skinned_sprite_get_weights (sprite, i, b, w);
		if (nb == 0 && slot_bone >= 0)
		{
			/* an unweighted vertex follows its slot's bone */
			b[0] = slot_bone;
			w[0] = 1.0f;
			nb = 1;
		}
		for (k = 0; k < nb; k++)
		{
			if (b[k] == bone)
			{
				cur = w[k];
				at = k;
			}
			else
				others += w[k];
		}
		nw = cur + ((erase ? 0.0f : 1.0f) - cur) * f;
		if (fabsf (nw - cur) < 1e-5f)
			continue;
		if (others <= 1e-6f && nw < 1.0f)
		{
			/* nothing else to give the rest to: the slot's bone takes it */
			if (slot_bone < 0 || slot_bone == bone)
				continue;
			b[nb] = slot_bone;
			w[nb] = 0.0f;
			nb++;
			others = 0.0f;
		}
		/* scale the others to fill 1 - nw */
		for (k = 0; k < nb; k++)
		{
			if (k == at)
				continue;
			if (others > 1e-6f)
				w[k] *= (1.0f - nw) / others;
			else
				w[k] = 1.0f - nw;
		}
		if (at == G_MAXUINT)
		{
			b[nb] = bone;
			w[nb] = nw;
			nb++;
		}
		else
			w[at] = nw;
		/* at most four: drop the lightest */
		while (nb > LRG_SKINNED_SPRITE_MAX_WEIGHTS)
		{
			guint lo = 0;

			for (k = 1; k < nb; k++)
				if (w[k] < w[lo])
					lo = k;
			b[lo] = b[nb - 1];
			w[lo] = w[nb - 1];
			nb--;
		}
		if (lrg_skinned_sprite_set_weights (sprite, i, nb, b, w))
			changed++;
	}
	return changed;
}

/* The weight of @bone on vertex @i (unweighted vertices: 1 on the slot's
 * bone). */
static gfloat
vertex_weight (LrgSkinnedSprite *sprite,
               guint             i,
               gint              bone,
               gint              slot_bone)
{
	gint   b[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
	gfloat w[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
	guint  k, nb = lrg_skinned_sprite_get_weights (sprite, i, b, w);

	if (nb == 0)
		return bone == slot_bone ? 1.0f : 0.0f;
	for (k = 0; k < nb; k++)
		if (b[k] == bone)
			return w[k];
	return 0.0f;
}

/* ==========================================================================
 * Public API: bones
 * ========================================================================== */

static void
select_bone_name (LrgEditorSkeleton2DEditor *self,
                  const gchar               *name)
{
	g_free (self->sel_bone);
	self->sel_bone = g_strdup (name);
}

gint
lrg_editor_skeleton2d_editor_add_bone (LrgEditorSkeleton2DEditor  *self,
                                       const gchar                *name,
                                       gint                        parent,
                                       gfloat                      x,
                                       gfloat                      y,
                                       gfloat                      rotation,
                                       gfloat                      length,
                                       GError                    **error)
{
	g_autofree gchar *auto_name = NULL;
	gint              index;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), -1);
	if (!require_skel (self, error))
		return -1;
	if (parent >= (gint) lrg_skeleton2d_get_bone_count (self->skel))
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no parent bone %d", parent);
		return -1;
	}
	if (name == NULL || *name == '\0')
		name = auto_name = unique_name ("bone", bone_taken, self->skel);
	if (lrg_skeleton2d_find_bone (self->skel, name) >= 0)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_EXISTS, "a bone named '%s' exists", name);
		return -1;
	}
	index = lrg_skeleton2d_add_bone (self->skel, name, MAX (parent, -1), x, y, rotation, 1.0f, 1.0f,
	                                 MAX (length, 0.0f));
	if (index < 0)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "cannot add bone '%s'", name);
		reload (self);
		return -1;
	}
	select_bone_name (self, name);
	if (!commit (self, "Add bone", error))
		return -1;
	return lrg_skeleton2d_find_bone (self->skel, name);
}

gboolean
lrg_editor_skeleton2d_editor_set_bone (LrgEditorSkeleton2DEditor  *self,
                                       gint                        bone,
                                       gfloat                      x,
                                       gfloat                      y,
                                       gfloat                      rotation,
                                       gfloat                      scale_x,
                                       gfloat                      scale_y,
                                       gfloat                      length,
                                       GError                    **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!bone_ok (self, bone, error))
		return FALSE;
	lrg_skeleton2d_set_bone_setup (self->skel, bone, x, y, rotation, scale_x, scale_y);
	lrg_skeleton2d_set_bone_length (self->skel, bone, MAX (length, 0.0f));
	return commit (self, "Edit bone", error);
}

gboolean
lrg_editor_skeleton2d_editor_rename_bone (LrgEditorSkeleton2DEditor  *self,
                                          gint                        bone,
                                          const gchar                *name,
                                          GError                    **error)
{
	g_autoptr(LrgEditorDocument) s = NULL;
	g_autoptr(GHashTable)        renames = NULL;
	g_autofree gchar            *old = NULL;
	gint                         c;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!bone_ok (self, bone, error))
		return FALSE;
	if (name == NULL || *name == '\0')
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "a bone name cannot be empty");
		return FALSE;
	}
	old = g_strdup (lrg_skeleton2d_get_bone_name (self->skel, bone));
	if (g_strcmp0 (old, name) == 0)
		return TRUE;
	if (lrg_skeleton2d_find_bone (self->skel, name) >= 0)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_EXISTS, "a bone named '%s' exists", name);
		return FALSE;
	}
	s = scratch_doc (self);
	if (s == NULL)
		return require_skel (self, error);

	/* the bone, its children's parent refs, slots, weights, timelines */
	{
		g_autofree gchar *p = g_strdup_printf ("/bones/%d/name", bone);

		lrg_editor_document_set_string (s, p, name, 0, NULL);
	}
	surgery_rename_field (s, "/bones", "parent", old, name);
	surgery_rename_field (s, "/slots", "bone", old, name);
	renames = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (renames, old, (gpointer) name);
	surgery_weights (s, NULL, renames);
	for (c = 0; c < seq_len (s, "/animations"); c++)
	{
		g_autofree gchar *bp = g_strdup_printf ("/animations/%d/bones", c);

		surgery_rename_field (s, bp, "bone", old, name);
	}
	if (g_strcmp0 (self->sel_bone, old) == 0)
		select_bone_name (self, name);
	if (!apply_scratch (self, "Rename bone", s, error))
	{
		if (g_strcmp0 (self->sel_bone, name) == 0)
			select_bone_name (self, old);
		return FALSE;
	}
	return TRUE;
}

gboolean
lrg_editor_skeleton2d_editor_remove_bone (LrgEditorSkeleton2DEditor  *self,
                                          gint                        bone,
                                          GError                    **error)
{
	g_autoptr(LrgEditorDocument) s = NULL;
	g_autoptr(GHashTable)        bones = NULL;
	g_autoptr(GHashTable)        slots = NULL;
	g_autofree gboolean         *gone = NULL;
	guint                        n, i, n_gone = 0;
	gint                         c, e;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!bone_ok (self, bone, error))
		return FALSE;

	/* the bone and its descendants (parents precede children) */
	n = lrg_skeleton2d_get_bone_count (self->skel);
	gone = g_new0 (gboolean, n);
	bones = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = (guint) bone; i < n; i++)
	{
		gint parent = lrg_skeleton2d_get_bone_parent (self->skel, (gint) i);

		if ((gint) i == bone || (parent >= 0 && gone[parent]))
		{
			gone[i] = TRUE;
			n_gone++;
			g_hash_table_add (bones, (gpointer) lrg_skeleton2d_get_bone_name (self->skel, (gint) i));
		}
	}
	if (n_gone >= n)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "a rig keeps at least one bone");
		return FALSE;
	}
	slots = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < lrg_skeleton2d_get_slot_count (self->skel); i++)
		if (gone[lrg_skeleton2d_get_slot_bone (self->skel, (gint) i)])
			g_hash_table_add (slots, (gpointer) lrg_skeleton2d_get_slot_name (self->skel, (gint) i));

	s = scratch_doc (self);
	if (s == NULL)
		return require_skel (self, error);
	surgery_drop_slots (s, slots);
	surgery_weights (s, bones, NULL);
	for (c = 0; c < seq_len (s, "/animations"); c++)
	{
		g_autofree gchar *bp = g_strdup_printf ("/animations/%d/bones", c);

		for (e = seq_len (s, bp) - 1; e >= 0; e--)
		{
			const gchar *b = scalar_at (s, "%s/%d/bone", bp, e);

			if (b != NULL && g_hash_table_contains (bones, b))
			{
				g_autofree gchar *p = g_strdup_printf ("%s/%d", bp, e);

				lrg_editor_document_remove (s, p, NULL);
			}
		}
		if (seq_len (s, bp) == 0 && lrg_editor_document_has_path (s, bp))
			lrg_editor_document_remove (s, bp, NULL);
	}
	for (c = (gint) n - 1; c >= 0; c--)
	{
		if (gone[c])
		{
			g_autofree gchar *p = g_strdup_printf ("/bones/%d", c);

			lrg_editor_document_remove (s, p, NULL);
		}
	}
	return apply_scratch (self, "Remove bone", s, error);
}

/* ==========================================================================
 * Public API: slots
 * ========================================================================== */

gint
lrg_editor_skeleton2d_editor_add_slot (LrgEditorSkeleton2DEditor  *self,
                                       const gchar                *name,
                                       gint                        bone,
                                       const gchar                *attachment,
                                       GError                    **error)
{
	g_autofree gchar *auto_name = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), -1);
	if (!bone_ok (self, bone, error))
		return -1;
	if (name == NULL || *name == '\0')
		name = auto_name = unique_name ("slot", slot_taken, self->skel);
	if (lrg_skeleton2d_find_slot (self->skel, name) >= 0)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_EXISTS, "a slot named '%s' exists", name);
		return -1;
	}
	if (lrg_skeleton2d_add_slot (self->skel, name, bone, attachment) < 0)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "cannot add slot '%s'", name);
		reload (self);
		return -1;
	}
	g_free (self->sel_slot);
	self->sel_slot = g_strdup (name);
	g_clear_pointer (&self->sel_attachment, g_free);
	if (!commit (self, "Add slot", error))
		return -1;
	return lrg_skeleton2d_find_slot (self->skel, name);
}

gboolean
lrg_editor_skeleton2d_editor_set_slot_attachment (LrgEditorSkeleton2DEditor  *self,
                                                  gint                        slot,
                                                  const gchar                *attachment,
                                                  GError                    **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!slot_ok (self, slot, error))
		return FALSE;
	lrg_skeleton2d_set_slot_setup_attachment (self->skel, slot, attachment);
	return commit (self, "Set attachment", error);
}

gboolean
lrg_editor_skeleton2d_editor_set_slot_color (LrgEditorSkeleton2DEditor  *self,
                                             gint                        slot,
                                             const gfloat               *rgba,
                                             GError                    **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	g_return_val_if_fail (rgba != NULL, FALSE);
	if (!slot_ok (self, slot, error))
		return FALSE;
	lrg_skeleton2d_set_slot_setup_color (self->skel, slot, CLAMP (rgba[0], 0.0f, 1.0f), CLAMP (rgba[1], 0.0f, 1.0f),
	                                     CLAMP (rgba[2], 0.0f, 1.0f), CLAMP (rgba[3], 0.0f, 1.0f));
	return commit (self, "Slot colour", error);
}

gboolean
lrg_editor_skeleton2d_editor_rename_slot (LrgEditorSkeleton2DEditor  *self,
                                          gint                        slot,
                                          const gchar                *name,
                                          GError                    **error)
{
	g_autoptr(LrgEditorDocument) s = NULL;
	g_autofree gchar            *old = NULL;
	gint                         c, k, i;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!slot_ok (self, slot, error))
		return FALSE;
	if (name == NULL || *name == '\0')
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "a slot name cannot be empty");
		return FALSE;
	}
	old = g_strdup (lrg_skeleton2d_get_slot_name (self->skel, slot));
	if (g_strcmp0 (old, name) == 0)
		return TRUE;
	if (lrg_skeleton2d_find_slot (self->skel, name) >= 0)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_EXISTS, "a slot named '%s' exists", name);
		return FALSE;
	}
	s = scratch_doc (self);
	if (s == NULL)
		return require_skel (self, error);
	{
		g_autofree gchar *p = g_strdup_printf ("/slots/%d/name", slot);

		lrg_editor_document_set_string (s, p, name, 0, NULL);
	}
	for (k = 0; k < seq_len (s, "/skins"); k++)
	{
		g_autofree gchar *ap = g_strdup_printf ("/skins/%d/attachments", k);

		surgery_rename_field (s, ap, "slot", old, name);
	}
	for (c = 0; c < seq_len (s, "/animations"); c++)
	{
		g_autofree gchar *sp = g_strdup_printf ("/animations/%d/slots", c);
		g_autofree gchar *dp = g_strdup_printf ("/animations/%d/draw_order", c);

		surgery_rename_field (s, sp, "slot", old, name);
		for (k = 0; k < seq_len (s, dp); k++)
		{
			g_autofree gchar *op = g_strdup_printf ("%s/%d/order", dp, k);

			for (i = 0; i < seq_len (s, op); i++)
			{
				g_autofree gchar *p = g_strdup_printf ("%s/%d", op, i);

				if (g_strcmp0 (lrg_editor_document_get_scalar (s, p), old) == 0)
					lrg_editor_document_set_string (s, p, name, 0, NULL);
			}
		}
	}
	if (g_strcmp0 (self->sel_slot, old) == 0)
	{
		g_free (self->sel_slot);
		self->sel_slot = g_strdup (name);
	}
	return apply_scratch (self, "Rename slot", s, error);
}

gboolean
lrg_editor_skeleton2d_editor_remove_slot (LrgEditorSkeleton2DEditor  *self,
                                          gint                        slot,
                                          GError                    **error)
{
	g_autoptr(LrgEditorDocument) s = NULL;
	g_autoptr(GHashTable)        slots = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!slot_ok (self, slot, error))
		return FALSE;
	s = scratch_doc (self);
	if (s == NULL)
		return require_skel (self, error);
	slots = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_add (slots, (gpointer) lrg_skeleton2d_get_slot_name (self->skel, slot));
	surgery_drop_slots (s, slots);
	return apply_scratch (self, "Remove slot", s, error);
}

gboolean
lrg_editor_skeleton2d_editor_move_slot (LrgEditorSkeleton2DEditor  *self,
                                        gint                        slot,
                                        gint                        delta,
                                        GError                    **error)
{
	g_autoptr(LrgEditorDocument) s = NULL;
	gint                         n, to;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!slot_ok (self, slot, error))
		return FALSE;
	n = (gint) lrg_skeleton2d_get_slot_count (self->skel);
	to = CLAMP (slot + delta, 0, n - 1);
	if (to == slot)
		return FALSE;
	s = scratch_doc (self);
	if (s == NULL)
		return require_skel (self, error);
	if (!lrg_editor_document_move_item (s, "/slots", (guint) slot, (guint) to, error))
		return FALSE;
	return apply_scratch (self, "Draw order", s, error);
}

gboolean
lrg_editor_skeleton2d_editor_set_draw_order (LrgEditorSkeleton2DEditor  *self,
                                             const gchar * const        *order,
                                             GError                    **error)
{
	g_autoptr(LrgEditorDocument) s = NULL;
	guint                        n, i;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	g_return_val_if_fail (order != NULL, FALSE);
	if (!require_skel (self, error))
		return FALSE;
	n = lrg_skeleton2d_get_slot_count (self->skel);
	if (g_strv_length ((gchar **) order) != n)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "the order must name every slot once (%u)", n);
		return FALSE;
	}
	s = scratch_doc (self);
	if (s == NULL)
		return require_skel (self, error);
	/* selection sort by moves: item i becomes order[i] */
	for (i = 0; i < n; i++)
	{
		gint j, found = -1;

		for (j = (gint) i; j < (gint) n && found < 0; j++)
			if (g_strcmp0 (scalar_at (s, "/slots/%d/name", j), order[i]) == 0)
				found = j;
		if (found < 0)
		{
			set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "unknown or repeated slot '%s'", order[i]);
			return FALSE;
		}
		if ((guint) found != i && !lrg_editor_document_move_item (s, "/slots", (guint) found, i, error))
			return FALSE;
	}
	return apply_scratch (self, "Draw order", s, error);
}

/* ==========================================================================
 * Public API: skins and attachments
 * ========================================================================== */

gboolean
lrg_editor_skeleton2d_editor_add_skin (LrgEditorSkeleton2DEditor  *self,
                                       const gchar                *name,
                                       GError                    **error)
{
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_skel (self, error))
		return FALSE;
	if (name == NULL || *name == '\0')
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "a skin name cannot be empty");
		return FALSE;
	}
	for (i = 0; i < lrg_skeleton2d_get_skin_count (self->skel); i++)
		if (g_strcmp0 (lrg_skeleton2d_get_skin_name (self->skel, i), name) == 0)
		{
			set_error (error, LRG_EDITOR_DOCUMENT_ERROR_EXISTS, "a skin named '%s' exists", name);
			return FALSE;
		}
	lrg_skeleton2d_add_skin (self->skel, name);
	g_free (self->skin);
	self->skin = g_strdup (name);
	return commit (self, "Add skin", error);
}

gboolean
lrg_editor_skeleton2d_editor_remove_skin (LrgEditorSkeleton2DEditor  *self,
                                          const gchar                *name,
                                          GError                    **error)
{
	g_autoptr(LrgEditorDocument) s = NULL;
	gint                         k;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_skel (self, error))
		return FALSE;
	if (g_strcmp0 (name, LRG_SKELETON2D_DEFAULT_SKIN) == 0)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "the default skin cannot be removed");
		return FALSE;
	}
	s = scratch_doc (self);
	if (s == NULL)
		return require_skel (self, error);
	for (k = seq_len (s, "/skins") - 1; k >= 0; k--)
	{
		if (g_strcmp0 (scalar_at (s, "/skins/%d/name", k), name) == 0)
		{
			g_autofree gchar *p = g_strdup_printf ("/skins/%d", k);

			lrg_editor_document_remove (s, p, NULL);
			if (g_strcmp0 (self->skin, name) == 0)
				g_clear_pointer (&self->skin, g_free);
			return apply_scratch (self, "Remove skin", s, error);
		}
	}
	set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no skin '%s'", name != NULL ? name : "(null)");
	return FALSE;
}

/* @path relative to the folder @dir, with ".." segments where needed
 * (both absolute). */
static gchar *
relative_to (const gchar *dir,
             const gchar *path)
{
	g_autofree gchar *cd = g_canonicalize_filename (dir, NULL);
	g_autofree gchar *cp = g_canonicalize_filename (path, NULL);
	g_auto(GStrv)     a = g_strsplit (cd, G_DIR_SEPARATOR_S, -1);
	g_auto(GStrv)     b = g_strsplit (cp, G_DIR_SEPARATOR_S, -1);
	GString          *out = g_string_new (NULL);
	guint             i = 0, j;

	while (a[i] != NULL && b[i] != NULL && g_str_equal (a[i], b[i]))
		i++;
	for (j = i; a[j] != NULL; j++)
		if (*a[j] != '\0')
			g_string_append (out, "../");
	for (j = i; b[j] != NULL; j++)
	{
		g_string_append (out, b[j]);
		if (b[j + 1] != NULL)
			g_string_append_c (out, '/');
	}
	return g_string_free (out, FALSE);
}

/* @image made relative to the rig's folder (absolute and project-relative
 * paths are accepted). */
static gchar *
image_ref (LrgEditorSkeleton2DEditor *self,
           const gchar               *image,
           gchar                    **abs_out)
{
	g_autofree gchar *dir = rig_dir (self);
	g_autofree gchar *abs = NULL;
	gchar            *rel;

	if (g_path_is_absolute (image))
		abs = g_strdup (image);
	else
	{
		abs = g_build_filename (dir, image, NULL);
		if (!g_file_test (abs, G_FILE_TEST_EXISTS) && app_of (self) != NULL &&
		    lrg_editor_app_get_project_dir (app_of (self)) != NULL)
		{
			g_autofree gchar *p = g_build_filename (lrg_editor_app_get_project_dir (app_of (self)), image, NULL);

			if (g_file_test (p, G_FILE_TEST_EXISTS))
			{
				g_free (abs);
				abs = g_steal_pointer (&p);
			}
		}
	}
	rel = relative_to (dir, abs);
	if (abs_out != NULL)
		*abs_out = g_steal_pointer (&abs);
	return rel;
}

gboolean
lrg_editor_skeleton2d_editor_add_attachment (LrgEditorSkeleton2DEditor  *self,
                                             gint                        slot,
                                             const gchar                *name,
                                             const gchar                *image,
                                             gfloat                      x,
                                             gfloat                      y,
                                             gfloat                      width,
                                             gfloat                      height,
                                             guint                       columns,
                                             guint                       rows,
                                             GError                    **error)
{
	g_autoptr(LrgSkinnedSprite) sprite = NULL;
	g_autofree gchar           *rel = NULL;
	g_autofree gchar           *abs = NULL;
	g_autofree gchar           *auto_name = NULL;
	const gchar                *skin;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!slot_ok (self, slot, error))
		return FALSE;
	if (image != NULL && *image != '\0')
	{
		rel = image_ref (self, image, &abs);
		/* the image's own size when none is given */
		if ((width <= 0.0f || height <= 0.0f) && g_file_test (abs, G_FILE_TEST_IS_REGULAR))
		{
			g_autoptr(GrlImage) img = grl_image_new_from_file (abs);

			if (img != NULL && grl_image_get_width (img) > 0)
			{
				if (width <= 0.0f)
					width = (gfloat) grl_image_get_width (img);
				if (height <= 0.0f)
					height = (gfloat) grl_image_get_height (img);
			}
		}
		if (name == NULL || *name == '\0')
		{
			g_autofree gchar *base = g_path_get_basename (rel);
			gchar            *dot = strrchr (base, '.');

			if (dot != NULL && dot != base)
				*dot = '\0';
			name = auto_name = g_strdup (base);
		}
	}
	if (name == NULL || *name == '\0')
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "an attachment needs a name or an image");
		return FALSE;
	}
	if (width <= 0.0f || height <= 0.0f)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "attachment '%s' needs a size", name);
		return FALSE;
	}
	columns = MAX (columns, 1u);
	rows = MAX (rows, 1u);
	if (columns == 1 && rows == 1)
		sprite = lrg_skinned_sprite_new_quad (x, y, width, height, -1);
	else
	{
		guint r, c;

		sprite = lrg_skinned_sprite_new_grid (x, y, width, height, columns, rows);
		/* new_grid has no triangles in old builds? make sure there are */
		if (lrg_skinned_sprite_get_triangle_count (sprite) == 0)
			for (r = 0; r < rows; r++)
				for (c = 0; c < columns; c++)
				{
					guint i0 = r * (columns + 1) + c;

					lrg_skinned_sprite_add_triangle (sprite, i0, i0 + 1, i0 + columns + 1);
					lrg_skinned_sprite_add_triangle (sprite, i0 + 1, i0 + columns + 2, i0 + columns + 1);
				}
	}
	if (rel != NULL)
		lrg_skinned_sprite_set_image (sprite, rel);
	skin = self->skin != NULL ? self->skin : LRG_SKELETON2D_DEFAULT_SKIN;
	lrg_skeleton2d_set_attachment (self->skel, skin, slot, name, sprite);
	if (lrg_skeleton2d_get_slot_setup_attachment (self->skel, slot) == NULL)
		lrg_skeleton2d_set_slot_setup_attachment (self->skel, slot, name);
	g_free (self->sel_slot);
	self->sel_slot = g_strdup (lrg_skeleton2d_get_slot_name (self->skel, slot));
	g_free (self->sel_attachment);
	self->sel_attachment = g_strdup (name);
	return commit (self, "Add attachment", error);
}

gboolean
lrg_editor_skeleton2d_editor_remove_attachment (LrgEditorSkeleton2DEditor  *self,
                                                gint                        slot,
                                                const gchar                *name,
                                                GError                    **error)
{
	const gchar *skin = LRG_SKELETON2D_DEFAULT_SKIN;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!slot_ok (self, slot, error))
		return FALSE;
	if (self->skin != NULL && lrg_skeleton2d_get_skin_attachment (self->skel, self->skin, slot, name) != NULL)
		skin = self->skin;
	if (lrg_skeleton2d_get_skin_attachment (self->skel, skin, slot, name) == NULL)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no attachment '%s' on slot %d",
		           name != NULL ? name : "(null)", slot);
		return FALSE;
	}
	lrg_skeleton2d_set_attachment (self->skel, skin, slot, name, NULL);
	if (g_strcmp0 (lrg_skeleton2d_get_slot_setup_attachment (self->skel, slot), name) == 0 &&
	    lrg_skeleton2d_get_attachment (self->skel, slot, name) == NULL)
		lrg_skeleton2d_set_slot_setup_attachment (self->skel, slot, NULL);
	return commit (self, "Remove attachment", error);
}

/* ==========================================================================
 * Public API: mesh and weights
 * ========================================================================== */

gint
lrg_editor_skeleton2d_editor_add_vertex (LrgEditorSkeleton2DEditor  *self,
                                         gfloat                      x,
                                         gfloat                      y,
                                         GError                    **error)
{
	LrgSkinnedSprite           *sprite;
	g_autoptr(LrgSkinnedSprite) next = NULL;
	g_autoptr(GArray)           tris = NULL;
	gfloat                      fit[6];
	gfloat                      u = 0.0f, v = 0.0f;
	gint                        index;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), -1);
	if (!require_sprite (self, &sprite, error))
		return -1;
	if (uv_fit (sprite, fit))
	{
		u = fit[0] * x + fit[1] * y + fit[2];
		v = fit[3] * x + fit[4] * y + fit[5];
	}
	next = sprite_rebuild (sprite, -1, NULL);
	index = lrg_skinned_sprite_add_vertex (next, x, y, u, v);
	if (index < 0)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "the mesh is full");
		return -1;
	}
	/* inherit the weights of the nearest vertex so the new one moves along */
	{
		guint  i, best = G_MAXUINT;
		gfloat bd = G_MAXFLOAT;

		for (i = 0; i < (guint) index; i++)
		{
			gfloat vx, vy, d;

			lrg_skinned_sprite_get_vertex (next, i, &vx, &vy, NULL, NULL);
			d = hypotf (vx - x, vy - y);
			if (d < bd)
			{
				bd = d;
				best = i;
			}
		}
		if (best != G_MAXUINT)
		{
			gint   b[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
			gfloat w[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
			guint  nb = lrg_skinned_sprite_get_weights (next, best, b, w);

			if (nb > 0)
				lrg_skinned_sprite_set_weights (next, (guint) index, nb, b, w);
		}
	}
	tris = delaunay (next);
	{
		g_autoptr(LrgSkinnedSprite) final = sprite_rebuild (next, -1, tris);

		g_clear_object (&next);
		next = g_steal_pointer (&final);
	}
	replace_sprite (self, next);
	self->sel_vertex = index;
	if (!commit (self, "Add vertex", error))
		return -1;
	return index;
}

gboolean
lrg_editor_skeleton2d_editor_move_vertex (LrgEditorSkeleton2DEditor  *self,
                                          guint                       index,
                                          gfloat                      x,
                                          gfloat                      y,
                                          GError                    **error)
{
	LrgSkinnedSprite *sprite;
	gfloat            fit[6], u, v;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_sprite (self, &sprite, error))
		return FALSE;
	if (index >= lrg_skinned_sprite_get_vertex_count (sprite))
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no vertex %u", index);
		return FALSE;
	}
	lrg_skinned_sprite_get_vertex (sprite, index, NULL, NULL, &u, &v);
	if (uv_fit (sprite, fit))
	{
		u = fit[0] * x + fit[1] * y + fit[2];
		v = fit[3] * x + fit[4] * y + fit[5];
	}
	lrg_skinned_sprite_set_vertex (sprite, index, x, y, u, v);
	return commit (self, "Move vertex", error);
}

gboolean
lrg_editor_skeleton2d_editor_remove_vertex (LrgEditorSkeleton2DEditor  *self,
                                            guint                       index,
                                            GError                    **error)
{
	LrgSkinnedSprite           *sprite;
	g_autoptr(LrgSkinnedSprite) cut = NULL;
	g_autoptr(LrgSkinnedSprite) next = NULL;
	g_autoptr(GArray)           tris = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_sprite (self, &sprite, error))
		return FALSE;
	if (index >= lrg_skinned_sprite_get_vertex_count (sprite))
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no vertex %u", index);
		return FALSE;
	}
	if (lrg_skinned_sprite_get_vertex_count (sprite) <= 3)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "a mesh keeps at least three vertices");
		return FALSE;
	}
	cut = sprite_rebuild (sprite, (gint) index, NULL);
	tris = delaunay (cut);
	next = sprite_rebuild (cut, -1, tris);
	replace_sprite (self, next);
	self->sel_vertex = -1;
	return commit (self, "Remove vertex", error);
}

gboolean
lrg_editor_skeleton2d_editor_triangulate (LrgEditorSkeleton2DEditor  *self,
                                          GError                    **error)
{
	LrgSkinnedSprite           *sprite;
	g_autoptr(LrgSkinnedSprite) next = NULL;
	g_autoptr(GArray)           tris = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_sprite (self, &sprite, error))
		return FALSE;
	tris = delaunay (sprite);
	if (tris->len == 0)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "the mesh needs three vertices that are not in a line");
		return FALSE;
	}
	next = sprite_rebuild (sprite, -1, tris);
	replace_sprite (self, next);
	return commit (self, "Triangulate", error);
}

gboolean
lrg_editor_skeleton2d_editor_auto_weights (LrgEditorSkeleton2DEditor  *self,
                                           const gint                 *bones,
                                           guint                       n_bones,
                                           gfloat                      falloff,
                                           guint                       max_influences,
                                           guint                       smooth_iterations,
                                           GError                    **error)
{
	LrgSkinnedSprite *sprite;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_sprite (self, &sprite, error))
		return FALSE;
	if (!lrg_skeleton2d_compute_auto_weights (self->skel, sprite, bones, n_bones, falloff > 0.0f ? falloff : 4.0f,
	                                          CLAMP (max_influences, 1u, (guint) LRG_SKINNED_SPRITE_MAX_WEIGHTS),
	                                          smooth_iterations))
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "automatic weights failed (no bones or no vertices)");
		reload (self);
		return FALSE;
	}
	return commit (self, "Automatic weights", error);
}

gint
lrg_editor_skeleton2d_editor_paint_weights (LrgEditorSkeleton2DEditor  *self,
                                            gint                        bone,
                                            gfloat                      x,
                                            gfloat                      y,
                                            gfloat                      radius,
                                            gfloat                      strength,
                                            GError                    **error)
{
	LrgSkinnedSprite *sprite;
	guint             changed;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), -1);
	if (!require_sprite (self, &sprite, error) || !bone_ok (self, bone, error))
		return -1;
	changed = brush_dab (sprite, bone, lrg_skeleton2d_get_slot_bone (self->skel, sel_slot_index (self)), x, y,
	                     radius, strength);
	if (changed > 0 && !commit (self, "Paint weights", error))
		return -1;
	return (gint) changed;
}

/* ==========================================================================
 * Public API: clips and keys
 * ========================================================================== */

gboolean
lrg_editor_skeleton2d_editor_add_clip (LrgEditorSkeleton2DEditor  *self,
                                       const gchar                *name,
                                       gfloat                      duration,
                                       gboolean                    loop,
                                       GError                    **error)
{
	g_autoptr(LrgSkeleton2DClip) clip = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_skel (self, error))
		return FALSE;
	if (name == NULL || *name == '\0')
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "a clip name cannot be empty");
		return FALSE;
	}
	if (lrg_skeleton2d_get_clip (self->skel, name) != NULL)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_EXISTS, "a clip named '%s' exists", name);
		return FALSE;
	}
	clip = lrg_skeleton2d_clip_new (name);
	lrg_skeleton2d_clip_set_duration (clip, MAX (duration, 0.0f));
	lrg_skeleton2d_clip_set_loop (clip, loop);
	lrg_skeleton2d_add_clip (self->skel, clip);
	g_free (self->clip);
	self->clip = g_strdup (name);
	self->time = 0.0f;
	return commit (self, "Add clip", error);
}

gboolean
lrg_editor_skeleton2d_editor_remove_clip (LrgEditorSkeleton2DEditor  *self,
                                          const gchar                *name,
                                          GError                    **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_skel (self, error))
		return FALSE;
	if (!lrg_skeleton2d_remove_clip (self->skel, name))
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no clip '%s'", name != NULL ? name : "(null)");
		return FALSE;
	}
	if (g_strcmp0 (self->clip, name) == 0)
		g_clear_pointer (&self->clip, g_free);
	return commit (self, "Remove clip", error);
}

gboolean
lrg_editor_skeleton2d_editor_set_clip_props (LrgEditorSkeleton2DEditor  *self,
                                             const gchar                *name,
                                             gfloat                      duration,
                                             gboolean                    loop,
                                             GError                    **error)
{
	LrgSkeleton2DClip *clip;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_skel (self, error))
		return FALSE;
	clip = lrg_skeleton2d_get_clip (self->skel, name);
	if (clip == NULL)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no clip '%s'", name != NULL ? name : "(null)");
		return FALSE;
	}
	lrg_skeleton2d_clip_set_duration (clip, MAX (duration, 0.0f));
	lrg_skeleton2d_clip_set_loop (clip, loop);
	return commit (self, "Edit clip", error);
}

static gboolean
require_clip (LrgEditorSkeleton2DEditor  *self,
              LrgSkeleton2DClip         **clip,
              GError                    **error)
{
	if (!require_skel (self, error))
		return FALSE;
	*clip = current_clip (self);
	if (*clip == NULL)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no clip selected");
		return FALSE;
	}
	return TRUE;
}

/* Index of the key of a timeline at @time (within 1 µs), or -1. */
static gint
key_at_time (LrgSkeleton2DClip    *clip,
             const gchar          *target,
             LrgSkeleton2DChannel  channel,
             gfloat                time)
{
	guint i, n = lrg_skeleton2d_clip_get_key_count (clip, target, channel);

	for (i = 0; i < n; i++)
	{
		gfloat t = 0.0f;

		if (channel == LRG_SKELETON2D_CHANNEL_ATTACHMENT)
			lrg_skeleton2d_clip_get_attachment_key (clip, target, i, &t, NULL);
		else
			lrg_skeleton2d_clip_get_key (clip, target, channel, i, &t, NULL, NULL);
		if (fabsf (t - time) <= 1e-6f)
			return (gint) i;
	}
	return -1;
}

/* Adds a key to @clip without committing. */
static gboolean
put_key (LrgEditorSkeleton2DEditor  *self,
         LrgSkeleton2DClip          *clip,
         const gchar                *target,
         LrgSkeleton2DChannel        channel,
         gfloat                      time,
         const gfloat               *values,
         guint                       n_values,
         const gchar                *attachment,
         LrgSkeleton2DCurve          curve,
         const gfloat               *bezier,
         GError                    **error)
{
	gfloat v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	guint  i;
	gint   index;
	gboolean slot_channel = channel == LRG_SKELETON2D_CHANNEL_COLOR || channel == LRG_SKELETON2D_CHANNEL_ATTACHMENT;

	if (target == NULL || (slot_channel ? lrg_skeleton2d_find_slot (self->skel, target)
	                                    : lrg_skeleton2d_find_bone (self->skel, target)) < 0)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no %s '%s'", slot_channel ? "slot" : "bone",
		           target != NULL ? target : "(null)");
		return FALSE;
	}
	if (channel == LRG_SKELETON2D_CHANNEL_SCALE || channel == LRG_SKELETON2D_CHANNEL_COLOR)
		v[0] = v[1] = v[2] = v[3] = 1.0f;
	for (i = 0; values != NULL && i < MIN (n_values, 4u); i++)
		v[i] = values[i];
	time = MAX (time, 0.0f);
	switch (channel)
	{
	case LRG_SKELETON2D_CHANNEL_TRANSLATE:
		lrg_skeleton2d_clip_add_translate_key (clip, target, time, v[0], v[1], curve);
		break;
	case LRG_SKELETON2D_CHANNEL_ROTATE:
		lrg_skeleton2d_clip_add_rotate_key (clip, target, time, v[0], curve);
		break;
	case LRG_SKELETON2D_CHANNEL_SCALE:
		lrg_skeleton2d_clip_add_scale_key (clip, target, time, v[0], v[1], curve);
		break;
	case LRG_SKELETON2D_CHANNEL_COLOR:
		lrg_skeleton2d_clip_add_color_key (clip, target, time, v[0], v[1], v[2], v[3], curve);
		break;
	case LRG_SKELETON2D_CHANNEL_ATTACHMENT:
		lrg_skeleton2d_clip_add_attachment_key (clip, target, time, attachment);
		return TRUE;
	default:
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "unknown channel %d", (gint) channel);
		return FALSE;
	}
	if (curve == LRG_SKELETON2D_CURVE_BEZIER && bezier != NULL)
	{
		index = key_at_time (clip, target, channel, time);
		if (index >= 0)
			lrg_skeleton2d_clip_set_key_bezier (clip, target, channel, (guint) index, CLAMP (bezier[0], 0.0f, 1.0f),
			                                    bezier[1], CLAMP (bezier[2], 0.0f, 1.0f), bezier[3]);
	}
	return TRUE;
}

gboolean
lrg_editor_skeleton2d_editor_set_key (LrgEditorSkeleton2DEditor  *self,
                                      const gchar                *target,
                                      LrgSkeleton2DChannel        channel,
                                      gfloat                      time,
                                      const gfloat               *values,
                                      guint                       n_values,
                                      const gchar                *attachment,
                                      LrgSkeleton2DCurve          curve,
                                      const gfloat               *bezier,
                                      GError                    **error)
{
	LrgSkeleton2DClip *clip;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_clip (self, &clip, error))
		return FALSE;
	if (!put_key (self, clip, target, channel, time, values, n_values, attachment, curve, bezier, error))
		return FALSE;
	return commit (self, "Set key", error);
}

gboolean
lrg_editor_skeleton2d_editor_remove_key (LrgEditorSkeleton2DEditor  *self,
                                         const gchar                *target,
                                         LrgSkeleton2DChannel        channel,
                                         guint                       index,
                                         GError                    **error)
{
	LrgSkeleton2DClip *clip;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_clip (self, &clip, error))
		return FALSE;
	if (!lrg_skeleton2d_clip_remove_key (clip, target, channel, index))
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no key %u on %s/%s", index,
		           target != NULL ? target : "(null)", channel_nicks[CLAMP ((gint) channel, 0, 4)]);
		return FALSE;
	}
	return commit (self, "Remove key", error);
}

/* Sets the time of key @index in the scratch document (YAML surgery keeps
 * every field, the Bézier control points included). */
static gboolean
scratch_move_key (LrgEditorSkeleton2DEditor  *self,
                  LrgEditorDocument          *s,
                  const gchar                *target,
                  LrgSkeleton2DChannel        channel,
                  guint                       index,
                  gfloat                      time,
                  GError                    **error)
{
	g_autofree gchar *tp = timeline_path (s, clip_index (self, self->clip), target, channel);
	g_autofree gchar *kp = NULL;

	if (tp == NULL || (gint) index >= seq_len (s, tp))
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no key %u on %s/%s", index,
		           target != NULL ? target : "(null)", channel_nicks[CLAMP ((gint) channel, 0, 4)]);
		return FALSE;
	}
	kp = g_strdup_printf ("%s/%u/time", tp, index);
	return lrg_editor_document_set_double (s, kp, round ((gdouble) MAX (time, 0.0f) * 10000.0) / 10000.0, 0, error);
}

gboolean
lrg_editor_skeleton2d_editor_move_key (LrgEditorSkeleton2DEditor  *self,
                                       const gchar                *target,
                                       LrgSkeleton2DChannel        channel,
                                       guint                       index,
                                       gfloat                      time,
                                       GError                    **error)
{
	LrgSkeleton2DClip           *clip;
	g_autoptr(LrgEditorDocument) s = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_clip (self, &clip, error))
		return FALSE;
	s = scratch_doc (self);
	if (s == NULL)
		return require_skel (self, error);
	if (!scratch_move_key (self, s, target, channel, index, time, error))
		return FALSE;
	return apply_scratch (self, "Move key", s, error);
}

/* Keys @bone's current local pose (offsets from setup) at the playhead
 * into @clip, without committing. */
static void
put_pose_keys (LrgEditorSkeleton2DEditor *self,
               LrgSkeleton2DClip         *clip,
               gint                       bone,
               gboolean                   translate,
               gboolean                   rotate,
               gboolean                   scale)
{
	const gchar *name = lrg_skeleton2d_get_bone_name (self->skel, bone);
	gfloat       lx, ly, lr, lsx, lsy, sx, sy, sr, ssx, ssy;

	lrg_skeleton2d_get_bone_local (self->skel, bone, &lx, &ly, &lr, &lsx, &lsy);
	lrg_skeleton2d_get_bone_setup (self->skel, bone, &sx, &sy, &sr, &ssx, &ssy);
	if (translate)
		lrg_skeleton2d_clip_add_translate_key (clip, name, self->time, lx - sx, ly - sy, LRG_SKELETON2D_CURVE_LINEAR);
	if (rotate)
		lrg_skeleton2d_clip_add_rotate_key (clip, name, self->time, lr - sr, LRG_SKELETON2D_CURVE_LINEAR);
	if (scale)
		lrg_skeleton2d_clip_add_scale_key (clip, name, self->time, fabsf (ssx) > 1e-6f ? lsx / ssx : 1.0f,
		                                   fabsf (ssy) > 1e-6f ? lsy / ssy : 1.0f, LRG_SKELETON2D_CURVE_LINEAR);
}

gboolean
lrg_editor_skeleton2d_editor_key_pose (LrgEditorSkeleton2DEditor  *self,
                                       gint                        bone,
                                       GError                    **error)
{
	LrgSkeleton2DClip *clip;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (!require_clip (self, &clip, error) || !bone_ok (self, bone, error))
		return FALSE;
	pose (self);
	put_pose_keys (self, clip, bone, TRUE, TRUE, TRUE);
	return commit (self, "Key pose", error);
}

/* ==========================================================================
 * Canvas: painting
 * ========================================================================== */

/* World matrix of @bone (the view for -1), as of the last pose(). */
static void
bone_world (LrgEditorSkeleton2DEditor *self,
            gint                       bone,
            gfloat                    *m)
{
	if (bone >= 0 && lrg_skeleton2d_get_bone_world_matrix (self->skel, bone, m))
		return;
	view_origin (self, &m[4], &m[5]);
	m[0] = self->zoom;
	m[1] = 0.0f;
	m[2] = 0.0f;
	m[3] = self->zoom;
}

static void
color_mix (const GrlColor *a,
           const GrlColor *b,
           gfloat          t,
           GrlColor       *out)
{
	t = CLAMP (t, 0.0f, 1.0f);
	out->r = (guint8) ((gfloat) a->r + ((gfloat) b->r - (gfloat) a->r) * t);
	out->g = (guint8) ((gfloat) a->g + ((gfloat) b->g - (gfloat) a->g) * t);
	out->b = (guint8) ((gfloat) a->b + ((gfloat) b->b - (gfloat) a->b) * t);
	out->a = (guint8) ((gfloat) a->a + ((gfloat) b->a - (gfloat) a->a) * t);
}

/* The weight heat ramp: blue (0) -> green -> yellow -> red (1). */
static void
heat_color (gfloat    w,
            guint8    alpha,
            GrlColor *out)
{
	static const GrlColor ramp[] = {
		{ 40, 60, 200, 255 }, { 40, 190, 120, 255 }, { 240, 220, 50, 255 }, { 235, 60, 50, 255 }
	};
	gfloat t = CLAMP (w, 0.0f, 1.0f) * 3.0f;
	guint  i = MIN ((guint) t, 2u);

	color_mix (&ramp[i], &ramp[i + 1], t - (gfloat) i, out);
	out->a = alpha;
}

/* Deformed positions of the selected attachment in canvas space (also when
 * the slot shows another one). */
static const gfloat *
sel_deformed (LrgEditorSkeleton2DEditor *self,
              LrgSkinnedSprite          *sprite,
              guint                     *n_floats)
{
	const gfloat *palette;
	guint         np = 0;
	gint          slot = sel_slot_index (self);

	palette = lrg_skeleton2d_get_palette (self->skel, &np);
	lrg_skinned_sprite_deform (sprite, palette, np, slot >= 0 ? lrg_skeleton2d_get_slot_bone (self->skel, slot) : -1);
	return lrg_skinned_sprite_get_deformed (sprite, n_floats);
}

/* The rig itself, immediate mode (textures need a window). */
static void
rig_draw_callback (gpointer user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);

	if (self->skel == NULL || !IsWindowReady ())
		return;
	lrg_skeleton2d_draw (self->skel, NULL);
	rlDrawRenderBatchActive ();
}

static void
paint_bone (LrgPaintList   *list,
            gfloat          ox,
            gfloat          oy,
            gfloat          tx,
            gfloat          ty,
            const GrlColor *fill,
            const GrlColor *edge,
            gboolean        selected)
{
	gfloat dx = tx - ox, dy = ty - oy, len = hypotf (dx, dy);
	gfloat nx, ny, w, kx, ky;

	if (len < 2.0f)
	{
		lrg_paint_list_circle (list, ox, oy, selected ? 6.0f : 5.0f, fill);
		lrg_paint_list_line (list, ox - 7.0f, oy, ox + 7.0f, oy, 1.5f, edge);
		lrg_paint_list_line (list, ox, oy - 7.0f, ox, oy + 7.0f, 1.5f, edge);
		return;
	}
	nx = -dy / len;
	ny = dx / len;
	w = CLAMP (len * 0.12f, 3.0f, 9.0f);
	kx = ox + dx * 0.18f;
	ky = oy + dy * 0.18f;
	/* a kite: wide near the joint, pointing at the tip */
	lrg_paint_list_triangle (list, ox, oy, kx + nx * w, ky + ny * w, tx, ty, fill);
	lrg_paint_list_triangle (list, ox, oy, tx, ty, kx - nx * w, ky - ny * w, fill);
	lrg_paint_list_line (list, ox, oy, kx + nx * w, ky + ny * w, selected ? 2.0f : 1.2f, edge);
	lrg_paint_list_line (list, kx + nx * w, ky + ny * w, tx, ty, selected ? 2.0f : 1.2f, edge);
	lrg_paint_list_line (list, ox, oy, kx - nx * w, ky - ny * w, selected ? 2.0f : 1.2f, edge);
	lrg_paint_list_line (list, kx - nx * w, ky - ny * w, tx, ty, selected ? 2.0f : 1.2f, edge);
}

static const gchar *
tool_hint (LrgEditorSkeleton2DEditor *self)
{
	if (self->parse_error != NULL)
		return "the file does not load: fix it in the issues list";
	if (self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE)
		return current_clip (self) == NULL ? "add a clip to animate" : "drag joints / bones to key  ·  K keys the pose";
	switch (self->tool)
	{
	case LRG_EDITOR_SKELETON2D_TOOL_BONE:
		return sel_bone_index (self) >= 0 ? "drag to add a child bone" : "drag to add a root bone";
	case LRG_EDITOR_SKELETON2D_TOOL_MESH:
		return sel_sprite (self, NULL) == NULL ? "select an attachment to edit its mesh"
		       : "click adds a vertex  ·  drag moves  ·  right click / Del removes";
	case LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS:
		return (sel_sprite (self, NULL) == NULL || sel_bone_index (self) < 0)
		       ? "select a bone and an attachment to paint" : "paint the bone's weight  ·  Shift erases";
	case LRG_EDITOR_SKELETON2D_TOOL_SELECT:
	default:
		return "drag a joint to move  ·  drag a bone to rotate  ·  drop images to attach";
	}
}

static void
canvas_paint (LrgCustomWidget *widget,
              LrgPaintList    *list,
              gfloat           x,
              gfloat           y,
              gfloat           width,
              gfloat           height,
              gpointer         user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	LrgTheme                  *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	const GrlColor            *window = lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW);
	const GrlColor            *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor            *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor            *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	const GrlColor            *border = lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER);
	GrlFont                   *font = lrg_theme_get_ui_font (theme);
	gfloat                     fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                     ox, oy, step, gx, gy;
	GrlColor                   c, c2;
	LrgSkinnedSprite          *sprite;
	gint                       sel_bone, i, n;

	if (width <= 0.0f || height <= 0.0f)
		return;
	/* fit the rig on the first paint, and again whenever it outgrows the
	 * canvas until the user pans or zooms */
	if (self->skel != NULL && self->parse_error == NULL && self->drag == DRAG_NONE &&
	    (!self->framed || (!self->user_view && !rig_in_view (self, x, y, width, height))))
		lrg_editor_skeleton2d_editor_frame_all (self);
	lrg_paint_list_push_clip (list, x, y, width, height);

	/* backdrop: the window colour, a grid that keeps ~32 px cells, axes */
	lrg_paint_list_rect (list, x, y, width, height, 0.0f, window);
	view_origin (self, &ox, &oy);
	step = 10.0f;
	while (step * self->zoom < 32.0f)
		step *= 2.0f;
	while (step * self->zoom > 96.0f && step > 1.0f)
		step *= 0.5f;
	lrg_paint_color_with_alpha (border, 0.35f, &c);
	for (gx = ox + floorf ((x - ox) / (step * self->zoom)) * step * self->zoom; gx < x + width; gx += step * self->zoom)
		lrg_paint_list_line (list, gx, y, gx, y + height, 1.0f, &c);
	for (gy = oy + floorf ((y - oy) / (step * self->zoom)) * step * self->zoom; gy < y + height; gy += step * self->zoom)
		lrg_paint_list_line (list, x, gy, x + width, gy, 1.0f, &c);
	lrg_paint_color_with_alpha (accent, 0.35f, &c);
	lrg_paint_list_line (list, ox, y, ox, y + height, 1.5f, &c);
	lrg_paint_list_line (list, x, oy, x + width, oy, 1.5f, &c);

	if (self->skel == NULL)
	{
		lrg_paint_list_pop_clip (list);
		return;
	}

	/* the rig */
	pose (self);
	lrg_skeleton2d_deform (self->skel);
	lrg_paint_list_callback (list, rig_draw_callback, g_object_ref (self), g_object_unref);

	/* untextured attachments (no image yet) as soft flat shapes */
	n = (gint) lrg_skeleton2d_get_slot_count (self->skel);
	lrg_paint_color_with_alpha (muted, 0.30f, &c);
	for (i = 0; i < n; i++)
	{
		LrgSkinnedSprite *sp = lrg_skeleton2d_get_attachment (self->skel, i, NULL);
		const gfloat     *dv;
		guint             nf = 0, t;

		if (sp == NULL || lrg_skinned_sprite_get_texture (sp) != NULL)
			continue;
		dv = lrg_skinned_sprite_get_deformed (sp, &nf);
		for (t = 0; t < lrg_skinned_sprite_get_triangle_count (sp); t++)
		{
			guint a, b, d;

			lrg_skinned_sprite_get_triangle (sp, t, &a, &b, &d);
			if ((a + 1) * 2 > nf || (b + 1) * 2 > nf || (d + 1) * 2 > nf)
				continue;
			lrg_paint_list_triangle (list, dv[a * 2], dv[a * 2 + 1], dv[b * 2], dv[b * 2 + 1], dv[d * 2],
			                         dv[d * 2 + 1], &c);
		}
	}

	/* the selected attachment: weight heat, wireframe, vertices */
	sprite = sel_sprite (self, NULL);
	sel_bone = sel_bone_index (self);
	if (sprite != NULL)
	{
		guint         nf = 0, t, nv = lrg_skinned_sprite_get_vertex_count (sprite);
		const gfloat *dv = sel_deformed (self, sprite, &nf);
		gint          slot_bone = lrg_skeleton2d_get_slot_bone (self->skel, sel_slot_index (self));
		gboolean      weights = self->tool == LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS &&
		                        self->mode == LRG_EDITOR_SKELETON2D_MODE_SETUP;
		gboolean      mesh = self->tool == LRG_EDITOR_SKELETON2D_TOOL_MESH &&
		                     self->mode == LRG_EDITOR_SKELETON2D_MODE_SETUP;

		for (t = 0; t < lrg_skinned_sprite_get_triangle_count (sprite); t++)
		{
			guint a, b, d;

			lrg_skinned_sprite_get_triangle (sprite, t, &a, &b, &d);
			if ((a + 1) * 2 > nf || (b + 1) * 2 > nf || (d + 1) * 2 > nf)
				continue;
			if (weights && sel_bone >= 0)
			{
				gfloat w = (vertex_weight (sprite, a, sel_bone, slot_bone) +
				            vertex_weight (sprite, b, sel_bone, slot_bone) +
				            vertex_weight (sprite, d, sel_bone, slot_bone)) / 3.0f;

				heat_color (w, 150, &c2);
				lrg_paint_list_triangle (list, dv[a * 2], dv[a * 2 + 1], dv[b * 2], dv[b * 2 + 1], dv[d * 2],
				                         dv[d * 2 + 1], &c2);
			}
			lrg_paint_color_with_alpha (accent, mesh || weights ? 0.95f : 0.55f, &c);
			lrg_paint_list_line (list, dv[a * 2], dv[a * 2 + 1], dv[b * 2], dv[b * 2 + 1], 1.0f, &c);
			lrg_paint_list_line (list, dv[b * 2], dv[b * 2 + 1], dv[d * 2], dv[d * 2 + 1], 1.0f, &c);
			lrg_paint_list_line (list, dv[d * 2], dv[d * 2 + 1], dv[a * 2], dv[a * 2 + 1], 1.0f, &c);
		}
		for (t = 0; (mesh || weights) && t < nv && (t + 1) * 2 <= nf; t++)
		{
			gboolean hot = mesh && (gint) t == self->sel_vertex;

			if (weights && sel_bone >= 0)
				heat_color (vertex_weight (sprite, t, sel_bone, slot_bone), 255, &c2);
			else
				c2 = *accent;
			lrg_paint_list_circle (list, dv[t * 2], dv[t * 2 + 1], hot ? VERTEX_RADIUS : VERTEX_RADIUS - 2.0f, window);
			lrg_paint_list_circle (list, dv[t * 2], dv[t * 2 + 1], hot ? VERTEX_RADIUS - 1.5f : VERTEX_RADIUS - 3.5f,
			                       &c2);
		}
	}

	/* bones: connector to the parent, kite, joint */
	n = (gint) lrg_skeleton2d_get_bone_count (self->skel);
	for (i = 0; i < n; i++)
	{
		gfloat   bx, by, tx, ty;
		gint     parent = lrg_skeleton2d_get_bone_parent (self->skel, i);
		gboolean selected = i == sel_bone;
		GrlColor fill, edge;

		lrg_skeleton2d_get_bone_world_position (self->skel, i, &bx, &by);
		lrg_skeleton2d_get_bone_world_tip (self->skel, i, &tx, &ty);
		if (parent >= 0)
		{
			gfloat px, py;

			lrg_skeleton2d_get_bone_world_tip (self->skel, parent, &px, &py);
			if (hypotf (px - bx, py - by) > 1.5f)
			{
				lrg_paint_color_with_alpha (muted, 0.5f, &c);
				lrg_paint_list_line (list, px, py, bx, by, 1.0f, &c);
			}
		}
		if (selected)
		{
			lrg_paint_color_with_alpha (accent, 0.85f, &fill);
			edge = *text;
		}
		else if (i == self->hover_bone)
		{
			lrg_paint_color_with_alpha (accent, 0.55f, &fill);
			lrg_paint_color_with_alpha (text, 0.8f, &edge);
		}
		else
		{
			lrg_paint_color_with_alpha (muted, 0.45f, &fill);
			lrg_paint_color_with_alpha (text, 0.65f, &edge);
		}
		paint_bone (list, bx, by, tx, ty, &fill, &edge, selected);
		lrg_paint_list_circle (list, bx, by, selected ? 5.0f : 4.0f, &edge);
		lrg_paint_list_circle (list, bx, by, selected ? 3.0f : 2.5f, selected ? accent : window);
	}

	/* bone tool preview */
	if (self->drag == DRAG_CREATE_BONE && hypotf (self->ptr_x - self->press_x, self->ptr_y - self->press_y) >= CLICK_SLOP)
	{
		lrg_paint_color_with_alpha (accent, 0.5f, &c);
		paint_bone (list, self->press_x, self->press_y, self->ptr_x, self->ptr_y, &c, accent, TRUE);
	}

	/* weight brush */
	if (self->tool == LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS && self->mode == LRG_EDITOR_SKELETON2D_MODE_SETUP &&
	    self->ptr_inside)
	{
		gfloat r = self->brush_radius * self->zoom;

		lrg_paint_color_with_alpha (self->erase ? lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER) : text, 0.9f,
		                            &c);
		lrg_paint_list_border (list, self->ptr_x - r, self->ptr_y - r, r * 2.0f, r * 2.0f, r, 1.5f, &c);
	}

	/* drop target */
	if (self->drop_hover)
	{
		lrg_paint_color_with_alpha (accent, 0.10f, &c);
		lrg_paint_list_rect (list, x + 4.0f, y + 4.0f, width - 8.0f, height - 8.0f, 8.0f, &c);
		lrg_paint_list_border (list, x + 4.0f, y + 4.0f, width - 8.0f, height - 8.0f, 8.0f, 2.0f, accent);
	}

	/* HUD: mode, tool and hint on a pill; the playhead in animation mode */
	{
		g_autofree gchar *line = NULL;
		gfloat            tw = 0.0f;
		LrgSkeleton2DClip *clip = current_clip (self);

		if (self->drop_hover && self->drop_label != NULL)
			line = g_strdup (self->drop_label);
		else if (self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE)
			line = g_strdup_printf ("Animate  ·  %s  %.2fs / %.2fs  ·  %s", clip != NULL ? lrg_skeleton2d_clip_get_name (clip) : "no clip",
			                        self->time, clip != NULL ? lrg_skeleton2d_clip_get_duration (clip) : 0.0f,
			                        tool_hint (self));
		else
			line = g_strdup_printf ("Setup  ·  %s  ·  %s", tool_nicks[self->tool], tool_hint (self));
		lrg_paint_measure_text (font, line, fs, 0.0f, &tw, NULL);
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), 0.88f, &c);
		lrg_paint_list_rect (list, x + 10.0f, y + 10.0f, MIN (tw + 20.0f, width - 20.0f), fs + 12.0f,
		                     (fs + 12.0f) * 0.5f, &c);
		lrg_paint_list_border (list, x + 10.0f, y + 10.0f, MIN (tw + 20.0f, width - 20.0f), fs + 12.0f,
		                       (fs + 12.0f) * 0.5f, 1.0f, border);
		lrg_paint_list_text (list, font, line, x + 20.0f, y + 16.0f, fs, 0.0f, text);
	}
	if (self->parse_error != NULL)
	{
		const GrlColor *danger = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);
		gfloat          tw = 0.0f;

		lrg_paint_measure_text (font, self->parse_error, fs, 0.0f, &tw, NULL);
		lrg_paint_color_with_alpha (danger, 0.9f, &c);
		lrg_paint_list_rect (list, x + 10.0f, y + height - fs - 22.0f, MIN (tw + 20.0f, width - 20.0f), fs + 12.0f,
		                     6.0f, &c);
		lrg_paint_list_text (list, font, self->parse_error, x + 20.0f, y + height - fs - 16.0f, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_ON_ACCENT));
	}
	lrg_paint_list_pop_clip (list);
}

/* ==========================================================================
 * Canvas: interaction
 * ========================================================================== */

static gboolean
point_in_triangle (gfloat px,
                   gfloat py,
                   const gfloat *a,
                   const gfloat *b,
                   const gfloat *c)
{
	gfloat d1 = (px - b[0]) * (a[1] - b[1]) - (a[0] - b[0]) * (py - b[1]);
	gfloat d2 = (px - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (py - c[1]);
	gfloat d3 = (px - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (py - a[1]);
	gboolean neg = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
	gboolean pos = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;

	return !(neg && pos);
}

/* The front-most slot whose shown attachment covers the canvas point
 * (after pose() + deform), or -1. */
static gint
pick_slot (LrgEditorSkeleton2DEditor *self,
           gfloat                     px,
           gfloat                     py)
{
	const gint *order;
	guint       n = 0;
	gint        k;

	order = lrg_skeleton2d_get_draw_order (self->skel, &n);
	for (k = (gint) n - 1; k >= 0; k--)
	{
		LrgSkinnedSprite *sp = lrg_skeleton2d_get_attachment (self->skel, order[k], NULL);
		const gfloat     *dv;
		guint             nf = 0, t;

		if (sp == NULL)
			continue;
		dv = lrg_skinned_sprite_get_deformed (sp, &nf);
		for (t = 0; t < lrg_skinned_sprite_get_triangle_count (sp); t++)
		{
			guint a, b, c;

			lrg_skinned_sprite_get_triangle (sp, t, &a, &b, &c);
			if ((a + 1) * 2 > nf || (b + 1) * 2 > nf || (c + 1) * 2 > nf)
				continue;
			if (point_in_triangle (px, py, dv + a * 2, dv + b * 2, dv + c * 2))
				return order[k];
		}
	}
	return -1;
}

/* A bone whose joint is within JOINT_RADIUS of the point (the selected
 * bone first, then the last one: children win), or -1. */
static gint
pick_joint (LrgEditorSkeleton2DEditor *self,
            gfloat                     px,
            gfloat                     py)
{
	gint i, sel = sel_bone_index (self);

	if (sel >= 0)
	{
		gfloat bx, by;

		lrg_skeleton2d_get_bone_world_position (self->skel, sel, &bx, &by);
		if (hypotf (bx - px, by - py) <= JOINT_RADIUS)
			return sel;
	}
	for (i = (gint) lrg_skeleton2d_get_bone_count (self->skel) - 1; i >= 0; i--)
	{
		gfloat bx, by;

		lrg_skeleton2d_get_bone_world_position (self->skel, i, &bx, &by);
		if (hypotf (bx - px, by - py) <= JOINT_RADIUS)
			return i;
	}
	return -1;
}

/* The vertex of the selected attachment under the canvas point, or -1. */
static gint
pick_vertex (LrgEditorSkeleton2DEditor *self,
             LrgSkinnedSprite          *sprite,
             gfloat                     px,
             gfloat                     py)
{
	guint         nf = 0, i;
	const gfloat *dv = sel_deformed (self, sprite, &nf);
	gint          best = -1;
	gfloat        bd = VERTEX_RADIUS + 2.0f;

	for (i = 0; (i + 1) * 2 <= nf; i++)
	{
		gfloat d = hypotf (dv[i * 2] - px, dv[i * 2 + 1] - py);

		if (d <= bd)
		{
			bd = d;
			best = (gint) i;
		}
	}
	return best;
}

static void
set_selection (LrgEditorSkeleton2DEditor *self,
               gint                       bone,
               gint                       slot,
               const gchar               *attachment)
{
	g_free (self->sel_bone);
	self->sel_bone = bone >= 0 ? g_strdup (lrg_skeleton2d_get_bone_name (self->skel, bone)) : NULL;
	g_free (self->sel_slot);
	self->sel_slot = slot >= 0 ? g_strdup (lrg_skeleton2d_get_slot_name (self->skel, slot)) : NULL;
	g_free (self->sel_attachment);
	self->sel_attachment = g_strdup (attachment);
	self->sel_vertex = -1;
	sync_widgets (self);
}

/* The live part of a bone drag: the setup pose, or a key at the playhead. */
static void
drag_bone_to (LrgEditorSkeleton2DEditor *self,
              gint                       bone,
              gboolean                   rotate,
              gfloat                     px,
              gfloat                     py)
{
	LrgSkeleton2DClip *clip = current_clip (self);
	gboolean           animate = self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE;
	gfloat             sx, sy, sr, ssx, ssy;

	if (animate && clip == NULL)
		return;
	pose (self);
	lrg_skeleton2d_get_bone_setup (self->skel, bone, &sx, &sy, &sr, &ssx, &ssy);
	if (rotate)
	{
		gfloat bx, by, a, delta;

		lrg_skeleton2d_get_bone_world_position (self->skel, bone, &bx, &by);
		a = atan2f (py - by, px - bx) * DEG;
		delta = fmodf (a - self->grab_angle + 540.0f, 360.0f) - 180.0f;
		if (animate)
			lrg_skeleton2d_clip_add_rotate_key (clip, lrg_skeleton2d_get_bone_name (self->skel, bone), self->time,
			                                    self->grab_rot + delta - sr, LRG_SKELETON2D_CURVE_LINEAR);
		else
			lrg_skeleton2d_set_bone_setup (self->skel, bone, sx, sy, self->grab_rot + delta, ssx, ssy);
	}
	else
	{
		gfloat lx, ly;

		canvas_to_parent (self, bone, px + self->grab_dx, py + self->grab_dy, &lx, &ly);
		if (animate)
			lrg_skeleton2d_clip_add_translate_key (clip, lrg_skeleton2d_get_bone_name (self->skel, bone), self->time,
			                                       lx - sx, ly - sy, LRG_SKELETON2D_CURVE_LINEAR);
		else
			lrg_skeleton2d_set_bone_setup (self->skel, bone, lx, ly, sr, ssx, ssy);
	}
	self->live = TRUE;
}

/* Creates the bone the bone tool dragged from the press to (px, py). */
static void
create_dragged_bone (LrgEditorSkeleton2DEditor *self,
                     gfloat                     px,
                     gfloat                     py)
{
	gint              parent = sel_bone_index (self);
	gfloat            m[6], inv[6], lx, ly, prot, rot, len;
	g_autoptr(GError) error = NULL;

	pose (self);
	bone_world (self, parent, m);
	if (!affine_invert (m, inv))
		return;
	affine_apply (inv, self->press_x, self->press_y, &lx, &ly);
	prot = atan2f (m[1], m[0]) * DEG;
	rot = atan2f (py - self->press_y, px - self->press_x) * DEG - prot;
	rot = fmodf (rot + 540.0f, 360.0f) - 180.0f;
	len = hypotf (px - self->press_x, py - self->press_y) / MAX (hypotf (m[0], m[1]), 1e-6f);
	if (lrg_editor_skeleton2d_editor_add_bone (self, NULL, parent, lx, ly, rot, len, &error) < 0)
		report (self, error);
}

static void
end_drag (LrgEditorSkeleton2DEditor *self,
          gboolean                   commit_it)
{
	DragKind          kind = self->drag;
	g_autoptr(GError) error = NULL;

	self->drag = DRAG_NONE;
	if (!self->live)
		return;
	if (!commit_it)
	{
		reload (self);
		return;
	}
	switch (kind)
	{
	case DRAG_MOVE_BONE:
		commit (self, self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE ? "Key translate" : "Move bone", &error);
		break;
	case DRAG_ROTATE_BONE:
		commit (self, self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE ? "Key rotate" : "Rotate bone", &error);
		break;
	case DRAG_VERTEX:
		commit (self, "Move vertex", &error);
		break;
	case DRAG_PAINT:
		commit (self, "Paint weights", &error);
		break;
	default:
		reload (self);
		break;
	}
	report (self, error);
}

static void
zoom_at (LrgEditorSkeleton2DEditor *self,
         gfloat                     px,
         gfloat                     py,
         gfloat                     factor)
{
	gfloat ox, oy, sx, sy, nz;

	view_origin (self, &ox, &oy);
	sx = (px - ox) / self->zoom;
	sy = (py - oy) / self->zoom;
	nz = CLAMP (self->zoom * factor, ZOOM_MIN, ZOOM_MAX);
	self->user_view = TRUE;
	/* keep the setup point under the pointer */
	self->pan_x = px - sx * nz - (ox - self->pan_x);
	self->pan_y = py - sy * nz - (oy - self->pan_y);
	self->zoom = nz;
}

static gboolean
canvas_press (LrgEditorSkeleton2DEditor *self,
              gint                       button,
              gfloat                     px,
              gfloat                     py,
              LrgInputModifiers          mods)
{
	g_autoptr(GError)  error = NULL;
	LrgSkinnedSprite  *sprite;
	gint               hit;

	self->press_x = px;
	self->press_y = py;
	if (button == GRL_MOUSE_BUTTON_MIDDLE || (button == GRL_MOUSE_BUTTON_LEFT && (mods & LRG_INPUT_MODIFIER_ALT)))
	{
		self->drag = DRAG_PAN;
		return TRUE;
	}
	if (self->skel == NULL || self->parse_error != NULL)
		return TRUE;
	pose (self);
	lrg_skeleton2d_deform (self->skel);
	sprite = sel_sprite (self, NULL);

	if (self->mode == LRG_EDITOR_SKELETON2D_MODE_SETUP && self->tool == LRG_EDITOR_SKELETON2D_TOOL_MESH)
	{
		gfloat sx, sy;

		if (sprite == NULL)
			return TRUE;
		hit = pick_vertex (self, sprite, px, py);
		if (button == GRL_MOUSE_BUTTON_RIGHT)
		{
			if (hit >= 0 && !lrg_editor_skeleton2d_editor_remove_vertex (self, (guint) hit, &error))
				report (self, error);
			return TRUE;
		}
		if (hit >= 0)
		{
			self->sel_vertex = hit;
			self->drag = DRAG_VERTEX;
			return TRUE;
		}
		lrg_editor_skeleton2d_editor_canvas_to_setup (self, px, py, &sx, &sy);
		if (lrg_editor_skeleton2d_editor_add_vertex (self, sx, sy, &error) < 0)
			report (self, error);
		return TRUE;
	}
	if (button != GRL_MOUSE_BUTTON_LEFT)
		return TRUE;

	if (self->mode == LRG_EDITOR_SKELETON2D_MODE_SETUP && self->tool == LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS)
	{
		gfloat sx, sy;
		gint   bone = sel_bone_index (self);

		if (sprite == NULL || bone < 0)
		{
			editor_app_set_status (app_of (self), "Select a bone and an attachment to paint weights");
			return TRUE;
		}
		self->erase = (mods & LRG_INPUT_MODIFIER_SHIFT) != 0;
		self->drag = DRAG_PAINT;
		lrg_editor_skeleton2d_editor_canvas_to_setup (self, px, py, &sx, &sy);
		if (brush_dab (sprite, bone, lrg_skeleton2d_get_slot_bone (self->skel, sel_slot_index (self)), sx, sy,
		               self->brush_radius, self->erase ? -self->brush_strength : self->brush_strength) > 0)
			self->live = TRUE;
		return TRUE;
	}
	if (self->mode == LRG_EDITOR_SKELETON2D_MODE_SETUP && self->tool == LRG_EDITOR_SKELETON2D_TOOL_BONE)
	{
		self->drag = DRAG_CREATE_BONE;
		return TRUE;
	}

	/* select tool (both modes): joints move, bodies rotate, meshes select */
	hit = pick_joint (self, px, py);
	if (hit >= 0)
	{
		gfloat bx, by;

		lrg_skeleton2d_get_bone_world_position (self->skel, hit, &bx, &by);
		self->grab_dx = bx - px;
		self->grab_dy = by - py;
		set_selection (self, hit, sel_slot_index (self), self->sel_attachment);
		self->drag = DRAG_MOVE_BONE;
		return TRUE;
	}
	hit = lrg_skeleton2d_pick_bone (self->skel, px, py, PICK_RADIUS);
	if (hit >= 0)
	{
		gfloat bx, by, lx, ly, lr, lsx, lsy;

		lrg_skeleton2d_get_bone_world_position (self->skel, hit, &bx, &by);
		lrg_skeleton2d_get_bone_local (self->skel, hit, &lx, &ly, &lr, &lsx, &lsy);
		self->grab_angle = atan2f (py - by, px - bx) * DEG;
		self->grab_rot = lr;
		set_selection (self, hit, sel_slot_index (self), self->sel_attachment);
		self->drag = DRAG_ROTATE_BONE;
		return TRUE;
	}
	hit = pick_slot (self, px, py);
	if (hit >= 0)
		set_selection (self, lrg_skeleton2d_get_slot_bone (self->skel, hit), hit,
		               lrg_skeleton2d_get_slot_attachment (self->skel, hit));
	else
		set_selection (self, -1, -1, NULL);
	self->drag = DRAG_PENDING;
	return TRUE;
}

static void
canvas_motion (LrgEditorSkeleton2DEditor *self,
               gfloat                     px,
               gfloat                     py)
{
	gfloat dx = px - self->ptr_x, dy = py - self->ptr_y;

	self->ptr_x = px;
	self->ptr_y = py;
	switch (self->drag)
	{
	case DRAG_PAN:
		self->pan_x += dx;
		self->pan_y += dy;
		self->user_view = TRUE;
		break;
	case DRAG_MOVE_BONE:
	case DRAG_ROTATE_BONE:
		if (!self->live && hypotf (px - self->press_x, py - self->press_y) < CLICK_SLOP)
			break;
		drag_bone_to (self, sel_bone_index (self), self->drag == DRAG_ROTATE_BONE, px, py);
		break;
	case DRAG_VERTEX:
	{
		LrgSkinnedSprite *sprite = sel_sprite (self, NULL);
		gfloat            sx, sy, u, v, fit[6];

		if (sprite == NULL || self->sel_vertex < 0)
			break;
		lrg_editor_skeleton2d_editor_canvas_to_setup (self, px, py, &sx, &sy);
		lrg_skinned_sprite_get_vertex (sprite, (guint) self->sel_vertex, NULL, NULL, &u, &v);
		if (uv_fit (sprite, fit))
		{
			u = fit[0] * sx + fit[1] * sy + fit[2];
			v = fit[3] * sx + fit[4] * sy + fit[5];
		}
		lrg_skinned_sprite_set_vertex (sprite, (guint) self->sel_vertex, sx, sy, u, v);
		self->live = TRUE;
		break;
	}
	case DRAG_PAINT:
	{
		LrgSkinnedSprite *sprite = sel_sprite (self, NULL);
		gint              bone = sel_bone_index (self);
		gfloat            sx, sy;

		if (sprite == NULL || bone < 0)
			break;
		lrg_editor_skeleton2d_editor_canvas_to_setup (self, px, py, &sx, &sy);
		if (brush_dab (sprite, bone, lrg_skeleton2d_get_slot_bone (self->skel, sel_slot_index (self)), sx, sy,
		               self->brush_radius, self->erase ? -self->brush_strength : self->brush_strength) > 0)
			self->live = TRUE;
		break;
	}
	case DRAG_NONE:
		if (self->skel != NULL && self->parse_error == NULL)
		{
			pose (self);
			self->hover_bone = pick_joint (self, px, py);
			if (self->hover_bone < 0)
				self->hover_bone = lrg_skeleton2d_pick_bone (self->skel, px, py, PICK_RADIUS);
		}
		break;
	default:
		break;
	}
}

static void
canvas_release (LrgEditorSkeleton2DEditor *self,
                gfloat                     px,
                gfloat                     py)
{
	if (self->drag == DRAG_CREATE_BONE)
	{
		self->drag = DRAG_NONE;
		if (hypotf (px - self->press_x, py - self->press_y) >= CLICK_SLOP)
			create_dragged_bone (self, px, py);
		else
		{
			gint hit;

			/* a click with the bone tool picks the parent for the next bone */
			pose (self);
			hit = pick_joint (self, px, py);
			if (hit < 0)
				hit = lrg_skeleton2d_pick_bone (self->skel, px, py, PICK_RADIUS);
			set_selection (self, hit, sel_slot_index (self), self->sel_attachment);
		}
		return;
	}
	end_drag (self, TRUE);
}

static gboolean
canvas_key (LrgEditorSkeleton2DEditor *self,
            GrlKey                     key,
            LrgInputModifiers          mods)
{
	g_autoptr(GError) error = NULL;

	if ((mods & (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_ALT | LRG_INPUT_MODIFIER_SUPER)) != 0)
		return FALSE;
	switch (key)
	{
	case GRL_KEY_ESCAPE:
		if (self->drag == DRAG_NONE)
			return FALSE;
		end_drag (self, FALSE);
		return TRUE;
	case GRL_KEY_DELETE:
	case GRL_KEY_BACKSPACE:
		if (self->mode == LRG_EDITOR_SKELETON2D_MODE_SETUP && self->tool == LRG_EDITOR_SKELETON2D_TOOL_MESH &&
		    self->sel_vertex >= 0)
		{
			if (!lrg_editor_skeleton2d_editor_remove_vertex (self, (guint) self->sel_vertex, &error))
				report (self, error);
			return TRUE;
		}
		if (self->mode == LRG_EDITOR_SKELETON2D_MODE_SETUP && sel_bone_index (self) >= 0)
		{
			if (!lrg_editor_skeleton2d_editor_remove_bone (self, sel_bone_index (self), &error))
				report (self, error);
			return TRUE;
		}
		return FALSE;
	case GRL_KEY_Q:
	case GRL_KEY_V:
		lrg_editor_skeleton2d_editor_set_tool (self, LRG_EDITOR_SKELETON2D_TOOL_SELECT);
		return TRUE;
	case GRL_KEY_B:
		lrg_editor_skeleton2d_editor_set_tool (self, LRG_EDITOR_SKELETON2D_TOOL_BONE);
		return TRUE;
	case GRL_KEY_M:
		lrg_editor_skeleton2d_editor_set_tool (self, LRG_EDITOR_SKELETON2D_TOOL_MESH);
		return TRUE;
	case GRL_KEY_W:
		lrg_editor_skeleton2d_editor_set_tool (self, LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS);
		return TRUE;
	case GRL_KEY_TAB:
		lrg_editor_skeleton2d_editor_set_mode (self, self->mode == LRG_EDITOR_SKELETON2D_MODE_SETUP
		                                       ? LRG_EDITOR_SKELETON2D_MODE_ANIMATE : LRG_EDITOR_SKELETON2D_MODE_SETUP);
		return TRUE;
	case GRL_KEY_F:
		lrg_editor_skeleton2d_editor_frame_all (self);
		return TRUE;
	case GRL_KEY_SPACE:
		lrg_editor_skeleton2d_editor_set_playing (self, !self->playing);
		return TRUE;
	case GRL_KEY_K:
		if (self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE && sel_bone_index (self) >= 0)
		{
			if (!lrg_editor_skeleton2d_editor_key_pose (self, sel_bone_index (self), &error))
				report (self, error);
			return TRUE;
		}
		return FALSE;
	default:
		return FALSE;
	}
}

static gboolean
canvas_event (LrgCustomWidget  *widget,
              const LrgUIEvent *event,
              gpointer          user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	gfloat                     px = lrg_ui_event_get_x (event);
	gfloat                     py = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		self->ptr_inside = TRUE;
		canvas_motion (self, px, py);
		return self->drag != DRAG_NONE;
	case LRG_UI_EVENT_POINTER_ENTER:
		self->ptr_inside = TRUE;
		return FALSE;
	case LRG_UI_EVENT_POINTER_LEAVE:
		self->ptr_inside = FALSE;
		self->hover_bone = -1;
		return FALSE;
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		self->ptr_x = px;
		self->ptr_y = py;
		self->ptr_inside = TRUE;
		if (self->drag != DRAG_NONE)
			return TRUE;
		return canvas_press (self, lrg_ui_event_get_button (event), px, py, lrg_ui_event_get_modifiers (event));
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (self->drag == DRAG_NONE)
			return FALSE;
		canvas_motion (self, px, py);
		canvas_release (self, px, py);
		return TRUE;
	case LRG_UI_EVENT_SCROLL:
		zoom_at (self, px, py, powf (1.15f, lrg_ui_event_get_scroll_y (event)));
		return TRUE;
	case LRG_UI_EVENT_KEY_DOWN:
		return canvas_key (self, lrg_ui_event_get_key (event), lrg_ui_event_get_modifiers (event));
	default:
		return FALSE;
	}
}

/* Asset drops: an image becomes an attachment of the selected slot (or of
 * a new slot on the selected bone, else the first bone) at the drop
 * point. */
static gboolean
drop_is_image (LrgDragPayload *payload)
{
	const gchar *path = lrg_drag_payload_get_string (payload);

	if (g_strcmp0 (lrg_drag_payload_get_content_type (payload), ASSET_MIME) != 0 || path == NULL)
		return FALSE;
	return g_str_has_suffix (path, ".png") || g_str_has_suffix (path, ".PNG") || g_str_has_suffix (path, ".jpg") ||
	       g_str_has_suffix (path, ".jpeg") || g_str_has_suffix (path, ".bmp") || g_str_has_suffix (path, ".tga") ||
	       g_str_has_suffix (path, ".qoi");
}

static LrgDragAction
on_drop_motion (LrgDropTarget  *target,
                LrgDragPayload *payload,
                gfloat          x,
                gfloat          y,
                gpointer        user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);

	if (!drop_is_image (payload) || self->mode != LRG_EDITOR_SKELETON2D_MODE_SETUP)
		return LRG_DRAG_ACTION_NONE;
	self->drop_hover = TRUE;
	g_free (self->drop_label);
	{
		g_autofree gchar *base = g_path_get_basename (lrg_drag_payload_get_string (payload));

		self->drop_label = g_strdup_printf ("Attach %s to %s", base,
		                                    self->sel_slot != NULL ? self->sel_slot
		                                    : self->sel_bone != NULL ? self->sel_bone : "a new slot");
	}
	return LRG_DRAG_ACTION_COPY;
}

static void
on_drop_leave (LrgDropTarget *target,
               gpointer       user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);

	self->drop_hover = FALSE;
	g_clear_pointer (&self->drop_label, g_free);
}

static gboolean
on_drop (LrgDropTarget  *target,
         LrgDragPayload *payload,
         gfloat          x,
         gfloat          y,
         gpointer        user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *path = NULL;
	gfloat                     sx, sy;

	on_drop_leave (target, self);
	if (!drop_is_image (payload) || self->mode != LRG_EDITOR_SKELETON2D_MODE_SETUP || !require_skel (self, &error))
	{
		report (self, error);
		return FALSE;
	}
	path = g_strdup (lrg_drag_payload_get_string (payload));
	lrg_editor_skeleton2d_editor_canvas_to_setup (self, x, y, &sx, &sy);
	if (!lrg_editor_skeleton2d_editor_drop_image (self, path, sx, sy, &error))
	{
		report (self, error);
		return FALSE;
	}
	return TRUE;
}

gboolean
lrg_editor_skeleton2d_editor_drop_image (LrgEditorSkeleton2DEditor  *self,
                                         const gchar                *image,
                                         gfloat                      x,
                                         gfloat                      y,
                                         GError                    **error)
{
	g_autoptr(LrgSkinnedSprite) sprite = NULL;
	g_autofree gchar           *abs = NULL;
	g_autofree gchar           *rel = NULL;
	g_autofree gchar           *base = NULL;
	g_autoptr(GrlImage)         img = NULL;
	gfloat                      w, h;
	gint                        slot, bone;
	gchar                      *dot;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	g_return_val_if_fail (image != NULL, FALSE);
	if (!require_skel (self, error))
		return FALSE;
	rel = image_ref (self, image, &abs);
	img = g_file_test (abs, G_FILE_TEST_IS_REGULAR) ? grl_image_new_from_file (abs) : NULL;
	if (img == NULL || grl_image_get_width (img) <= 0)
	{
		set_error (error, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "cannot read image %s", abs);
		return FALSE;
	}
	w = (gfloat) grl_image_get_width (img);
	h = (gfloat) grl_image_get_height (img);
	base = g_path_get_basename (rel);
	dot = strrchr (base, '.');
	if (dot != NULL && dot != base)
		*dot = '\0';

	/* the slot: the selected one, else a new slot named after the image */
	slot = sel_slot_index (self);
	if (slot < 0)
	{
		g_autofree gchar *name = NULL;

		bone = sel_bone_index (self);
		if (bone < 0)
			bone = 0;
		name = lrg_skeleton2d_find_slot (self->skel, base) < 0 ? g_strdup (base)
		       : unique_name (base, slot_taken, self->skel);
		slot = lrg_skeleton2d_add_slot (self->skel, name, bone, NULL);
		if (slot < 0)
		{
			set_error (error, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "cannot add a slot for %s", base);
			reload (self);
			return FALSE;
		}
	}
	sprite = lrg_skinned_sprite_new_quad (x - w * 0.5f, y - h * 0.5f, w, h, -1);
	lrg_skinned_sprite_set_image (sprite, rel);
	lrg_skeleton2d_set_attachment (self->skel, self->skin != NULL ? self->skin : LRG_SKELETON2D_DEFAULT_SKIN, slot,
	                               base, sprite);
	lrg_skeleton2d_set_slot_setup_attachment (self->skel, slot, base);
	g_free (self->sel_slot);
	self->sel_slot = g_strdup (lrg_skeleton2d_get_slot_name (self->skel, slot));
	g_free (self->sel_attachment);
	self->sel_attachment = g_strdup (base);
	return commit (self, "Attach image", error);
}

/* ==========================================================================
 * Outline: bones, slots (front first) with attachments, skins, clips
 * ========================================================================== */

static void
outline_row_free (gpointer data)
{
	OutlineRow *row = data;

	g_free (row->label);
	g_free (row->extra);
	g_free (row);
}

static void
add_row (LrgEditorSkeleton2DEditor *self,
         RowKind                    kind,
         gint                       index,
         gint                       depth,
         const gchar               *label,
         const gchar               *extra)
{
	OutlineRow *row = g_new0 (OutlineRow, 1);

	row->kind = kind;
	row->index = index;
	row->depth = depth;
	row->label = g_strdup (label);
	row->extra = g_strdup (extra);
	g_ptr_array_add (self->rows, row);
}

/* Bones depth first, so children sit under their parent. */
static void
add_bone_rows (LrgEditorSkeleton2DEditor *self,
               gint                       parent,
               gint                       depth)
{
	gint i, n = (gint) lrg_skeleton2d_get_bone_count (self->skel);

	for (i = 0; i < n; i++)
	{
		if (lrg_skeleton2d_get_bone_parent (self->skel, i) != parent)
			continue;
		add_row (self, ROW_BONE, i, depth, lrg_skeleton2d_get_bone_name (self->skel, i), NULL);
		add_bone_rows (self, i, depth + 1);
	}
}

static void
rebuild_outline (LrgEditorSkeleton2DEditor *self)
{
	gint  i, n;
	guint k;

	g_ptr_array_set_size (self->rows, 0);
	if (self->skel == NULL)
		return;
	add_row (self, ROW_HEADER, 0, 0, "BONES", "bones");
	add_bone_rows (self, -1, 0);
	add_row (self, ROW_HEADER, 0, 0, "SLOTS  ·  front first", "slots");
	n = (gint) lrg_skeleton2d_get_slot_count (self->skel);
	for (i = n - 1; i >= 0; i--)
	{
		add_row (self, ROW_SLOT, i, 0, lrg_skeleton2d_get_slot_name (self->skel, i),
		         lrg_skeleton2d_get_bone_name (self->skel, lrg_skeleton2d_get_slot_bone (self->skel, i)));
		if (g_strcmp0 (self->sel_slot, lrg_skeleton2d_get_slot_name (self->skel, i)) == 0)
		{
			g_autoptr(GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
			const gchar          *skins[2] = { self->skin, LRG_SKELETON2D_DEFAULT_SKIN };
			guint                 s;

			for (s = 0; s < 2; s++)
			{
				g_auto(GStrv) names = NULL;
				guint         a;

				if (skins[s] == NULL)
					continue;
				names = lrg_skeleton2d_list_attachments (self->skel, skins[s], i);
				for (a = 0; names != NULL && names[a] != NULL; a++)
				{
					if (g_hash_table_contains (seen, names[a]))
						continue;
					g_hash_table_add (seen, g_strdup (names[a]));
					add_row (self, ROW_ATTACHMENT, i, 1, names[a], skins[s]);
				}
			}
		}
	}
	add_row (self, ROW_HEADER, 0, 0, "SKINS", "skins");
	for (k = 0; k < lrg_skeleton2d_get_skin_count (self->skel); k++)
		add_row (self, ROW_SKIN, (gint) k, 0, lrg_skeleton2d_get_skin_name (self->skel, k), NULL);
	add_row (self, ROW_HEADER, 0, 0, "CLIPS", "clips");
	for (k = 0; k < lrg_skeleton2d_get_clip_count (self->skel); k++)
	{
		LrgSkeleton2DClip *clip = lrg_skeleton2d_get_clip_at (self->skel, k);
		g_autofree gchar  *extra = g_strdup_printf ("%.2fs%s", lrg_skeleton2d_clip_get_duration (clip),
		                                            lrg_skeleton2d_clip_get_loop (clip) ? "  loop" : "");

		add_row (self, ROW_CLIP, (gint) k, 0, lrg_skeleton2d_clip_get_name (clip), extra);
	}
	if (self->outline != NULL)
	{
		lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->outline),
		                                        (gfloat) self->rows->len * OUTLINE_ROW + 8.0f);
		lrg_widget_queue_layout (self->outline);
	}
}

static gfloat
row_height (const OutlineRow *row)
{
	return row->kind == ROW_HEADER ? OUTLINE_HEADER : OUTLINE_ROW;
}

static gboolean
row_selected (LrgEditorSkeleton2DEditor *self,
              const OutlineRow          *row)
{
	switch (row->kind)
	{
	case ROW_BONE:
		return g_strcmp0 (self->sel_bone, row->label) == 0;
	case ROW_SLOT:
		return g_strcmp0 (self->sel_slot, row->label) == 0 && self->sel_attachment == NULL;
	case ROW_ATTACHMENT:
		return g_strcmp0 (self->sel_attachment, row->label) == 0;
	case ROW_SKIN:
		return g_strcmp0 (self->skin != NULL ? self->skin : LRG_SKELETON2D_DEFAULT_SKIN, row->label) == 0;
	case ROW_CLIP:
		return g_strcmp0 (self->clip, row->label) == 0;
	case ROW_HEADER:
	default:
		return FALSE;
	}
}

static void
outline_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	LrgTheme                  *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                   *font = lrg_theme_get_ui_font (theme);
	gfloat                     fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat                     small = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor            *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor            *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor            *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	GrlColor                   sel;
	gfloat                     ry = y + 4.0f;
	guint                      i;

	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION), 0.9f, &sel);
	for (i = 0; i < self->rows->len; i++)
	{
		OutlineRow *row = g_ptr_array_index (self->rows, i);
		gfloat      h = row_height (row);
		gfloat      ix = x + 10.0f + (gfloat) row->depth * 14.0f;
		LrgIcon     icon = LRG_ICON_NONE;

		if (row->kind == ROW_HEADER)
		{
			lrg_paint_list_text (list, font, row->label, x + 10.0f, ry + h - small - 6.0f, small, 1.0f, muted);
			ry += h;
			continue;
		}
		if (row_selected (self, row))
		{
			lrg_paint_list_rect (list, x + 4.0f, ry + 1.0f, width - 8.0f, h - 2.0f, 5.0f, &sel);
			lrg_paint_list_rect (list, x + 4.0f, ry + 4.0f, 3.0f, h - 8.0f, 1.5f, accent);
		}
		switch (row->kind)
		{
		case ROW_BONE:       icon = LRG_ICON_EDIT; break;
		case ROW_SLOT:       icon = LRG_ICON_MENU; break;
		case ROW_ATTACHMENT: icon = LRG_ICON_FILE; break;
		case ROW_SKIN:       icon = LRG_ICON_STAR_OUTLINE; break;
		case ROW_CLIP:       icon = LRG_ICON_PLAY; break;
		default:             break;
		}
		lrg_paint_list_icon (list, icon, ix, ry + (h - 14.0f) * 0.5f, 14.0f, 0.0f, row_selected (self, row) ? accent : muted);
		lrg_paint_list_text (list, font, row->label, ix + 20.0f, ry + (h - fs) * 0.5f, fs, 0.0f, text);
		if (row->kind == ROW_SLOT || row->kind == ROW_CLIP)
		{
			gfloat tw = 0.0f;

			lrg_paint_measure_text (font, row->extra, small, 0.0f, &tw, NULL);
			lrg_paint_list_text (list, font, row->extra, x + width - tw - 12.0f, ry + (h - small) * 0.5f, small, 0.0f,
			                     muted);
		}
		if (row->kind == ROW_ATTACHMENT)
		{
			/* the eye: lit when this is what the slot shows in the setup pose */
			gboolean shown = g_strcmp0 (lrg_skeleton2d_get_slot_setup_attachment (self->skel, row->index),
			                            row->label) == 0;

			lrg_paint_list_icon (list, LRG_ICON_EYE, x + width - 26.0f, ry + (h - 14.0f) * 0.5f, 14.0f, 0.0f,
			                     shown ? accent : muted);
		}
		ry += h;
	}
}

static void
outline_activate (LrgEditorSkeleton2DEditor *self,
                  OutlineRow                *row,
                  gboolean                   on_eye)
{
	g_autoptr(GError) error = NULL;

	switch (row->kind)
	{
	case ROW_BONE:
		set_selection (self, row->index, sel_slot_index (self), self->sel_attachment);
		break;
	case ROW_SLOT:
		set_selection (self, sel_bone_index (self), row->index, NULL);
		rebuild_outline (self);
		break;
	case ROW_ATTACHMENT:
		if (on_eye)
		{
			gboolean shown = g_strcmp0 (lrg_skeleton2d_get_slot_setup_attachment (self->skel, row->index),
			                            row->label) == 0;

			if (!lrg_editor_skeleton2d_editor_set_slot_attachment (self, row->index, shown ? NULL : row->label,
			                                                       &error))
				report (self, error);
			break;
		}
		lrg_editor_skeleton2d_editor_select_attachment (self, row->index, row->label);
		break;
	case ROW_SKIN:
		lrg_editor_skeleton2d_editor_set_skin (self, row->label);
		break;
	case ROW_CLIP:
		lrg_editor_skeleton2d_editor_set_clip (self, row->label);
		lrg_editor_skeleton2d_editor_set_mode (self, LRG_EDITOR_SKELETON2D_MODE_ANIMATE);
		break;
	case ROW_HEADER:
	default:
		break;
	}
}

static gboolean
outline_event (LrgCustomWidget  *widget,
               const LrgUIEvent *event,
               gpointer          user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	gfloat                     wy = lrg_widget_get_world_y (LRG_WIDGET (widget)) + 4.0f;
	gfloat                     wx = lrg_widget_get_world_x (LRG_WIDGET (widget));
	gfloat                     ww = lrg_widget_get_width (LRG_WIDGET (widget));
	gfloat                     py = lrg_ui_event_get_y (event);
	guint                      i;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_UP ||
	    lrg_ui_event_get_button (event) != GRL_MOUSE_BUTTON_LEFT)
		return lrg_ui_event_get_event_type (event) == LRG_UI_EVENT_MOUSE_BUTTON_DOWN;
	for (i = 0; i < self->rows->len; i++)
	{
		OutlineRow *row = g_ptr_array_index (self->rows, i);
		gfloat      h = row_height (row);

		if (py >= wy && py < wy + h)
		{
			outline_activate (self, row, lrg_ui_event_get_x (event) >= wx + ww - 32.0f);
			return TRUE;
		}
		wy += h;
	}
	return TRUE;
}

/* ==========================================================================
 * Property grid of the selection
 * ========================================================================== */

/* The real document's index of a skin entry and one of its attachments. */
static gboolean
doc_attachment_path (LrgEditorSkeleton2DEditor *self,
                     gchar                    **out)
{
	LrgEditorDocument *doc = doc_of (self);
	const gchar       *skin = NULL;
	gint               k, a;

	if (sel_sprite (self, &skin) == NULL)
		return FALSE;
	for (k = 0; k < seq_len (doc, "/skins"); k++)
	{
		g_autofree gchar *ap = NULL;

		if (g_strcmp0 (scalar_at (doc, "/skins/%d/name", k), skin) != 0)
			continue;
		ap = g_strdup_printf ("/skins/%d/attachments", k);
		for (a = 0; a < seq_len (doc, ap); a++)
			if (g_strcmp0 (scalar_at (doc, "%s/%d/slot", ap, a), self->sel_slot) == 0 &&
			    g_strcmp0 (scalar_at (doc, "%s/%d/name", ap, a), self->sel_attachment) == 0)
			{
				*out = g_strdup_printf ("%s/%d", ap, a);
				return TRUE;
			}
	}
	return FALSE;
}

static gboolean
source_read (LrgEditorDataSource *source,
             const gchar         *name,
             GValue              *value,
             gpointer             user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	gint                       bone = sel_bone_index (self), slot = sel_slot_index (self);

	if (self->skel == NULL)
		return FALSE;
	if (g_str_equal (name, "bone-name") && bone >= 0)
		g_value_set_string (value, lrg_skeleton2d_get_bone_name (self->skel, bone));
	else if ((g_str_equal (name, "bone-scale-x") || g_str_equal (name, "bone-scale-y")) && bone >= 0)
	{
		gfloat sx = 1.0f, sy = 1.0f;

		lrg_skeleton2d_get_bone_setup (self->skel, bone, NULL, NULL, NULL, &sx, &sy);
		g_value_set_double (value, g_str_equal (name, "bone-scale-x") ? sx : sy);
	}
	else if (g_str_equal (name, "slot-name") && slot >= 0)
		g_value_set_string (value, lrg_skeleton2d_get_slot_name (self->skel, slot));
	else if (g_str_has_prefix (name, "slot-color-") && slot >= 0)
	{
		gfloat rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		guint  k = (guint) (strchr ("rgba", name[11]) - "rgba");

		lrg_skeleton2d_reset_pose (self->skel);
		lrg_skeleton2d_get_slot_color (self->skel, slot, rgba);
		g_value_set_double (value, rgba[MIN (k, 3u)]);
	}
	else if (g_str_equal (name, "brush-radius"))
		g_value_set_double (value, self->brush_radius);
	else if (g_str_equal (name, "brush-strength"))
		g_value_set_double (value, self->brush_strength);
	else
		return FALSE;
	return TRUE;
}

static gboolean
source_write (LrgEditorDataSource *source,
              const gchar         *name,
              GValue              *value,
              gpointer             user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	gint                       bone = sel_bone_index (self), slot = sel_slot_index (self);
	g_autoptr(GError)          error = NULL;

	if (self->skel == NULL)
		return FALSE;
	if (g_str_equal (name, "bone-name") && bone >= 0)
	{
		if (!lrg_editor_skeleton2d_editor_rename_bone (self, bone, g_value_get_string (value), &error))
			report (self, error);
		return FALSE;
	}
	if ((g_str_equal (name, "bone-scale-x") || g_str_equal (name, "bone-scale-y")) && bone >= 0)
	{
		gfloat x, y, r, sx, sy;

		lrg_skeleton2d_get_bone_setup (self->skel, bone, &x, &y, &r, &sx, &sy);
		if (g_str_equal (name, "bone-scale-x"))
			sx = (gfloat) g_value_get_double (value);
		else
			sy = (gfloat) g_value_get_double (value);
		if (!lrg_editor_skeleton2d_editor_set_bone (self, bone, x, y, r, sx, sy,
		                                            lrg_skeleton2d_get_bone_length (self->skel, bone), &error))
			report (self, error);
		return FALSE;
	}
	if (g_str_equal (name, "slot-name") && slot >= 0)
	{
		if (!lrg_editor_skeleton2d_editor_rename_slot (self, slot, g_value_get_string (value), &error))
			report (self, error);
		return FALSE;
	}
	if (g_str_has_prefix (name, "slot-color-") && slot >= 0)
	{
		gfloat rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		guint  k = (guint) (strchr ("rgba", name[11]) - "rgba");

		lrg_skeleton2d_reset_pose (self->skel);
		lrg_skeleton2d_get_slot_color (self->skel, slot, rgba);
		rgba[MIN (k, 3u)] = (gfloat) g_value_get_double (value);
		if (!lrg_editor_skeleton2d_editor_set_slot_color (self, slot, rgba, &error))
			report (self, error);
		return FALSE;
	}
	if (g_str_equal (name, "brush-radius"))
	{
		self->brush_radius = MAX ((gfloat) g_value_get_double (value), 0.5f);
		return FALSE;
	}
	if (g_str_equal (name, "brush-strength"))
	{
		self->brush_strength = CLAMP ((gfloat) g_value_get_double (value), 0.0f, 1.0f);
		return FALSE;
	}
	return TRUE;
}

static gchar *
grid_signature (LrgEditorSkeleton2DEditor *self)
{
	return g_strdup_printf ("%s|%s|%s|%s|%d|%d|%d", self->sel_bone != NULL ? self->sel_bone : "",
	                        self->sel_slot != NULL ? self->sel_slot : "",
	                        self->sel_attachment != NULL ? self->sel_attachment : "",
	                        self->clip != NULL ? self->clip : "", (gint) self->tool, (gint) self->mode,
	                        self->skel != NULL ? (gint) lrg_skeleton2d_get_slot_count (self->skel) : -1);
}

static void
rebuild_grid (LrgEditorSkeleton2DEditor *self)
{
	LrgEditorDataSource *src = self->source;
	gint                 bone = sel_bone_index (self), slot = sel_slot_index (self), c = clip_index (self, self->clip);

	self->grid_stale = FALSE;
	g_free (self->grid_sig);
	self->grid_sig = grid_signature (self);
	lrg_editor_data_source_clear (src);
	if (self->skel != NULL && self->parse_error == NULL)
	{
		if (bone >= 0)
		{
			g_autofree gchar *b = g_strdup_printf ("/bones/%d", bone);
			g_autofree gchar *px = g_strconcat (b, "/x", NULL);
			g_autofree gchar *py = g_strconcat (b, "/y", NULL);
			g_autofree gchar *pr = g_strconcat (b, "/rotation", NULL);
			g_autofree gchar *pl = g_strconcat (b, "/length", NULL);

			lrg_editor_data_source_add_field (src, "bone-name", b, G_TYPE_STRING, "Name", "Bone", NULL, NULL);
			lrg_editor_data_source_add_field (src, "bone-x", px, G_TYPE_DOUBLE, "X", "Bone", "range:-100000,100000,1", "0");
			lrg_editor_data_source_add_field (src, "bone-y", py, G_TYPE_DOUBLE, "Y", "Bone", "range:-100000,100000,1", "0");
			lrg_editor_data_source_add_field (src, "bone-rotation", pr, G_TYPE_DOUBLE, "Rotation", "Bone",
			                                  "range:-360,360,1", "0");
			lrg_editor_data_source_add_field (src, "bone-scale-x", b, G_TYPE_DOUBLE, "Scale X", "Bone",
			                                  "range:-100,100,0.05", "1");
			lrg_editor_data_source_add_field (src, "bone-scale-y", b, G_TYPE_DOUBLE, "Scale Y", "Bone",
			                                  "range:-100,100,0.05", "1");
			lrg_editor_data_source_add_field (src, "bone-length", pl, G_TYPE_DOUBLE, "Length", "Bone",
			                                  "range:0,100000,1", "0");
		}
		if (slot >= 0)
		{
			g_autofree gchar *sp = g_strdup_printf ("/slots/%d", slot);
			g_autofree gchar *pa = g_strconcat (sp, "/attachment", NULL);

			lrg_editor_data_source_add_field (src, "slot-name", sp, G_TYPE_STRING, "Name", "Slot", NULL, NULL);
			lrg_editor_data_source_add_field (src, "slot-attachment", pa, G_TYPE_STRING, "Attachment", "Slot", NULL, NULL);
			lrg_editor_data_source_add_field (src, "slot-color-r", sp, G_TYPE_DOUBLE, "Red", "Slot", "range:0,1,0.01", "1");
			lrg_editor_data_source_add_field (src, "slot-color-g", sp, G_TYPE_DOUBLE, "Green", "Slot", "range:0,1,0.01", "1");
			lrg_editor_data_source_add_field (src, "slot-color-b", sp, G_TYPE_DOUBLE, "Blue", "Slot", "range:0,1,0.01", "1");
			lrg_editor_data_source_add_field (src, "slot-color-a", sp, G_TYPE_DOUBLE, "Alpha", "Slot", "range:0,1,0.01", "1");
		}
		{
			g_autofree gchar *ap = NULL;

			if (doc_attachment_path (self, &ap))
			{
				g_autofree gchar *pi = g_strconcat (ap, "/image", NULL);

				lrg_editor_data_source_add_field (src, "attachment-image", pi, G_TYPE_STRING, "Image", "Attachment",
				                                  "asset:texture", NULL);
			}
		}
		if (c >= 0)
		{
			g_autofree gchar *cp = g_strdup_printf ("/animations/%d", c);
			g_autofree gchar *pn = g_strconcat (cp, "/name", NULL);
			g_autofree gchar *pd = g_strconcat (cp, "/duration", NULL);
			g_autofree gchar *pl = g_strconcat (cp, "/loop", NULL);

			lrg_editor_data_source_add_field (src, "clip-name", pn, G_TYPE_STRING, "Name", "Clip", NULL, NULL);
			lrg_editor_data_source_add_field (src, "clip-duration", pd, G_TYPE_DOUBLE, "Duration", "Clip",
			                                  "range:0,3600,0.05", "0");
			lrg_editor_data_source_add_field (src, "clip-loop", pl, G_TYPE_BOOLEAN, "Loop", "Clip", NULL, "false");
		}
		lrg_editor_data_source_add_field (src, "rig-name", "/name", G_TYPE_STRING, "Name", "Rig", NULL, NULL);
		if (self->tool == LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS)
		{
			lrg_editor_data_source_add_field (src, "brush-radius", "/~brush", G_TYPE_DOUBLE, "Radius", "Brush",
			                                  "range:0.5,1000,0.5", NULL);
			lrg_editor_data_source_add_field (src, "brush-strength", "/~brush", G_TYPE_DOUBLE, "Strength", "Brush",
			                                  "range:0,1,0.05", NULL);
		}
	}
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (src));
	lrg_property_grid_rebuild (self->grid);
}

/* ==========================================================================
 * Dope sheet and curves
 * ========================================================================== */

static void
keyref_free (gpointer data)
{
	KeyRef *ref = data;

	g_free (ref->target);
	g_free (ref);
}

static void
dope_clear (LrgEditorSkeleton2DEditor *self)
{
	guint n;

	g_hash_table_remove_all (self->keyrefs);
	while ((n = lrg_timeline_view_get_n_tracks (self->dope)) > 0)
		lrg_timeline_view_remove_track (self->dope, lrg_timeline_view_get_track (self->dope, n - 1));
	for (n = 0; n < self->marker_ids->len; n++)
		lrg_timeline_view_remove_marker (self->dope, g_array_index (self->marker_ids, guint, n));
	g_array_set_size (self->marker_ids, 0);
}

static void
dope_add_timeline (LrgEditorSkeleton2DEditor *self,
                   LrgSkeleton2DClip         *clip,
                   LrgTimelineViewTrack      *group,
                   const gchar               *target,
                   LrgSkeleton2DChannel       channel,
                   const GrlColor            *color)
{
	guint                 i, n = lrg_skeleton2d_clip_get_key_count (clip, target, channel);
	LrgTimelineViewTrack *track;
	g_autofree gchar     *label = NULL;

	if (n == 0)
		return;
	label = g_strdup_printf ("%s", channel_nicks[channel]);
	track = lrg_timeline_view_add_track (self->dope, label, group);
	lrg_timeline_view_track_set_color (track, color);
	for (i = 0; i < n; i++)
	{
		gfloat             t = 0.0f, v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		LrgSkeleton2DCurve curve = LRG_SKELETON2D_CURVE_LINEAR;
		guint              id;
		KeyRef            *ref;

		if (channel == LRG_SKELETON2D_CHANNEL_ATTACHMENT)
		{
			lrg_skeleton2d_clip_get_attachment_key (clip, target, i, &t, NULL);
			curve = LRG_SKELETON2D_CURVE_STEPPED;
		}
		else
			lrg_skeleton2d_clip_get_key (clip, target, channel, i, &t, v, &curve);
		id = lrg_timeline_view_add_keyframe (self->dope, track, t, v[0],
		                                     curve == LRG_SKELETON2D_CURVE_STEPPED ? LRG_TIMELINE_VIEW_INTERPOLATION_STEP
		                                     : curve == LRG_SKELETON2D_CURVE_BEZIER ? LRG_TIMELINE_VIEW_INTERPOLATION_EASE
		                                     : LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
		if (id == LRG_TIMELINE_VIEW_INVALID_ID)
			continue;
		ref = g_new0 (KeyRef, 1);
		ref->target = g_strdup (target);
		ref->channel = channel;
		ref->index = i;
		g_hash_table_insert (self->keyrefs, GUINT_TO_POINTER (id), ref);
	}
}

static void
rebuild_dope (LrgEditorSkeleton2DEditor *self)
{
	static const GrlColor bone_color = { 120, 170, 255, 255 };
	static const GrlColor slot_color = { 245, 170, 90, 255 };
	LrgSkeleton2DClip    *clip;
	guint                 i, k;

	if (self->dope == NULL)
		return;
	self->syncing = TRUE;
	dope_clear (self);
	clip = current_clip (self);
	if (clip != NULL)
	{
		g_auto(GStrv) bones = lrg_skeleton2d_clip_get_targets (clip, FALSE);
		g_auto(GStrv) slots = lrg_skeleton2d_clip_get_targets (clip, TRUE);

		lrg_timeline_view_set_duration (self->dope, MAX (lrg_skeleton2d_clip_get_duration (clip), 0.1f));
		for (i = 0; bones != NULL && bones[i] != NULL; i++)
		{
			LrgTimelineViewTrack *group = lrg_timeline_view_add_track (self->dope, bones[i], NULL);

			lrg_timeline_view_track_set_color (group, &bone_color);
			for (k = LRG_SKELETON2D_CHANNEL_TRANSLATE; k <= LRG_SKELETON2D_CHANNEL_SCALE; k++)
				dope_add_timeline (self, clip, group, bones[i], (LrgSkeleton2DChannel) k, &bone_color);
		}
		for (i = 0; slots != NULL && slots[i] != NULL; i++)
		{
			LrgTimelineViewTrack *group = lrg_timeline_view_add_track (self->dope, slots[i], NULL);

			lrg_timeline_view_track_set_color (group, &slot_color);
			dope_add_timeline (self, clip, group, slots[i], LRG_SKELETON2D_CHANNEL_COLOR, &slot_color);
			dope_add_timeline (self, clip, group, slots[i], LRG_SKELETON2D_CHANNEL_ATTACHMENT, &slot_color);
		}
		for (i = 0; i < lrg_skeleton2d_clip_get_event_count (clip); i++)
		{
			gfloat       t = 0.0f;
			const gchar *name = lrg_skeleton2d_clip_get_event (clip, i, &t);

			guint id = lrg_timeline_view_add_marker (self->dope, t, name);

			g_array_append_val (self->marker_ids, id);
		}
		for (i = 0; i < lrg_skeleton2d_clip_get_draw_order_key_count (clip); i++)
		{
			gfloat t = 0.0f;

			guint  id;

			lrg_skeleton2d_clip_get_draw_order_key (clip, i, &t);
			id = lrg_timeline_view_add_marker (self->dope, t, "draw order");
			g_array_append_val (self->marker_ids, id);
		}
	}
	else
		lrg_timeline_view_set_duration (self->dope, 1.0);
	lrg_timeline_view_set_time (self->dope, self->time);
	self->syncing = FALSE;
	self->dope_stale = FALSE;
}

/* Easings <-> engine curves: Bézier presets (easings.net) for the easings
 * that have one; the rest map to the closest one. */
typedef struct
{
	LrgEasingType easing;
	gfloat        cp[4];
} EasePreset;

static const EasePreset ease_presets[] = {
	{ LRG_EASING_EASE_IN_QUAD,      { 0.11f, 0.0f, 0.5f, 0.0f } },
	{ LRG_EASING_EASE_OUT_QUAD,     { 0.5f, 1.0f, 0.89f, 1.0f } },
	{ LRG_EASING_EASE_IN_OUT_QUAD,  { 0.45f, 0.0f, 0.55f, 1.0f } },
	{ LRG_EASING_EASE_IN_CUBIC,     { 0.32f, 0.0f, 0.67f, 0.0f } },
	{ LRG_EASING_EASE_OUT_CUBIC,    { 0.33f, 1.0f, 0.68f, 1.0f } },
	{ LRG_EASING_EASE_IN_OUT_CUBIC, { 0.65f, 0.0f, 0.35f, 1.0f } },
	{ LRG_EASING_EASE_IN_QUART,     { 0.5f, 0.0f, 0.75f, 0.0f } },
	{ LRG_EASING_EASE_OUT_QUART,    { 0.25f, 1.0f, 0.5f, 1.0f } },
	{ LRG_EASING_EASE_IN_OUT_QUART, { 0.76f, 0.0f, 0.24f, 1.0f } },
	{ LRG_EASING_EASE_IN_QUINT,     { 0.64f, 0.0f, 0.78f, 0.0f } },
	{ LRG_EASING_EASE_OUT_QUINT,    { 0.22f, 1.0f, 0.36f, 1.0f } },
	{ LRG_EASING_EASE_IN_OUT_QUINT, { 0.83f, 0.0f, 0.17f, 1.0f } },
	{ LRG_EASING_EASE_IN_SINE,      { 0.12f, 0.0f, 0.39f, 0.0f } },
	{ LRG_EASING_EASE_OUT_SINE,     { 0.61f, 1.0f, 0.88f, 1.0f } },
	{ LRG_EASING_EASE_IN_OUT_SINE,  { 0.37f, 0.0f, 0.63f, 1.0f } },
	{ LRG_EASING_EASE_IN_EXPO,      { 0.7f, 0.0f, 0.84f, 0.0f } },
	{ LRG_EASING_EASE_OUT_EXPO,     { 0.16f, 1.0f, 0.3f, 1.0f } },
	{ LRG_EASING_EASE_IN_OUT_EXPO,  { 0.87f, 0.0f, 0.13f, 1.0f } },
	{ LRG_EASING_EASE_IN_CIRC,      { 0.55f, 0.0f, 1.0f, 0.45f } },
	{ LRG_EASING_EASE_OUT_CIRC,     { 0.0f, 0.55f, 0.45f, 1.0f } },
	{ LRG_EASING_EASE_IN_OUT_CIRC,  { 0.85f, 0.0f, 0.15f, 1.0f } },
	{ LRG_EASING_EASE_IN_BACK,      { 0.36f, 0.0f, 0.66f, -0.56f } },
	{ LRG_EASING_EASE_OUT_BACK,     { 0.34f, 1.56f, 0.64f, 1.0f } },
	{ LRG_EASING_EASE_IN_OUT_BACK,  { 0.68f, -0.6f, 0.32f, 1.6f } },
	{ LRG_EASING_EASE_IN_ELASTIC,   { 0.36f, 0.0f, 0.66f, -0.56f } },
	{ LRG_EASING_EASE_OUT_ELASTIC,  { 0.34f, 1.56f, 0.64f, 1.0f } },
	{ LRG_EASING_EASE_IN_OUT_ELASTIC, { 0.68f, -0.6f, 0.32f, 1.6f } },
	{ LRG_EASING_EASE_IN_BOUNCE,    { 0.7f, 0.0f, 0.84f, 0.0f } },
	{ LRG_EASING_EASE_OUT_BOUNCE,   { 0.16f, 1.0f, 0.3f, 1.0f } },
	{ LRG_EASING_EASE_IN_OUT_BOUNCE, { 0.87f, 0.0f, 0.13f, 1.0f } }
};

/* The easing the curve view shows for a key. A stepped key shows as an
 * expo ease-in (flat, then a jump), the closest look. */
static LrgEasingType
curve_to_easing (LrgSkeleton2DCurve  curve,
                 const gfloat       *cp)
{
	guint         i;
	gfloat        best = G_MAXFLOAT;
	LrgEasingType out = LRG_EASING_EASE_IN_OUT_CUBIC;

	if (curve == LRG_SKELETON2D_CURVE_LINEAR)
		return LRG_EASING_LINEAR;
	if (curve == LRG_SKELETON2D_CURVE_STEPPED || cp == NULL)
		return curve == LRG_SKELETON2D_CURVE_STEPPED ? LRG_EASING_EASE_IN_EXPO : out;
	/* elastic and bounce share their presets with back and expo: stop at
	 * the first 24 entries so the mapping round-trips */
	for (i = 0; i < 24; i++)
	{
		const gfloat *p = ease_presets[i].cp;
		gfloat        d = (p[0] - cp[0]) * (p[0] - cp[0]) + (p[1] - cp[1]) * (p[1] - cp[1]) +
		                  (p[2] - cp[2]) * (p[2] - cp[2]) + (p[3] - cp[3]) * (p[3] - cp[3]);

		if (d < best)
		{
			best = d;
			out = ease_presets[i].easing;
		}
	}
	return out;
}

static LrgSkeleton2DCurve
easing_to_curve (LrgEasingType  easing,
                 gfloat        *cp)
{
	guint i;

	if (easing == LRG_EASING_LINEAR)
		return LRG_SKELETON2D_CURVE_LINEAR;
	for (i = 0; i < G_N_ELEMENTS (ease_presets); i++)
		if (ease_presets[i].easing == easing)
		{
			memcpy (cp, ease_presets[i].cp, sizeof (gfloat) * 4);
			return LRG_SKELETON2D_CURVE_BEZIER;
		}
	return LRG_SKELETON2D_CURVE_LINEAR;
}

/* Shows the selected timeline (first value over normalised time) in the
 * curve view. */
static void
sync_curve (LrgEditorSkeleton2DEditor *self)
{
	LrgSkeleton2DClip           *clip = current_clip (self);
	g_autoptr(LrgKeyframeCurve)  curve = lrg_keyframe_curve_new ();
	gfloat                       dur, lo = G_MAXFLOAT, hi = -G_MAXFLOAT;
	guint                        i, n = 0;

	if (self->curves == NULL)
		return;
	g_clear_pointer (&self->curve_easings, g_free);
	self->n_curve_easings = 0;
	if (clip != NULL && self->curve_target != NULL && self->curve_channel != LRG_SKELETON2D_CHANNEL_ATTACHMENT)
		n = lrg_skeleton2d_clip_get_key_count (clip, self->curve_target, self->curve_channel);
	dur = clip != NULL ? MAX (lrg_skeleton2d_clip_get_duration (clip), 1e-3f) : 1.0f;
	self->curve_easings = g_new0 (LrgEasingType, MAX (n, 1u));
	self->n_curve_easings = n;
	for (i = 0; i < n; i++)
	{
		gfloat             t, v[4], cp[4];
		LrgSkeleton2DCurve kind;
		gboolean           has_cp;

		lrg_skeleton2d_clip_get_key (clip, self->curve_target, self->curve_channel, i, &t, v, &kind);
		has_cp = kind == LRG_SKELETON2D_CURVE_BEZIER &&
		         key_bezier (self, self->curve_target, self->curve_channel, i, cp);
		self->curve_easings[i] = curve_to_easing (kind, has_cp ? cp : NULL);
		lrg_keyframe_curve_add_key (curve, t / dur, v[0], self->curve_easings[i]);
		lo = MIN (lo, v[0]);
		hi = MAX (hi, v[0]);
	}
	if (n == 0)
	{
		lrg_keyframe_curve_add_key (curve, 0.0f, 0.0f, LRG_EASING_LINEAR);
		lo = -1.0f;
		hi = 1.0f;
	}
	if (hi - lo < 1e-3f)
	{
		lo -= 1.0f;
		hi += 1.0f;
	}
	self->syncing = TRUE;
	lrg_curve_edit_set_curve (self->curves, curve);
	lrg_curve_edit_set_value_range (self->curves, lo - (hi - lo) * 0.15f, hi + (hi - lo) * 0.15f);
	lrg_widget_set_enabled (LRG_WIDGET (self->curves), n > 0);
	self->syncing = FALSE;
}

/* The curve view finished a gesture: rewrite the timeline from its keys,
 * keeping the other values and the curves of keys whose easing was not
 * changed (Bézier control points included). */
static void
on_curve_edit_end (LrgCurveEdit *edit,
                   gboolean      commit_it,
                   gpointer      user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	LrgSkeleton2DClip         *clip = current_clip (self);
	LrgKeyframeCurve          *curve = lrg_curve_edit_get_curve (edit);
	g_autoptr(GError)          error = NULL;
	g_autofree gfloat         *ot = NULL;
	g_autofree gfloat         *ov = NULL;
	g_autofree gfloat         *ocp = NULL;
	g_autofree LrgSkeleton2DCurve *oc = NULL;
	g_autofree gboolean       *has_cp = NULL;
	guint                      i, on, nn;
	gfloat                     dur;

	if (self->syncing || !commit_it || clip == NULL || self->curve_target == NULL || curve == NULL)
		return;
	on = lrg_skeleton2d_clip_get_key_count (clip, self->curve_target, self->curve_channel);
	dur = MAX (lrg_skeleton2d_clip_get_duration (clip), 1e-3f);
	ot = g_new0 (gfloat, MAX (on, 1u));
	ov = g_new0 (gfloat, MAX (on, 1u) * 4);
	ocp = g_new0 (gfloat, MAX (on, 1u) * 4);
	oc = g_new0 (LrgSkeleton2DCurve, MAX (on, 1u));
	has_cp = g_new0 (gboolean, MAX (on, 1u));
	for (i = 0; i < on; i++)
	{
		lrg_skeleton2d_clip_get_key (clip, self->curve_target, self->curve_channel, i, &ot[i], ov + i * 4, &oc[i]);
		has_cp[i] = oc[i] == LRG_SKELETON2D_CURVE_BEZIER &&
		            key_bezier (self, self->curve_target, self->curve_channel, i, ocp + i * 4);
	}
	/* sample before removing: new keys take the other values from here */
	nn = lrg_keyframe_curve_get_key_count (curve);
	{
		g_autofree gfloat *samples = g_new0 (gfloat, MAX (nn, 1u) * 4);
		g_autofree gfloat *times = g_new0 (gfloat, MAX (nn, 1u));
		g_autofree gfloat *values = g_new0 (gfloat, MAX (nn, 1u));
		g_autofree LrgEasingType *easings = g_new0 (LrgEasingType, MAX (nn, 1u));

		for (i = 0; i < nn; i++)
		{
			lrg_keyframe_curve_get_key (curve, i, &times[i], &values[i], &easings[i]);
			times[i] = CLAMP (times[i], 0.0f, 1.0f) * dur;
			lrg_skeleton2d_clip_sample (clip, self->curve_target, self->curve_channel, times[i], samples + i * 4);
		}
		while (lrg_skeleton2d_clip_get_key_count (clip, self->curve_target, self->curve_channel) > 0)
			lrg_skeleton2d_clip_remove_key (clip, self->curve_target, self->curve_channel, 0);
		for (i = 0; i < nn; i++)
		{
			gfloat             v[4], cp[4] = { 0.42f, 0.0f, 0.58f, 1.0f };
			LrgSkeleton2DCurve kind;

			memcpy (v, i < on ? ov + i * 4 : samples + i * 4, sizeof v);
			v[0] = values[i];
			if (i < on && i < self->n_curve_easings && easings[i] == self->curve_easings[i])
			{
				kind = oc[i];
				if (has_cp[i])
					memcpy (cp, ocp + i * 4, sizeof cp);
			}
			else
				kind = easing_to_curve (easings[i], cp);
			put_key (self, clip, self->curve_target, self->curve_channel, times[i], v, 4, NULL, kind,
			         kind == LRG_SKELETON2D_CURVE_BEZIER ? cp : NULL, NULL);
		}
	}
	if (!commit (self, "Edit curve", &error))
		report (self, error);
}

static void
on_dope_selection (LrgTimelineView *view,
                   gpointer         user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	g_autoptr(GArray)          sel = NULL;
	KeyRef                    *ref;

	if (self->syncing)
		return;
	sel = lrg_timeline_view_get_selected_keyframes (view);
	if (sel == NULL || sel->len == 0)
		return;
	ref = g_hash_table_lookup (self->keyrefs, GUINT_TO_POINTER (g_array_index (sel, guint, 0)));
	if (ref == NULL)
		return;
	g_free (self->curve_target);
	self->curve_target = g_strdup (ref->target);
	self->curve_channel = ref->channel;
	if (ref->channel != LRG_SKELETON2D_CHANNEL_COLOR && ref->channel != LRG_SKELETON2D_CHANNEL_ATTACHMENT &&
	    self->skel != NULL && lrg_skeleton2d_find_bone (self->skel, ref->target) >= 0)
		select_bone_name (self, ref->target);
	sync_curve (self);
}

static void
on_dope_playhead (LrgTimelineView *view,
                  gdouble          time,
                  gpointer         user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);

	if (self->syncing)
		return;
	self->time = (gfloat) time;
}

static void
on_dope_key_moved (LrgTimelineView      *view,
                   LrgTimelineViewTrack *track,
                   guint                 id,
                   gpointer              user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);

	if (!self->syncing && g_hash_table_contains (self->keyrefs, GUINT_TO_POINTER (id)))
	{
		g_array_append_val (self->moved_keys, id);
		self->dope_stale = TRUE;
	}
}

static void
on_dope_key_removed (LrgTimelineView      *view,
                     LrgTimelineViewTrack *track,
                     guint                 id,
                     gpointer              user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	guint                      tagged = id | 0x80000000u;

	/* removals are queued with the high bit set */
	if (!self->syncing && g_hash_table_contains (self->keyrefs, GUINT_TO_POINTER (id)))
	{
		g_array_append_val (self->moved_keys, tagged);
		self->dope_stale = TRUE;
	}
}

/* Applies the key drags and deletions of the dope sheet as one step:
 * times are edited in a scratch copy (curves and control points stay),
 * removed keys are cut out of it. */
static void
flush_dope_edits (LrgEditorSkeleton2DEditor *self)
{
	g_autoptr(LrgEditorDocument) s = NULL;
	g_autoptr(GError)            error = NULL;
	g_autoptr(GPtrArray)         removals = g_ptr_array_new ();
	gboolean                     any = FALSE;
	guint                        i;

	if (self->moved_keys->len == 0)
		return;
	s = scratch_doc (self);
	for (i = 0; s != NULL && i < self->moved_keys->len; i++)
	{
		guint   raw = g_array_index (self->moved_keys, guint, i);
		guint   id = raw & 0x7fffffffu;
		KeyRef *ref = g_hash_table_lookup (self->keyrefs, GUINT_TO_POINTER (id));

		if (ref == NULL)
			continue;
		if (raw & 0x80000000u)
		{
			g_ptr_array_add (removals, ref);
			continue;
		}
		if (scratch_move_key (self, s, ref->target, ref->channel, ref->index,
		                      (gfloat) lrg_timeline_view_get_keyframe_time (self->dope, id), NULL))
			any = TRUE;
	}
	/* removals from the highest index down, so indices stay valid */
	while (s != NULL && removals->len > 0)
	{
		guint   best = 0, k;
		KeyRef *ref;

		for (k = 1; k < removals->len; k++)
			if (((KeyRef *) g_ptr_array_index (removals, k))->index > ((KeyRef *) g_ptr_array_index (removals, best))->index)
				best = k;
		ref = g_ptr_array_steal_index (removals, best);
		{
			g_autofree gchar *tp = timeline_path (s, clip_index (self, self->clip), ref->target, ref->channel);

			if (tp != NULL)
			{
				g_autofree gchar *kp = g_strdup_printf ("%s/%u", tp, ref->index);

				if (lrg_editor_document_remove (s, kp, NULL))
					any = TRUE;
				if (seq_len (s, tp) == 0)
					lrg_editor_document_remove (s, tp, NULL);
			}
		}
	}
	g_array_set_size (self->moved_keys, 0);
	if (any && !apply_scratch (self, "Edit keys", s, &error))
		report (self, error);
	if (!any)
		rebuild_dope (self);
}

/* ==========================================================================
 * Widgets <- state
 * ========================================================================== */

static void
sync_widgets (LrgEditorSkeleton2DEditor *self)
{
	guint             i;
	g_autofree gchar *sig = NULL;

	if (self->canvas == NULL)
		return;
	for (i = 0; i < 2; i++)
		editor_chip_set_checked (self->mode_chips[i], (guint) self->mode == i);
	for (i = 0; i < 4; i++)
		editor_chip_set_checked (self->tool_chips[i], (guint) self->tool == i);
	lrg_widget_set_visible (self->weights_chip, self->mode == LRG_EDITOR_SKELETON2D_MODE_SETUP);
	lrg_widget_set_visible (self->triangulate_chip, self->mode == LRG_EDITOR_SKELETON2D_MODE_SETUP);
	lrg_widget_set_visible (self->anim_box, self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE);
	editor_chip_set_checked (self->play_chip, self->playing);
	editor_chip_set_icon (self->play_chip, self->playing ? LRG_ICON_PAUSE : LRG_ICON_PLAY);
	editor_chip_set_label (self->play_chip, self->playing ? "Pause" : "Play");
	editor_chip_set_checked (self->loop_chip, self->loop);
	rebuild_outline (self);
	if (!self->syncing)
		rebuild_dope (self);
	sync_curve (self);
	sig = grid_signature (self);
	if (g_strcmp0 (sig, self->grid_sig) != 0)
		self->grid_stale = TRUE;
	else
	{
		lrg_editor_data_source_document_changed (self->source, "");
		lrg_property_grid_refresh (self->grid);
	}
}

/* ==========================================================================
 * Toolbar and side-bar commands
 * ========================================================================== */

static void
on_mode_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);

	lrg_editor_skeleton2d_editor_set_mode (self, chip == self->mode_chips[1] ? LRG_EDITOR_SKELETON2D_MODE_ANIMATE
	                                                                          : LRG_EDITOR_SKELETON2D_MODE_SETUP);
}

static void
on_tool_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	guint                      i;

	for (i = 0; i < 4; i++)
		if (self->tool_chips[i] == chip)
			lrg_editor_skeleton2d_editor_set_tool (self, (LrgEditorSkeleton2DTool) i);
}

static void
on_auto_weights (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;

	if (!lrg_editor_skeleton2d_editor_auto_weights (self, NULL, 0, 4.0f, 2, 1, &error))
		report (self, error);
}

static void
on_triangulate (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;

	if (!lrg_editor_skeleton2d_editor_triangulate (self, &error))
		report (self, error);
}

static void
on_frame (LrgWidget *chip,
          gpointer   user_data)
{
	lrg_editor_skeleton2d_editor_frame_all (LRG_EDITOR_SKELETON2D_EDITOR (user_data));
}

static void
on_add_slot (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;

	if (lrg_editor_skeleton2d_editor_add_slot (self, NULL, MAX (sel_bone_index (self), 0), NULL, &error) < 0)
		report (self, error);
}

static gboolean
skin_taken (gpointer     data,
            const gchar *name)
{
	LrgSkeleton2D *skel = LRG_SKELETON2D (data);
	guint          i;

	for (i = 0; i < lrg_skeleton2d_get_skin_count (skel); i++)
		if (g_strcmp0 (lrg_skeleton2d_get_skin_name (skel, i), name) == 0)
			return TRUE;
	return FALSE;
}

static gboolean
clip_taken (gpointer     data,
            const gchar *name)
{
	return lrg_skeleton2d_get_clip (LRG_SKELETON2D (data), name) != NULL;
}

static void
on_add_skin (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *name = NULL;

	if (!require_skel (self, &error))
	{
		report (self, error);
		return;
	}
	name = unique_name ("skin", skin_taken, self->skel);
	if (!lrg_editor_skeleton2d_editor_add_skin (self, name, &error))
		report (self, error);
}

static void
on_add_clip (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *name = NULL;

	if (!require_skel (self, &error))
	{
		report (self, error);
		return;
	}
	name = unique_name ("clip", clip_taken, self->skel);
	if (!lrg_editor_skeleton2d_editor_add_clip (self, name, 1.0f, TRUE, &error))
		report (self, error);
	else
		lrg_editor_skeleton2d_editor_set_mode (self, LRG_EDITOR_SKELETON2D_MODE_ANIMATE);
}

static void
on_slot_up (LrgWidget *chip,
            gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;

	if (sel_slot_index (self) >= 0)
		lrg_editor_skeleton2d_editor_move_slot (self, sel_slot_index (self), 1, &error);
	report (self, error);
}

static void
on_slot_down (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;

	if (sel_slot_index (self) >= 0)
		lrg_editor_skeleton2d_editor_move_slot (self, sel_slot_index (self), -1, &error);
	report (self, error);
}

/* Delete: the selected attachment, else slot, else bone (setup mode); the
 * selected clip in animation mode. */
static void
on_delete (LrgWidget *chip,
           gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;

	if (self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE && self->clip != NULL)
	{
		g_autofree gchar *name = g_strdup (self->clip);

		lrg_editor_skeleton2d_editor_remove_clip (self, name, &error);
	}
	else if (self->sel_attachment != NULL && sel_slot_index (self) >= 0)
	{
		g_autofree gchar *name = g_strdup (self->sel_attachment);

		lrg_editor_skeleton2d_editor_remove_attachment (self, sel_slot_index (self), name, &error);
	}
	else if (sel_slot_index (self) >= 0)
		lrg_editor_skeleton2d_editor_remove_slot (self, sel_slot_index (self), &error);
	else if (sel_bone_index (self) >= 0)
		lrg_editor_skeleton2d_editor_remove_bone (self, sel_bone_index (self), &error);
	report (self, error);
}

static void
on_play_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);

	lrg_editor_skeleton2d_editor_set_playing (self, !self->playing);
}

static void
on_loop_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);

	lrg_editor_skeleton2d_editor_set_loop (self, !self->loop);
}

static void
on_key_chip (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;

	if (!lrg_editor_skeleton2d_editor_key_pose (self, sel_bone_index (self), &error))
		report (self, error);
}

/* ==========================================================================
 * LrgEditorDataEditor
 * ========================================================================== */

static void
skel_refresh (LrgEditorDataEditor *editor,
              const gchar         *path)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (editor);

	if (self->committing)
		return;
	if (self->drag != DRAG_NONE)
		self->drag = DRAG_NONE;
	reload (self);
}

static void
skel_reveal (LrgEditorDataEditor *editor,
             const gchar         *path,
             guint                line)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (editor);
	g_auto(GStrv)              parts = lrg_editor_document_path_split (path, NULL);
	gint                       index;

	if (parts == NULL || parts[0] == NULL || parts[1] == NULL || self->skel == NULL)
		return;
	index = (gint) g_ascii_strtoll (parts[1], NULL, 10);
	if (g_str_equal (parts[0], "bones") && index < (gint) lrg_skeleton2d_get_bone_count (self->skel))
		set_selection (self, index, sel_slot_index (self), NULL);
	else if (g_str_equal (parts[0], "slots") && index < (gint) lrg_skeleton2d_get_slot_count (self->skel))
		set_selection (self, sel_bone_index (self), index, lrg_skeleton2d_get_slot_attachment (self->skel, index));
	else if (g_str_equal (parts[0], "animations") && index < (gint) lrg_skeleton2d_get_clip_count (self->skel))
	{
		lrg_editor_skeleton2d_editor_set_clip (self, lrg_skeleton2d_clip_get_name (
		                                       lrg_skeleton2d_get_clip_at (self->skel, (guint) index)));
		lrg_editor_skeleton2d_editor_set_mode (self, LRG_EDITOR_SKELETON2D_MODE_ANIMATE);
	}
}

/* The engine's own error at the node path it names
 * ("<data>: /bones/3/parent: unknown bone ..."). */
static void
skel_validate (LrgEditorDataEditor *editor,
               GPtrArray           *issues)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (editor);
	g_autofree gchar          *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autoptr(GError)          error = NULL;
	g_autoptr(LrgSkeleton2D)   probe = lrg_skeleton2d_new_from_data (text, -1, &error);
	g_autofree gchar          *path = NULL;
	const gchar               *msg, *p;
	guint                      line = 0, col = 0, i;

	if (probe != NULL)
	{
		/* attachments whose image is missing */
		g_autofree gchar *dir = rig_dir (self);
		guint             k, s;

		for (k = 0; k < lrg_skeleton2d_get_skin_count (probe); k++)
			for (s = 0; s < lrg_skeleton2d_get_slot_count (probe); s++)
			{
				const gchar  *skin = lrg_skeleton2d_get_skin_name (probe, k);
				g_auto(GStrv) names = lrg_skeleton2d_list_attachments (probe, skin, (gint) s);

				for (i = 0; names != NULL && names[i] != NULL; i++)
				{
					const gchar      *image = lrg_skinned_sprite_get_image (
					                            lrg_skeleton2d_get_skin_attachment (probe, skin, (gint) s, names[i]));
					g_autofree gchar *abs = NULL;
					g_autofree gchar *m = NULL;

					if (image == NULL)
						continue;
					abs = g_path_is_absolute (image) ? g_strdup (image) : g_build_filename (dir, image, NULL);
					if (g_file_test (abs, G_FILE_TEST_IS_REGULAR))
						continue;
					m = g_strdup_printf ("attachment '%s' (skin %s): image '%s' not found", names[i], skin, image);
					g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, "/skins", 0, 0, m));
				}
			}
		return;
	}
	msg = error->message;
	p = strstr (msg, ": /");
	if (p != NULL)
	{
		const gchar *end = strstr (p + 2, ": ");

		path = end != NULL ? g_strndup (p + 2, (gsize) (end - (p + 2))) : g_strdup (p + 2);
		msg = end != NULL ? end + 2 : msg;
	}
	if (path != NULL)
	{
		/* the deepest existing ancestor carries the line */
		while (path[0] != '\0' && !lrg_editor_document_get_line (doc_of (self), path, &line, &col))
		{
			gchar *slash = strrchr (path, '/');

			if (slash == NULL)
				break;
			*slash = '\0';
		}
	}
	g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, path != NULL ? path : "",
	                                                        line, col, msg));
}

static gchar *
skel_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (editor);

	return g_strdup_printf ("mode=%s tool=%s bones=%u slots=%u skins=%u clips=%u bone=%s slot=%s attachment=%s "
	                        "skin=%s clip=%s time=%.3f playing=%d",
	                        mode_nicks[self->mode], tool_nicks[self->tool],
	                        self->skel != NULL ? lrg_skeleton2d_get_bone_count (self->skel) : 0,
	                        self->skel != NULL ? lrg_skeleton2d_get_slot_count (self->skel) : 0,
	                        self->skel != NULL ? lrg_skeleton2d_get_skin_count (self->skel) : 0,
	                        self->skel != NULL ? lrg_skeleton2d_get_clip_count (self->skel) : 0,
	                        self->sel_bone != NULL ? self->sel_bone : "-", self->sel_slot != NULL ? self->sel_slot : "-",
	                        self->sel_attachment != NULL ? self->sel_attachment : "-",
	                        self->skin != NULL ? self->skin : LRG_SKELETON2D_DEFAULT_SKIN,
	                        self->clip != NULL ? self->clip : "-", self->time, self->playing ? 1 : 0);
}

static gboolean
lrg_editor_skeleton2d_editor_step (LrgWidget *widget,
                                   gfloat     delta_time)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (widget);
	gboolean                   busy = LRG_WIDGET_CLASS (lrg_editor_skeleton2d_editor_parent_class)->step (widget,
	                                                                                                    delta_time);
	LrgSkeleton2DClip         *clip = current_clip (self);

	/* preview playback */
	if (self->playing)
	{
		gfloat dur = clip != NULL ? lrg_skeleton2d_clip_get_duration (clip) : 0.0f;

		if (clip == NULL || dur <= 0.0f)
			self->playing = FALSE;
		else
		{
			self->time += delta_time;
			if (self->time > dur)
			{
				if (self->loop)
					self->time = fmodf (self->time, dur);
				else
				{
					self->time = dur;
					self->playing = FALSE;
				}
			}
			if (self->dope != NULL)
			{
				self->syncing = TRUE;
				lrg_timeline_view_set_time (self->dope, self->time);
				self->syncing = FALSE;
			}
			if (!self->playing)
				sync_widgets (self);
			busy = TRUE;
		}
	}
	if (self->moved_keys->len > 0)
		flush_dope_edits (self);
	/* narrow panels: chips drop their labels */
	if (self->canvas_bar != NULL && self->side_bar != NULL)
	{
		editor_panel_fit_toolbar (self->canvas_bar, lrg_widget_get_width (LRG_WIDGET (lrg_widget_get_parent (self->canvas_bar))));
		editor_panel_fit_toolbar (self->side_bar, lrg_widget_get_width (LRG_WIDGET (lrg_widget_get_parent (self->side_bar))));
	}
	if (self->grid_stale)
		rebuild_grid (self);
	return busy || self->drag != DRAG_NONE;
}

/* Keys pressed anywhere in the editor reach the canvas shortcuts unless a
 * text field has the focus (the base class handles undo/save first). */
static gboolean
lrg_editor_skeleton2d_editor_handle_event (LrgWidget        *widget,
                                           const LrgUIEvent *event)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (widget);

	if (LRG_WIDGET_CLASS (lrg_editor_skeleton2d_editor_parent_class)->handle_event (widget, event))
		return TRUE;
	if (lrg_ui_event_get_event_type (event) == LRG_UI_EVENT_KEY_DOWN &&
	    (lrg_ui_event_get_target (event) == NULL || lrg_ui_event_get_target (event) == widget ||
	     lrg_ui_event_get_target (event) == self->canvas || lrg_ui_event_get_target (event) == self->outline))
		return canvas_key (self, lrg_ui_event_get_key (event), lrg_ui_event_get_modifiers (event));
	return FALSE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static LrgWidget *
add_chip (LrgContainer   *box,
          const gchar    *name,
          LrgIcon         icon,
          const gchar    *label,
          EditorChipFunc  func,
          gpointer        data)
{
	LrgWidget *chip = editor_chip_new (name, icon, label, func, data);

	lrg_container_add_child (box, chip);
	g_object_unref (chip);
	return chip;
}

/* A small fixed gap between chip groups. */
static void
add_gap (LrgContainer *box)
{
	LrgWidget *gap = LRG_WIDGET (lrg_custom_widget_new ());

	lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (gap), 10.0f);
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (gap), 4.0f);
	lrg_widget_set_pointer_transparent (gap, TRUE);
	lrg_container_add_child (box, gap);
	g_object_unref (gap);
}

static LrgWidget *
add_tool (LrgEditorSkeleton2DEditor *self,
          const gchar               *name,
          LrgIcon                    icon,
          const gchar               *label,
          EditorChipFunc             func)
{
	LrgWidget *chip = editor_chip_new (name, icon, label, func, self);

	lrg_editor_data_editor_add_tool (LRG_EDITOR_DATA_EDITOR (self), chip);
	g_object_unref (chip);
	return chip;
}

static void
lrg_editor_skeleton2d_editor_constructed (GObject *object)
{
	LrgEditorSkeleton2DEditor        *self = LRG_EDITOR_SKELETON2D_EDITOR (object);
	LrgEditorDataEditor              *base = LRG_EDITOR_DATA_EDITOR (object);
	static const LrgCustomWidgetFuncs canvas_funcs = { NULL, canvas_paint, NULL, canvas_event, { NULL } };
	static const LrgCustomWidgetFuncs outline_funcs = { NULL, outline_paint, NULL, outline_event, { NULL } };
	static const gchar * const        types[] = { ASSET_MIME, NULL };
	LrgDropTarget                    *drop;
	LrgVBox                          *left;
	LrgVBox                          *side;
	LrgHBox                          *cbar;
	LrgHBox                          *bar;
	LrgHBox                          *transport;
	LrgHBox                          *lower;

	G_OBJECT_CLASS (lrg_editor_skeleton2d_editor_parent_class)->constructed (object);
	if (app_of (self) != NULL)
		lrg_editor_skeleton2d_editor_install (app_of (self));

	/* the canvas bar: modes | tools | mesh commands | frame */
	cbar = lrg_hbox_new ();
	lrg_widget_set_name (LRG_WIDGET (cbar), "skel-canvas-bar");
	lrg_container_set_padding (LRG_CONTAINER (cbar), 4.0f);
	lrg_container_set_spacing (LRG_CONTAINER (cbar), 2.0f);
	self->mode_chips[0] = add_chip (LRG_CONTAINER (cbar), "skel-mode-setup", LRG_ICON_EDIT, "Setup", on_mode_chip, self);
	self->mode_chips[1] = add_chip (LRG_CONTAINER (cbar), "skel-mode-animate", LRG_ICON_PLAY, "Animate", on_mode_chip,
	                                self);
	editor_chip_set_tint (self->mode_chips[1], LRG_THEME_COLOR_SUCCESS);
	add_gap (LRG_CONTAINER (cbar));
	self->tool_chips[0] = add_chip (LRG_CONTAINER (cbar), "skel-tool-select", LRG_ICON_ARROW_RIGHT, "Select",
	                                on_tool_chip, self);
	self->tool_chips[1] = add_chip (LRG_CONTAINER (cbar), "skel-tool-bone", LRG_ICON_PLUS, "Bone", on_tool_chip, self);
	self->tool_chips[2] = add_chip (LRG_CONTAINER (cbar), "skel-tool-mesh", LRG_ICON_STAR_HALF, "Mesh", on_tool_chip,
	                                self);
	self->tool_chips[3] = add_chip (LRG_CONTAINER (cbar), "skel-tool-weights", LRG_ICON_HEART, "Weights",
	                                on_tool_chip, self);
	add_gap (LRG_CONTAINER (cbar));
	self->weights_chip = add_chip (LRG_CONTAINER (cbar), "skel-auto-weights", LRG_ICON_REFRESH, "Auto weights",
	                               on_auto_weights, self);
	self->triangulate_chip = add_chip (LRG_CONTAINER (cbar), "skel-triangulate", LRG_ICON_CHECK, "Triangulate",
	                                   on_triangulate, self);
	add_tool (self, "skel-frame", LRG_ICON_SEARCH, "Frame", on_frame);

	/* the canvas */
	self->canvas = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->canvas, "skel-canvas");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->canvas), &canvas_funcs, self, NULL);
	lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (self->canvas), 320.0f);
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->canvas), 240.0f);
	lrg_widget_set_fill_weight (self->canvas, 1.0f);
	lrg_widget_set_focusable (self->canvas, TRUE);
	lrg_widget_set_accepts_tab (self->canvas, TRUE);  /* Tab toggles setup / animate */
	drop = lrg_drop_target_new (types, LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_LINK);
	g_signal_connect_object (drop, "motion", G_CALLBACK (on_drop_motion), self, 0);
	g_signal_connect_object (drop, "leave", G_CALLBACK (on_drop_leave), self, 0);
	g_signal_connect_object (drop, "drop", G_CALLBACK (on_drop), self, 0);
	lrg_widget_add_drop_target (self->canvas, drop);
	g_object_unref (drop);

	/* animation strip: transport, dope sheet | curves */
	self->anim_box = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->anim_box, "skel-anim");
	lrg_container_set_spacing (LRG_CONTAINER (self->anim_box), 4.0f);
	lrg_widget_set_min_size (self->anim_box, 0.0f, DOPE_HEIGHT);
	transport = lrg_hbox_new ();
	lrg_container_set_padding (LRG_CONTAINER (transport), 4.0f);
	lrg_container_set_spacing (LRG_CONTAINER (transport), 4.0f);
	self->play_chip = add_chip (LRG_CONTAINER (transport), "skel-play", LRG_ICON_PLAY, "Play", on_play_chip, self);
	editor_chip_set_tint (self->play_chip, LRG_THEME_COLOR_SUCCESS);
	self->loop_chip = add_chip (LRG_CONTAINER (transport), "skel-loop", LRG_ICON_REFRESH, "Loop", on_loop_chip, self);
	self->key_chip = add_chip (LRG_CONTAINER (transport), "skel-key", LRG_ICON_STAR, "Key pose", on_key_chip, self);
	editor_chip_set_tint (self->key_chip, LRG_THEME_COLOR_WARNING);
	add_chip (LRG_CONTAINER (transport), "skel-add-clip-2", LRG_ICON_PLUS, "Clip", on_add_clip, self);
	lrg_container_add_child (LRG_CONTAINER (self->anim_box), LRG_WIDGET (transport));
	g_object_unref (transport);
	lower = lrg_hbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (lower), 4.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (lower), 1.0f);
	self->dope = lrg_timeline_view_new ();
	lrg_widget_set_name (LRG_WIDGET (self->dope), "skel-dope");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->dope), 1.0f);
	lrg_timeline_view_set_show_play_button (self->dope, FALSE);
	lrg_timeline_view_set_fps (self->dope, 30.0);
	lrg_timeline_view_set_snap (self->dope, LRG_TIMELINE_VIEW_SNAP_GRID);
	g_signal_connect_object (self->dope, "selection-changed", G_CALLBACK (on_dope_selection), self, 0);
	g_signal_connect_object (self->dope, "playhead-moved", G_CALLBACK (on_dope_playhead), self, 0);
	g_signal_connect_object (self->dope, "keyframe-moved", G_CALLBACK (on_dope_key_moved), self, 0);
	g_signal_connect_object (self->dope, "keyframe-removed", G_CALLBACK (on_dope_key_removed), self, 0);
	lrg_container_add_child (LRG_CONTAINER (lower), LRG_WIDGET (self->dope));
	g_object_unref (self->dope);
	self->curves = lrg_curve_edit_new ();
	lrg_widget_set_name (LRG_WIDGET (self->curves), "skel-curves");
	lrg_curve_edit_set_compact (self->curves, FALSE);
	lrg_widget_set_min_size (LRG_WIDGET (self->curves), 240.0f, 0.0f);
	g_signal_connect_object (self->curves, "edit-end", G_CALLBACK (on_curve_edit_end), self, 0);
	lrg_container_add_child (LRG_CONTAINER (lower), LRG_WIDGET (self->curves));
	g_object_unref (self->curves);
	lrg_container_add_child (LRG_CONTAINER (self->anim_box), LRG_WIDGET (lower));
	g_object_unref (lower);

	left = lrg_vbox_new ();
	lrg_widget_set_name (LRG_WIDGET (left), "skel-left");
	self->canvas_bar = LRG_WIDGET (cbar);
	lrg_container_add_child (LRG_CONTAINER (left), LRG_WIDGET (cbar));
	g_object_unref (cbar);
	lrg_container_add_child (LRG_CONTAINER (left), self->canvas);
	g_object_unref (self->canvas);
	lrg_container_add_child (LRG_CONTAINER (left), self->anim_box);
	g_object_unref (self->anim_box);

	/* side bar: commands, outline, properties */
	side = lrg_vbox_new ();
	lrg_widget_set_name (LRG_WIDGET (side), "skel-side");
	lrg_container_set_spacing (LRG_CONTAINER (side), 2.0f);
	bar = lrg_hbox_new ();
	lrg_container_set_padding (LRG_CONTAINER (bar), 4.0f);
	lrg_container_set_spacing (LRG_CONTAINER (bar), 2.0f);
	add_chip (LRG_CONTAINER (bar), "skel-add-slot", LRG_ICON_PLUS, "Slot", on_add_slot, self);
	add_chip (LRG_CONTAINER (bar), "skel-add-skin", LRG_ICON_PLUS, "Skin", on_add_skin, self);
	add_chip (LRG_CONTAINER (bar), "skel-add-clip", LRG_ICON_PLUS, "Clip", on_add_clip, self);
	add_chip (LRG_CONTAINER (bar), "skel-slot-up", LRG_ICON_CHEVRON_UP, NULL, on_slot_up, self);
	add_chip (LRG_CONTAINER (bar), "skel-slot-down", LRG_ICON_CHEVRON_DOWN, NULL, on_slot_down, self);
	editor_chip_set_tint (add_chip (LRG_CONTAINER (bar), "skel-delete", LRG_ICON_TRASH, NULL, on_delete, self),
	                      LRG_THEME_COLOR_DANGER);
	self->side_bar = LRG_WIDGET (bar);
	lrg_container_add_child (LRG_CONTAINER (side), LRG_WIDGET (bar));
	g_object_unref (bar);
	self->outline = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->outline, "skel-outline");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->outline), &outline_funcs, self, NULL);
	lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (self->outline), 200.0f);
	self->outline_scroll = lrg_scroll_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->outline_scroll), "skel-outline-scroll");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->outline_scroll), 1.0f);
	lrg_scroll_box_set_child (self->outline_scroll, self->outline);
	g_object_unref (self->outline);
	lrg_container_add_child (LRG_CONTAINER (side), LRG_WIDGET (self->outline_scroll));
	g_object_unref (self->outline_scroll);
	self->source = lrg_editor_data_source_new (base);
	lrg_editor_data_source_set_read_func (self->source, source_read, self, NULL);
	lrg_editor_data_source_set_write_func (self->source, source_write, self, NULL);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "skel-grid");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->grid), 1.0f);
	lrg_property_grid_set_label_width (self->grid, 96.0f);
	lrg_editor_data_source_bind_grid (self->source, self->grid);
	lrg_container_add_child (LRG_CONTAINER (side), LRG_WIDGET (self->grid));
	g_object_unref (self->grid);

	self->main_split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (self->main_split), "skel-split");
	lrg_splitter_set_start_child (self->main_split, LRG_WIDGET (left));
	g_object_unref (left);
	lrg_splitter_set_end_child (self->main_split, LRG_WIDGET (side));
	g_object_unref (side);
	lrg_splitter_set_default_fraction (self->main_split, 0.72f);
	lrg_splitter_set_fraction (self->main_split, 0.72f, FALSE);
	lrg_splitter_set_min_sizes (self->main_split, 280.0f, 220.0f);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (self->main_split));
	g_object_unref (self->main_split);

	reload (self);
	rebuild_grid (self);
}

static void
lrg_editor_skeleton2d_editor_dispose (GObject *object)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (object);

	if (self->canvas != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->canvas), NULL, NULL, NULL);
	if (self->outline != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->outline), NULL, NULL, NULL);
	self->canvas = NULL;
	self->outline = NULL;
	self->canvas_bar = NULL;
	self->side_bar = NULL;
	self->dope = NULL;
	self->curves = NULL;
	G_OBJECT_CLASS (lrg_editor_skeleton2d_editor_parent_class)->dispose (object);
}

static void
lrg_editor_skeleton2d_editor_finalize (GObject *object)
{
	LrgEditorSkeleton2DEditor *self = LRG_EDITOR_SKELETON2D_EDITOR (object);

	g_clear_object (&self->skel);
	g_clear_object (&self->canon);
	g_clear_object (&self->source);
	g_clear_pointer (&self->textures, g_hash_table_unref);
	g_clear_pointer (&self->keyrefs, g_hash_table_unref);
	g_clear_pointer (&self->rows, g_ptr_array_unref);
	g_clear_pointer (&self->moved_keys, g_array_unref);
	g_clear_pointer (&self->marker_ids, g_array_unref);
	g_free (self->parse_error);
	g_free (self->sel_bone);
	g_free (self->sel_slot);
	g_free (self->sel_attachment);
	g_free (self->skin);
	g_free (self->clip);
	g_free (self->drop_label);
	g_free (self->grid_sig);
	g_free (self->curve_target);
	g_free (self->curve_easings);
	G_OBJECT_CLASS (lrg_editor_skeleton2d_editor_parent_class)->finalize (object);
}

static void
lrg_editor_skeleton2d_editor_class_init (LrgEditorSkeleton2DEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_skeleton2d_editor_constructed;
	object_class->dispose = lrg_editor_skeleton2d_editor_dispose;
	object_class->finalize = lrg_editor_skeleton2d_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_skeleton2d_editor_step;
	LRG_WIDGET_CLASS (klass)->handle_event = lrg_editor_skeleton2d_editor_handle_event;
	data_class->refresh = skel_refresh;
	data_class->reveal = skel_reveal;
	data_class->validate = skel_validate;
	data_class->get_state = skel_get_state;
}

static void
lrg_editor_skeleton2d_editor_init (LrgEditorSkeleton2DEditor *self)
{
	self->textures = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	self->keyrefs = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, keyref_free);
	self->rows = g_ptr_array_new_with_free_func (outline_row_free);
	self->moved_keys = g_array_new (FALSE, FALSE, sizeof (guint));
	self->marker_ids = g_array_new (FALSE, FALSE, sizeof (guint));
	self->zoom = 1.0f;
	self->loop = TRUE;
	self->sel_vertex = -1;
	self->hover_bone = -1;
	self->brush_radius = 12.0f;
	self->brush_strength = 0.35f;
}

/* ==========================================================================
 * Public API: construction and state
 * ========================================================================== */

LrgEditorSkeleton2DEditor *
lrg_editor_skeleton2d_editor_new (LrgEditorApp      *app,
                                  LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_SKELETON2D_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorSkeleton2DEditor *
lrg_editor_skeleton2d_editor_open (LrgEditorApp  *app,
                                   const gchar   *path,
                                   GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_SKELETON2D_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a 2D skeleton (.rskel2d)", path);
		return NULL;
	}
	return LRG_EDITOR_SKELETON2D_EDITOR (e);
}

gboolean
lrg_editor_skeleton2d_editor_create (LrgEditorApp  *app,
                                     const gchar   *path,
                                     const gchar   *name,
                                     GError       **error)
{
	g_autofree gchar         *abs = NULL;
	g_autofree gchar         *dir = NULL;
	g_autofree gchar         *base = NULL;
	g_autoptr(LrgSkeleton2D)  skel = NULL;

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
	skel = lrg_skeleton2d_new ();
	lrg_skeleton2d_set_name (skel, name != NULL && *name != '\0' ? name : base);
	lrg_skeleton2d_add_bone (skel, "root", -1, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 50.0f);
	return lrg_skeleton2d_save_to_file (skel, abs, error);
}

LrgSkeleton2D *
lrg_editor_skeleton2d_editor_get_skeleton (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), NULL);
	return self->skel;
}

const gchar *
lrg_editor_skeleton2d_editor_get_parse_error (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), NULL);
	return self->parse_error;
}

LrgEditorSkeleton2DMode
lrg_editor_skeleton2d_editor_get_mode (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), LRG_EDITOR_SKELETON2D_MODE_SETUP);
	return self->mode;
}

void
lrg_editor_skeleton2d_editor_set_mode (LrgEditorSkeleton2DEditor *self,
                                       LrgEditorSkeleton2DMode    mode)
{
	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	if (self->drag != DRAG_NONE)
		end_drag (self, TRUE);
	self->mode = mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE ? mode : LRG_EDITOR_SKELETON2D_MODE_SETUP;
	if (self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE)
	{
		self->tool = LRG_EDITOR_SKELETON2D_TOOL_SELECT;
		if (self->clip == NULL && self->skel != NULL && lrg_skeleton2d_get_clip_count (self->skel) > 0)
			self->clip = g_strdup (lrg_skeleton2d_clip_get_name (lrg_skeleton2d_get_clip_at (self->skel, 0)));
	}
	else
		self->playing = FALSE;
	sync_widgets (self);
}

LrgEditorSkeleton2DTool
lrg_editor_skeleton2d_editor_get_tool (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), LRG_EDITOR_SKELETON2D_TOOL_SELECT);
	return self->tool;
}

void
lrg_editor_skeleton2d_editor_set_tool (LrgEditorSkeleton2DEditor *self,
                                       LrgEditorSkeleton2DTool    tool)
{
	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	if (self->drag != DRAG_NONE)
		end_drag (self, TRUE);
	self->tool = (guint) tool <= LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS ? tool : LRG_EDITOR_SKELETON2D_TOOL_SELECT;
	if (self->tool != LRG_EDITOR_SKELETON2D_TOOL_SELECT && self->mode != LRG_EDITOR_SKELETON2D_MODE_SETUP)
	{
		self->mode = LRG_EDITOR_SKELETON2D_MODE_SETUP;
		self->playing = FALSE;
	}
	/* the mesh and weight tools work on an attachment: pick the slot's one */
	if ((self->tool == LRG_EDITOR_SKELETON2D_TOOL_MESH || self->tool == LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS) &&
	    self->sel_attachment == NULL && sel_slot_index (self) >= 0)
		self->sel_attachment = g_strdup (lrg_skeleton2d_get_slot_attachment (self->skel, sel_slot_index (self)));
	sync_widgets (self);
}

gint
lrg_editor_skeleton2d_editor_get_selected_bone (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), -1);
	return sel_bone_index (self);
}

void
lrg_editor_skeleton2d_editor_set_selected_bone (LrgEditorSkeleton2DEditor *self,
                                                gint                       bone)
{
	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	if (self->skel == NULL || bone >= (gint) lrg_skeleton2d_get_bone_count (self->skel))
		bone = -1;
	select_bone_name (self, bone >= 0 ? lrg_skeleton2d_get_bone_name (self->skel, bone) : NULL);
	sync_widgets (self);
}

gint
lrg_editor_skeleton2d_editor_get_selected_slot (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), -1);
	return sel_slot_index (self);
}

void
lrg_editor_skeleton2d_editor_set_selected_slot (LrgEditorSkeleton2DEditor *self,
                                                gint                       slot)
{
	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	if (self->skel == NULL || slot >= (gint) lrg_skeleton2d_get_slot_count (self->skel))
		slot = -1;
	g_free (self->sel_slot);
	self->sel_slot = slot >= 0 ? g_strdup (lrg_skeleton2d_get_slot_name (self->skel, slot)) : NULL;
	g_free (self->sel_attachment);
	self->sel_attachment = slot >= 0 ? g_strdup (lrg_skeleton2d_get_slot_attachment (self->skel, slot)) : NULL;
	self->sel_vertex = -1;
	sync_widgets (self);
}

const gchar *
lrg_editor_skeleton2d_editor_get_selected_attachment (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), NULL);
	return self->sel_attachment;
}

gboolean
lrg_editor_skeleton2d_editor_select_attachment (LrgEditorSkeleton2DEditor *self,
                                                gint                       slot,
                                                const gchar               *name)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (self->skel == NULL || slot < 0 || slot >= (gint) lrg_skeleton2d_get_slot_count (self->skel))
		return FALSE;
	if (name != NULL && lrg_skeleton2d_get_attachment (self->skel, slot, name) == NULL)
		return FALSE;
	g_free (self->sel_slot);
	self->sel_slot = g_strdup (lrg_skeleton2d_get_slot_name (self->skel, slot));
	g_free (self->sel_attachment);
	self->sel_attachment = g_strdup (name);
	self->sel_vertex = -1;
	sync_widgets (self);
	return TRUE;
}

const gchar *
lrg_editor_skeleton2d_editor_get_skin (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), NULL);
	return self->skin != NULL ? self->skin : LRG_SKELETON2D_DEFAULT_SKIN;
}

gboolean
lrg_editor_skeleton2d_editor_set_skin (LrgEditorSkeleton2DEditor *self,
                                       const gchar               *name)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (self->skel == NULL)
		return FALSE;
	if (name == NULL || g_str_equal (name, LRG_SKELETON2D_DEFAULT_SKIN))
		name = NULL;
	if (!lrg_skeleton2d_set_skin (self->skel, name))
		return FALSE;
	g_free (self->skin);
	self->skin = g_strdup (name);
	validate_selection (self);
	sync_widgets (self);
	return TRUE;
}

void
lrg_editor_skeleton2d_editor_get_view (LrgEditorSkeleton2DEditor *self,
                                       gfloat                    *zoom,
                                       gfloat                    *pan_x,
                                       gfloat                    *pan_y)
{
	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	if (zoom != NULL)
		*zoom = self->zoom;
	if (pan_x != NULL)
		*pan_x = self->pan_x;
	if (pan_y != NULL)
		*pan_y = self->pan_y;
}

void
lrg_editor_skeleton2d_editor_set_view (LrgEditorSkeleton2DEditor *self,
                                       gfloat                     zoom,
                                       gfloat                     pan_x,
                                       gfloat                     pan_y)
{
	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	self->zoom = CLAMP (zoom, ZOOM_MIN, ZOOM_MAX);
	self->pan_x = pan_x;
	self->pan_y = pan_y;
	self->framed = TRUE;
	self->user_view = TRUE;
}

/* Setup-space bounds of the bones and the shown attachments (in the current
 * mode's pose); FALSE for an empty rig. */
static gboolean
rig_bounds (LrgEditorSkeleton2DEditor *self,
            gfloat                    *b)
{
	LrgSkeleton2DClip *clip = current_clip (self);
	guint              i;

	b[0] = b[1] = G_MAXFLOAT;
	b[2] = b[3] = -G_MAXFLOAT;
	lrg_skeleton2d_reset_pose (self->skel);
	if (self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE && clip != NULL)
		lrg_skeleton2d_clip_apply (clip, self->skel, self->time, 1.0f);
	lrg_skeleton2d_set_root_transform (self->skel, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
	lrg_skeleton2d_update_world (self->skel);
	lrg_skeleton2d_deform (self->skel);
	for (i = 0; i < lrg_skeleton2d_get_bone_count (self->skel); i++)
	{
		gfloat p[4];

		lrg_skeleton2d_get_bone_world_position (self->skel, (gint) i, &p[0], &p[1]);
		lrg_skeleton2d_get_bone_world_tip (self->skel, (gint) i, &p[2], &p[3]);
		b[0] = MIN (b[0], MIN (p[0], p[2]));
		b[1] = MIN (b[1], MIN (p[1], p[3]));
		b[2] = MAX (b[2], MAX (p[0], p[2]));
		b[3] = MAX (b[3], MAX (p[1], p[3]));
	}
	for (i = 0; i < lrg_skeleton2d_get_slot_count (self->skel); i++)
	{
		LrgSkinnedSprite *sp = lrg_skeleton2d_get_attachment (self->skel, (gint) i, NULL);
		gfloat            mn[2], mx[2];

		if (sp != NULL && lrg_skinned_sprite_get_deformed_bounds (sp, &mn[0], &mn[1], &mx[0], &mx[1]))
		{
			b[0] = MIN (b[0], mn[0]);
			b[1] = MIN (b[1], mn[1]);
			b[2] = MAX (b[2], mx[0]);
			b[3] = MAX (b[3], mx[1]);
		}
	}
	return b[0] <= b[2];
}

/* Whether the whole rig shows inside the canvas rectangle (8 px margin). */
static gboolean
rig_in_view (LrgEditorSkeleton2DEditor *self,
             gfloat                     x,
             gfloat                     y,
             gfloat                     w,
             gfloat                     h)
{
	gfloat b[4], x0, y0, x1, y1;

	if (self->skel == NULL || !rig_bounds (self, b))
		return TRUE;
	lrg_editor_skeleton2d_editor_setup_to_canvas (self, b[0], b[1], &x0, &y0);
	lrg_editor_skeleton2d_editor_setup_to_canvas (self, b[2], b[3], &x1, &y1);
	return x0 >= x + 8.0f && y0 >= y + 8.0f && x1 <= x + w - 8.0f && y1 <= y + h - 8.0f;
}

/* Fits the rig into a w x h area: zoom and pan relative to its centre. */
static void
fit_view (LrgEditorSkeleton2DEditor *self,
          gfloat                     w,
          gfloat                     h)
{
	gfloat b[4], bw, bh;

	if (self->skel == NULL || !rig_bounds (self, b))
	{
		self->zoom = 1.0f;
		self->pan_x = self->pan_y = 0.0f;
		return;
	}
	bw = MAX (b[2] - b[0], 20.0f);
	bh = MAX (b[3] - b[1], 20.0f);
	self->zoom = CLAMP (MIN (w / (bw * 1.3f), h / (bh * 1.3f)), ZOOM_MIN, ZOOM_MAX);
	self->pan_x = -((b[0] + b[2]) * 0.5f) * self->zoom;
	self->pan_y = -((b[1] + b[3]) * 0.5f) * self->zoom + 12.0f;  /* clear of the HUD pill */
}

void
lrg_editor_skeleton2d_editor_frame_all (LrgEditorSkeleton2DEditor *self)
{
	gfloat w, h;

	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	w = self->canvas != NULL ? lrg_widget_get_width (self->canvas) : 0.0f;
	h = self->canvas != NULL ? lrg_widget_get_height (self->canvas) : 0.0f;
	if (w <= 0.0f || h <= 0.0f)
		return;
	fit_view (self, w, h);
	self->framed = TRUE;
}

void
lrg_editor_skeleton2d_editor_setup_to_canvas (LrgEditorSkeleton2DEditor *self,
                                              gfloat                     x,
                                              gfloat                     y,
                                              gfloat                    *canvas_x,
                                              gfloat                    *canvas_y)
{
	gfloat ox, oy;

	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	view_origin (self, &ox, &oy);
	*canvas_x = ox + x * self->zoom;
	*canvas_y = oy + y * self->zoom;
}

void
lrg_editor_skeleton2d_editor_canvas_to_setup (LrgEditorSkeleton2DEditor *self,
                                              gfloat                     canvas_x,
                                              gfloat                     canvas_y,
                                              gfloat                    *x,
                                              gfloat                    *y)
{
	gfloat ox, oy;

	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	view_origin (self, &ox, &oy);
	*x = (canvas_x - ox) / self->zoom;
	*y = (canvas_y - oy) / self->zoom;
}

const gchar *
lrg_editor_skeleton2d_editor_get_clip (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), NULL);
	return self->clip;
}

gboolean
lrg_editor_skeleton2d_editor_set_clip (LrgEditorSkeleton2DEditor *self,
                                       const gchar               *name)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	if (name != NULL && (self->skel == NULL || lrg_skeleton2d_get_clip (self->skel, name) == NULL))
		return FALSE;
	if (g_strcmp0 (self->clip, name) != 0)
	{
		g_free (self->clip);
		self->clip = g_strdup (name);
		self->time = 0.0f;
		g_clear_pointer (&self->curve_target, g_free);
	}
	sync_widgets (self);
	return TRUE;
}

gfloat
lrg_editor_skeleton2d_editor_get_time (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), 0.0f);
	return self->time;
}

void
lrg_editor_skeleton2d_editor_set_time (LrgEditorSkeleton2DEditor *self,
                                       gfloat                     time)
{
	LrgSkeleton2DClip *clip;

	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	clip = current_clip (self);
	self->time = CLAMP (time, 0.0f, clip != NULL ? lrg_skeleton2d_clip_get_duration (clip) : 0.0f);
	if (self->dope != NULL)
	{
		self->syncing = TRUE;
		lrg_timeline_view_set_time (self->dope, self->time);
		self->syncing = FALSE;
	}
}

gboolean
lrg_editor_skeleton2d_editor_get_playing (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	return self->playing;
}

void
lrg_editor_skeleton2d_editor_set_playing (LrgEditorSkeleton2DEditor *self,
                                          gboolean                   playing)
{
	LrgSkeleton2DClip *clip;

	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	if (playing && self->mode != LRG_EDITOR_SKELETON2D_MODE_ANIMATE)
		lrg_editor_skeleton2d_editor_set_mode (self, LRG_EDITOR_SKELETON2D_MODE_ANIMATE);
	clip = current_clip (self);
	self->playing = playing && clip != NULL && lrg_skeleton2d_clip_get_duration (clip) > 0.0f;
	/* playing from the end starts over */
	if (self->playing && clip != NULL && self->time >= lrg_skeleton2d_clip_get_duration (clip))
		self->time = 0.0f;
	sync_widgets (self);
}

gboolean
lrg_editor_skeleton2d_editor_get_loop (LrgEditorSkeleton2DEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), FALSE);
	return self->loop;
}

void
lrg_editor_skeleton2d_editor_set_loop (LrgEditorSkeleton2DEditor *self,
                                       gboolean                   loop)
{
	g_return_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self));
	self->loop = !!loop;
	sync_widgets (self);
}

/* ==========================================================================
 * Rendering to an image (tests, thumbnails)
 * ========================================================================== */

GrlImage *
lrg_editor_skeleton2d_editor_render_image (LrgEditorSkeleton2DEditor *self,
                                           guint                      width,
                                           guint                      height)
{
	static const GrlColor bones = { 250, 210, 60, 255 };
	RenderTexture2D       rt;
	Image                 img;
	GrlImage             *out;
	gfloat                zoom = self->zoom, pan_x = self->pan_x, pan_y = self->pan_y;
	const GrlColor       *bg;
	LrgSkeleton2DClip    *clip;

	g_return_val_if_fail (LRG_IS_EDITOR_SKELETON2D_EDITOR (self), NULL);
	if (width == 0 || height == 0 || !IsWindowReady () || self->skel == NULL)
		return NULL;
	attach_textures (self);
	rt = LoadRenderTexture ((gint) width, (gint) height);
	if (rt.id == 0)
		return NULL;

	/* fit the rig into the image and pose it there */
	fit_view (self, (gfloat) width, (gfloat) height);
	clip = current_clip (self);
	lrg_skeleton2d_reset_pose (self->skel);
	if (self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE && clip != NULL)
		lrg_skeleton2d_clip_apply (clip, self->skel, self->time, 1.0f);
	lrg_skeleton2d_set_root_transform (self->skel, (gfloat) width * 0.5f + self->pan_x,
	                                   (gfloat) height * 0.5f + self->pan_y, 0.0f, self->zoom, self->zoom);
	lrg_skeleton2d_update_world (self->skel);

	bg = lrg_theme_get_color (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_COLOR_WINDOW);
	BeginTextureMode (rt);
	ClearBackground ((Color) { bg->r, bg->g, bg->b, 255 });
	lrg_skeleton2d_draw (self->skel, NULL);
	lrg_skeleton2d_draw_bones (self->skel, &bones);
	EndTextureMode ();
	img = LoadImageFromTexture (rt.texture);
	UnloadRenderTexture (rt);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	ImageFlipVertical (&img);
	out = grl_image_new_from_pixels (img.width, img.height, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, img.data);
	UnloadImage (img);

	self->zoom = zoom;
	self->pan_x = pan_x;
	self->pan_y = pan_y;
	pose (self);
	return out;
}

/* ==========================================================================
 * IPC: skel2d.*
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
param_has (JsonObject  *params,
           const gchar *name)
{
	return params != NULL && json_object_has_member (params, name);
}

static gboolean
param_is_null (JsonObject  *params,
               const gchar *name)
{
	return param_has (params, name) && JSON_NODE_HOLDS_NULL (json_object_get_member (params, name));
}

static gdouble
param_number (JsonObject  *params,
              const gchar *name,
              gdouble      fallback)
{
	JsonNode *n;

	if (!param_has (params, name))
		return fallback;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || (json_node_get_value_type (n) != G_TYPE_DOUBLE &&
	                                   json_node_get_value_type (n) != G_TYPE_INT64))
		return fallback;
	return json_node_get_double (n);
}

static gboolean
param_bool (JsonObject  *params,
            const gchar *name,
            gboolean     fallback)
{
	JsonNode *n;

	if (!param_has (params, name))
		return fallback;
	n = json_object_get_member (params, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_BOOLEAN) ? json_node_get_boolean (n)
	                                                                                    : fallback;
}

static void
invalid (GError      **error,
         const gchar  *format,
         ...) G_GNUC_PRINTF (2, 3);

static void
invalid (GError      **error,
         const gchar  *format,
         ...)
{
	va_list           args;
	g_autofree gchar *msg = NULL;

	va_start (args, format);
	msg = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, msg);
}

static void
fail (GError       **error,
      const GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	                      g_error_matches (cause, G_FILE_ERROR, G_FILE_ERROR_NOENT) ||
	                      g_error_matches (cause, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND)))
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

static gdouble
r4 (gfloat v)
{
	return round ((gdouble) v * 10000.0) / 10000.0;
}

static JsonArray *
float_array (const gfloat *v,
             guint         n)
{
	JsonArray *a = json_array_new ();
	guint      i;

	for (i = 0; i < n; i++)
		json_array_add_double_element (a, r4 (v[i]));
	return a;
}

/* The editor of params.path (opening it). */
static LrgEditorSkeleton2DEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar               *path = param_string (params, "path");
	LrgEditorSkeleton2DEditor *self;
	g_autoptr(GError)          err = NULL;

	if (path == NULL)
	{
		invalid (error, "Missing string parameter 'path'");
		return NULL;
	}
	self = lrg_editor_skeleton2d_editor_open (app, path, &err);
	if (self == NULL)
	{
		fail (error, err);
		return NULL;
	}
	if (self->skel == NULL || self->parse_error != NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "the rig does not load: %s",
		             self->parse_error != NULL ? self->parse_error : "unknown error");
		return NULL;
	}
	return self;
}

/* A bone or slot given by name or index. */
static gint
param_index (LrgEditorSkeleton2DEditor  *self,
             JsonObject                 *params,
             const gchar                *name,
             gboolean                    slot,
             gboolean                    required,
             GError                    **error)
{
	const gchar *s = param_string (params, name);
	gint         index = -1;

	if (s != NULL)
		index = slot ? lrg_skeleton2d_find_slot (self->skel, s) : lrg_skeleton2d_find_bone (self->skel, s);
	else if (param_has (params, name) && !param_is_null (params, name))
		index = (gint) param_number (params, name, -1.0);
	else if (!required)
		return -1;
	if (index < 0 || index >= (gint) (slot ? lrg_skeleton2d_get_slot_count (self->skel)
	                                       : lrg_skeleton2d_get_bone_count (self->skel)))
	{
		if (s == NULL && !param_has (params, name))
			invalid (error, "Missing parameter '%s'", name);
		else
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no %s '%s'",
			             slot ? "slot" : "bone", s != NULL ? s : "(index)");
		return -2;
	}
	return index;
}

static JsonObject *
bone_json (LrgSkeleton2D *skel,
           gint           i)
{
	JsonObject *b = json_object_new ();
	gfloat      x, y, r, sx, sy, s[2];
	gint        parent = lrg_skeleton2d_get_bone_parent (skel, i);

	lrg_skeleton2d_get_bone_setup (skel, i, &x, &y, &r, &sx, &sy);
	s[0] = sx;
	s[1] = sy;
	json_object_set_string_member (b, "name", lrg_skeleton2d_get_bone_name (skel, i));
	if (parent >= 0)
		json_object_set_string_member (b, "parent", lrg_skeleton2d_get_bone_name (skel, parent));
	else
		json_object_set_null_member (b, "parent");
	json_object_set_double_member (b, "x", r4 (x));
	json_object_set_double_member (b, "y", r4 (y));
	json_object_set_double_member (b, "rotation", r4 (r));
	json_object_set_array_member (b, "scale", float_array (s, 2));
	json_object_set_double_member (b, "length", r4 (lrg_skeleton2d_get_bone_length (skel, i)));
	return b;
}

static JsonObject *
slot_json (LrgSkeleton2D *skel,
           gint           i)
{
	JsonObject  *o = json_object_new ();
	gfloat       rgba[4];
	const gchar *att = lrg_skeleton2d_get_slot_setup_attachment (skel, i);

	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_get_slot_color (skel, i, rgba);
	json_object_set_string_member (o, "name", lrg_skeleton2d_get_slot_name (skel, i));
	json_object_set_string_member (o, "bone", lrg_skeleton2d_get_bone_name (skel, lrg_skeleton2d_get_slot_bone (skel, i)));
	if (att != NULL)
		json_object_set_string_member (o, "attachment", att);
	else
		json_object_set_null_member (o, "attachment");
	json_object_set_array_member (o, "color", float_array (rgba, 4));
	return o;
}

static JsonObject *
editor_json (LrgEditorSkeleton2DEditor *self)
{
	LrgSkeleton2D *skel = self->skel;
	JsonObject    *o = json_object_new ();
	JsonObject    *sel = json_object_new ();
	JsonArray     *a;
	const gint    *order;
	guint          i, n = 0, k;

	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "name", lrg_skeleton2d_get_name (skel));
	json_object_set_boolean_member (o, "dirty", lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "mode", mode_nicks[self->mode]);
	json_object_set_string_member (o, "tool", tool_nicks[self->tool]);
	json_object_set_string_member (o, "skin", lrg_editor_skeleton2d_editor_get_skin (self));
	json_object_set_string_member (o, "clip", self->clip);
	json_object_set_double_member (o, "time", r4 (self->time));
	json_object_set_boolean_member (o, "playing", self->playing);
	json_object_set_boolean_member (o, "loop", self->loop);
	json_object_set_string_member (sel, "bone", self->sel_bone);
	json_object_set_string_member (sel, "slot", self->sel_slot);
	json_object_set_string_member (sel, "attachment", self->sel_attachment);
	json_object_set_object_member (o, "selected", sel);

	a = json_array_new ();
	for (i = 0; i < lrg_skeleton2d_get_bone_count (skel); i++)
		json_array_add_object_element (a, bone_json (skel, (gint) i));
	json_object_set_array_member (o, "bones", a);
	a = json_array_new ();
	for (i = 0; i < lrg_skeleton2d_get_slot_count (skel); i++)
		json_array_add_object_element (a, slot_json (skel, (gint) i));
	json_object_set_array_member (o, "slots", a);
	a = json_array_new ();
	lrg_skeleton2d_reset_pose (skel);
	order = lrg_skeleton2d_get_draw_order (skel, &n);
	for (i = 0; i < n; i++)
		json_array_add_string_element (a, lrg_skeleton2d_get_slot_name (skel, order[i]));
	json_object_set_array_member (o, "draw_order", a);

	a = json_array_new ();
	for (k = 0; k < lrg_skeleton2d_get_skin_count (skel); k++)
	{
		const gchar *skin = lrg_skeleton2d_get_skin_name (skel, k);
		JsonObject  *so = json_object_new ();
		JsonArray   *atts = json_array_new ();

		for (i = 0; i < lrg_skeleton2d_get_slot_count (skel); i++)
		{
			g_auto(GStrv) names = lrg_skeleton2d_list_attachments (skel, skin, (gint) i);
			guint         j;

			for (j = 0; names != NULL && names[j] != NULL; j++)
			{
				LrgSkinnedSprite *sp = lrg_skeleton2d_get_skin_attachment (skel, skin, (gint) i, names[j]);
				JsonObject       *ao = json_object_new ();

				json_object_set_string_member (ao, "slot", lrg_skeleton2d_get_slot_name (skel, (gint) i));
				json_object_set_string_member (ao, "name", names[j]);
				json_object_set_string_member (ao, "image", lrg_skinned_sprite_get_image (sp));
				json_object_set_int_member (ao, "vertices", lrg_skinned_sprite_get_vertex_count (sp));
				json_object_set_int_member (ao, "triangles", lrg_skinned_sprite_get_triangle_count (sp));
				json_object_set_boolean_member (ao, "weighted", lrg_skinned_sprite_has_weights (sp));
				json_array_add_object_element (atts, ao);
			}
		}
		json_object_set_string_member (so, "name", skin);
		json_object_set_array_member (so, "attachments", atts);
		json_array_add_object_element (a, so);
	}
	json_object_set_array_member (o, "skins", a);

	a = json_array_new ();
	for (k = 0; k < lrg_skeleton2d_get_clip_count (skel); k++)
	{
		LrgSkeleton2DClip *clip = lrg_skeleton2d_get_clip_at (skel, k);
		JsonObject        *co = json_object_new ();
		JsonArray         *t;
		g_auto(GStrv)      bones = lrg_skeleton2d_clip_get_targets (clip, FALSE);
		g_auto(GStrv)      slots = lrg_skeleton2d_clip_get_targets (clip, TRUE);

		json_object_set_string_member (co, "name", lrg_skeleton2d_clip_get_name (clip));
		json_object_set_double_member (co, "duration", r4 (lrg_skeleton2d_clip_get_duration (clip)));
		json_object_set_boolean_member (co, "loop", lrg_skeleton2d_clip_get_loop (clip));
		t = json_array_new ();
		for (i = 0; bones != NULL && bones[i] != NULL; i++)
			json_array_add_string_element (t, bones[i]);
		json_object_set_array_member (co, "bones", t);
		t = json_array_new ();
		for (i = 0; slots != NULL && slots[i] != NULL; i++)
			json_array_add_string_element (t, slots[i]);
		json_object_set_array_member (co, "slots", t);
		json_array_add_object_element (a, co);
	}
	json_object_set_array_member (o, "clips", a);
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
	JsonObject        *o;

	if (path == NULL)
	{
		invalid (error, "Missing string parameter 'path'");
		return NULL;
	}
	if (!lrg_editor_skeleton2d_editor_create (app, path, param_string (params, "name"), &err))
	{
		fail (error, err);
		return NULL;
	}
	{
		g_autofree gchar *abs = editor_app_project_path (app, path);

		o = json_object_new ();
		json_object_set_string_member (o, "path", abs);
	}
	return object_node (o);
}

static JsonNode *
m_open (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject                *o;

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
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return self != NULL ? object_node (editor_json (self)) : NULL;
}

static JsonNode *
m_add_bone (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	gint                       parent, index;
	JsonObject                *o;

	if (self == NULL)
		return NULL;
	parent = param_index (self, params, "parent", FALSE, FALSE, error);
	if (parent < -1)
		return NULL;
	index = lrg_editor_skeleton2d_editor_add_bone (self, param_string (params, "name"), parent,
	                                               (gfloat) param_number (params, "x", 0.0),
	                                               (gfloat) param_number (params, "y", 0.0),
	                                               (gfloat) param_number (params, "rotation", 0.0),
	                                               (gfloat) param_number (params, "length", 0.0), &err);
	if (index < 0)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "bone", lrg_skeleton2d_get_bone_name (self->skel, index));
	json_object_set_int_member (o, "index", index);
	return object_node (o);
}

static JsonNode *
m_set_bone (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	gfloat                     x, y, r, sx, sy, len;
	gint                       bone;
	const gchar               *name;

	if (self == NULL)
		return NULL;
	bone = param_index (self, params, "bone", FALSE, TRUE, error);
	if (bone < 0)
		return NULL;
	lrg_skeleton2d_get_bone_setup (self->skel, bone, &x, &y, &r, &sx, &sy);
	len = lrg_skeleton2d_get_bone_length (self->skel, bone);
	if (param_has (params, "x") || param_has (params, "y") || param_has (params, "rotation") ||
	    param_has (params, "scale_x") || param_has (params, "scale_y") || param_has (params, "length"))
	{
		if (!lrg_editor_skeleton2d_editor_set_bone (self, bone, (gfloat) param_number (params, "x", x),
		                                            (gfloat) param_number (params, "y", y),
		                                            (gfloat) param_number (params, "rotation", r),
		                                            (gfloat) param_number (params, "scale_x", sx),
		                                            (gfloat) param_number (params, "scale_y", sy),
		                                            (gfloat) param_number (params, "length", len), &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	name = param_string (params, "name");
	if (name != NULL && !lrg_editor_skeleton2d_editor_rename_bone (self, bone, name, &err))
	{
		fail (error, err);
		return NULL;
	}
	{
		JsonObject *o = bone_json (self->skel, bone);

		json_object_set_string_member (o, "bone", lrg_skeleton2d_get_bone_name (self->skel, bone));
		return object_node (o);
	}
}

static JsonNode *
m_remove_bone (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	g_autoptr(GPtrArray)       before = g_ptr_array_new_with_free_func (g_free);
	JsonArray                 *removed;
	JsonObject                *o;
	gint                       bone;
	guint                      i;

	if (self == NULL)
		return NULL;
	bone = param_index (self, params, "bone", FALSE, TRUE, error);
	if (bone < 0)
		return NULL;
	for (i = 0; i < lrg_skeleton2d_get_bone_count (self->skel); i++)
		g_ptr_array_add (before, g_strdup (lrg_skeleton2d_get_bone_name (self->skel, (gint) i)));
	if (!lrg_editor_skeleton2d_editor_remove_bone (self, bone, &err))
	{
		fail (error, err);
		return NULL;
	}
	removed = json_array_new ();
	for (i = 0; i < before->len; i++)
		if (lrg_skeleton2d_find_bone (self->skel, g_ptr_array_index (before, i)) < 0)
			json_array_add_string_element (removed, g_ptr_array_index (before, i));
	o = json_object_new ();
	json_object_set_array_member (o, "removed", removed);
	return object_node (o);
}

static JsonNode *
m_add_slot (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	gint                       bone, index;
	JsonObject                *o;

	if (self == NULL)
		return NULL;
	bone = param_index (self, params, "bone", FALSE, TRUE, error);
	if (bone < 0)
		return NULL;
	index = lrg_editor_skeleton2d_editor_add_slot (self, param_string (params, "name"), bone,
	                                               param_string (params, "attachment"), &err);
	if (index < 0)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "slot", lrg_skeleton2d_get_slot_name (self->skel, index));
	json_object_set_int_member (o, "index", index);
	return object_node (o);
}

static JsonNode *
m_set_slot (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	gint                       slot;
	const gchar               *name;

	if (self == NULL)
		return NULL;
	slot = param_index (self, params, "slot", TRUE, TRUE, error);
	if (slot < 0)
		return NULL;
	if (param_has (params, "attachment") &&
	    !lrg_editor_skeleton2d_editor_set_slot_attachment (self, slot, param_string (params, "attachment"), &err))
	{
		fail (error, err);
		return NULL;
	}
	if (param_has (params, "color"))
	{
		JsonNode *n = json_object_get_member (params, "color");
		gfloat    rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		guint     i;

		if (!JSON_NODE_HOLDS_ARRAY (n) || json_array_get_length (json_node_get_array (n)) < 3)
		{
			invalid (error, "'color' must be [r, g, b] or [r, g, b, a]");
			return NULL;
		}
		for (i = 0; i < MIN (json_array_get_length (json_node_get_array (n)), 4u); i++)
			rgba[i] = (gfloat) json_array_get_double_element (json_node_get_array (n), i);
		if (!lrg_editor_skeleton2d_editor_set_slot_color (self, slot, rgba, &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	name = param_string (params, "name");
	if (name != NULL && !lrg_editor_skeleton2d_editor_rename_slot (self, slot, name, &err))
	{
		fail (error, err);
		return NULL;
	}
	{
		JsonObject *o = slot_json (self->skel, slot);

		json_object_set_string_member (o, "slot", lrg_skeleton2d_get_slot_name (self->skel, slot));
		return object_node (o);
	}
}

static JsonNode *
m_remove_slot (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	g_autofree gchar          *name = NULL;
	JsonObject                *o;
	gint                       slot;

	if (self == NULL)
		return NULL;
	slot = param_index (self, params, "slot", TRUE, TRUE, error);
	if (slot < 0)
		return NULL;
	name = g_strdup (lrg_skeleton2d_get_slot_name (self->skel, slot));
	if (!lrg_editor_skeleton2d_editor_remove_slot (self, slot, &err))
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "removed", name);
	return object_node (o);
}

static JsonNode *
m_draw_order (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	g_autoptr(GPtrArray)       names = g_ptr_array_new ();
	JsonArray                 *a, *out;
	JsonObject                *o;
	guint                      i;

	if (self == NULL)
		return NULL;
	if (!param_has (params, "order") || !JSON_NODE_HOLDS_ARRAY (json_object_get_member (params, "order")))
	{
		invalid (error, "Missing array parameter 'order'");
		return NULL;
	}
	a = json_object_get_array_member (params, "order");
	for (i = 0; i < json_array_get_length (a); i++)
		g_ptr_array_add (names, (gpointer) json_array_get_string_element (a, i));
	g_ptr_array_add (names, NULL);
	if (!lrg_editor_skeleton2d_editor_set_draw_order (self, (const gchar * const *) names->pdata, &err))
	{
		fail (error, err);
		return NULL;
	}
	out = json_array_new ();
	for (i = 0; i < lrg_skeleton2d_get_slot_count (self->skel); i++)
		json_array_add_string_element (out, lrg_skeleton2d_get_slot_name (self->skel, (gint) i));
	o = json_object_new ();
	json_object_set_array_member (o, "order", out);
	return object_node (o);
}

/* The attachment named by params (slot, name, skin) becomes the selection. */
static gboolean
ipc_select_attachment (LrgEditorSkeleton2DEditor  *self,
                       JsonObject                 *params,
                       GError                    **error)
{
	gint         slot = param_index (self, params, "slot", TRUE, TRUE, error);
	const gchar *name = param_string (params, "name");
	const gchar *skin = param_string (params, "skin");

	if (slot < 0)
		return FALSE;
	if (name == NULL)
	{
		invalid (error, "Missing string parameter 'name'");
		return FALSE;
	}
	if (skin != NULL && !lrg_editor_skeleton2d_editor_set_skin (self, skin))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no skin '%s'", skin);
		return FALSE;
	}
	if (!lrg_editor_skeleton2d_editor_select_attachment (self, slot, name))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no attachment '%s' on slot %s",
		             name, lrg_skeleton2d_get_slot_name (self->skel, slot));
		return FALSE;
	}
	return TRUE;
}

static JsonNode *
mesh_result (LrgEditorSkeleton2DEditor *self)
{
	LrgSkinnedSprite *sp = sel_sprite (self, NULL);
	JsonObject       *o = json_object_new ();

	json_object_set_string_member (o, "slot", self->sel_slot);
	json_object_set_string_member (o, "name", self->sel_attachment);
	json_object_set_int_member (o, "vertices", sp != NULL ? lrg_skinned_sprite_get_vertex_count (sp) : 0);
	json_object_set_int_member (o, "triangles", sp != NULL ? lrg_skinned_sprite_get_triangle_count (sp) : 0);
	json_object_set_boolean_member (o, "weighted", sp != NULL && lrg_skinned_sprite_has_weights (sp));
	return object_node (o);
}

static JsonNode *
m_attach (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	const gchar               *skin;
	gint                       slot;

	if (self == NULL)
		return NULL;
	slot = param_index (self, params, "slot", TRUE, TRUE, error);
	if (slot < 0)
		return NULL;
	skin = param_string (params, "skin");
	if (skin != NULL && !lrg_editor_skeleton2d_editor_set_skin (self, skin))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no skin '%s'", skin);
		return NULL;
	}
	if (!lrg_editor_skeleton2d_editor_add_attachment (self, slot, param_string (params, "name"),
	                                                  param_string (params, "image"),
	                                                  (gfloat) param_number (params, "x", 0.0),
	                                                  (gfloat) param_number (params, "y", 0.0),
	                                                  (gfloat) param_number (params, "width", 0.0),
	                                                  (gfloat) param_number (params, "height", 0.0),
	                                                  (guint) MAX (param_number (params, "columns", 1.0), 1.0),
	                                                  (guint) MAX (param_number (params, "rows", 1.0), 1.0), &err))
	{
		fail (error, err);
		return NULL;
	}
	return mesh_result (self);
}

static JsonNode *
m_detach (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	JsonObject                *o;
	gint                       slot;

	if (self == NULL || !ipc_select_attachment (self, params, error))
		return NULL;
	slot = sel_slot_index (self);
	if (!lrg_editor_skeleton2d_editor_remove_attachment (self, slot, param_string (params, "name"), &err))
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "removed", param_string (params, "name"));
	return object_node (o);
}

static JsonNode *
m_mesh (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;

	if (self == NULL || !ipc_select_attachment (self, params, error))
		return NULL;
	if (param_has (params, "add"))
	{
		JsonNode  *n = json_object_get_member (params, "add");
		JsonArray *a;
		guint      i;

		if (!JSON_NODE_HOLDS_ARRAY (n))
		{
			invalid (error, "'add' must be a list of [x, y]");
			return NULL;
		}
		a = json_node_get_array (n);
		for (i = 0; i < json_array_get_length (a); i++)
		{
			JsonArray *p = json_array_get_array_element (a, i);

			if (p == NULL || json_array_get_length (p) < 2)
			{
				invalid (error, "'add' must be a list of [x, y]");
				return NULL;
			}
			if (lrg_editor_skeleton2d_editor_add_vertex (self, (gfloat) json_array_get_double_element (p, 0),
			                                             (gfloat) json_array_get_double_element (p, 1), &err) < 0)
			{
				fail (error, err);
				return NULL;
			}
		}
	}
	if (param_has (params, "move"))
	{
		JsonObject *m = json_object_get_object_member (params, "move");

		if (m == NULL || !lrg_editor_skeleton2d_editor_move_vertex (self, (guint) param_number (m, "index", -1.0),
		                                                            (gfloat) param_number (m, "x", 0.0),
		                                                            (gfloat) param_number (m, "y", 0.0), &err))
		{
			if (err != NULL)
				fail (error, err);
			else
				invalid (error, "'move' must be {index, x, y}");
			return NULL;
		}
	}
	if (param_has (params, "remove") &&
	    !lrg_editor_skeleton2d_editor_remove_vertex (self, (guint) param_number (params, "remove", -1.0), &err))
	{
		fail (error, err);
		return NULL;
	}
	if (param_bool (params, "triangulate", FALSE) && !lrg_editor_skeleton2d_editor_triangulate (self, &err))
	{
		fail (error, err);
		return NULL;
	}
	return mesh_result (self);
}

static JsonNode *
m_auto_weights (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	g_autoptr(GArray)          bones = g_array_new (FALSE, FALSE, sizeof (gint));

	if (self == NULL || !ipc_select_attachment (self, params, error))
		return NULL;
	if (param_has (params, "bones") && JSON_NODE_HOLDS_ARRAY (json_object_get_member (params, "bones")))
	{
		JsonArray *a = json_object_get_array_member (params, "bones");
		guint      i;

		for (i = 0; i < json_array_get_length (a); i++)
		{
			gint b = lrg_skeleton2d_find_bone (self->skel, json_array_get_string_element (a, i));

			if (b < 0)
			{
				g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no bone '%s'",
				             json_array_get_string_element (a, i));
				return NULL;
			}
			g_array_append_val (bones, b);
		}
	}
	if (!lrg_editor_skeleton2d_editor_auto_weights (self, bones->len > 0 ? (const gint *) bones->data : NULL,
	                                                bones->len, (gfloat) param_number (params, "falloff", 4.0),
	                                                (guint) param_number (params, "max_influences", 2.0),
	                                                (guint) param_number (params, "smooth", 1.0), &err))
	{
		fail (error, err);
		return NULL;
	}
	return mesh_result (self);
}

static JsonNode *
m_paint_weights (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	gfloat                     strength;
	gint                       bone, changed;
	JsonObject                *o;

	if (self == NULL || !ipc_select_attachment (self, params, error))
		return NULL;
	bone = param_index (self, params, "bone", FALSE, TRUE, error);
	if (bone < 0)
		return NULL;
	strength = (gfloat) param_number (params, "strength", 0.5);
	if (param_bool (params, "erase", FALSE))
		strength = -fabsf (strength);
	changed = lrg_editor_skeleton2d_editor_paint_weights (self, bone, (gfloat) param_number (params, "x", 0.0),
	                                                      (gfloat) param_number (params, "y", 0.0),
	                                                      (gfloat) param_number (params, "radius", 20.0), strength, &err);
	if (changed < 0)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_int_member (o, "changed", changed);
	return object_node (o);
}

static JsonNode *
m_skin (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	const gchar               *name = param_string (params, "name");
	const gchar               *op = param_string (params, "op");
	JsonArray                 *skins;
	JsonObject                *o;
	guint                      k;

	if (self == NULL)
		return NULL;
	if (name == NULL)
	{
		invalid (error, "Missing string parameter 'name'");
		return NULL;
	}
	if (op == NULL || g_str_equal (op, "activate"))
	{
		if (!lrg_editor_skeleton2d_editor_set_skin (self, name))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no skin '%s'", name);
			return NULL;
		}
	}
	else if (g_str_equal (op, "add"))
	{
		if (!lrg_editor_skeleton2d_editor_add_skin (self, name, &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	else if (g_str_equal (op, "remove"))
	{
		if (!lrg_editor_skeleton2d_editor_remove_skin (self, name, &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	else
	{
		invalid (error, "Unknown op '%s' (add, remove or activate)", op);
		return NULL;
	}
	skins = json_array_new ();
	for (k = 0; k < lrg_skeleton2d_get_skin_count (self->skel); k++)
		json_array_add_string_element (skins, lrg_skeleton2d_get_skin_name (self->skel, k));
	o = json_object_new ();
	json_object_set_array_member (o, "skins", skins);
	json_object_set_string_member (o, "active", lrg_editor_skeleton2d_editor_get_skin (self));
	return object_node (o);
}

static JsonNode *
m_clip (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	const gchar               *name = param_string (params, "name");
	const gchar               *op = param_string (params, "op");
	LrgSkeleton2DClip         *clip;
	JsonArray                 *clips;
	JsonObject                *o;
	guint                      k;

	if (self == NULL)
		return NULL;
	if (name == NULL)
	{
		invalid (error, "Missing string parameter 'name'");
		return NULL;
	}
	if (op == NULL || g_str_equal (op, "select"))
	{
		if (!lrg_editor_skeleton2d_editor_set_clip (self, name))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no clip '%s'", name);
			return NULL;
		}
	}
	else if (g_str_equal (op, "add"))
	{
		if (!lrg_editor_skeleton2d_editor_add_clip (self, name, (gfloat) param_number (params, "duration", 1.0),
		                                            param_bool (params, "loop", FALSE), &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	else if (g_str_equal (op, "remove"))
	{
		if (!lrg_editor_skeleton2d_editor_remove_clip (self, name, &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	else if (g_str_equal (op, "set"))
	{
		clip = lrg_skeleton2d_get_clip (self->skel, name);
		if (clip == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no clip '%s'", name);
			return NULL;
		}
		if (!lrg_editor_skeleton2d_editor_set_clip_props (self, name,
		                                                  (gfloat) param_number (params, "duration",
		                                                                         lrg_skeleton2d_clip_get_duration (clip)),
		                                                  param_bool (params, "loop", lrg_skeleton2d_clip_get_loop (clip)),
		                                                  &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	else
	{
		invalid (error, "Unknown op '%s' (add, remove, select or set)", op);
		return NULL;
	}
	clips = json_array_new ();
	for (k = 0; k < lrg_skeleton2d_get_clip_count (self->skel); k++)
		json_array_add_string_element (clips, lrg_skeleton2d_clip_get_name (lrg_skeleton2d_get_clip_at (self->skel, k)));
	o = json_object_new ();
	json_object_set_array_member (o, "clips", clips);
	json_object_set_string_member (o, "clip", self->clip);
	return object_node (o);
}

static gint
channel_from_nick (const gchar *nick)
{
	guint i;

	for (i = 0; nick != NULL && i < G_N_ELEMENTS (channel_nicks); i++)
		if (g_str_equal (nick, channel_nicks[i]))
			return (gint) i;
	return -1;
}

static JsonNode *
m_key (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	const gchar               *target = param_string (params, "target");
	const gchar               *clip_name = param_string (params, "clip");
	gint                       channel = channel_from_nick (param_string (params, "channel"));
	gfloat                     time = (gfloat) param_number (params, "time", 0.0);
	LrgSkeleton2DClip         *clip;
	JsonArray                 *keys;
	JsonObject                *o;
	guint                      i, n;

	if (self == NULL)
		return NULL;
	if (target == NULL || channel < 0)
	{
		invalid (error, "Need 'target' and 'channel' (translate, rotate, scale, color or attachment)");
		return NULL;
	}
	if (clip_name != NULL && !lrg_editor_skeleton2d_editor_set_clip (self, clip_name))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no clip '%s'", clip_name);
		return NULL;
	}
	clip = current_clip (self);
	if (clip == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no clip selected");
		return NULL;
	}
	if (param_bool (params, "remove", FALSE))
	{
		gint index = key_at_time (clip, target, (LrgSkeleton2DChannel) channel, time);

		if (index < 0 || !lrg_editor_skeleton2d_editor_remove_key (self, target, (LrgSkeleton2DChannel) channel,
		                                                           (guint) index, &err))
		{
			if (err != NULL)
				fail (error, err);
			else
				g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no key at %g", time);
			return NULL;
		}
	}
	else
	{
		gfloat             v[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, cp[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		guint              nv = 0;
		LrgSkeleton2DCurve curve = LRG_SKELETON2D_CURVE_LINEAR;

		if (param_has (params, "values") && JSON_NODE_HOLDS_ARRAY (json_object_get_member (params, "values")))
		{
			JsonArray *a = json_object_get_array_member (params, "values");

			for (nv = 0; nv < MIN (json_array_get_length (a), 4u); nv++)
				v[nv] = (gfloat) json_array_get_double_element (a, nv);
		}
		if (param_has (params, "curve"))
		{
			JsonNode *c = json_object_get_member (params, "curve");

			if (JSON_NODE_HOLDS_ARRAY (c) && json_array_get_length (json_node_get_array (c)) == 4)
			{
				for (i = 0; i < 4; i++)
					cp[i] = (gfloat) json_array_get_double_element (json_node_get_array (c), i);
				curve = LRG_SKELETON2D_CURVE_BEZIER;
			}
			else if (g_strcmp0 (param_string (params, "curve"), "stepped") == 0)
				curve = LRG_SKELETON2D_CURVE_STEPPED;
			else if (g_strcmp0 (param_string (params, "curve"), "linear") != 0)
			{
				invalid (error, "'curve' must be \"linear\", \"stepped\" or [x1, y1, x2, y2]");
				return NULL;
			}
		}
		if (!lrg_editor_skeleton2d_editor_set_key (self, target, (LrgSkeleton2DChannel) channel, time,
		                                           nv > 0 ? v : NULL, nv, param_string (params, "attachment"), curve,
		                                           curve == LRG_SKELETON2D_CURVE_BEZIER ? cp : NULL, &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	clip = current_clip (self);
	keys = json_array_new ();
	n = clip != NULL ? lrg_skeleton2d_clip_get_key_count (clip, target, (LrgSkeleton2DChannel) channel) : 0;
	for (i = 0; i < n; i++)
	{
		JsonObject        *k = json_object_new ();
		gfloat             t = 0.0f, v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		LrgSkeleton2DCurve curve = LRG_SKELETON2D_CURVE_LINEAR;

		if (channel == LRG_SKELETON2D_CHANNEL_ATTACHMENT)
		{
			const gchar *att = NULL;

			lrg_skeleton2d_clip_get_attachment_key (clip, target, i, &t, &att);
			json_object_set_string_member (k, "attachment", att);
			curve = LRG_SKELETON2D_CURVE_STEPPED;
		}
		else
		{
			lrg_skeleton2d_clip_get_key (clip, target, (LrgSkeleton2DChannel) channel, i, &t, v, &curve);
			json_object_set_array_member (k, "values", float_array (v, channel == LRG_SKELETON2D_CHANNEL_ROTATE ? 1
			                                                           : channel == LRG_SKELETON2D_CHANNEL_COLOR ? 4 : 2));
		}
		json_object_set_double_member (k, "time", r4 (t));
		json_object_set_string_member (k, "curve", curve == LRG_SKELETON2D_CURVE_STEPPED ? "stepped"
		                                           : curve == LRG_SKELETON2D_CURVE_BEZIER ? "bezier" : "linear");
		json_array_add_object_element (keys, k);
	}
	o = json_object_new ();
	json_object_set_string_member (o, "target", target);
	json_object_set_string_member (o, "channel", channel_nicks[channel]);
	json_object_set_array_member (o, "keys", keys);
	return object_node (o);
}

static JsonNode *
playback_result (LrgEditorSkeleton2DEditor *self)
{
	JsonObject *o = json_object_new ();

	json_object_set_string_member (o, "clip", self->clip);
	json_object_set_double_member (o, "time", r4 (self->time));
	json_object_set_boolean_member (o, "playing", self->playing);
	json_object_set_boolean_member (o, "loop", self->loop);
	return object_node (o);
}

static JsonNode *
m_play (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar               *clip = param_string (params, "clip");

	if (self == NULL)
		return NULL;
	if (clip != NULL && !lrg_editor_skeleton2d_editor_set_clip (self, clip))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no clip '%s'", clip);
		return NULL;
	}
	if (param_has (params, "loop"))
		lrg_editor_skeleton2d_editor_set_loop (self, param_bool (params, "loop", TRUE));
	if (param_has (params, "time") || param_has (params, "playing") || clip != NULL)
		lrg_editor_skeleton2d_editor_set_mode (self, LRG_EDITOR_SKELETON2D_MODE_ANIMATE);
	if (param_has (params, "time"))
		lrg_editor_skeleton2d_editor_set_time (self, (gfloat) param_number (params, "time", 0.0));
	if (param_has (params, "playing"))
		lrg_editor_skeleton2d_editor_set_playing (self, param_bool (params, "playing", FALSE));
	return playback_result (self);
}

static JsonNode *
m_mode (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar               *mode = param_string (params, "mode");
	const gchar               *tool = param_string (params, "tool");
	JsonObject                *o, *sel;
	guint                      i;
	gint                       bone, slot;

	if (self == NULL)
		return NULL;
	if (mode != NULL)
	{
		if (!g_str_equal (mode, "setup") && !g_str_equal (mode, "animate"))
		{
			invalid (error, "Unknown mode '%s' (setup or animate)", mode);
			return NULL;
		}
		lrg_editor_skeleton2d_editor_set_mode (self, g_str_equal (mode, "animate") ? LRG_EDITOR_SKELETON2D_MODE_ANIMATE
		                                                                         : LRG_EDITOR_SKELETON2D_MODE_SETUP);
	}
	bone = param_index (self, params, "bone", FALSE, FALSE, error);
	if (bone < -1)
		return NULL;
	if (param_has (params, "bone"))
		lrg_editor_skeleton2d_editor_set_selected_bone (self, bone);
	slot = param_index (self, params, "slot", TRUE, FALSE, error);
	if (slot < -1)
		return NULL;
	if (param_has (params, "slot"))
		lrg_editor_skeleton2d_editor_set_selected_slot (self, slot);
	if (tool != NULL)
	{
		for (i = 0; i < G_N_ELEMENTS (tool_nicks) && !g_str_equal (tool, tool_nicks[i]); i++)
			;
		if (i == G_N_ELEMENTS (tool_nicks))
		{
			invalid (error, "Unknown tool '%s' (select, bone, mesh or weights)", tool);
			return NULL;
		}
		lrg_editor_skeleton2d_editor_set_tool (self, (LrgEditorSkeleton2DTool) i);
	}
	o = json_object_new ();
	sel = json_object_new ();
	json_object_set_string_member (o, "mode", mode_nicks[self->mode]);
	json_object_set_string_member (o, "tool", tool_nicks[self->tool]);
	json_object_set_string_member (sel, "bone", self->sel_bone);
	json_object_set_string_member (sel, "slot", self->sel_slot);
	json_object_set_string_member (sel, "attachment", self->sel_attachment);
	json_object_set_object_member (o, "selected", sel);
	return object_node (o);
}

static JsonNode *
m_pick (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject                *o;
	gint                       bone;

	if (self == NULL)
		return NULL;
	/* pick in setup space: identity root, the shown pose */
	lrg_skeleton2d_reset_pose (self->skel);
	if (self->mode == LRG_EDITOR_SKELETON2D_MODE_ANIMATE && current_clip (self) != NULL)
		lrg_skeleton2d_clip_apply (current_clip (self), self->skel, self->time, 1.0f);
	lrg_skeleton2d_set_root_transform (self->skel, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
	lrg_skeleton2d_update_world (self->skel);
	bone = lrg_skeleton2d_pick_bone (self->skel, (gfloat) param_number (params, "x", 0.0),
	                                 (gfloat) param_number (params, "y", 0.0),
	                                 (gfloat) param_number (params, "radius", 8.0));
	pose (self);
	o = json_object_new ();
	if (bone >= 0)
		json_object_set_string_member (o, "bone", lrg_skeleton2d_get_bone_name (self->skel, bone));
	else
		json_object_set_null_member (o, "bone");
	return object_node (o);
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSkeleton2DEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)          err = NULL;
	JsonObject                *o;

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
lrg_editor_skeleton2d_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "skel2d.create", m_create },
		{ "skel2d.open", m_open },
		{ "skel2d.get", m_get },
		{ "skel2d.add_bone", m_add_bone },
		{ "skel2d.set_bone", m_set_bone },
		{ "skel2d.remove_bone", m_remove_bone },
		{ "skel2d.add_slot", m_add_slot },
		{ "skel2d.set_slot", m_set_slot },
		{ "skel2d.remove_slot", m_remove_slot },
		{ "skel2d.draw_order", m_draw_order },
		{ "skel2d.attach", m_attach },
		{ "skel2d.detach", m_detach },
		{ "skel2d.mesh", m_mesh },
		{ "skel2d.auto_weights", m_auto_weights },
		{ "skel2d.paint_weights", m_paint_weights },
		{ "skel2d.skin", m_skin },
		{ "skel2d.clip", m_clip },
		{ "skel2d.key", m_key },
		{ "skel2d.play", m_play },
		{ "skel2d.mode", m_mode },
		{ "skel2d.pick", m_pick },
		{ "skel2d.save", m_save }
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
