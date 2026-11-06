/* lrg-file-browser.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFileBrowser: browse folders and pick files to open or save.
 *
 * The browser is a container that composes a few real widgets (an
 * LrgBreadcrumb for the path, an LrgTextInput to type a path, an
 * LrgTextInput for the file name in SAVE mode and an LrgComboBox for
 * the filters) and draws everything else itself:
 *
 *   +------------------------------------------------------------+
 *   | < > ^  [ breadcrumb / path field ...........]  edit eye  r  |  toolbar
 *   +------------+-----------------------------------------------+
 *   | Places     | Name                    Size     Modified      |  header
 *   |  Home      | [] folder                                       |
 *   |  Documents | [] file.txt             1.2 kB   12 Mar          |  rows
 *   |  ...       | ...                                             |
 *   +------------+-----------------------------------------------+
 *   | Name [ name field ................ ]       [ filter combo ] |  footer
 *   +------------------------------------------------------------+
 *
 * Self-drawn areas are backed by plain, invisible LrgWidget "zones"
 * (toolbar buttons, the places sidebar, the list, the two scrollbar
 * hit zones). The canvas hit-tests, hovers and presses them like any
 * widget, so their hover / press springs drive the toolbar button
 * looks for free, and their events bubble to the browser, which
 * recognises the zone as the event target. Zones are laid out in
 * left-to-right coordinates and mirrored by the container machinery
 * in right-to-left; inside the list and sidebar zones the browser
 * mirrors its own drawing and hit-testing.
 *
 * Folders are enumerated asynchronously with GIO in batches
 * (g_file_enumerate_children_async + g_file_enumerator_next_files_async)
 * under a GCancellable that navigation cancels. A navigation clears the
 * list and fills it batch by batch; a reload (F5, or the GFileMonitor
 * reporting a change) keeps the old list until the new one is complete
 * and carries the selection over by name. The visible list ("view") is
 * the full list with hidden files, the mode and the current filter
 * applied, sorted; only the rows inside the viewport are drawn, so big
 * folders stay cheap.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-file-browser.h"
#include "lrg-breadcrumb.h"
#include "lrg-text-input.h"
#include "lrg-combo-box.h"
#include "lrg-canvas.h"
#include "lrg-scrollbar-private.h"
#include "../paint/lrg-paint-icons.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Files fetched per next_files_async() round trip */
#define LOAD_BATCH (256)

/* Two presses on one row within this time (canvas clock) activate it */
#define DOUBLE_CLICK_TIME (0.4)

/* Type-ahead text is forgotten after this long without typing */
#define TYPEAHEAD_TIMEOUT (1.0)

/* Cursor jumps longer than this many rows snap instead of sliding */
#define SLIDE_MAX_ROWS (8.0f)

/* Default width of the places sidebar */
#define DEFAULT_SIDEBAR_WIDTH (176.0f)

/* Columns: width of the size and modified columns */
#define SIZE_COLUMN_WIDTH (88.0f)
#define DATE_COLUMN_WIDTH (132.0f)

/* Below these list widths the modified / size columns are dropped */
#define DATE_COLUMN_MIN_LIST (420.0f)
#define SIZE_COLUMN_MIN_LIST (280.0f)

/* Width of the filter combo in the footer */
#define FILTER_COMBO_WIDTH (190.0f)

/* Attributes every listing asks for */
#define LIST_ATTRIBUTES \
	G_FILE_ATTRIBUTE_STANDARD_NAME "," \
	G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," \
	G_FILE_ATTRIBUTE_STANDARD_TYPE "," \
	G_FILE_ATTRIBUTE_STANDARD_SIZE "," \
	G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN "," \
	G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP "," \
	G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK "," \
	G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE "," \
	G_FILE_ATTRIBUTE_TIME_MODIFIED "," \
	G_FILE_ATTRIBUTE_ACCESS_CAN_READ

/* ==========================================================================
 * Enum types
 * ========================================================================== */

GType
lrg_file_browser_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_FILE_BROWSER_MODE_OPEN, "LRG_FILE_BROWSER_MODE_OPEN", "open" },
			{ LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE, "LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE", "open-multiple" },
			{ LRG_FILE_BROWSER_MODE_SAVE, "LRG_FILE_BROWSER_MODE_SAVE", "save" },
			{ LRG_FILE_BROWSER_MODE_SELECT_FOLDER, "LRG_FILE_BROWSER_MODE_SELECT_FOLDER", "select-folder" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgFileBrowserMode"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_file_browser_sort_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_FILE_BROWSER_SORT_NAME, "LRG_FILE_BROWSER_SORT_NAME", "name" },
			{ LRG_FILE_BROWSER_SORT_SIZE, "LRG_FILE_BROWSER_SORT_SIZE", "size" },
			{ LRG_FILE_BROWSER_SORT_MODIFIED, "LRG_FILE_BROWSER_SORT_MODIFIED", "modified" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgFileBrowserSort"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * LrgFileFilter
 * ========================================================================== */

struct _LrgFileFilter
{
	GObject    parent_instance;

	gchar     *name;
	GPtrArray *patterns;    /* gchar *, NULL-terminated */
	GPtrArray *suffixes;    /* gchar *, NULL-terminated, lower case */
	GPtrArray *mime_types;  /* gchar *, NULL-terminated */
	GPtrArray *specs;       /* GPatternSpec * of the case-folded patterns */
};

G_DEFINE_FINAL_TYPE (LrgFileFilter, lrg_file_filter, G_TYPE_OBJECT)

enum
{
	FILTER_PROP_0,
	FILTER_PROP_NAME,
	FILTER_PROP_PATTERNS,
	FILTER_PROP_SUFFIXES,
	FILTER_PROP_MIME_TYPES,
	FILTER_N_PROPS
};

static GParamSpec *filter_properties[FILTER_N_PROPS];

/* strv_array_new: an empty, NULL-terminated string array */
static GPtrArray *
strv_array_new (void)
{
	GPtrArray *a = g_ptr_array_new_null_terminated (2, g_free, TRUE);

	return a;
}

/* filter_add_pattern_internal: store a pattern and its case-folded matcher */
static void
filter_add_pattern_internal (LrgFileFilter *self,
                             const gchar   *pattern)
{
	g_autofree gchar *folded = g_utf8_casefold (pattern, -1);

	g_ptr_array_add (self->patterns, g_strdup (pattern));
	g_ptr_array_add (self->specs, g_pattern_spec_new (folded));
}

static void
lrg_file_filter_finalize (GObject *object)
{
	LrgFileFilter *self = LRG_FILE_FILTER (object);

	g_free (self->name);
	g_ptr_array_unref (self->patterns);
	g_ptr_array_unref (self->suffixes);
	g_ptr_array_unref (self->mime_types);
	g_ptr_array_unref (self->specs);

	G_OBJECT_CLASS (lrg_file_filter_parent_class)->finalize (object);
}

static void
lrg_file_filter_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgFileFilter *self = LRG_FILE_FILTER (object);

	switch (prop_id)
	{
	case FILTER_PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case FILTER_PROP_PATTERNS:
		g_value_set_boxed (value, self->patterns->pdata);
		break;
	case FILTER_PROP_SUFFIXES:
		g_value_set_boxed (value, self->suffixes->pdata);
		break;
	case FILTER_PROP_MIME_TYPES:
		g_value_set_boxed (value, self->mime_types->pdata);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_file_filter_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgFileFilter       *self = LRG_FILE_FILTER (object);
	const gchar * const *strv;
	guint                i;

	switch (prop_id)
	{
	case FILTER_PROP_NAME:
		lrg_file_filter_set_name (self, g_value_get_string (value));
		break;

	case FILTER_PROP_PATTERNS:
		/* Setting a list replaces the previous one */
		g_ptr_array_set_size (self->patterns, 0);
		g_ptr_array_set_size (self->specs, 0);
		strv = g_value_get_boxed (value);
		for (i = 0; strv != NULL && strv[i] != NULL; i++)
			filter_add_pattern_internal (self, strv[i]);
		g_object_notify_by_pspec (object, pspec);
		break;

	case FILTER_PROP_SUFFIXES:
		g_ptr_array_set_size (self->suffixes, 0);
		strv = g_value_get_boxed (value);
		for (i = 0; strv != NULL && strv[i] != NULL; i++)
			g_ptr_array_add (self->suffixes, g_utf8_casefold (strv[i], -1));
		g_object_notify_by_pspec (object, pspec);
		break;

	case FILTER_PROP_MIME_TYPES:
		g_ptr_array_set_size (self->mime_types, 0);
		strv = g_value_get_boxed (value);
		for (i = 0; strv != NULL && strv[i] != NULL; i++)
			g_ptr_array_add (self->mime_types, g_strdup (strv[i]));
		g_object_notify_by_pspec (object, pspec);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_file_filter_class_init (LrgFileFilterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_file_filter_finalize;
	object_class->get_property = lrg_file_filter_get_property;
	object_class->set_property = lrg_file_filter_set_property;

	/**
	 * LrgFileFilter:name:
	 *
	 * The name shown in the browser's filter list.
	 */
	filter_properties[FILTER_PROP_NAME] =
		g_param_spec_string ("name", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileFilter:patterns:
	 *
	 * Glob patterns matched (ignoring case) against file names. Setting
	 * the property replaces every pattern.
	 */
	filter_properties[FILTER_PROP_PATTERNS] =
		g_param_spec_boxed ("patterns", NULL, NULL, G_TYPE_STRV,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileFilter:suffixes:
	 *
	 * File extensions without the dot, matched ignoring case. Setting
	 * the property replaces every suffix.
	 */
	filter_properties[FILTER_PROP_SUFFIXES] =
		g_param_spec_boxed ("suffixes", NULL, NULL, G_TYPE_STRV,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileFilter:mime-types:
	 *
	 * MIME types ("image/png") or families ("image/\*"). Setting the
	 * property replaces every MIME type.
	 */
	filter_properties[FILTER_PROP_MIME_TYPES] =
		g_param_spec_boxed ("mime-types", NULL, NULL, G_TYPE_STRV,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, FILTER_N_PROPS, filter_properties);
}

static void
lrg_file_filter_init (LrgFileFilter *self)
{
	self->patterns = strv_array_new ();
	self->suffixes = strv_array_new ();
	self->mime_types = strv_array_new ();
	self->specs = g_ptr_array_new_with_free_func ((GDestroyNotify) g_pattern_spec_free);
}

LrgFileFilter *
lrg_file_filter_new (const gchar *name)
{
	return g_object_new (LRG_TYPE_FILE_FILTER, "name", name, NULL);
}

const gchar *
lrg_file_filter_get_name (LrgFileFilter *self)
{
	g_return_val_if_fail (LRG_IS_FILE_FILTER (self), NULL);

	return self->name;
}

void
lrg_file_filter_set_name (LrgFileFilter *self,
                          const gchar   *name)
{
	g_return_if_fail (LRG_IS_FILE_FILTER (self));

	if (g_set_str (&self->name, name))
		g_object_notify_by_pspec (G_OBJECT (self), filter_properties[FILTER_PROP_NAME]);
}

void
lrg_file_filter_add_pattern (LrgFileFilter *self,
                             const gchar   *pattern)
{
	g_return_if_fail (LRG_IS_FILE_FILTER (self));
	g_return_if_fail (pattern != NULL);

	filter_add_pattern_internal (self, pattern);
	g_object_notify_by_pspec (G_OBJECT (self), filter_properties[FILTER_PROP_PATTERNS]);
}

void
lrg_file_filter_add_suffix (LrgFileFilter *self,
                            const gchar   *suffix)
{
	g_return_if_fail (LRG_IS_FILE_FILTER (self));
	g_return_if_fail (suffix != NULL);

	/* Tolerate a leading dot ("." + "png") */
	if (suffix[0] == '.')
		suffix++;
	g_ptr_array_add (self->suffixes, g_utf8_casefold (suffix, -1));
	g_object_notify_by_pspec (G_OBJECT (self), filter_properties[FILTER_PROP_SUFFIXES]);
}

void
lrg_file_filter_add_mime_type (LrgFileFilter *self,
                               const gchar   *mime_type)
{
	g_return_if_fail (LRG_IS_FILE_FILTER (self));
	g_return_if_fail (mime_type != NULL);

	g_ptr_array_add (self->mime_types, g_strdup (mime_type));
	g_object_notify_by_pspec (G_OBJECT (self), filter_properties[FILTER_PROP_MIME_TYPES]);
}

const gchar * const *
lrg_file_filter_get_patterns (LrgFileFilter *self)
{
	g_return_val_if_fail (LRG_IS_FILE_FILTER (self), NULL);

	return (const gchar * const *) self->patterns->pdata;
}

const gchar * const *
lrg_file_filter_get_suffixes (LrgFileFilter *self)
{
	g_return_val_if_fail (LRG_IS_FILE_FILTER (self), NULL);

	return (const gchar * const *) self->suffixes->pdata;
}

const gchar * const *
lrg_file_filter_get_mime_types (LrgFileFilter *self)
{
	g_return_val_if_fail (LRG_IS_FILE_FILTER (self), NULL);

	return (const gchar * const *) self->mime_types->pdata;
}

/*
 * mime_matches:
 *
 * Does @content_type satisfy the MIME rule @mime? A family rule
 * ("image/\*") compares the MIME type prefix; an exact rule asks GIO,
 * so subclasses ("text/x-csrc" is a "text/plain") match too.
 */
static gboolean
mime_matches (const gchar *content_type,
              const gchar *mime)
{
	gsize len = strlen (mime);

	if (len >= 2 && mime[len - 1] == '*' && mime[len - 2] == '/')
	{
		g_autofree gchar *ct_mime = g_content_type_get_mime_type (content_type);

		return ct_mime != NULL && g_ascii_strncasecmp (ct_mime, mime, len - 1) == 0;
	}
	else
	{
		g_autofree gchar *wanted = g_content_type_from_mime_type (mime);

		return wanted != NULL && g_content_type_is_a (content_type, wanted);
	}
}

gboolean
lrg_file_filter_match_name (LrgFileFilter *self,
                            const gchar   *name,
                            const gchar   *content_type)
{
	g_autofree gchar *display = NULL;
	g_autofree gchar *folded = NULL;
	g_autofree gchar *guessed = NULL;
	guint             i;

	g_return_val_if_fail (LRG_IS_FILE_FILTER (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	/* No rules at all: an "All files" filter */
	if (self->specs->len == 0 && self->suffixes->len == 0 && self->mime_types->len == 0)
		return TRUE;

	/* 1. Name rules, on the case-folded UTF-8 name */
	display = g_filename_display_name (name);
	folded = g_utf8_casefold (display, -1);
	for (i = 0; i < self->specs->len; i++)
	{
		if (g_pattern_spec_match_string (g_ptr_array_index (self->specs, i), folded))
			return TRUE;
	}
	for (i = 0; i < self->suffixes->len; i++)
	{
		const gchar *suffix = g_ptr_array_index (self->suffixes, i);
		gsize        sl = strlen (suffix);
		gsize        fl = strlen (folded);

		if (fl > sl && folded[fl - sl - 1] == '.' && strcmp (folded + fl - sl, suffix) == 0)
			return TRUE;
	}

	/* 2. MIME rules, guessing the type from the name when not given */
	if (self->mime_types->len == 0)
		return FALSE;
	if (content_type == NULL)
	{
		guessed = g_content_type_guess (name, NULL, 0, NULL);
		content_type = guessed;
	}
	if (content_type == NULL)
		return FALSE;
	for (i = 0; i < self->mime_types->len; i++)
	{
		if (mime_matches (content_type, g_ptr_array_index (self->mime_types, i)))
			return TRUE;
	}
	return FALSE;
}

gboolean
lrg_file_filter_match (LrgFileFilter *self,
                       GFileInfo     *info)
{
	const gchar *ct = NULL;

	g_return_val_if_fail (LRG_IS_FILE_FILTER (self), FALSE);
	g_return_val_if_fail (G_IS_FILE_INFO (info), FALSE);

	if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE))
		ct = g_file_info_get_content_type (info);
	else if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE))
		ct = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);

	return lrg_file_filter_match_name (self, g_file_info_get_name (info), ct);
}

gchar *
lrg_file_filter_get_default_extension (LrgFileFilter *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_FILE_FILTER (self), NULL);

	if (self->suffixes->len > 0)
		return g_strdup (g_ptr_array_index (self->suffixes, 0));

	/* "*.ext" with no other wildcard */
	for (i = 0; i < self->patterns->len; i++)
	{
		const gchar *p = g_ptr_array_index (self->patterns, i);

		if (p[0] == '*' && p[1] == '.' && p[2] != '\0' && strpbrk (p + 2, "*?[") == NULL)
			return g_strdup (p + 2);
	}
	return NULL;
}

/* ==========================================================================
 * Browser types
 * ========================================================================== */

/* One file or folder of the current location */
typedef struct
{
	GFileInfo     *info;       /* owned */
	gchar         *name;       /* on-disk name */
	gchar         *display;    /* UTF-8 name shown */
	gchar         *fold;       /* case-folded display name, for type-ahead */
	gchar         *sort_key;   /* collation key, numbers in natural order */
	gchar         *size_text;  /* "" for folders */
	gchar         *date_text;
	guint64        size;
	gint64         mtime;
	LrgIcon        icon;
	LrgThemeColor  tint;
	gboolean       is_dir;
	gboolean       hidden;
	gboolean       unreadable;
	gboolean       selected;
} Entry;

/* Sidebar sections, in display order */
typedef enum
{
	SECTION_APP,
	SECTION_SYSTEM,
	SECTION_BOOKMARKS,
	SECTION_DEVICES,
	N_SECTIONS
} PlaceSection;

typedef struct
{
	gchar        *label;
	GFile        *location;
	LrgIcon       icon;
	PlaceSection  section;
} Place;

/* One line of the sidebar: a section heading or a place */
typedef struct
{
	gint    place;  /* index into places, or -1 for a heading */
	gint    section;
	gfloat  y;      /* top, relative to the sidebar content */
	gfloat  h;
} SideRow;

/* Toolbar buttons */
enum
{
	NAV_BACK,
	NAV_FORWARD,
	NAV_UP,
	NAV_EDIT,
	NAV_HIDDEN,
	NAV_RELOAD,
	NAV_COUNT
};

/* An enumeration in flight */
typedef struct
{
	GWeakRef      browser;
	GCancellable *cancellable;
	GFile        *location;
	GFileEnumerator *enumerator;
	GPtrArray    *fresh;       /* Entry *; reloads only (NULL: fill the browser directly) */
	GDateTime    *now;
	gboolean      first_batch;
} LoadOp;

/* Layout of the browser, in left-to-right coordinates relative to it */
typedef struct
{
	gfloat pad;
	gfloat gap;
	gfloat bar_h;         /* toolbar / footer height */
	gfloat nav[NAV_COUNT];
	gfloat crumb_x;
	gfloat crumb_w;
	gfloat body_y;
	gfloat body_h;
	gfloat side_w;        /* 0 when hidden */
	gfloat list_x;
	gfloat list_w;
	gfloat foot_y;
	gfloat foot_h;        /* 0 when there is no footer */
	gfloat label_w;       /* "Name" label in SAVE mode */
} Geom;

struct _LrgFileBrowser
{
	LrgContainer        parent_instance;

	/* Settings */
	LrgFileBrowserMode  mode;
	GFile              *location;
	gboolean            show_hidden;
	gboolean            folders_first;
	LrgFileBrowserSort  sort_column;
	gboolean            sort_ascending;
	gboolean            show_sidebar;
	gboolean            show_system_places;
	gfloat              sidebar_width;
	gfloat              row_height;     /* 0 = theme ROW_HEIGHT */
	GListStore         *filters;        /* LrgFileFilter */
	LrgFileFilter      *filter;         /* current, nullable */

	/* Child widgets (owned by the container) */
	LrgWidget          *nav[NAV_COUNT];
	LrgWidget          *places_zone;
	LrgWidget          *list_zone;
	LrgBreadcrumb      *crumb;
	LrgTextInput       *path_entry;
	LrgTextInput       *name_entry;
	LrgComboBox        *filter_combo;
	gboolean            editing;
	gboolean            syncing;        /* a child is being updated from code */

	/* History */
	GPtrArray          *back;           /* GFile */
	GPtrArray          *forward;        /* GFile */

	/* Listing */
	GPtrArray          *all;            /* Entry *, owned */
	GPtrArray          *view;           /* Entry *, visible and sorted */
	gint                cursor;         /* view index or -1 */
	gint                anchor;
	gboolean            loading;
	GError             *error;
	GCancellable       *load_cancel;
	GHashTable         *pending;        /* names to select when they show up */
	gchar              *pending_cursor;
	GFileMonitor       *monitor;
	guint               reload_idle;

	/* Places */
	GPtrArray          *places;         /* Place *, owned, sidebar order */
	GArray             *side_rows;      /* SideRow */
	gfloat              side_content;
	GVolumeMonitor     *volumes;

	/* Interaction */
	gint                hover_row;
	gint                press_header;   /* LrgFileBrowserSort or -1 */
	gint                hover_header;
	gint                hover_place;
	gint                press_place;
	gint                press_nav;
	gfloat              pointer_x;
	gfloat              pointer_y;
	gboolean            pointer_in;
	gdouble             clock;
	gboolean            click_armed;
	gdouble             last_click_time;
	gint                last_click_row;
	GString            *typeahead;
	gdouble             typeahead_time;

	/* Motion */
	LrgScrollAxis       ay;             /* list rows */
	LrgScrollAxis       py;             /* sidebar */
	LrgScrollFx         fx;
	LrgSpring           hover_y;        /* hover pill position (rows) */
	LrgSpring           hover_a;        /* hover pill opacity */
	LrgSpring           sel_y;          /* cursor selection slide */
	LrgSpring           place_y;
	LrgSpring           place_a;
	LrgSpring           content;        /* list fade-in after navigation */
	gboolean            stepped;
	gboolean            in_layout;
};

G_DEFINE_FINAL_TYPE (LrgFileBrowser, lrg_file_browser, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_MODE,
	PROP_LOCATION,
	PROP_SHOW_HIDDEN,
	PROP_FOLDERS_FIRST,
	PROP_SORT_COLUMN,
	PROP_SORT_ASCENDING,
	PROP_FILTER,
	PROP_FILTERS,
	PROP_CURRENT_NAME,
	PROP_SHOW_SIDEBAR,
	PROP_SHOW_SYSTEM_PLACES,
	PROP_SIDEBAR_WIDTH,
	PROP_ROW_HEIGHT,
	PROP_EDITING_LOCATION,
	PROP_LOADING,
	N_PROPS
};

enum
{
	SIGNAL_FILE_ACTIVATED,
	SIGNAL_SELECTION_CHANGED,
	SIGNAL_LOCATION_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void navigate (LrgFileBrowser *self, GFile *location, gint how);
static gint row_at (LrgFileBrowser *self, gfloat x, gfloat y);
static void load_start (LrgFileBrowser *self, gboolean reload);
static void rebuild_view (LrgFileBrowser *self);
static void refresh_places (LrgFileBrowser *self);

/* How navigate() treats the history */
enum
{
	HOW_PUSH,
	HOW_BACK,
	HOW_FORWARD
};

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

#define entry_at(self, i) ((Entry *) g_ptr_array_index ((self)->view, (i)))
#define place_at(self, i) ((Place *) g_ptr_array_index ((self)->places, (i)))

static LrgTheme *
theme_of (LrgFileBrowser *self)
{
	return lrg_widget_get_theme (LRG_WIDGET (self));
}

static gfloat
metric (LrgFileBrowser *self,
        LrgThemeMetric  m)
{
	return lrg_theme_get_metric (theme_of (self), m);
}

static gfloat
row_h (LrgFileBrowser *self)
{
	gfloat h = self->row_height;

	if (h <= 0.0f)
		h = metric (self, LRG_THEME_METRIC_ROW_HEIGHT);
	return MAX (h, 12.0f);
}

/* header_h: the column header strip */
static gfloat
header_h (LrgFileBrowser *self)
{
	return row_h (self);
}

static gboolean
smooth (LrgFileBrowser *self)
{
	return lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE;
}

static gboolean
rtl (LrgFileBrowser *self)
{
	return lrg_widget_is_rtl (LRG_WIDGET (self));
}

static guint
n_view (LrgFileBrowser *self)
{
	return self->view->len;
}

/* entry_file: the GFile of an entry in the current location */
static GFile *
entry_file (LrgFileBrowser *self,
            const Entry    *e)
{
	return g_file_get_child (self->location, e->name);
}

static void
entry_free (gpointer data)
{
	Entry *e = data;

	g_clear_object (&e->info);
	g_free (e->name);
	g_free (e->display);
	g_free (e->fold);
	g_free (e->sort_key);
	g_free (e->size_text);
	g_free (e->date_text);
	g_free (e);
}

static void
place_free (gpointer data)
{
	Place *p = data;

	g_free (p->label);
	g_clear_object (&p->location);
	g_free (p);
}

/*
 * kind_for:
 *
 * Icon and tint for a content type: folders in the accent colour,
 * pictures, media, archives, programs and text each get their own
 * shape, everything else a plain page.
 */
static LrgIcon
kind_for (const gchar   *ct,
          gboolean       is_dir,
          gboolean       unreadable,
          LrgThemeColor *tint)
{
	*tint = LRG_THEME_COLOR_TEXT_MUTED;
	if (is_dir)
	{
		*tint = unreadable ? LRG_THEME_COLOR_TEXT_MUTED : LRG_THEME_COLOR_ACCENT;
		return unreadable ? LRG_ICON_LOCK : LRG_ICON_FOLDER;
	}
	if (ct == NULL)
		return LRG_ICON_FILE;
	if (g_str_has_prefix (ct, "image/"))
	{
		*tint = LRG_THEME_COLOR_LINK;
		return LRG_ICON_EYE;
	}
	if (g_str_has_prefix (ct, "audio/") || g_str_has_prefix (ct, "video/"))
	{
		*tint = LRG_THEME_COLOR_SUCCESS;
		return LRG_ICON_PLAY;
	}
	if (g_content_type_is_a (ct, "application/zip") || g_content_type_is_a (ct, "application/x-tar") ||
	    g_content_type_is_a (ct, "application/gzip") || g_content_type_is_a (ct, "application/x-7z-compressed") ||
	    g_content_type_is_a (ct, "application/x-compressed-tar") || g_content_type_is_a (ct, "application/x-xz"))
	{
		*tint = LRG_THEME_COLOR_WARNING;
		return LRG_ICON_DOWNLOAD;
	}
	if (g_content_type_can_be_executable (ct) && !g_content_type_is_a (ct, "text/plain"))
	{
		*tint = LRG_THEME_COLOR_DANGER;
		return LRG_ICON_GEAR;
	}
	if (g_content_type_is_a (ct, "text/plain"))
		return LRG_ICON_EDIT;
	return LRG_ICON_FILE;
}

/*
 * format_date:
 *
 * Short modification date: the time for today, day and month for this
 * year, day month year otherwise.
 */
static gchar *
format_date (gint64     mtime,
             GDateTime *now)
{
	g_autoptr(GDateTime) dt = NULL;

	if (mtime <= 0 || now == NULL)
		return g_strdup ("");
	dt = g_date_time_new_from_unix_local (mtime);
	if (dt == NULL)
		return g_strdup ("");

	if (g_date_time_get_year (dt) == g_date_time_get_year (now) &&
	    g_date_time_get_day_of_year (dt) == g_date_time_get_day_of_year (now))
		return g_date_time_format (dt, "%H:%M");
	if (g_date_time_get_year (dt) == g_date_time_get_year (now))
		return g_date_time_format (dt, "%-d %b");
	return g_date_time_format (dt, "%-d %b %Y");
}

/* entry_new: everything the list needs from one GFileInfo, computed once */
static Entry *
entry_new (GFileInfo *info,
           GDateTime *now)
{
	Entry       *e = g_new0 (Entry, 1);
	const gchar *ct = NULL;
	const gchar *display;
	GDateTime   *mt;

	e->info = g_object_ref (info);
	e->name = g_strdup (g_file_info_get_name (info));
	display = g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME)
	          ? g_file_info_get_display_name (info) : NULL;
	e->display = display != NULL ? g_strdup (display) : g_filename_display_name (e->name);
	e->fold = g_utf8_casefold (e->display, -1);
	e->sort_key = g_utf8_collate_key_for_filename (e->fold, -1);
	e->is_dir = g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY;
	e->hidden = (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN) &&
	             g_file_info_get_is_hidden (info)) ||
	            (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP) &&
	             g_file_info_get_is_backup (info)) ||
	            e->name[0] == '.';
	e->unreadable = g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ) &&
	                !g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ);
	e->size = g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_SIZE)
	          ? (guint64) g_file_info_get_size (info) : 0;
	mt = g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_TIME_MODIFIED)
	     ? g_file_info_get_modification_date_time (info) : NULL;
	e->mtime = mt != NULL ? g_date_time_to_unix (mt) : 0;
	g_clear_pointer (&mt, g_date_time_unref);

	if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE))
		ct = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
	e->icon = kind_for (ct, e->is_dir, e->unreadable, &e->tint);
	e->size_text = e->is_dir ? g_strdup ("") : g_format_size (e->size);
	e->date_text = format_date (e->mtime, now);
	return e;
}

/* view_index_of: position of @e in the view, or -1 */
static gint
view_index_of (LrgFileBrowser *self,
               const Entry    *e)
{
	guint i;

	if (e == NULL)
		return -1;
	for (i = 0; i < self->view->len; i++)
	{
		if (entry_at (self, i) == e)
			return (gint) i;
	}
	return -1;
}

/* view_index_of_name: position of the entry called @name, or -1 */
static gint
view_index_of_name (LrgFileBrowser *self,
                    const gchar    *name)
{
	guint i;

	if (name == NULL)
		return -1;
	for (i = 0; i < self->view->len; i++)
	{
		if (strcmp (entry_at (self, i)->name, name) == 0)
			return (gint) i;
	}
	return -1;
}

/* ==========================================================================
 * Geometry
 * ========================================================================== */

/* has_footer: SAVE mode always (name field), other modes when filtering */
static gboolean
has_footer (LrgFileBrowser *self)
{
	return self->mode == LRG_FILE_BROWSER_MODE_SAVE ||
	       g_list_model_get_n_items (G_LIST_MODEL (self->filters)) > 0;
}

static gboolean
sidebar_shown (LrgFileBrowser *self)
{
	return self->show_sidebar && self->places->len > 0;
}

/*
 * geometry:
 *
 * The browser's areas in left-to-right coordinates relative to the
 * browser. Zones and child widgets are allocated from this; in
 * right-to-left the container mirrors them.
 */
static void
geometry (LrgFileBrowser *self,
          Geom           *g)
{
	gfloat w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat x;
	gfloat size = metric (self, LRG_THEME_METRIC_FONT_SIZE);
	gint   i;

	memset (g, 0, sizeof (Geom));
	g->pad = roundf (metric (self, LRG_THEME_METRIC_PADDING_Y));
	g->gap = roundf (metric (self, LRG_THEME_METRIC_SPACING) * 0.5f);
	g->bar_h = roundf (metric (self, LRG_THEME_METRIC_CONTROL_HEIGHT));

	/* Toolbar: three buttons on the start side, three on the end side,
	 * the breadcrumb / path field in between */
	x = g->pad;
	for (i = NAV_BACK; i <= NAV_UP; i++)
	{
		g->nav[i] = x;
		x += g->bar_h + (i == NAV_UP ? g->gap * 2.0f : 2.0f);
	}
	g->crumb_x = x;
	x = w - g->pad - g->bar_h;
	for (i = NAV_RELOAD; i >= NAV_EDIT; i--)
	{
		g->nav[i] = x;
		x -= g->bar_h + 2.0f;
	}
	g->crumb_w = MAX (0.0f, x + g->bar_h - g->gap * 2.0f - g->crumb_x);

	/* Footer */
	g->foot_h = has_footer (self) ? g->bar_h : 0.0f;
	g->foot_y = h - g->pad - g->foot_h;
	if (self->mode == LRG_FILE_BROWSER_MODE_SAVE)
	{
		gfloat lw = size * 3.0f;

		lrg_paint_measure_text (lrg_theme_get_ui_font (theme_of (self)), "Name", size, 1.0f, &lw, NULL);
		g->label_w = ceilf (lw) + g->gap * 2.0f;
	}

	/* Body between them */
	g->body_y = g->pad + g->bar_h + g->gap * 2.0f;
	g->body_h = MAX (0.0f, (g->foot_h > 0.0f ? g->foot_y - g->gap * 2.0f : h - g->pad) - g->body_y);
	g->side_w = sidebar_shown (self) ? MIN (self->sidebar_width, MAX (0.0f, (w - g->pad * 2.0f) * 0.4f)) : 0.0f;
	g->list_x = g->pad + g->side_w + (g->side_w > 0.0f ? g->gap * 2.0f : 0.0f);
	g->list_w = MAX (0.0f, w - g->pad - g->list_x);
}

/* zone_rect: the world rectangle of a zone (already mirrored in RTL) */
static void
zone_rect (LrgWidget *zone,
           gfloat    *x,
           gfloat    *y,
           gfloat    *w,
           gfloat    *h)
{
	*x = lrg_widget_get_world_x (zone);
	*y = lrg_widget_get_world_y (zone);
	*w = lrg_widget_get_width (zone);
	*h = lrg_widget_get_height (zone);
}

/*
 * columns:
 *
 * Widths of the name, size and modified columns for a list @lw wide.
 * Narrow lists drop the modified column, then the size column.
 */
static void
columns (gfloat  lw,
         gfloat *name_w,
         gfloat *size_w,
         gfloat *date_w)
{
	*date_w = lw >= DATE_COLUMN_MIN_LIST ? DATE_COLUMN_WIDTH : 0.0f;
	*size_w = lw >= SIZE_COLUMN_MIN_LIST ? SIZE_COLUMN_WIDTH : 0.0f;
	*name_w = MAX (0.0f, lw - *size_w - *date_w);
}

/* span_x: world left edge of a span at @rel inside a zone, mirrored in RTL */
static gfloat
span_x (LrgFileBrowser *self,
        gfloat          zx,
        gfloat          zw,
        gfloat          rel,
        gfloat          w)
{
	return rtl (self) ? zx + zw - rel - w : zx + rel;
}

/* rel_x: a world x as a logical offset inside a zone */
static gfloat
rel_x (LrgFileBrowser *self,
       gfloat          zx,
       gfloat          zw,
       gfloat          x)
{
	return rtl (self) ? zx + zw - x : x - zx;
}

/* list_body: world rectangle of the rows area (under the header) */
static void
list_body (LrgFileBrowser *self,
           gfloat         *bx,
           gfloat         *by,
           gfloat         *bw,
           gfloat         *bh)
{
	gfloat lx;
	gfloat ly;
	gfloat lw;
	gfloat lh;

	zone_rect (self->list_zone, &lx, &ly, &lw, &lh);
	*bx = lx;
	*by = ly + header_h (self);
	*bw = lw;
	*bh = MAX (0.0f, lh - header_h (self));
}

static guint
rows_per_page (LrgFileBrowser *self)
{
	gfloat bx;
	gfloat by;
	gfloat bw;
	gfloat bh;

	list_body (self, &bx, &by, &bw, &bh);
	return MAX (1u, (guint) floorf (bh / row_h (self)));
}

/* update_extent: scroll ranges of the list and the sidebar */
static void
update_extent (LrgFileBrowser *self)
{
	gfloat bx;
	gfloat by;
	gfloat bw;
	gfloat bh;

	list_body (self, &bx, &by, &bw, &bh);
	lrg_scroll_axis_set_extent (&self->ay, (gfloat) n_view (self) * row_h (self), bh);
	lrg_scroll_axis_set_extent (&self->py, self->side_content, lrg_widget_get_height (self->places_zone));
}

/* ==========================================================================
 * Sorting and filtering
 * ========================================================================== */

static gint
compare_entries (gconstpointer a,
                 gconstpointer b,
                 gpointer      user_data)
{
	LrgFileBrowser *self = user_data;
	const Entry    *ea = *(const Entry * const *) a;
	const Entry    *eb = *(const Entry * const *) b;
	gint            r = 0;

	/* Folders on top whatever the direction */
	if (self->folders_first && ea->is_dir != eb->is_dir)
		return ea->is_dir ? -1 : 1;

	switch (self->sort_column)
	{
	case LRG_FILE_BROWSER_SORT_SIZE:
		r = ea->size < eb->size ? -1 : (ea->size > eb->size ? 1 : 0);
		break;
	case LRG_FILE_BROWSER_SORT_MODIFIED:
		r = ea->mtime < eb->mtime ? -1 : (ea->mtime > eb->mtime ? 1 : 0);
		break;
	case LRG_FILE_BROWSER_SORT_NAME:
	default:
		break;
	}
	if (r == 0)
		r = strcmp (ea->sort_key, eb->sort_key);
	if (r == 0)
		r = strcmp (ea->name, eb->name);
	return self->sort_ascending ? r : -r;
}

/* entry_visible: hidden files, the mode and the filter */
static gboolean
entry_visible (LrgFileBrowser *self,
               const Entry    *e)
{
	if (e->hidden && !self->show_hidden)
		return FALSE;
	if (e->is_dir)
		return TRUE;
	if (self->mode == LRG_FILE_BROWSER_MODE_SELECT_FOLDER)
		return FALSE;
	return self->filter == NULL || lrg_file_filter_match (self->filter, e->info);
}

static void
emit_selection_changed (LrgFileBrowser *self)
{
	g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

/*
 * rebuild_view:
 *
 * Recomputes the visible, sorted list from the full one. The cursor and
 * anchor follow their entries; selected entries that are no longer
 * visible are unselected (emitting selection-changed).
 */
static void
rebuild_view (LrgFileBrowser *self)
{
	Entry    *cursor_e = self->cursor >= 0 && (guint) self->cursor < n_view (self)
	                     ? entry_at (self, self->cursor) : NULL;
	Entry    *anchor_e = self->anchor >= 0 && (guint) self->anchor < n_view (self)
	                     ? entry_at (self, self->anchor) : NULL;
	gboolean  dropped = FALSE;
	guint     i;

	g_ptr_array_set_size (self->view, 0);
	for (i = 0; i < self->all->len; i++)
	{
		Entry *e = g_ptr_array_index (self->all, i);

		if (entry_visible (self, e))
			g_ptr_array_add (self->view, e);
		else if (e->selected)
		{
			e->selected = FALSE;
			dropped = TRUE;
		}
	}
	g_ptr_array_sort_with_data (self->view, compare_entries, self);

	self->cursor = view_index_of (self, cursor_e);
	self->anchor = view_index_of (self, anchor_e);
	self->click_armed = FALSE;
	update_extent (self);
	self->hover_row = self->pointer_in ? row_at (self, self->pointer_x, self->pointer_y) : -1;
	if (dropped)
		emit_selection_changed (self);
}

/* ==========================================================================
 * Selection and cursor
 * ========================================================================== */

/* sync_name_from_selection: SAVE mode copies a picked file's name */
static void
sync_name_from_selection (LrgFileBrowser *self)
{
	Entry *e;

	if (self->mode != LRG_FILE_BROWSER_MODE_SAVE || self->cursor < 0)
		return;
	e = entry_at (self, self->cursor);
	if (!e->is_dir && e->selected)
		lrg_file_browser_set_current_name (self, e->display);
}

/* clear_selection: returns TRUE if anything was selected */
static gboolean
clear_selection (LrgFileBrowser *self)
{
	gboolean changed = FALSE;
	guint    i;

	for (i = 0; i < self->all->len; i++)
	{
		Entry *e = g_ptr_array_index (self->all, i);

		if (e->selected)
		{
			e->selected = FALSE;
			changed = TRUE;
		}
	}
	return changed;
}

/* scroll_to_row: bring row @i into view */
static void
scroll_to_row (LrgFileBrowser *self,
               gint            i,
               gboolean        animate)
{
	gfloat rh = row_h (self);
	gfloat top = (gfloat) i * rh;
	gfloat view = self->ay.viewport;
	gfloat target = self->ay.pos.target;

	if (i < 0)
		return;
	update_extent (self);
	if (top < target)
		target = top;
	else if (top + rh > target + view)
		target = top + rh - view;
	else
		return;
	lrg_scroll_axis_scroll_to (&self->ay, target, animate && smooth (self));
	lrg_scroll_fx_poke (&self->fx);
}

static void
set_cursor (LrgFileBrowser *self,
            gint            i,
            gboolean        reveal)
{
	self->cursor = i;
	if (reveal)
		scroll_to_row (self, i, TRUE);
}

/* select_only: exactly row @i selected */
static void
select_only (LrgFileBrowser *self,
             gint            i)
{
	gboolean changed = clear_selection (self);

	if (i >= 0 && (guint) i < n_view (self))
	{
		entry_at (self, i)->selected = TRUE;
		changed = TRUE;
	}
	self->anchor = i;
	set_cursor (self, i, TRUE);
	if (changed)
	{
		sync_name_from_selection (self);
		emit_selection_changed (self);
	}
}

/* select_range: rows between the anchor and @i, inclusive */
static void
select_range (LrgFileBrowser *self,
              gint            i)
{
	gint lo = MIN (self->anchor, i);
	gint hi = MAX (self->anchor, i);
	gint k;

	clear_selection (self);
	for (k = MAX (lo, 0); k <= hi && (guint) k < n_view (self); k++)
		entry_at (self, k)->selected = TRUE;
	set_cursor (self, i, TRUE);
	emit_selection_changed (self);
}

/* move_cursor_to: keyboard movement; Shift extends in multiple mode */
static void
move_cursor_to (LrgFileBrowser *self,
                gint            i,
                gboolean        extend)
{
	guint n = n_view (self);

	if (n == 0)
		return;
	i = CLAMP (i, 0, (gint) n - 1);
	if (extend && self->mode == LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE && self->anchor >= 0)
		select_range (self, i);
	else
		select_only (self, i);
}

/*
 * apply_pending:
 *
 * Selects entries that were asked for before they were listed (by
 * lrg_file_browser_select_file(), going up, or a reload keeping the
 * selection), and puts the cursor on the requested one.
 */
static void
apply_pending (LrgFileBrowser *self)
{
	gboolean changed = FALSE;
	guint    i;

	if (g_hash_table_size (self->pending) == 0 && self->pending_cursor == NULL)
		return;

	for (i = 0; i < n_view (self); i++)
	{
		Entry *e = entry_at (self, i);

		if (g_hash_table_remove (self->pending, e->name) && !e->selected)
		{
			e->selected = TRUE;
			changed = TRUE;
		}
		if (self->pending_cursor != NULL && strcmp (self->pending_cursor, e->name) == 0)
		{
			self->anchor = (gint) i;
			set_cursor (self, (gint) i, TRUE);
			g_clear_pointer (&self->pending_cursor, g_free);
		}
	}
	if (changed)
	{
		sync_name_from_selection (self);
		emit_selection_changed (self);
	}
}

/* ==========================================================================
 * Loading
 * ========================================================================== */

static void
load_op_free (LoadOp *op)
{
	g_weak_ref_clear (&op->browser);
	g_clear_object (&op->cancellable);
	g_clear_object (&op->location);
	g_clear_object (&op->enumerator);
	g_clear_pointer (&op->fresh, g_ptr_array_unref);
	g_clear_pointer (&op->now, g_date_time_unref);
	g_free (op);
}

/* set_loading: update the property */
static void
set_loading (LrgFileBrowser *self,
             gboolean        loading)
{
	if (self->loading == loading)
		return;
	self->loading = loading;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOADING]);
}

/*
 * load_finish:
 *
 * The enumeration ended (normally or with @error). A reload swaps its
 * collected entries in, carrying the selection and cursor over by name.
 */
static void
load_finish (LrgFileBrowser *self,
             LoadOp         *op,
             GError         *error)
{
	guint i;

	if (op->fresh != NULL && error == NULL)
	{
		g_autoptr(GHashTable) chosen = g_hash_table_new (g_str_hash, g_str_equal);
		g_autofree gchar     *cursor_name = NULL;
		guint                 before = 0;
		guint                 after = 0;

		/* Carry the selection and cursor over by name, quietly: only a
		 * selected file that disappeared changes the selection */
		for (i = 0; i < self->all->len; i++)
		{
			Entry *e = g_ptr_array_index (self->all, i);

			if (e->selected)
			{
				g_hash_table_add (chosen, e->name);
				before++;
			}
		}
		if (self->cursor >= 0)
			cursor_name = g_strdup (entry_at (self, self->cursor)->name);
		for (i = 0; i < op->fresh->len; i++)
		{
			Entry *e = g_ptr_array_index (op->fresh, i);

			e->selected = g_hash_table_contains (chosen, e->name);
		}
		g_hash_table_remove_all (chosen);  /* keys belong to the old list */

		self->cursor = -1;
		self->anchor = -1;
		g_ptr_array_set_size (self->view, 0);
		g_ptr_array_unref (self->all);
		self->all = g_steal_pointer (&op->fresh);
		rebuild_view (self);
		self->cursor = view_index_of_name (self, cursor_name);
		self->anchor = self->cursor;
		for (i = 0; i < n_view (self); i++)
			after += entry_at (self, i)->selected ? 1u : 0u;
		apply_pending (self);
		if (after != before)
			emit_selection_changed (self);
	}
	else if (error != NULL)
	{
		/* The folder cannot be listed: show the error row */
		if (op->fresh != NULL)
		{
			g_ptr_array_set_size (self->view, 0);
			g_ptr_array_set_size (self->all, 0);
			self->cursor = -1;
			self->anchor = -1;
			update_extent (self);
		}
		g_clear_error (&self->error);
		self->error = g_error_copy (error);
		lrg_spring_set_target (&self->content, 1.0f);
	}

	g_hash_table_remove_all (self->pending);
	g_clear_pointer (&self->pending_cursor, g_free);
	if (op->first_batch)
		lrg_spring_set_target (&self->content, 1.0f);
	g_clear_object (&self->load_cancel);
	set_loading (self, FALSE);
}

static void on_next_files (GObject *source, GAsyncResult *result, gpointer user_data);

/* request_batch: ask the enumerator for the next batch */
static void
request_batch (LoadOp *op)
{
	g_file_enumerator_next_files_async (op->enumerator, LOAD_BATCH, G_PRIORITY_DEFAULT,
	                                    op->cancellable, on_next_files, op);
}

static void
on_next_files (GObject      *source,
               GAsyncResult *result,
               gpointer      user_data)
{
	LoadOp                    *op = user_data;
	g_autoptr(LrgFileBrowser)  self = NULL;
	g_autoptr(GError)          error = NULL;
	GList                     *files;
	GList                     *l;

	files = g_file_enumerator_next_files_finish (G_FILE_ENUMERATOR (source), result, &error);

	/* 1. Cancelled (navigation, dispose) or the browser is gone */
	self = g_weak_ref_get (&op->browser);
	if (self == NULL || g_cancellable_is_cancelled (op->cancellable) || self->load_cancel != op->cancellable)
	{
		g_list_free_full (files, g_object_unref);
		load_op_free (op);
		return;
	}

	if (error != NULL)
	{
		load_finish (self, op, error);
		g_file_enumerator_close_async (op->enumerator, G_PRIORITY_DEFAULT, NULL, NULL, NULL);
		load_op_free (op);
		return;
	}

	/* 2. End of the folder */
	if (files == NULL)
	{
		load_finish (self, op, NULL);
		g_file_enumerator_close_async (op->enumerator, G_PRIORITY_DEFAULT, NULL, NULL, NULL);
		load_op_free (op);
		return;
	}

	/* 3. Add the batch: straight into the list for a navigation (the
	 *    list grows while loading), aside for a reload */
	for (l = files; l != NULL; l = l->next)
	{
		Entry *e = entry_new (G_FILE_INFO (l->data), op->now);

		g_ptr_array_add (op->fresh != NULL ? op->fresh : self->all, e);
	}
	g_list_free_full (files, g_object_unref);

	if (op->fresh == NULL)
	{
		rebuild_view (self);
		apply_pending (self);
		if (op->first_batch)
		{
			/* Rows fade in as the first batch arrives */
			lrg_spring_set_target (&self->content, 1.0f);
			op->first_batch = FALSE;
		}
	}
	request_batch (op);
}

static void
on_enumerated (GObject      *source,
               GAsyncResult *result,
               gpointer      user_data)
{
	LoadOp                    *op = user_data;
	g_autoptr(LrgFileBrowser)  self = NULL;
	g_autoptr(GError)          error = NULL;

	op->enumerator = g_file_enumerate_children_finish (G_FILE (source), result, &error);

	self = g_weak_ref_get (&op->browser);
	if (self == NULL || g_cancellable_is_cancelled (op->cancellable) || self->load_cancel != op->cancellable)
	{
		load_op_free (op);
		return;
	}
	if (op->enumerator == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN_UI, "file browser: cannot list folder: %s", error->message);
		load_finish (self, op, error);
		load_op_free (op);
		return;
	}
	request_batch (op);
}

/*
 * load_start:
 *
 * Starts enumerating the location, cancelling any load in flight. A
 * navigation (@reload FALSE) empties the list first and fades the new
 * rows in; a reload keeps the list until the new one is complete.
 */
static void
load_start (LrgFileBrowser *self,
            gboolean        reload)
{
	LoadOp *op;

	if (self->load_cancel != NULL)
		g_cancellable_cancel (self->load_cancel);
	g_clear_object (&self->load_cancel);
	if (self->location == NULL)
		return;

	op = g_new0 (LoadOp, 1);
	g_weak_ref_init (&op->browser, self);
	op->cancellable = g_cancellable_new ();
	op->location = g_object_ref (self->location);
	op->now = g_date_time_new_now_local ();
	self->load_cancel = g_object_ref (op->cancellable);
	g_clear_error (&self->error);

	if (reload)
	{
		op->fresh = g_ptr_array_new_with_free_func (entry_free);
	}
	else
	{
		gboolean had = clear_selection (self);

		g_ptr_array_set_size (self->view, 0);
		g_ptr_array_set_size (self->all, 0);
		self->cursor = -1;
		self->anchor = -1;
		self->hover_row = -1;
		self->click_armed = FALSE;
		lrg_scroll_axis_scroll_to (&self->ay, 0.0f, FALSE);
		update_extent (self);
		op->first_batch = TRUE;
		if (smooth (self) && self->stepped)
			lrg_spring_snap (&self->content, 0.0f);
		if (had)
			emit_selection_changed (self);
	}

	set_loading (self, TRUE);
	g_file_enumerate_children_async (self->location, LIST_ATTRIBUTES, G_FILE_QUERY_INFO_NONE,
	                                 G_PRIORITY_DEFAULT, op->cancellable, on_enumerated, op);
}

/* ==========================================================================
 * Folder monitor
 * ========================================================================== */

static gboolean
reload_idle_cb (gpointer user_data)
{
	LrgFileBrowser *self = user_data;

	self->reload_idle = 0;
	lrg_file_browser_reload (self);
	return G_SOURCE_REMOVE;
}

static void
on_monitor_changed (GFileMonitor      *monitor,
                    GFile             *file,
                    GFile             *other,
                    GFileMonitorEvent  event,
                    gpointer           user_data)
{
	LrgFileBrowser *self = user_data;

	switch (event)
	{
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
	case G_FILE_MONITOR_EVENT_DELETED:
	case G_FILE_MONITOR_EVENT_CREATED:
	case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
	case G_FILE_MONITOR_EVENT_RENAMED:
	case G_FILE_MONITOR_EVENT_MOVED_IN:
	case G_FILE_MONITOR_EVENT_MOVED_OUT:
		/* Coalesce bursts (a copy fires many events) into one reload */
		if (self->reload_idle == 0)
			self->reload_idle = g_idle_add (reload_idle_cb, self);
		break;
	default:
		break;
	}
}

/* watch_location: a directory monitor on the current location */
static void
watch_location (LrgFileBrowser *self)
{
	if (self->monitor != NULL)
	{
		g_signal_handlers_disconnect_by_data (self->monitor, self);
		g_file_monitor_cancel (self->monitor);
		g_clear_object (&self->monitor);
	}
	if (self->location == NULL)
		return;
	self->monitor = g_file_monitor_directory (self->location, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
	if (self->monitor != NULL)
		g_signal_connect (self->monitor, "changed", G_CALLBACK (on_monitor_changed), self);
}

/* ==========================================================================
 * Navigation
 * ========================================================================== */

/* label_for_folder: breadcrumb label of one folder on the path */
static gchar *
label_for_folder (GFile *folder,
                  GFile *home)
{
	g_autofree gchar *base = NULL;

	if (home != NULL && g_file_equal (folder, home))
		return g_strdup ("Home");
	base = g_file_get_basename (folder);
	if (base == NULL || strcmp (base, G_DIR_SEPARATOR_S) == 0 || base[0] == '\0')
		return g_strdup (G_DIR_SEPARATOR_S);
	return g_filename_display_name (base);
}

/*
 * sync_breadcrumb:
 *
 * One segment per folder from the root (or the home folder, when the
 * location is inside it) down to the location. Each segment carries
 * its GFile.
 */
static void
sync_breadcrumb (LrgFileBrowser *self)
{
	g_autoptr(GFile)  home = g_file_new_for_path (g_get_home_dir ());
	g_autoptr(GPtrArray) chain = g_ptr_array_new_with_free_func (g_object_unref);
	GFile            *f;
	gint              i;

	lrg_breadcrumb_remove_all (self->crumb);
	if (self->location == NULL)
		return;

	/* Walk up to the root, stopping at home */
	f = g_object_ref (self->location);
	while (f != NULL)
	{
		g_ptr_array_add (chain, f);
		if (g_file_equal (f, home))
			break;
		f = g_file_get_parent (f);
	}

	for (i = (gint) chain->len - 1; i >= 0; i--)
	{
		GFile            *folder = g_ptr_array_index (chain, i);
		g_autofree gchar *label = label_for_folder (folder, home);

		lrg_breadcrumb_append_with_data (self->crumb, label, g_object_ref (folder), g_object_unref);
	}
}

/* update_nav_zones: enable the toolbar buttons that can act */
static void
update_nav_zones (LrgFileBrowser *self)
{
	lrg_widget_set_enabled (self->nav[NAV_BACK], self->back->len > 0);
	lrg_widget_set_enabled (self->nav[NAV_FORWARD], self->forward->len > 0);
	lrg_widget_set_enabled (self->nav[NAV_UP], lrg_file_browser_can_go_up (self));
	if (self->show_hidden)
		lrg_widget_set_state_flags (self->nav[NAV_HIDDEN], LRG_WIDGET_STATE_CHECKED);
	else
		lrg_widget_unset_state_flags (self->nav[NAV_HIDDEN], LRG_WIDGET_STATE_CHECKED);
	if (self->editing)
		lrg_widget_set_state_flags (self->nav[NAV_EDIT], LRG_WIDGET_STATE_CHECKED);
	else
		lrg_widget_unset_state_flags (self->nav[NAV_EDIT], LRG_WIDGET_STATE_CHECKED);
}

/*
 * navigate:
 *
 * Shows @location. @how says what happens to the history: a plain
 * navigation pushes the old location on the back stack and clears the
 * forward stack; going back / forward moves it to the other stack.
 */
static void
navigate (LrgFileBrowser *self,
          GFile          *location,
          gint            how)
{
	g_autoptr(GFile) loc = g_object_ref (location);

	if (self->location != NULL && g_file_equal (self->location, loc))
		return;

	if (self->location != NULL)
	{
		switch (how)
		{
		case HOW_BACK:
			g_ptr_array_add (self->forward, g_steal_pointer (&self->location));
			break;
		case HOW_FORWARD:
			g_ptr_array_add (self->back, g_steal_pointer (&self->location));
			break;
		case HOW_PUSH:
		default:
			g_ptr_array_add (self->back, g_steal_pointer (&self->location));
			g_ptr_array_set_size (self->forward, 0);
			break;
		}
	}
	g_clear_object (&self->location);
	self->location = g_steal_pointer (&loc);

	/* Fresh state for the new folder */
	g_hash_table_remove_all (self->pending);
	g_clear_pointer (&self->pending_cursor, g_free);
	g_string_truncate (self->typeahead, 0);
	if (self->editing)
		lrg_file_browser_set_editing_location (self, FALSE);
	sync_breadcrumb (self);
	watch_location (self);
	load_start (self, FALSE);
	update_nav_zones (self);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCATION]);
	g_signal_emit (self, signals[SIGNAL_LOCATION_CHANGED], 0);
}

/* expand_path: "~", absolute and relative paths to a GFile */
static GFile *
expand_path (LrgFileBrowser *self,
             const gchar    *text)
{
	g_autofree gchar *trimmed = g_strstrip (g_strdup (text));

	if (trimmed[0] == '\0')
		return NULL;
	if (trimmed[0] == '~' && (trimmed[1] == '\0' || trimmed[1] == '/'))
	{
		g_autofree gchar *path = g_build_filename (g_get_home_dir (), trimmed + 1, NULL);

		return g_file_new_for_path (path);
	}
	if (g_path_is_absolute (trimmed) || self->location == NULL)
		return g_file_new_for_commandline_arg (trimmed);
	if (strstr (trimmed, "://") != NULL)
		return g_file_new_for_uri (trimmed);
	return g_file_resolve_relative_path (self->location, trimmed);
}

/* activate_row: enter a folder, or report a file */
static void
activate_row (LrgFileBrowser *self,
              gint            i)
{
	Entry           *e;
	g_autoptr(GFile) file = NULL;

	if (i < 0 || (guint) i >= n_view (self))
		return;
	e = entry_at (self, i);
	file = entry_file (self, e);
	if (e->is_dir)
	{
		navigate (self, file, HOW_PUSH);
		return;
	}
	if (self->mode == LRG_FILE_BROWSER_MODE_SAVE)
		lrg_file_browser_set_current_name (self, e->display);
	g_signal_emit (self, signals[SIGNAL_FILE_ACTIVATED], 0, file);
}

/* ==========================================================================
 * Places
 * ========================================================================== */

static void
add_place_internal (LrgFileBrowser *self,
                    const gchar    *label,
                    GFile          *location,
                    LrgIcon         icon,
                    PlaceSection    section)
{
	Place *p = g_new0 (Place, 1);

	p->label = g_strdup (label);
	p->location = g_object_ref (location);
	p->icon = icon;
	p->section = section;
	g_ptr_array_add (self->places, p);
}

/* add_special_dir: an XDG user folder, if it exists and is not home */
static void
add_special_dir (LrgFileBrowser *self,
                 GUserDirectory  dir,
                 const gchar    *label,
                 LrgIcon         icon)
{
	const gchar      *path = g_get_user_special_dir (dir);
	g_autoptr(GFile)  file = NULL;
	g_autoptr(GFile)  home = NULL;

	if (path == NULL || !g_file_test (path, G_FILE_TEST_IS_DIR))
		return;
	file = g_file_new_for_path (path);
	home = g_file_new_for_path (g_get_home_dir ());
	if (g_file_equal (file, home))
		return;
	add_place_internal (self, label, file, icon, SECTION_SYSTEM);
}

/*
 * add_bookmarks:
 *
 * GTK bookmarks: one "URI [label]" per line in
 * ~/.config/gtk-3.0/bookmarks. Only local folders that exist are kept.
 */
static void
add_bookmarks (LrgFileBrowser *self)
{
	g_autofree gchar *path = g_build_filename (g_get_user_config_dir (), "gtk-3.0", "bookmarks", NULL);
	g_autofree gchar *contents = NULL;
	g_auto(GStrv)     lines = NULL;
	guint             i;

	if (!g_file_get_contents (path, &contents, NULL, NULL))
		return;
	lines = g_strsplit (contents, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
	{
		g_autofree gchar *uri = NULL;
		g_autofree gchar *label = NULL;
		g_autoptr(GFile)  file = NULL;
		gchar            *space;

		g_strstrip (lines[i]);
		if (lines[i][0] == '\0' || !g_str_has_prefix (lines[i], "file://"))
			continue;
		space = strchr (lines[i], ' ');
		uri = space != NULL ? g_strndup (lines[i], (gsize) (space - lines[i])) : g_strdup (lines[i]);
		file = g_file_new_for_uri (uri);
		if (!g_file_query_exists (file, NULL))
			continue;
		if (space != NULL && space[1] != '\0')
			label = g_strdup (space + 1);
		else
		{
			g_autofree gchar *base = g_file_get_basename (file);

			label = g_filename_display_name (base != NULL ? base : uri);
		}
		add_place_internal (self, label, file, LRG_ICON_STAR, SECTION_BOOKMARKS);
	}
}

/* add_mounts: mounted volumes from the GIO volume monitor */
static void
add_mounts (LrgFileBrowser *self)
{
	GList *mounts;
	GList *l;

	if (self->volumes == NULL)
		return;
	mounts = g_volume_monitor_get_mounts (self->volumes);
	for (l = mounts; l != NULL; l = l->next)
	{
		GMount           *mount = l->data;
		g_autoptr(GFile)  root = g_mount_get_root (mount);
		g_autofree gchar *name = g_mount_get_name (mount);

		if (g_mount_is_shadowed (mount) || root == NULL)
			continue;
		add_place_internal (self, name != NULL ? name : "Volume", root, LRG_ICON_STOP, SECTION_DEVICES);
	}
	g_list_free_full (mounts, g_object_unref);
}

static void
on_mounts_changed (GVolumeMonitor *monitor,
                   GMount         *mount,
                   gpointer        user_data)
{
	refresh_places (LRG_FILE_BROWSER (user_data));
}

/* layout_sidebar: headings and rows, top to bottom */
static void
layout_sidebar (LrgFileBrowser *self)
{
	gfloat rh = row_h (self);
	gfloat y = 0.0f;
	gint   section = -1;
	guint  i;

	g_array_set_size (self->side_rows, 0);
	for (i = 0; i < self->places->len; i++)
	{
		Place  *p = place_at (self, i);
		SideRow r;

		/* A heading before each section except the application one */
		if ((gint) p->section != section)
		{
			section = (gint) p->section;
			if (p->section != SECTION_APP)
			{
				r.place = -1;
				r.section = section;
				r.y = y + (y > 0.0f ? 4.0f : 0.0f);
				r.h = roundf (rh * 0.85f);
				g_array_append_val (self->side_rows, r);
				y = r.y + r.h;
			}
		}
		r.place = (gint) i;
		r.section = section;
		r.y = y;
		r.h = rh;
		g_array_append_val (self->side_rows, r);
		y += rh;
	}
	self->side_content = y;
}

/*
 * refresh_places:
 *
 * Rebuilds the system part of the sidebar (home, XDG folders, the root,
 * bookmarks, mounts), keeping the application places on top.
 */
static void
refresh_places (LrgFileBrowser *self)
{
	g_autoptr(GFile) home = NULL;
	g_autoptr(GFile) root = NULL;
	guint            i;

	/* Drop everything but the application places */
	for (i = self->places->len; i > 0; i--)
	{
		if (place_at (self, i - 1)->section != SECTION_APP)
			g_ptr_array_remove_index (self->places, i - 1);
	}
	self->hover_place = -1;
	self->press_place = -1;

	if (self->show_system_places)
	{
		home = g_file_new_for_path (g_get_home_dir ());
		add_place_internal (self, "Home", home, LRG_ICON_HOME, SECTION_SYSTEM);
		add_special_dir (self, G_USER_DIRECTORY_DESKTOP, "Desktop", LRG_ICON_FOLDER);
		add_special_dir (self, G_USER_DIRECTORY_DOCUMENTS, "Documents", LRG_ICON_FILE);
		add_special_dir (self, G_USER_DIRECTORY_DOWNLOAD, "Downloads", LRG_ICON_DOWNLOAD);
		add_special_dir (self, G_USER_DIRECTORY_MUSIC, "Music", LRG_ICON_PLAY);
		add_special_dir (self, G_USER_DIRECTORY_PICTURES, "Pictures", LRG_ICON_EYE);
		add_special_dir (self, G_USER_DIRECTORY_VIDEOS, "Videos", LRG_ICON_PLAY);
		root = g_file_new_for_path (G_DIR_SEPARATOR_S);
		add_place_internal (self, "File System", root, LRG_ICON_GEAR, SECTION_SYSTEM);
		add_bookmarks (self);
		add_mounts (self);
	}

	layout_sidebar (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* place_is_current: does place @i show the current location? */
static gboolean
place_is_current (LrgFileBrowser *self,
                  guint           i)
{
	return self->location != NULL && g_file_equal (place_at (self, i)->location, self->location);
}

/* ==========================================================================
 * Child widgets
 * ========================================================================== */

static void
on_crumb_activated (LrgBreadcrumb *crumb,
                    guint          index,
                    gpointer       user_data)
{
	LrgFileBrowser *self = user_data;
	GFile          *folder = lrg_breadcrumb_get_segment_data (crumb, index);

	if (folder != NULL)
		navigate (self, folder, HOW_PUSH);
}

/*
 * on_path_submitted:
 *
 * Enter in the path field: a folder is entered; a file in an open mode
 * is selected and activated; in SAVE mode a new name inside an
 * existing folder becomes the folder plus the name.
 */
static void
on_path_submitted (LrgTextInput *input,
                   gpointer      user_data)
{
	LrgFileBrowser   *self = user_data;
	g_autoptr(GFile)  file = expand_path (self, lrg_text_input_get_text (input));
	g_autoptr(GFile)  parent = NULL;
	g_autofree gchar *base = NULL;
	GFileType         type;

	if (file == NULL)
		return;
	type = g_file_query_file_type (file, G_FILE_QUERY_INFO_NONE, NULL);
	if (type == G_FILE_TYPE_DIRECTORY)
	{
		lrg_file_browser_set_editing_location (self, FALSE);
		navigate (self, file, HOW_PUSH);
		return;
	}

	parent = g_file_get_parent (file);
	base = g_file_get_basename (file);
	if (parent == NULL || base == NULL ||
	    g_file_query_file_type (parent, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY)
		return;  /* nothing sensible: keep the field open */

	lrg_file_browser_set_editing_location (self, FALSE);
	navigate (self, parent, HOW_PUSH);
	if (self->mode == LRG_FILE_BROWSER_MODE_SAVE)
	{
		g_autofree gchar *display = g_filename_display_name (base);

		lrg_file_browser_set_current_name (self, display);
		return;
	}
	if (type != G_FILE_TYPE_UNKNOWN && self->mode != LRG_FILE_BROWSER_MODE_SELECT_FOLDER)
	{
		lrg_file_browser_select_file (self, file);
		g_signal_emit (self, signals[SIGNAL_FILE_ACTIVATED], 0, file);
	}
}

/*
 * on_name_submitted:
 *
 * Enter in the SAVE name field: a name that is (or leads to) a folder
 * enters it; anything else activates the save file.
 */
static void
on_name_submitted (LrgTextInput *input,
                   gpointer      user_data)
{
	LrgFileBrowser   *self = user_data;
	g_autoptr(GFile)  raw = expand_path (self, lrg_text_input_get_text (input));
	g_autoptr(GFile)  file = NULL;

	if (raw == NULL)
		return;
	if (g_file_query_file_type (raw, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY)
	{
		self->syncing = TRUE;
		lrg_text_input_set_text (input, "");
		self->syncing = FALSE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_NAME]);
		navigate (self, raw, HOW_PUSH);
		return;
	}
	file = lrg_file_browser_get_save_file (self);
	if (file != NULL)
		g_signal_emit (self, signals[SIGNAL_FILE_ACTIVATED], 0, file);
}

static void
on_name_changed (LrgTextInput *input,
                 gpointer      user_data)
{
	LrgFileBrowser *self = user_data;

	if (!self->syncing)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_NAME]);
}

static void
on_filter_combo_changed (LrgComboBox *combo,
                         gpointer     user_data)
{
	LrgFileBrowser             *self = user_data;
	gint                        i = lrg_combo_box_get_selected (combo);
	g_autoptr(LrgFileFilter)    filter = NULL;

	if (self->syncing || i < 0)
		return;
	filter = g_list_model_get_item (G_LIST_MODEL (self->filters), (guint) i);
	lrg_file_browser_set_filter (self, filter);
}

/* filter_index: position of the current filter in the list, or -1 */
static gint
filter_index (LrgFileBrowser *self)
{
	guint pos;

	if (self->filter == NULL || !g_list_store_find (self->filters, self->filter, &pos))
		return -1;
	return (gint) pos;
}

/* sync_filter_selection: the combo shows the current filter */
static void
sync_filter_selection (LrgFileBrowser *self)
{
	gint sel = filter_index (self);

	if (lrg_combo_box_get_selected (self->filter_combo) == sel)
		return;
	self->syncing = TRUE;
	lrg_combo_box_set_selected (self->filter_combo, sel);
	self->syncing = FALSE;
}

/* sync_filter_items: combo items from the filter list (shown when any) */
static void
sync_filter_items (LrgFileBrowser *self)
{
	guint n = g_list_model_get_n_items (G_LIST_MODEL (self->filters));
	guint i;

	self->syncing = TRUE;
	lrg_combo_box_remove_all (self->filter_combo);
	for (i = 0; i < n; i++)
	{
		g_autoptr(LrgFileFilter) f = g_list_model_get_item (G_LIST_MODEL (self->filters), i);
		const gchar             *name = lrg_file_filter_get_name (f);

		lrg_combo_box_append (self->filter_combo, name != NULL ? name : "Files");
	}
	self->syncing = FALSE;
	sync_filter_selection (self);
	lrg_widget_set_visible (LRG_WIDGET (self->filter_combo), n > 0);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_file_browser_layout_children (LrgContainer *container)
{
	LrgFileBrowser *self = LRG_FILE_BROWSER (container);
	Geom            g;
	gfloat          zone;
	gfloat          crumb_h;
	gfloat          x;
	gint            i;

	if (self->in_layout || self->ay.bar == NULL)
		return;
	self->in_layout = TRUE;
	geometry (self, &g);

	/* 1. Toolbar buttons and the path widgets between them */
	for (i = 0; i < NAV_COUNT; i++)
		lrg_widget_allocate (self->nav[i], g.nav[i], g.pad, g.bar_h, g.bar_h);
	lrg_widget_get_preferred_size (LRG_WIDGET (self->crumb), NULL, &crumb_h);
	crumb_h = MIN (crumb_h, g.bar_h);
	lrg_widget_set_visible (LRG_WIDGET (self->crumb), !self->editing);
	lrg_widget_set_visible (LRG_WIDGET (self->path_entry), self->editing);
	lrg_widget_allocate (LRG_WIDGET (self->crumb), g.crumb_x, g.pad + (g.bar_h - crumb_h) * 0.5f,
	                     g.crumb_w, crumb_h);
	lrg_widget_allocate (LRG_WIDGET (self->path_entry), g.crumb_x, g.pad, g.crumb_w, g.bar_h);

	/* 2. Sidebar and list zones, with a scrollbar zone along each */
	zone = lrg_scroll_bar_zone (LRG_WIDGET (self));
	lrg_widget_set_visible (self->places_zone, g.side_w > 0.0f);
	lrg_widget_allocate (self->places_zone, g.pad, g.body_y, g.side_w, g.body_h);
	lrg_widget_allocate (self->list_zone, g.list_x, g.body_y, g.list_w, g.body_h);
	update_extent (self);
	lrg_widget_set_visible (self->ay.bar, lrg_scroll_axis_get_max (&self->ay) > 0.0f);
	lrg_widget_set_visible (self->py.bar, g.side_w > 0.0f && lrg_scroll_axis_get_max (&self->py) > 0.0f);
	lrg_widget_allocate (self->ay.bar, g.list_x + g.list_w - zone, g.body_y + header_h (self), zone,
	                     MAX (0.0f, g.body_h - header_h (self)));
	lrg_widget_allocate (self->py.bar, g.pad + g.side_w - zone, g.body_y, zone, g.body_h);

	/* 3. Footer: name label + field, filter combo at the end */
	lrg_widget_set_visible (LRG_WIDGET (self->name_entry), self->mode == LRG_FILE_BROWSER_MODE_SAVE);
	x = lrg_widget_get_width (LRG_WIDGET (self)) - g.pad;
	if (lrg_widget_get_visible (LRG_WIDGET (self->filter_combo)))
	{
		gfloat cw = MIN (FILTER_COMBO_WIDTH, MAX (80.0f, (x - g.pad) * 0.4f));

		lrg_widget_allocate (LRG_WIDGET (self->filter_combo), x - cw, g.foot_y, cw, g.foot_h);
		x -= cw + g.gap * 2.0f;
	}
	if (self->mode == LRG_FILE_BROWSER_MODE_SAVE)
		lrg_widget_allocate (LRG_WIDGET (self->name_entry), g.pad + g.label_w, g.foot_y,
		                     MAX (0.0f, x - g.pad - g.label_w), g.foot_h);

	self->in_layout = FALSE;
}

static void
lrg_file_browser_measure (LrgWidget *widget,
                          gfloat    *preferred_width,
                          gfloat    *preferred_height)
{
	*preferred_width = 680.0f;
	*preferred_height = 420.0f;
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

static void
lrg_file_browser_style_defaults (LrgWidget      *widget,
                                 LrgUIPart       part,
                                 LrgWidgetState  state,
                                 LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	gfloat    bw = lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH);
	GrlColor  c;

	if (lrg_scroll_style_defaults (theme, part, state, look))
		return;

	switch (part)
	{
	case LRG_UI_PART_BOX:
		/* The whole browser: a panel card */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), bw);
		lrg_part_look_set_radius (look, radius);
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_TRACK:
		/* The places sidebar */
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), 0.7f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_radius (look, MAX (0.0f, radius - 1.0f));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		return;

	case LRG_UI_PART_FILL:
		/* The file list well */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme,
		                          (state & LRG_WIDGET_STATE_FOCUSED) ? LRG_THEME_COLOR_ACCENT
		                                                             : LRG_THEME_COLOR_BORDER), bw);
		lrg_part_look_set_radius (look, MAX (0.0f, radius - 1.0f));
		return;

	case LRG_UI_PART_HEADER:
		/* Column headers; hovered / pressed cells are shaded */
		if (state & LRG_WIDGET_STATE_PRESSED)
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED));
		else if (state & LRG_WIDGET_STATE_HOVERED)
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER));
		else
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                              (state & (LRG_WIDGET_STATE_SELECTED | LRG_WIDGET_STATE_HOVERED))
		                              ? LRG_THEME_COLOR_TEXT : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), bw);
		return;

	case LRG_UI_PART_ITEM:
		/* Rows and places: selection, hover and press pills */
		if (state & LRG_WIDGET_STATE_SELECTED)
		{
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);
			if (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED))
				c.a = (guint8) MIN (255, (gint) c.a + 40);
		}
		else if (state & LRG_WIDGET_STATE_PRESSED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED);
		else if (state & LRG_WIDGET_STATE_HOVERED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
		else
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), 0.0f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_radius (look, MAX (2.0f, radius * 0.75f));
		return;

	case LRG_UI_PART_TAB:
		/* Toolbar buttons: flat until hovered; CHECKED toggles tinted */
		if (state & LRG_WIDGET_STATE_PRESSED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED);
		else if (state & LRG_WIDGET_STATE_HOVERED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
		else if (state & LRG_WIDGET_STATE_CHECKED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);
		else
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL), 0.0f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_CHECKED) ? LRG_THEME_COLOR_ACCENT
		                                                                 : LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_radius (look, radius);
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.35f);
		return;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		return;

	case LRG_UI_PART_ARROW:
		/* Sort indicator */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		return;

	case LRG_UI_PART_INDICATOR:
		/* Keyboard cursor ring, current-place bar, loading dots */
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_FOCUS_RING_WIDTH) * 0.75f);
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_radius (look, MAX (2.0f, radius * 0.75f));
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_file_browser_parent_class)->style_defaults (widget, part, state, look);
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

/*
 * paint_text:
 *
 * Text centred vertically on @cy, starting at @x, or ending at @x + @w
 * when @end is set, clipped to @w.
 */
static void
paint_text (LrgPaintList   *list,
            LrgTheme       *theme,
            const gchar    *text,
            gfloat          x,
            gfloat          cy,
            gfloat          w,
            gfloat          size,
            gboolean        end,
            const GrlColor *color)
{
	GrlFont *font = lrg_theme_get_ui_font (theme);
	gfloat   tw = 0.0f;
	gfloat   th = size;

	if (text == NULL || text[0] == '\0' || w <= 1.0f)
		return;
	lrg_paint_measure_text (font, text, size, 1.0f, &tw, &th);
	lrg_paint_list_push_clip (list, x, cy - th, w, th * 2.0f);
	lrg_paint_list_text (list, font, text, roundf (end ? x + w - tw : x), roundf (cy - th * 0.5f), size, 1.0f,
	                     color);
	lrg_paint_list_pop_clip (list);
}

/* paint_nav: one toolbar button at its zone, icon tinted by its look */
static void
paint_nav (LrgFileBrowser *self,
           LrgPaintList   *list,
           gint            which)
{
	LrgWidget  *zone = self->nav[which];
	LrgPartLook look;
	LrgIcon     icon = LRG_ICON_NONE;
	gfloat      x;
	gfloat      y;
	gfloat      w;
	gfloat      h;
	gfloat      s;
	gfloat      squash;
	gboolean    flip = rtl (self);

	zone_rect (zone, &x, &y, &w, &h);
	switch (which)
	{
	case NAV_BACK:    icon = flip ? LRG_ICON_ARROW_RIGHT : LRG_ICON_ARROW_LEFT; break;
	case NAV_FORWARD: icon = flip ? LRG_ICON_ARROW_LEFT : LRG_ICON_ARROW_RIGHT; break;
	case NAV_UP:      icon = LRG_ICON_CHEVRON_UP; break;
	case NAV_EDIT:    icon = LRG_ICON_EDIT; break;
	case NAV_HIDDEN:  icon = LRG_ICON_EYE; break;
	case NAV_RELOAD:  icon = LRG_ICON_REFRESH; break;
	default:          break;
	}

	/* The zone carries the hover / press springs; blend its looks */
	lrg_widget_resolve_look (zone, LRG_UI_PART_TAB, &look);
	if (look.background.a > 0)
		lrg_widget_paint_box (zone, list, &look, x, y, w, h);

	/* A pressed button squeezes its icon a little */
	squash = 1.0f - 0.12f * CLAMP (lrg_widget_get_press_progress (zone), 0.0f, 1.0f);
	s = roundf (h * 0.55f * squash);
	lrg_paint_list_push_opacity (list, (look.set & LRG_PART_LOOK_OPACITY) ? look.opacity : 1.0f);
	lrg_paint_list_icon (list, icon, x + (w - s) * 0.5f, y + (h - s) * 0.5f, s, 0.0f, &look.foreground);
	lrg_paint_list_pop_opacity (list);
}

/*
 * style_defaults for the zones: they are plain LrgWidgets, so their
 * looks come from this browser. lrg_widget_resolve_look() on a zone
 * asks the zone's class (LrgWidget), which has no TAB defaults; the
 * zone therefore delegates to the browser through this subclass.
 */
#define LRG_TYPE_FILE_BROWSER_ZONE (lrg_file_browser_zone_get_type ())
G_DECLARE_FINAL_TYPE (LrgFileBrowserZone, lrg_file_browser_zone, LRG, FILE_BROWSER_ZONE, LrgWidget)

struct _LrgFileBrowserZone
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgFileBrowserZone, lrg_file_browser_zone, LRG_TYPE_WIDGET)

static void
lrg_file_browser_zone_style_defaults (LrgWidget      *widget,
                                      LrgUIPart       part,
                                      LrgWidgetState  state,
                                      LrgPartLook    *look)
{
	LrgContainer *parent = lrg_widget_get_parent (widget);

	/* Same looks as the browser, with the zone's own state */
	if (parent != NULL && LRG_IS_FILE_BROWSER (parent))
	{
		lrg_widget_resolve_look_for_state (LRG_WIDGET (parent), part, state, look);
		return;
	}
	LRG_WIDGET_CLASS (lrg_file_browser_zone_parent_class)->style_defaults (widget, part, state, look);
}

static void
lrg_file_browser_zone_class_init (LrgFileBrowserZoneClass *klass)
{
	LRG_WIDGET_CLASS (klass)->style_defaults = lrg_file_browser_zone_style_defaults;
}

static void
lrg_file_browser_zone_init (LrgFileBrowserZone *self)
{
}

static LrgWidget *
zone_new (const gchar *name)
{
	LrgWidget *zone = g_object_new (LRG_TYPE_FILE_BROWSER_ZONE, NULL);

	lrg_widget_set_name (zone, name);
	return zone;
}

/* item_look: the ITEM look for a state, with hover blended in by @hover */
static void
item_look (LrgFileBrowser *self,
           LrgWidgetState  base,
           gfloat          hover,
           LrgPartLook    *look)
{
	LrgPartLook variant;

	lrg_widget_resolve_look_for_state (LRG_WIDGET (self), LRG_UI_PART_ITEM, base, look);
	if (hover > 0.001f)
	{
		lrg_widget_resolve_look_for_state (LRG_WIDGET (self), LRG_UI_PART_ITEM, base | LRG_WIDGET_STATE_HOVERED,
		                                   &variant);
		lrg_part_look_lerp (look, &variant, CLAMP (hover, 0.0f, 1.0f), look);
	}
}

/*
 * paint_sidebar:
 *
 * The places strip: section headings, a sliding hover pill, the current
 * place in the selection colour with an accent bar on its leading edge,
 * icon and label per place.
 */
static void
paint_sidebar (LrgFileBrowser *self,
               LrgPaintList   *list)
{
	LrgTheme    *theme = theme_of (self);
	LrgPartLook  strip;
	LrgPartLook  look;
	LrgPartLook  ind;
	gfloat       zx;
	gfloat       zy;
	gfloat       zw;
	gfloat       zh;
	gfloat       off = lrg_scroll_axis_get_value (&self->py);
	gfloat       size = metric (self, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       small = metric (self, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat       inset = 4.0f;
	gfloat       ipad = roundf (metric (self, LRG_THEME_METRIC_PADDING_X) * 0.5f);
	static const gchar *headings[N_SECTIONS] = { NULL, "Places", "Bookmarks", "Devices" };
	guint        i;

	if (!lrg_widget_get_visible (self->places_zone))
		return;
	zone_rect (self->places_zone, &zx, &zy, &zw, &zh);
	lrg_widget_resolve_look_for_state (LRG_WIDGET (self), LRG_UI_PART_TRACK, LRG_WIDGET_STATE_NONE, &strip);
	lrg_widget_paint_box (LRG_WIDGET (self), list, &strip, zx, zy, zw, zh);
	lrg_widget_resolve_look_for_state (LRG_WIDGET (self), LRG_UI_PART_INDICATOR, LRG_WIDGET_STATE_NONE, &ind);

	lrg_paint_list_push_clip (list, zx, zy, zw, zh);

	/* Hover pill, sliding between places */
	if (self->place_a.value > 0.01f)
	{
		item_look (self, LRG_WIDGET_STATE_NONE, 1.0f, &look);
		lrg_paint_list_push_opacity (list, CLAMP (self->place_a.value, 0.0f, 1.0f));
		lrg_widget_paint_box (LRG_WIDGET (self), list, &look, zx + inset, zy + self->place_y.value - off,
		                      zw - inset * 2.0f, row_h (self));
		lrg_paint_list_pop_opacity (list);
	}

	for (i = 0; i < self->side_rows->len; i++)
	{
		const SideRow *r = &g_array_index (self->side_rows, SideRow, i);
		gfloat         ry = zy + r->y - off;
		gfloat         icon_s;
		gfloat         tx;
		Place         *p;
		gboolean       current;

		if (ry + r->h < zy || ry > zy + zh)
			continue;

		if (r->place < 0)
		{
			/* Section heading */
			paint_text (list, theme, headings[r->section], span_x (self, zx, zw, ipad + inset, zw - ipad * 2.0f),
			            ry + r->h * 0.55f, zw - ipad * 2.0f - inset, small, rtl (self), &strip.foreground);
			continue;
		}

		p = place_at (self, (guint) r->place);
		current = place_is_current (self, (guint) r->place);
		if (current)
		{
			gfloat bar_w = 3.0f;

			item_look (self, LRG_WIDGET_STATE_SELECTED, 0.0f, &look);
			lrg_widget_paint_box (LRG_WIDGET (self), list, &look, zx + inset, ry, zw - inset * 2.0f, r->h);
			lrg_paint_list_rect (list, span_x (self, zx, zw, inset, bar_w), ry + r->h * 0.2f, bar_w,
			                     r->h * 0.6f, bar_w * 0.5f, &ind.background);
		}
		else if (self->press_place == r->place)
		{
			lrg_widget_resolve_look_for_state (LRG_WIDGET (self), LRG_UI_PART_ITEM,
			                                   LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED, &look);
			lrg_widget_paint_box (LRG_WIDGET (self), list, &look, zx + inset, ry, zw - inset * 2.0f, r->h);
		}

		/* Icon and label */
		item_look (self, LRG_WIDGET_STATE_NONE, 0.0f, &look);
		icon_s = roundf (r->h * 0.55f);
		lrg_paint_list_icon (list, p->icon, span_x (self, zx, zw, inset + ipad, icon_s),
		                     ry + (r->h - icon_s) * 0.5f, icon_s, 0.0f,
		                     current ? &ind.background : &strip.foreground);
		tx = inset + ipad + icon_s + ipad;
		paint_text (list, theme, p->label, span_x (self, zx, zw, tx, zw - tx - inset - ipad),
		            ry + r->h * 0.5f, zw - tx - inset - ipad, size, rtl (self), &look.foreground);
	}
	lrg_paint_list_pop_clip (list);
}

/*
 * paint_header:
 *
 * The column header strip: titles, hover / press shading of the cell
 * under the pointer, the sort chevron on the sorted column.
 */
static void
paint_header (LrgFileBrowser *self,
              LrgPaintList   *list,
              gfloat          lx,
              gfloat          ly,
              gfloat          lw)
{
	LrgTheme    *theme = theme_of (self);
	LrgPartLook  strip;
	LrgPartLook  arrow;
	gfloat       hh = header_h (self);
	gfloat       size = metric (self, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat       tpad = roundf (metric (self, LRG_THEME_METRIC_PADDING_X) * 0.5f);
	gfloat       widths[3];
	gfloat       rel = 0.0f;
	static const gchar *titles[3] = { "Name", "Size", "Modified" };
	LrgCornerRadii radii;
	gint         i;

	columns (lw, &widths[0], &widths[1], &widths[2]);
	lrg_widget_resolve_look_for_state (LRG_WIDGET (self), LRG_UI_PART_HEADER, LRG_WIDGET_STATE_NONE, &strip);
	lrg_widget_resolve_look (LRG_WIDGET (self), LRG_UI_PART_ARROW, &arrow);
	radii.top_left = radii.top_right = MAX (0.0f, metric (self, LRG_THEME_METRIC_CORNER_RADIUS) - 1.0f);
	radii.bottom_left = radii.bottom_right = 0.0f;
	lrg_paint_list_rect_ex (list, lx, ly, lw, hh, &radii, &strip.background,
	                        (strip.set & LRG_PART_LOOK_GRADIENT) ? &strip.gradient : NULL, 0.0f, NULL);
	lrg_paint_list_rect (list, lx, ly + hh - MAX (1.0f, strip.border_width), lw, MAX (1.0f, strip.border_width),
	                     0.0f, &strip.border);

	for (i = 0; i < 3; i++)
	{
		LrgWidgetState st = LRG_WIDGET_STATE_NONE;
		LrgPartLook    cell;
		gfloat         cw = widths[i];
		gfloat         cx;
		gboolean       sorted = (gint) self->sort_column == i;
		gboolean       end = i == 1;  /* sizes align to the end */
		gfloat         chev = sorted ? size * 0.9f : 0.0f;

		if (cw <= 0.0f)
			continue;
		cx = span_x (self, lx, lw, rel, cw);
		rel += cw;

		if (sorted)
			st |= LRG_WIDGET_STATE_SELECTED;
		if (self->hover_header == i)
			st |= LRG_WIDGET_STATE_HOVERED;
		if (self->press_header == i)
			st |= LRG_WIDGET_STATE_PRESSED | LRG_WIDGET_STATE_HOVERED;
		lrg_widget_resolve_look_for_state (LRG_WIDGET (self), LRG_UI_PART_HEADER, st, &cell);
		if (st & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED))
			lrg_paint_list_rect (list, cx, ly, cw, hh - 1.0f, 0.0f, &cell.background);

		/* Title on the start side (end side for sizes), chevron opposite */
		if (end != rtl (self))
			paint_text (list, theme, titles[i], cx + tpad + chev, ly + hh * 0.5f, cw - tpad * 2.0f - chev, size,
			            TRUE, &cell.foreground);
		else
			paint_text (list, theme, titles[i], cx + tpad, ly + hh * 0.5f, cw - tpad * 2.0f - chev, size, FALSE,
			            &cell.foreground);
		if (sorted)
		{
			gfloat cxx = (end != rtl (self)) ? cx + tpad : cx + cw - tpad - chev;

			lrg_paint_list_icon (list, self->sort_ascending ? LRG_ICON_CHEVRON_UP : LRG_ICON_CHEVRON_DOWN,
			                     cxx, ly + (hh - chev) * 0.5f, chev, 0.0f, &arrow.foreground);
		}

		/* Separator after every column but the last */
		if (i < 2 && widths[i + 1] > 0.0f)
			lrg_paint_list_rect (list, rtl (self) ? cx : cx + cw - 1.0f, ly + hh * 0.25f, 1.0f, hh * 0.5f, 0.0f,
			                     &strip.border);
	}
}

/* paint_status: centred message (loading dots, error, empty folder) */
static void
paint_status (LrgFileBrowser *self,
              LrgPaintList   *list,
              gfloat          bx,
              gfloat          by,
              gfloat          bw)
{
	LrgTheme    *theme = theme_of (self);
	LrgPartLook  ind;
	gfloat       rh = row_h (self);
	gfloat       size = metric (self, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       cy = by + rh * 1.5f;
	gfloat       icon_s = roundf (rh * 0.7f);
	gfloat       tw = 0.0f;
	const gchar *text = NULL;
	GrlColor     muted = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	gint         k;

	lrg_widget_resolve_look_for_state (LRG_WIDGET (self), LRG_UI_PART_INDICATOR, LRG_WIDGET_STATE_NONE, &ind);

	if (self->error != NULL)
	{
		/* Error row: warning icon in the danger colour and the message */
		const GrlColor *danger = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);
		gfloat          tpad = roundf (metric (self, LRG_THEME_METRIC_PADDING_X));

		lrg_paint_list_icon (list, LRG_ICON_WARNING, span_x (self, bx, bw, tpad, icon_s), cy - icon_s * 0.5f,
		                     icon_s, 0.0f, danger);
		paint_text (list, theme, self->error->message,
		            span_x (self, bx, bw, tpad * 1.5f + icon_s, bw - tpad * 2.5f - icon_s), cy,
		            bw - tpad * 2.5f - icon_s, size, rtl (self), danger);
		return;
	}

	if (self->loading && n_view (self) == 0)
	{
		/* Three dots pulsing one after the other (canvas clock) */
		gfloat r = MAX (2.0f, rh * 0.1f);
		gfloat cx = bx + bw * 0.5f;

		for (k = 0; k < 3; k++)
		{
			GrlColor c;
			gfloat   a = 0.35f + 0.65f * (0.5f + 0.5f * sinf ((gfloat) self->clock * 6.0f - (gfloat) k * 0.9f));

			lrg_paint_color_with_alpha (&ind.background, a, &c);
			lrg_paint_list_circle (list, cx + ((gfloat) k - 1.0f) * r * 3.5f, cy, r, &c);
		}
		return;
	}

	if (self->loading || n_view (self) > 0)
		return;
	text = self->all->len > 0 ? "No matching files" : "This folder is empty";
	lrg_paint_measure_text (lrg_theme_get_ui_font (theme), text, size, 1.0f, &tw, NULL);
	paint_text (list, theme, text, bx + MAX (0.0f, (bw - tw) * 0.5f), cy, MIN (bw, tw + 2.0f), size, FALSE,
	            &muted);
}

/*
 * paint_list:
 *
 * The file list well: header, the visible rows (selection, the sliding
 * selection / hover pills, the cursor ring, icon, name, size, date),
 * status messages and the scrollbar.
 */
static void
paint_list (LrgFileBrowser *self,
            LrgPaintList   *list)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	LrgTheme    *theme = theme_of (self);
	LrgPartLook  well;
	LrgPartLook  look;
	LrgPartLook  ind;
	gfloat       lx;
	gfloat       ly;
	gfloat       lw;
	gfloat       lh;
	gfloat       bx;
	gfloat       by;
	gfloat       bw;
	gfloat       bh;
	gfloat       rh = row_h (self);
	gfloat       off = lrg_scroll_axis_get_value (&self->ay);
	gfloat       size = metric (self, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       small = metric (self, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat       tpad = roundf (metric (self, LRG_THEME_METRIC_PADDING_X) * 0.5f);
	gfloat       inset = 3.0f;
	gfloat       name_w;
	gfloat       size_w;
	gfloat       date_w;
	gfloat       icon_s = roundf (rh * 0.62f);
	gfloat       fade = CLAMP (self->content.value, 0.0f, 1.0f);
	GrlColor     muted = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	gboolean     focus_visible = (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_FOCUS_VISIBLE) != 0;
	gboolean     focused = (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_FOCUSED) != 0;
	guint        first;
	guint        last;
	guint        mark;
	guint        i;

	zone_rect (self->list_zone, &lx, &ly, &lw, &lh);
	list_body (self, &bx, &by, &bw, &bh);
	columns (lw, &name_w, &size_w, &date_w);

	/* 1. Well and header */
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_FILL,
	                                   focused ? LRG_WIDGET_STATE_FOCUSED : LRG_WIDGET_STATE_NONE, &well);
	lrg_widget_paint_box (widget, list, &well, lx, ly, lw, lh);
	paint_header (self, list, lx, ly, lw);
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_INDICATOR, LRG_WIDGET_STATE_NONE, &ind);

	/* 2. Rows, clipped to the body, fading / sliding in after a
	 *    navigation as one group */
	lrg_paint_list_push_clip (list, bx, by, bw, bh);
	mark = lrg_paint_list_begin_group (list);

	/* Hover pill under everything else */
	if (self->hover_a.value > 0.01f && n_view (self) > 0)
	{
		item_look (self, LRG_WIDGET_STATE_NONE, 1.0f, &look);
		lrg_paint_list_push_opacity (list, CLAMP (self->hover_a.value, 0.0f, 1.0f));
		lrg_widget_paint_box (widget, list, &look, bx + inset, by + self->hover_y.value - off, bw - inset * 2.0f, rh);
		lrg_paint_list_pop_opacity (list);
	}

	first = (guint) MAX (0.0f, floorf (off / rh));
	last = MIN (n_view (self), (guint) MAX (0.0f, ceilf ((off + bh) / rh)) + 1u);
	for (i = first; i < last; i++)
	{
		Entry   *e = entry_at (self, i);
		gfloat   ry = by + (gfloat) i * rh - off;
		gfloat   rel;
		gfloat   cy = ry + rh * 0.5f;
		GrlColor tint = *lrg_theme_get_color (theme, e->tint);

		/* Selection: the cursor row slides on a spring, others are static */
		if (e->selected)
		{
			gfloat sy = ((gint) i == self->cursor) ? by + self->sel_y.value - off : ry;

			item_look (self, LRG_WIDGET_STATE_SELECTED, (gint) i == self->hover_row ? self->hover_a.value : 0.0f,
			           &look);
			lrg_widget_paint_box (widget, list, &look, bx + inset, sy, bw - inset * 2.0f, rh);
		}

		/* Cursor ring after keyboard / gamepad navigation */
		if ((gint) i == self->cursor && focus_visible)
			lrg_paint_list_rect_ex (list, bx + inset + 1.0f, ry + 1.0f, bw - inset * 2.0f - 2.0f, rh - 2.0f,
			                        &ind.radii, NULL, NULL, ind.border_width, &ind.border);

		/* Icon + name, then size and date in muted text */
		item_look (self, e->selected ? LRG_WIDGET_STATE_SELECTED : LRG_WIDGET_STATE_NONE, 0.0f, &look);
		if (e->hidden)
			lrg_paint_color_with_alpha (&tint, (gfloat) tint.a / 255.0f * 0.6f, &tint);
		lrg_paint_list_icon (list, e->icon, span_x (self, bx, bw, tpad + inset, icon_s), cy - icon_s * 0.5f,
		                     icon_s, 0.0f, &tint);
		rel = tpad + inset + icon_s + tpad;
		paint_text (list, theme, e->display, span_x (self, bx, bw, rel, name_w - rel - tpad), cy,
		            name_w - rel - tpad, size, rtl (self), e->hidden ? &muted : &look.foreground);
		if (size_w > 0.0f)
			paint_text (list, theme, e->size_text, span_x (self, bx, bw, name_w + tpad, size_w - tpad * 2.0f), cy,
			            size_w - tpad * 2.0f, small, !rtl (self), &muted);
		if (date_w > 0.0f)
			paint_text (list, theme, e->date_text, span_x (self, bx, bw, name_w + size_w + tpad, date_w - tpad * 2.0f),
			            cy, date_w - tpad * 2.0f, small, rtl (self), &muted);
	}
	if (fade < 0.999f)
		lrg_paint_list_transform_group (list, mark, 1.0f, bx, by, 0.0f, (1.0f - fade) * 10.0f, fade);

	paint_status (self, list, bx, by, bw);
	lrg_paint_list_pop_clip (list);

	/* 3. Overlay scrollbar */
	lrg_scroll_bar_paint (widget, list, &self->ay, TRUE, self->fx.fade.value);
}

static void
lrg_file_browser_paint (LrgWidget    *widget,
                        LrgPaintList *list)
{
	LrgFileBrowser *self = LRG_FILE_BROWSER (widget);
	LrgPartLook     box;
	LrgPartLook     label;
	Geom            g;
	gint            i;

	update_extent (self);
	geometry (self, &g);

	/* 1. Card */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, lrg_widget_get_world_x (widget), lrg_widget_get_world_y (widget),
	                      lrg_widget_get_width (widget), lrg_widget_get_height (widget));

	/* 2. Toolbar */
	for (i = 0; i < NAV_COUNT; i++)
		paint_nav (self, list, i);
	lrg_widget_paint (LRG_WIDGET (self->crumb), list);
	lrg_widget_paint (LRG_WIDGET (self->path_entry), list);

	/* 3. Sidebar and list */
	paint_sidebar (self, list);
	lrg_scroll_bar_paint (widget, list, &self->py, TRUE, self->fx.fade.value);
	paint_list (self, list);

	/* 4. Footer */
	if (self->mode == LRG_FILE_BROWSER_MODE_SAVE)
	{
		gfloat wx = lrg_widget_get_world_x (widget);
		gfloat ww = lrg_widget_get_width (widget);

		lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
		paint_text (list, theme_of (self), "Name", span_x (self, wx, ww, g.pad, g.label_w - g.gap),
		            lrg_widget_get_world_y (widget) + g.foot_y + g.foot_h * 0.5f, g.label_w - g.gap,
		            metric (self, LRG_THEME_METRIC_FONT_SIZE), rtl (self), &label.foreground);
	}
	lrg_widget_paint (LRG_WIDGET (self->name_entry), list);
	lrg_widget_paint (LRG_WIDGET (self->filter_combo), list);
}

/* ==========================================================================
 * Animation
 * ========================================================================== */

/* step_pill: a hover pill following row @row (-1 hides it) */
static gboolean
step_pill (LrgSpring *y,
           LrgSpring *a,
           gint       row,
           gfloat     row_y,
           gboolean   snap,
           gfloat     dt)
{
	gboolean moving = FALSE;

	if (row >= 0)
	{
		/* Appear in place, then slide between rows */
		if (a->value < 0.05f)
			lrg_spring_snap (y, row_y);
		lrg_spring_set_target (y, row_y);
		lrg_spring_set_target (a, 1.0f);
	}
	else
		lrg_spring_set_target (a, 0.0f);

	if (snap)
	{
		lrg_spring_snap (y, y->target);
		lrg_spring_snap (a, a->target);
		return FALSE;
	}
	moving |= lrg_spring_step (y, dt);
	moving |= lrg_spring_step (a, dt);
	return moving;
}

/* place_row_y: sidebar content y of place @place */
static gfloat
place_row_y (LrgFileBrowser *self,
             gint            place)
{
	guint i;

	for (i = 0; i < self->side_rows->len; i++)
	{
		const SideRow *r = &g_array_index (self->side_rows, SideRow, i);

		if (r->place == place)
			return r->y;
	}
	return 0.0f;
}

static gboolean
lrg_file_browser_step (LrgWidget *widget,
                       gfloat     delta_time)
{
	LrgFileBrowser *self = LRG_FILE_BROWSER (widget);
	LrgMotion       motion = lrg_widget_get_motion (widget);
	gboolean        snap = !self->stepped || motion == LRG_MOTION_NONE;
	gboolean        moving = FALSE;
	gboolean        keep;
	gfloat          rh = row_h (self);

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;
	if (self->ay.bar == NULL)
		return FALSE;

	lrg_spring_set_motion (&self->hover_y, motion);
	lrg_spring_set_motion (&self->hover_a, motion);
	lrg_spring_set_motion (&self->sel_y, motion);
	lrg_spring_set_motion (&self->place_y, motion);
	lrg_spring_set_motion (&self->place_a, motion);

	/* 1. Hover pills in the list and the sidebar */
	moving |= step_pill (&self->hover_y, &self->hover_a, self->hover_row, (gfloat) self->hover_row * rh, snap,
	                     delta_time);
	moving |= step_pill (&self->place_y, &self->place_a,
	                     (self->hover_place >= 0 && !place_is_current (self, (guint) self->hover_place))
	                     ? self->hover_place : -1,
	                     self->hover_place >= 0 ? place_row_y (self, self->hover_place) : 0.0f, snap, delta_time);

	/* 2. The cursor's selection slides; long jumps snap */
	if (self->cursor >= 0)
	{
		gfloat target = (gfloat) self->cursor * rh;

		if (fabsf (target - self->sel_y.value) > rh * SLIDE_MAX_ROWS)
			lrg_spring_snap (&self->sel_y, target);
		lrg_spring_set_target (&self->sel_y, target);
	}
	if (snap)
		lrg_spring_snap (&self->sel_y, self->sel_y.target);
	else
		moving |= lrg_spring_step (&self->sel_y, delta_time);

	/* 3. Content fade-in */
	if (snap)
		lrg_spring_snap (&self->content, self->content.target);
	else
		moving |= lrg_spring_step (&self->content, delta_time);

	/* 4. Scrolling and the scrollbar fade */
	if (lrg_scroll_axis_step_snap (&self->ay, delta_time) | lrg_scroll_axis_step_snap (&self->py, delta_time))
	{
		moving = TRUE;
		lrg_scroll_fx_poke (&self->fx);
	}
	keep = self->ay.dragging || self->py.dragging ||
	       (lrg_widget_get_state_flags (self->ay.bar) & LRG_WIDGET_STATE_HOVERED) ||
	       (lrg_widget_get_state_flags (self->py.bar) & LRG_WIDGET_STATE_HOVERED);
	moving |= lrg_scroll_fx_update (&self->fx, keep, delta_time);

	/* 5. Type-ahead expires on the canvas clock */
	if (self->typeahead->len > 0 && self->clock - self->typeahead_time > TYPEAHEAD_TIMEOUT)
		g_string_truncate (self->typeahead, 0);

	self->stepped = TRUE;

	/* The loading dots pulse while a folder loads with nothing to show */
	return moving || (self->loading && n_view (self) == 0);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* row_at: the view row under a world point inside the list body, or -1 */
static gint
row_at (LrgFileBrowser *self,
        gfloat          x,
        gfloat          y)
{
	gfloat bx;
	gfloat by;
	gfloat bw;
	gfloat bh;
	gfloat rel;
	gint   r;

	list_body (self, &bx, &by, &bw, &bh);
	if (x < bx || x >= bx + bw || y < by || y >= by + bh)
		return -1;
	rel = y - by + lrg_scroll_axis_get_value (&self->ay);
	if (rel < 0.0f)
		return -1;
	r = (gint) floorf (rel / row_h (self));
	return (guint) r < n_view (self) ? r : -1;
}

/* header_at: the column header under a world point, or -1 */
static gint
header_at (LrgFileBrowser *self,
           gfloat          x,
           gfloat          y)
{
	gfloat lx;
	gfloat ly;
	gfloat lw;
	gfloat lh;
	gfloat widths[3];
	gfloat rel;
	gfloat acc = 0.0f;
	gint   i;

	zone_rect (self->list_zone, &lx, &ly, &lw, &lh);
	if (x < lx || x >= lx + lw || y < ly || y >= ly + header_h (self))
		return -1;
	columns (lw, &widths[0], &widths[1], &widths[2]);
	rel = rel_x (self, lx, lw, x);
	for (i = 0; i < 3; i++)
	{
		if (widths[i] > 0.0f && rel >= acc && rel < acc + widths[i])
			return i;
		acc += widths[i];
	}
	return -1;
}

/* place_at_point: the place under a world point, or -1 */
static gint
place_at_point (LrgFileBrowser *self,
                gfloat          x,
                gfloat          y)
{
	gfloat zx;
	gfloat zy;
	gfloat zw;
	gfloat zh;
	gfloat rel;
	guint  i;

	if (!lrg_widget_get_visible (self->places_zone))
		return -1;
	zone_rect (self->places_zone, &zx, &zy, &zw, &zh);
	if (x < zx || x >= zx + zw || y < zy || y >= zy + zh)
		return -1;
	rel = y - zy + lrg_scroll_axis_get_value (&self->py);
	for (i = 0; i < self->side_rows->len; i++)
	{
		const SideRow *r = &g_array_index (self->side_rows, SideRow, i);

		if (rel >= r->y && rel < r->y + r->h)
			return r->place;
	}
	return -1;
}

/* track_hover: hovered row, header cell and place from the pointer */
static void
track_hover (LrgFileBrowser *self,
             gfloat          x,
             gfloat          y)
{
	self->pointer_x = x;
	self->pointer_y = y;
	self->pointer_in = lrg_widget_contains_point (LRG_WIDGET (self), x, y);
	if (!self->pointer_in || self->ay.dragging || self->py.dragging)
	{
		self->hover_row = -1;
		self->hover_header = -1;
		self->hover_place = -1;
		return;
	}
	self->hover_row = row_at (self, x, y);
	self->hover_header = header_at (self, x, y);
	self->hover_place = place_at_point (self, x, y);
}

/* nav_index: which toolbar zone @w is, or -1 */
static gint
nav_index (LrgFileBrowser *self,
           LrgWidget      *w)
{
	gint i;

	for (i = 0; i < NAV_COUNT; i++)
	{
		if (self->nav[i] == w)
			return i;
	}
	return -1;
}

static void
run_nav (LrgFileBrowser *self,
         gint            which)
{
	switch (which)
	{
	case NAV_BACK:
		lrg_file_browser_go_back (self);
		break;
	case NAV_FORWARD:
		lrg_file_browser_go_forward (self);
		break;
	case NAV_UP:
		lrg_file_browser_go_up (self);
		break;
	case NAV_EDIT:
		lrg_file_browser_set_editing_location (self, !self->editing);
		break;
	case NAV_HIDDEN:
		lrg_file_browser_set_show_hidden (self, !self->show_hidden);
		break;
	case NAV_RELOAD:
		lrg_file_browser_reload (self);
		break;
	default:
		break;
	}
}

/* click_row: select (Ctrl toggles, Shift ranges), double click activates */
static void
click_row (LrgFileBrowser    *self,
           gint               r,
           LrgInputModifiers  mods)
{
	gboolean multi = self->mode == LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE;

	if (multi && (mods & LRG_INPUT_MODIFIER_SHIFT) && self->anchor >= 0)
		select_range (self, r);
	else if (multi && (mods & LRG_INPUT_MODIFIER_CTRL))
	{
		Entry *e = entry_at (self, r);

		e->selected = !e->selected;
		self->anchor = r;
		set_cursor (self, r, TRUE);
		emit_selection_changed (self);
	}
	else
		select_only (self, r);

	if (self->click_armed && self->last_click_row == r && self->clock - self->last_click_time <= DOUBLE_CLICK_TIME)
	{
		self->click_armed = FALSE;
		activate_row (self, r);
		return;
	}
	self->click_armed = TRUE;
	self->last_click_time = self->clock;
	self->last_click_row = r;
}

/*
 * typeahead:
 *
 * Jumps to the first row whose name starts with the typed text. Typing
 * the same letter again cycles through the rows starting with it.
 */
static void
typeahead (LrgFileBrowser *self,
           gunichar        c)
{
	g_autofree gchar *folded = NULL;
	gchar             buf[8];
	gint              len;
	guint             n = n_view (self);
	guint             start = 0;
	guint             k;
	gboolean          cycle;

	if (self->clock - self->typeahead_time > TYPEAHEAD_TIMEOUT)
		g_string_truncate (self->typeahead, 0);
	self->typeahead_time = self->clock;

	len = g_unichar_to_utf8 (c, buf);
	buf[len] = '\0';
	cycle = self->typeahead->len > 0 && self->typeahead->len == (gsize) len &&
	        memcmp (self->typeahead->str, buf, (gsize) len) == 0;
	if (!cycle)
		g_string_append (self->typeahead, buf);
	folded = g_utf8_casefold (self->typeahead->str, -1);

	if (cycle && self->cursor >= 0)
		start = (guint) self->cursor + 1;
	for (k = 0; k < n; k++)
	{
		guint i = (start + k) % n;

		if (g_str_has_prefix (entry_at (self, i)->fold, folded))
		{
			select_only (self, (gint) i);
			return;
		}
	}
}

/*
 * handle_key:
 *
 * Keys aimed at the list (@own) and browser-wide shortcuts that also
 * work while a child (the name or path field) has focus.
 */
static gboolean
handle_key (LrgFileBrowser    *self,
            GrlKey             key,
            LrgInputModifiers  mods,
            gboolean           own)
{
	gboolean alt = (mods & LRG_INPUT_MODIFIER_ALT) != 0;
	gboolean ctrl = (mods & LRG_INPUT_MODIFIER_CTRL) != 0;
	gboolean extend = (mods & LRG_INPUT_MODIFIER_SHIFT) != 0;
	gint     n = (gint) n_view (self);
	gint     cur = self->cursor;
	gint     page = (gint) rows_per_page (self);

	/* 1. Everywhere inside the browser */
	if (alt && (key == GRL_KEY_LEFT || key == GRL_KEY_RIGHT))
	{
		/* Left is "back" in left-to-right, "forward" in right-to-left */
		if ((key == GRL_KEY_LEFT) != rtl (self))
			lrg_file_browser_go_back (self);
		else
			lrg_file_browser_go_forward (self);
		return TRUE;
	}
	if (alt && key == GRL_KEY_UP)
	{
		lrg_file_browser_go_up (self);
		return TRUE;
	}
	if (alt && key == GRL_KEY_HOME)
	{
		g_autoptr(GFile) home = g_file_new_for_path (g_get_home_dir ());

		navigate (self, home, HOW_PUSH);
		return TRUE;
	}
	if (ctrl && key == GRL_KEY_L)
	{
		lrg_file_browser_set_editing_location (self, TRUE);
		return TRUE;
	}
	if (ctrl && key == GRL_KEY_H)
	{
		lrg_file_browser_set_show_hidden (self, !self->show_hidden);
		return TRUE;
	}
	if (key == GRL_KEY_F5)
	{
		lrg_file_browser_reload (self);
		return TRUE;
	}
	if (key == GRL_KEY_ESCAPE && self->editing)
	{
		lrg_file_browser_set_editing_location (self, FALSE);
		return TRUE;
	}
	if (!own)
		return FALSE;

	/* 2. The list itself */
	switch (key)
	{
	case GRL_KEY_UP:
		if (n > 0)
			move_cursor_to (self, cur < 0 ? 0 : cur - 1, extend);
		return TRUE;
	case GRL_KEY_DOWN:
		if (n > 0)
			move_cursor_to (self, cur < 0 ? 0 : cur + 1, extend);
		return TRUE;
	case GRL_KEY_HOME:
		move_cursor_to (self, 0, extend);
		return TRUE;
	case GRL_KEY_END:
		move_cursor_to (self, n - 1, extend);
		return TRUE;
	case GRL_KEY_PAGE_UP:
		move_cursor_to (self, cur < 0 ? 0 : cur - page, extend);
		return TRUE;
	case GRL_KEY_PAGE_DOWN:
		move_cursor_to (self, cur < 0 ? MIN (page, n - 1) : cur + page, extend);
		return TRUE;
	case GRL_KEY_ENTER:
	case GRL_KEY_KP_ENTER:
		return lrg_file_browser_activate_cursor (self);
	case GRL_KEY_BACKSPACE:
		if (self->typeahead->len > 0)
		{
			g_string_truncate (self->typeahead, 0);
			return TRUE;
		}
		lrg_file_browser_go_up (self);
		return TRUE;
	case GRL_KEY_A:
		if (ctrl)
		{
			lrg_file_browser_select_all (self);
			return TRUE;
		}
		return FALSE;
	case GRL_KEY_SPACE:
		/* Part of a type-ahead name; otherwise toggles in multiple mode */
		if (self->typeahead->len > 0)
			return TRUE;
		if (self->mode == LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE && cur >= 0)
		{
			entry_at (self, cur)->selected = !entry_at (self, cur)->selected;
			emit_selection_changed (self);
			return TRUE;
		}
		return FALSE;
	default:
		return FALSE;
	}
}

static gboolean
lrg_file_browser_handle_event (LrgWidget        *widget,
                               const LrgUIEvent *event)
{
	LrgFileBrowser *self = LRG_FILE_BROWSER (widget);
	LrgWidget      *target = lrg_ui_event_get_target (event);
	gfloat          x = lrg_ui_event_get_x (event);
	gfloat          y = lrg_ui_event_get_y (event);
	gboolean        own = target == widget || target == self->list_zone || target == self->places_zone;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_POINTER_ENTER:
		lrg_scroll_fx_poke (&self->fx);
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		if (target == widget || target == self->list_zone || target == self->places_zone)
			track_hover (self, x, y);
		return FALSE;

	case LRG_UI_EVENT_MOUSE_MOVE:
		lrg_scroll_fx_poke (&self->fx);
		if (self->ay.dragging)
		{
			lrg_scroll_bar_drag (&self->ay, TRUE, y);
			return TRUE;
		}
		if (self->py.dragging)
		{
			lrg_scroll_bar_drag (&self->py, TRUE, y);
			return TRUE;
		}
		track_hover (self, x, y);
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			gint r;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			update_extent (self);
			track_hover (self, x, y);
			if (target == self->ay.bar || target == self->py.bar)
			{
				LrgScrollAxis *axis = target == self->ay.bar ? &self->ay : &self->py;

				lrg_scroll_bar_press (axis, TRUE, y, smooth (self));
				lrg_scroll_fx_poke (&self->fx);
				return TRUE;
			}
			if (nav_index (self, target) >= 0)
			{
				self->press_nav = nav_index (self, target);
				return TRUE;
			}
			if (target == self->places_zone)
			{
				self->press_place = place_at_point (self, x, y);
				return TRUE;
			}
			if (target != self->list_zone)
				return FALSE;
			if (self->editing)
				lrg_file_browser_set_editing_location (self, FALSE);
			if (header_at (self, x, y) >= 0)
			{
				self->press_header = header_at (self, x, y);
				return TRUE;
			}
			r = row_at (self, x, y);
			if (r < 0)
			{
				/* Empty space below the rows clears the selection */
				if (clear_selection (self))
					emit_selection_changed (self);
				return TRUE;
			}
			click_row (self, r, lrg_ui_event_get_modifiers (event));
			return TRUE;
		}

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (self->ay.dragging || self->py.dragging)
		{
			self->ay.dragging = FALSE;
			self->py.dragging = FALSE;
			return TRUE;
		}
		if (self->press_nav >= 0)
		{
			gint which = self->press_nav;

			self->press_nav = -1;
			if (lrg_widget_contains_point (self->nav[which], x, y))
				run_nav (self, which);
			return TRUE;
		}
		if (self->press_place >= 0 || target == self->places_zone)
		{
			gint p = self->press_place;

			self->press_place = -1;
			if (p >= 0 && place_at_point (self, x, y) == p)
				navigate (self, place_at (self, (guint) p)->location, HOW_PUSH);
			return TRUE;
		}
		if (self->press_header >= 0)
		{
			gint col = self->press_header;

			self->press_header = -1;
			if (header_at (self, x, y) == col)
				lrg_file_browser_set_sort (self, (LrgFileBrowserSort) col,
				                           (gint) self->sort_column == col ? !self->sort_ascending : TRUE);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_SCROLL:
		{
			gfloat   dy = -lrg_ui_event_get_scroll_y (event) * LRG_SCROLL_WHEEL_STEP;
			gboolean handled;

			update_extent (self);
			if (target == self->places_zone || target == self->py.bar)
				handled = lrg_scroll_axis_wheel (&self->py, dy, smooth (self));
			else if (target == self->list_zone || target == self->ay.bar)
				handled = lrg_scroll_axis_wheel (&self->ay, dy, smooth (self));
			else
				return FALSE;
			if (handled)
				lrg_scroll_fx_poke (&self->fx);
			return handled;
		}

	case LRG_UI_EVENT_PAN:
		update_extent (self);
		if (target == self->places_zone)
			return lrg_scroll_axis_pan (&self->py, lrg_ui_event_get_delta_y (event));
		if (target == self->list_zone)
			return lrg_scroll_axis_pan (&self->ay, lrg_ui_event_get_delta_y (event));
		return FALSE;

	case LRG_UI_EVENT_PAN_END:
		if (target != self->places_zone && target != self->list_zone)
			return FALSE;
		lrg_scroll_axis_flick (target == self->places_zone ? &self->py : &self->ay,
		                       smooth (self) ? lrg_ui_event_get_velocity_y (event) : 0.0f);
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		update_extent (self);
		return handle_key (self, lrg_ui_event_get_key (event), lrg_ui_event_get_modifiers (event), own);

	case LRG_UI_EVENT_TEXT_INPUT:
		{
			gunichar c = lrg_ui_event_get_codepoint (event);

			if (!own || (lrg_ui_event_get_modifiers (event) & (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_ALT)))
				return FALSE;
			/* "/" or "~" starts typing a path */
			if (self->typeahead->len == 0 && (c == '/' || c == '~'))
			{
				gchar buf[2] = { (gchar) c, '\0' };

				lrg_file_browser_set_editing_location (self, TRUE);
				lrg_text_input_set_text (self->path_entry, buf);
				lrg_text_input_set_cursor_position (self->path_entry, 1);
				return TRUE;
			}
			if (!g_unichar_isprint (c))
				return FALSE;
			typeahead (self, c);
			return TRUE;
		}

	case LRG_UI_EVENT_ACTIVATE:
		if (!own)
			return FALSE;
		return lrg_file_browser_activate_cursor (self);

	case LRG_UI_EVENT_CANCEL:
		if (self->editing)
		{
			lrg_file_browser_set_editing_location (self, FALSE);
			return TRUE;
		}
		return FALSE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

/*
 * create_children:
 *
 * Children, bottom to top in hit order: the sidebar and list zones,
 * the toolbar, the path widgets, the footer widgets and the scrollbar
 * zones on top. Created in init so every property setter can rely on
 * them, also at construction time.
 */
static void
create_children (LrgFileBrowser *self)
{
	LrgContainer       *c = LRG_CONTAINER (self);
	static const gchar *nav_names[NAV_COUNT] = { "back", "forward", "up", "edit-location", "show-hidden", "reload" };
	gint                i;

	self->in_layout = TRUE;
	self->places_zone = zone_new ("places");
	self->list_zone = zone_new ("list");
	lrg_container_add_child (c, self->places_zone);
	lrg_container_add_child (c, self->list_zone);
	for (i = 0; i < NAV_COUNT; i++)
	{
		self->nav[i] = zone_new (nav_names[i]);
		lrg_container_add_child (c, self->nav[i]);
	}

	self->crumb = lrg_breadcrumb_new ();
	lrg_widget_set_name (LRG_WIDGET (self->crumb), "breadcrumb");
	g_signal_connect_object (self->crumb, "activated", G_CALLBACK (on_crumb_activated), self, 0);
	lrg_container_add_child (c, LRG_WIDGET (self->crumb));

	self->path_entry = lrg_text_input_new_with_placeholder ("Type a path");
	lrg_widget_set_name (LRG_WIDGET (self->path_entry), "location-entry");
	lrg_widget_set_visible (LRG_WIDGET (self->path_entry), FALSE);
	g_signal_connect_object (self->path_entry, "submitted", G_CALLBACK (on_path_submitted), self, 0);
	lrg_container_add_child (c, LRG_WIDGET (self->path_entry));

	self->name_entry = lrg_text_input_new_with_placeholder ("File name");
	lrg_widget_set_name (LRG_WIDGET (self->name_entry), "name-entry");
	lrg_widget_set_visible (LRG_WIDGET (self->name_entry), FALSE);
	g_signal_connect_object (self->name_entry, "submitted", G_CALLBACK (on_name_submitted), self, 0);
	g_signal_connect_object (self->name_entry, "text-changed", G_CALLBACK (on_name_changed), self, 0);
	lrg_container_add_child (c, LRG_WIDGET (self->name_entry));

	self->filter_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->filter_combo), "filter-combo");
	lrg_combo_box_set_placeholder (self->filter_combo, "All files");
	lrg_widget_set_visible (LRG_WIDGET (self->filter_combo), FALSE);
	g_signal_connect_object (self->filter_combo, "changed", G_CALLBACK (on_filter_combo_changed), self, 0);
	lrg_container_add_child (c, LRG_WIDGET (self->filter_combo));

	self->ay.bar = lrg_scroll_bar_new ("vscrollbar");
	self->py.bar = lrg_scroll_bar_new ("places-scrollbar");
	lrg_container_add_child (c, self->ay.bar);
	lrg_container_add_child (c, self->py.bar);

	/* The container holds the children; drop the creation references
	 * but keep the pointers (they live as long as the browser) */
	g_object_unref (self->places_zone);
	g_object_unref (self->list_zone);
	for (i = 0; i < NAV_COUNT; i++)
		g_object_unref (self->nav[i]);
	g_object_unref (self->crumb);
	g_object_unref (self->path_entry);
	g_object_unref (self->name_entry);
	g_object_unref (self->filter_combo);
	g_object_unref (self->ay.bar);
	g_object_unref (self->py.bar);
	self->in_layout = FALSE;
}


static void
lrg_file_browser_constructed (GObject *object)
{
	LrgFileBrowser *self = LRG_FILE_BROWSER (object);

	G_OBJECT_CLASS (lrg_file_browser_parent_class)->constructed (object);

	/* Places: GIO volume monitor for mounts (also tracks changes) */
	if (self->show_system_places && self->volumes == NULL)
	{
		self->volumes = g_volume_monitor_get ();
		g_signal_connect_object (self->volumes, "mount-added", G_CALLBACK (on_mounts_changed), self, 0);
		g_signal_connect_object (self->volumes, "mount-removed", G_CALLBACK (on_mounts_changed), self, 0);
	}
	refresh_places (self);

	/* Start where the process runs unless a location was given */
	if (self->location == NULL)
	{
		g_autofree gchar *cwd = g_get_current_dir ();
		g_autoptr(GFile)  here = g_file_new_for_path (cwd);

		navigate (self, here, HOW_PUSH);
	}
	sync_filter_items (self);
	update_nav_zones (self);
}

static void
lrg_file_browser_dispose (GObject *object)
{
	LrgFileBrowser *self = LRG_FILE_BROWSER (object);

	if (self->load_cancel != NULL)
		g_cancellable_cancel (self->load_cancel);
	g_clear_object (&self->load_cancel);
	g_clear_handle_id (&self->reload_idle, g_source_remove);
	if (self->monitor != NULL)
	{
		g_signal_handlers_disconnect_by_data (self->monitor, self);
		g_file_monitor_cancel (self->monitor);
		g_clear_object (&self->monitor);
	}
	g_clear_object (&self->volumes);
	g_clear_object (&self->filter);
	g_clear_object (&self->filters);

	/* The scrollbar helper treats a NULL bar as "torn down" */
	self->ay.bar = NULL;
	self->py.bar = NULL;

	G_OBJECT_CLASS (lrg_file_browser_parent_class)->dispose (object);
}

static void
lrg_file_browser_finalize (GObject *object)
{
	LrgFileBrowser *self = LRG_FILE_BROWSER (object);

	g_clear_object (&self->location);
	g_clear_pointer (&self->back, g_ptr_array_unref);
	g_clear_pointer (&self->forward, g_ptr_array_unref);
	g_clear_pointer (&self->view, g_ptr_array_unref);
	g_clear_pointer (&self->all, g_ptr_array_unref);
	g_clear_pointer (&self->pending, g_hash_table_unref);
	g_free (self->pending_cursor);
	g_clear_pointer (&self->places, g_ptr_array_unref);
	g_clear_pointer (&self->side_rows, g_array_unref);
	g_clear_error (&self->error);
	if (self->typeahead != NULL)
		g_string_free (self->typeahead, TRUE);

	G_OBJECT_CLASS (lrg_file_browser_parent_class)->finalize (object);
}

static void
lrg_file_browser_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	LrgFileBrowser *self = LRG_FILE_BROWSER (object);

	switch (prop_id)
	{
	case PROP_MODE:
		g_value_set_enum (value, self->mode);
		break;
	case PROP_LOCATION:
		g_value_set_object (value, self->location);
		break;
	case PROP_SHOW_HIDDEN:
		g_value_set_boolean (value, self->show_hidden);
		break;
	case PROP_FOLDERS_FIRST:
		g_value_set_boolean (value, self->folders_first);
		break;
	case PROP_SORT_COLUMN:
		g_value_set_enum (value, self->sort_column);
		break;
	case PROP_SORT_ASCENDING:
		g_value_set_boolean (value, self->sort_ascending);
		break;
	case PROP_FILTER:
		g_value_set_object (value, self->filter);
		break;
	case PROP_FILTERS:
		g_value_set_object (value, self->filters);
		break;
	case PROP_CURRENT_NAME:
		g_value_set_string (value, self->name_entry != NULL ? lrg_text_input_get_text (self->name_entry) : "");
		break;
	case PROP_SHOW_SIDEBAR:
		g_value_set_boolean (value, self->show_sidebar);
		break;
	case PROP_SHOW_SYSTEM_PLACES:
		g_value_set_boolean (value, self->show_system_places);
		break;
	case PROP_SIDEBAR_WIDTH:
		g_value_set_float (value, self->sidebar_width);
		break;
	case PROP_ROW_HEIGHT:
		g_value_set_float (value, self->row_height);
		break;
	case PROP_EDITING_LOCATION:
		g_value_set_boolean (value, self->editing);
		break;
	case PROP_LOADING:
		g_value_set_boolean (value, self->loading);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_file_browser_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	LrgFileBrowser *self = LRG_FILE_BROWSER (object);

	switch (prop_id)
	{
	case PROP_MODE:
		lrg_file_browser_set_mode (self, g_value_get_enum (value));
		break;
	case PROP_LOCATION:
		if (g_value_get_object (value) != NULL)
			lrg_file_browser_set_location (self, g_value_get_object (value));
		break;
	case PROP_SHOW_HIDDEN:
		lrg_file_browser_set_show_hidden (self, g_value_get_boolean (value));
		break;
	case PROP_FOLDERS_FIRST:
		lrg_file_browser_set_folders_first (self, g_value_get_boolean (value));
		break;
	case PROP_SORT_COLUMN:
		lrg_file_browser_set_sort (self, g_value_get_enum (value), self->sort_ascending);
		break;
	case PROP_SORT_ASCENDING:
		lrg_file_browser_set_sort (self, self->sort_column, g_value_get_boolean (value));
		break;
	case PROP_FILTER:
		lrg_file_browser_set_filter (self, g_value_get_object (value));
		break;
	case PROP_CURRENT_NAME:
		lrg_file_browser_set_current_name (self, g_value_get_string (value));
		break;
	case PROP_SHOW_SIDEBAR:
		lrg_file_browser_set_show_sidebar (self, g_value_get_boolean (value));
		break;
	case PROP_SHOW_SYSTEM_PLACES:
		lrg_file_browser_set_show_system_places (self, g_value_get_boolean (value));
		break;
	case PROP_SIDEBAR_WIDTH:
		if (self->sidebar_width != g_value_get_float (value))
		{
			self->sidebar_width = g_value_get_float (value);
			lrg_widget_queue_layout (LRG_WIDGET (self));
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_ROW_HEIGHT:
		if (self->row_height != g_value_get_float (value))
		{
			self->row_height = g_value_get_float (value);
			layout_sidebar (self);
			lrg_widget_queue_layout (LRG_WIDGET (self));
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_EDITING_LOCATION:
		lrg_file_browser_set_editing_location (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_file_browser_class_init (LrgFileBrowserClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->constructed = lrg_file_browser_constructed;
	object_class->dispose = lrg_file_browser_dispose;
	object_class->finalize = lrg_file_browser_finalize;
	object_class->get_property = lrg_file_browser_get_property;
	object_class->set_property = lrg_file_browser_set_property;

	widget_class->measure = lrg_file_browser_measure;
	widget_class->paint = lrg_file_browser_paint;
	widget_class->style_defaults = lrg_file_browser_style_defaults;
	widget_class->step = lrg_file_browser_step;
	widget_class->handle_event = lrg_file_browser_handle_event;

	container_class->layout_children = lrg_file_browser_layout_children;

	/**
	 * LrgFileBrowser:mode:
	 *
	 * What the browser is for: opening one or several files, saving, or
	 * picking a folder.
	 */
	properties[PROP_MODE] =
		g_param_spec_enum ("mode", NULL, NULL, LRG_TYPE_FILE_BROWSER_MODE, LRG_FILE_BROWSER_MODE_OPEN,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:location:
	 *
	 * The folder being shown. Setting it navigates (with history).
	 */
	properties[PROP_LOCATION] =
		g_param_spec_object ("location", NULL, NULL, G_TYPE_FILE,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:show-hidden:
	 *
	 * Whether dot files, backup files and names listed in ".hidden" are
	 * shown.
	 */
	properties[PROP_SHOW_HIDDEN] =
		g_param_spec_boolean ("show-hidden", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:folders-first:
	 *
	 * Whether folders are kept above files whatever the sort order.
	 */
	properties[PROP_FOLDERS_FIRST] =
		g_param_spec_boolean ("folders-first", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:sort-column:
	 *
	 * The column the list is sorted by.
	 */
	properties[PROP_SORT_COLUMN] =
		g_param_spec_enum ("sort-column", NULL, NULL, LRG_TYPE_FILE_BROWSER_SORT, LRG_FILE_BROWSER_SORT_NAME,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:sort-ascending:
	 *
	 * Whether the sort order is ascending.
	 */
	properties[PROP_SORT_ASCENDING] =
		g_param_spec_boolean ("sort-ascending", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:filter:
	 *
	 * The filter files are matched against, %NULL for all files.
	 */
	properties[PROP_FILTER] =
		g_param_spec_object ("filter", NULL, NULL, LRG_TYPE_FILE_FILTER,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:filters:
	 *
	 * The #GListModel of #LrgFileFilter offered in the filter list.
	 */
	properties[PROP_FILTERS] =
		g_param_spec_object ("filters", NULL, NULL, G_TYPE_LIST_MODEL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:current-name:
	 *
	 * The text of the name field in %LRG_FILE_BROWSER_MODE_SAVE.
	 */
	properties[PROP_CURRENT_NAME] =
		g_param_spec_string ("current-name", NULL, NULL, "",
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:show-sidebar:
	 *
	 * Whether the places sidebar is shown.
	 */
	properties[PROP_SHOW_SIDEBAR] =
		g_param_spec_boolean ("show-sidebar", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:show-system-places:
	 *
	 * Whether home, the XDG user folders, the file system root, GTK
	 * bookmarks and mounted volumes are offered as places. Set it at
	 * construction to avoid touching the volume monitor at all.
	 */
	properties[PROP_SHOW_SYSTEM_PLACES] =
		g_param_spec_boolean ("show-system-places", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
		                      G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:sidebar-width:
	 *
	 * Width of the places sidebar (at most 40% of the browser).
	 */
	properties[PROP_SIDEBAR_WIDTH] =
		g_param_spec_float ("sidebar-width", NULL, NULL, 0.0f, G_MAXFLOAT, DEFAULT_SIDEBAR_WIDTH,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:row-height:
	 *
	 * Height of list rows and places, 0 for the theme's ROW_HEIGHT.
	 */
	properties[PROP_ROW_HEIGHT] =
		g_param_spec_float ("row-height", NULL, NULL, 0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:editing-location:
	 *
	 * Whether the path field replaces the breadcrumb.
	 */
	properties[PROP_EDITING_LOCATION] =
		g_param_spec_boolean ("editing-location", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileBrowser:loading:
	 *
	 * %TRUE while the folder is being enumerated.
	 */
	properties[PROP_LOADING] =
		g_param_spec_boolean ("loading", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgFileBrowser::file-activated:
	 * @self: the browser
	 * @file: the file
	 *
	 * Emitted when a file (not a folder: folders are entered) is double
	 * clicked or activated with Enter / gamepad A, when a file path is
	 * typed in the path field, and in %LRG_FILE_BROWSER_MODE_SAVE when
	 * Enter is pressed in the name field (@file is then
	 * lrg_file_browser_get_save_file()).
	 */
	signals[SIGNAL_FILE_ACTIVATED] =
		g_signal_new ("file-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_FILE);

	/**
	 * LrgFileBrowser::selection-changed:
	 * @self: the browser
	 *
	 * Emitted when rows are selected or unselected, by the user or by
	 * code (navigation clears the selection).
	 */
	signals[SIGNAL_SELECTION_CHANGED] =
		g_signal_new ("selection-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgFileBrowser::location-changed:
	 * @self: the browser
	 *
	 * Emitted after the browser moved to another folder; the new
	 * folder is #LrgFileBrowser:location and is now loading.
	 */
	signals[SIGNAL_LOCATION_CHANGED] =
		g_signal_new ("location-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/* The browser mirrors its own drawing; its zones are mirrored by
	 * the container machinery like any child */
}

static void
lrg_file_browser_init (LrgFileBrowser *self)
{
	self->mode = LRG_FILE_BROWSER_MODE_OPEN;
	self->folders_first = TRUE;
	self->sort_column = LRG_FILE_BROWSER_SORT_NAME;
	self->sort_ascending = TRUE;
	self->show_sidebar = TRUE;
	self->show_system_places = TRUE;
	self->sidebar_width = DEFAULT_SIDEBAR_WIDTH;
	self->filters = g_list_store_new (LRG_TYPE_FILE_FILTER);
	self->back = g_ptr_array_new_with_free_func (g_object_unref);
	self->forward = g_ptr_array_new_with_free_func (g_object_unref);
	self->all = g_ptr_array_new_with_free_func (entry_free);
	self->view = g_ptr_array_new ();
	self->pending = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->places = g_ptr_array_new_with_free_func (place_free);
	self->side_rows = g_array_new (FALSE, TRUE, sizeof (SideRow));
	self->typeahead = g_string_new (NULL);
	self->cursor = -1;
	self->anchor = -1;
	self->hover_row = -1;
	self->hover_header = -1;
	self->press_header = -1;
	self->hover_place = -1;
	self->press_place = -1;
	self->press_nav = -1;
	self->last_click_row = -1;
	self->typeahead_time = -10.0;

	lrg_spring_init (&self->hover_y, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->hover_a, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->sel_y, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->place_y, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->place_a, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->content, 1.0f, LRG_MOTION_SMOOTH);
	lrg_scroll_axis_init (&self->ay);
	lrg_scroll_axis_init (&self->py);
	lrg_scroll_fx_init (&self->fx);

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
	create_children (self);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgFileBrowser *
lrg_file_browser_new (void)
{
	return g_object_new (LRG_TYPE_FILE_BROWSER, NULL);
}

LrgFileBrowserMode
lrg_file_browser_get_mode (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), LRG_FILE_BROWSER_MODE_OPEN);

	return self->mode;
}

void
lrg_file_browser_set_mode (LrgFileBrowser     *self,
                           LrgFileBrowserMode  mode)
{
	gboolean changed = FALSE;
	guint    i;

	g_return_if_fail (LRG_IS_FILE_BROWSER (self));

	if (self->mode == mode)
		return;
	self->mode = mode;

	/* Single-selection modes keep only the cursor selected */
	if (mode != LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE)
	{
		for (i = 0; i < n_view (self); i++)
		{
			Entry *e = entry_at (self, i);

			if (e->selected && (gint) i != self->cursor)
			{
				e->selected = FALSE;
				changed = TRUE;
			}
		}
	}
	rebuild_view (self);
	if (changed)
		emit_selection_changed (self);

	lrg_widget_set_visible (LRG_WIDGET (self->name_entry), mode == LRG_FILE_BROWSER_MODE_SAVE);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

GFile *
lrg_file_browser_get_location (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), NULL);

	return self->location;
}

void
lrg_file_browser_set_location (LrgFileBrowser *self,
                               GFile          *location)
{
	g_return_if_fail (LRG_IS_FILE_BROWSER (self));
	g_return_if_fail (G_IS_FILE (location));

	navigate (self, location, HOW_PUSH);
}

void
lrg_file_browser_set_location_path (LrgFileBrowser *self,
                                    const gchar    *path)
{
	g_autoptr(GFile) file = NULL;

	g_return_if_fail (LRG_IS_FILE_BROWSER (self));
	g_return_if_fail (path != NULL);

	file = expand_path (self, path);
	if (file != NULL)
		navigate (self, file, HOW_PUSH);
}

gboolean
lrg_file_browser_go_back (LrgFileBrowser *self)
{
	g_autoptr(GFile) prev = NULL;

	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);

	if (self->back->len == 0)
		return FALSE;
	prev = g_ptr_array_steal_index (self->back, self->back->len - 1);
	navigate (self, prev, HOW_BACK);
	return TRUE;
}

gboolean
lrg_file_browser_go_forward (LrgFileBrowser *self)
{
	g_autoptr(GFile) next = NULL;

	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);

	if (self->forward->len == 0)
		return FALSE;
	next = g_ptr_array_steal_index (self->forward, self->forward->len - 1);
	navigate (self, next, HOW_FORWARD);
	return TRUE;
}

gboolean
lrg_file_browser_go_up (LrgFileBrowser *self)
{
	g_autoptr(GFile)  parent = NULL;
	g_autofree gchar *child = NULL;

	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);

	if (self->location == NULL)
		return FALSE;
	parent = g_file_get_parent (self->location);
	if (parent == NULL)
		return FALSE;

	/* Land on the folder we came out of */
	child = g_file_get_basename (self->location);
	navigate (self, parent, HOW_PUSH);
	if (child != NULL)
	{
		g_hash_table_add (self->pending, g_strdup (child));
		g_free (self->pending_cursor);
		self->pending_cursor = g_steal_pointer (&child);
	}
	return TRUE;
}

gboolean
lrg_file_browser_can_go_back (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);

	return self->back->len > 0;
}

gboolean
lrg_file_browser_can_go_forward (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);

	return self->forward->len > 0;
}

gboolean
lrg_file_browser_can_go_up (LrgFileBrowser *self)
{
	g_autoptr(GFile) parent = NULL;

	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);

	if (self->location == NULL)
		return FALSE;
	parent = g_file_get_parent (self->location);
	return parent != NULL;
}

void
lrg_file_browser_reload (LrgFileBrowser *self)
{
	g_return_if_fail (LRG_IS_FILE_BROWSER (self));

	g_clear_handle_id (&self->reload_idle, g_source_remove);
	load_start (self, TRUE);
}

gboolean
lrg_file_browser_get_loading (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);

	return self->loading;
}

const GError *
lrg_file_browser_get_error (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), NULL);

	return self->error;
}

gboolean
lrg_file_browser_get_show_hidden (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);

	return self->show_hidden;
}

void
lrg_file_browser_set_show_hidden (LrgFileBrowser *self,
                                  gboolean        show_hidden)
{
	g_return_if_fail (LRG_IS_FILE_BROWSER (self));

	show_hidden = !!show_hidden;
	if (self->show_hidden == show_hidden)
		return;
	self->show_hidden = show_hidden;
	rebuild_view (self);
	if (self->nav[NAV_HIDDEN] != NULL)
		update_nav_zones (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_HIDDEN]);
}

gboolean
lrg_file_browser_get_folders_first (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), TRUE);

	return self->folders_first;
}

void
lrg_file_browser_set_folders_first (LrgFileBrowser *self,
                                    gboolean        folders_first)
{
	g_return_if_fail (LRG_IS_FILE_BROWSER (self));

	folders_first = !!folders_first;
	if (self->folders_first == folders_first)
		return;
	self->folders_first = folders_first;
	rebuild_view (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOLDERS_FIRST]);
}

LrgFileBrowserSort
lrg_file_browser_get_sort_column (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), LRG_FILE_BROWSER_SORT_NAME);

	return self->sort_column;
}

gboolean
lrg_file_browser_get_sort_ascending (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), TRUE);

	return self->sort_ascending;
}

void
lrg_file_browser_set_sort (LrgFileBrowser     *self,
                           LrgFileBrowserSort  column,
                           gboolean            ascending)
{
	gboolean col_changed;
	gboolean dir_changed;

	g_return_if_fail (LRG_IS_FILE_BROWSER (self));
	g_return_if_fail (column <= LRG_FILE_BROWSER_SORT_MODIFIED);

	ascending = !!ascending;
	col_changed = self->sort_column != column;
	dir_changed = self->sort_ascending != ascending;
	if (!col_changed && !dir_changed)
		return;
	self->sort_column = column;
	self->sort_ascending = ascending;
	rebuild_view (self);
	if (self->cursor >= 0)
		scroll_to_row (self, self->cursor, TRUE);
	if (col_changed)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SORT_COLUMN]);
	if (dir_changed)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SORT_ASCENDING]);
}

void
lrg_file_browser_add_filter (LrgFileBrowser *self,
                             LrgFileFilter  *filter)
{
	g_return_if_fail (LRG_IS_FILE_BROWSER (self));
	g_return_if_fail (LRG_IS_FILE_FILTER (filter));

	if (g_list_store_find (self->filters, filter, NULL))
		return;
	g_list_store_append (self->filters, filter);
	sync_filter_items (self);
	if (self->filter == NULL)
		lrg_file_browser_set_filter (self, filter);
}

void
lrg_file_browser_remove_filter (LrgFileBrowser *self,
                                LrgFileFilter  *filter)
{
	guint pos;

	g_return_if_fail (LRG_IS_FILE_BROWSER (self));
	g_return_if_fail (LRG_IS_FILE_FILTER (filter));

	if (!g_list_store_find (self->filters, filter, &pos))
		return;
	g_list_store_remove (self->filters, pos);
	if (self->filter == filter)
	{
		g_autoptr(LrgFileFilter) first = g_list_model_get_item (G_LIST_MODEL (self->filters), 0);

		lrg_file_browser_set_filter (self, first);
	}
	sync_filter_items (self);
}

GListModel *
lrg_file_browser_get_filters (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), NULL);

	return G_LIST_MODEL (self->filters);
}

LrgFileFilter *
lrg_file_browser_get_filter (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), NULL);

	return self->filter;
}

/*
 * swap_extension:
 *
 * SAVE mode: when the typed name ends with the old filter's extension,
 * replace it with the new filter's ("map.png" -> "map.jpg").
 */
static void
swap_extension (LrgFileBrowser *self,
                LrgFileFilter  *old_filter,
                LrgFileFilter  *new_filter)
{
	g_autofree gchar *old_ext = old_filter != NULL ? lrg_file_filter_get_default_extension (old_filter) : NULL;
	g_autofree gchar *new_ext = new_filter != NULL ? lrg_file_filter_get_default_extension (new_filter) : NULL;
	g_autofree gchar *suffix = NULL;
	g_autofree gchar *name = NULL;
	const gchar      *text;

	if (self->mode != LRG_FILE_BROWSER_MODE_SAVE || old_ext == NULL || new_ext == NULL || self->name_entry == NULL)
		return;
	text = lrg_text_input_get_text (self->name_entry);
	suffix = g_strconcat (".", old_ext, NULL);
	if (text == NULL || !g_str_has_suffix (text, suffix) || strlen (text) == strlen (suffix))
		return;
	name = g_strdup_printf ("%.*s.%s", (gint) (strlen (text) - strlen (suffix)), text, new_ext);
	lrg_file_browser_set_current_name (self, name);
}

void
lrg_file_browser_set_filter (LrgFileBrowser *self,
                             LrgFileFilter  *filter)
{
	g_autoptr(LrgFileFilter) old = NULL;

	g_return_if_fail (LRG_IS_FILE_BROWSER (self));
	g_return_if_fail (filter == NULL || LRG_IS_FILE_FILTER (filter));

	if (self->filter == filter)
		return;
	if (filter != NULL && !g_list_store_find (self->filters, filter, NULL))
	{
		g_list_store_append (self->filters, filter);
		sync_filter_items (self);
	}

	old = self->filter != NULL ? g_object_ref (self->filter) : NULL;
	g_set_object (&self->filter, filter);
	rebuild_view (self);
	swap_extension (self, old, filter);
	sync_filter_selection (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTER]);
}

const gchar *
lrg_file_browser_get_current_name (LrgFileBrowser *self)
{
	const gchar *text;

	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), "");

	text = self->name_entry != NULL ? lrg_text_input_get_text (self->name_entry) : NULL;
	return text != NULL ? text : "";
}

void
lrg_file_browser_set_current_name (LrgFileBrowser *self,
                                   const gchar    *name)
{
	g_return_if_fail (LRG_IS_FILE_BROWSER (self));

	if (name == NULL)
		name = "";
	if (g_strcmp0 (lrg_file_browser_get_current_name (self), name) == 0)
		return;
	self->syncing = TRUE;
	lrg_text_input_set_text (self->name_entry, name);
	self->syncing = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_NAME]);
}

GFile *
lrg_file_browser_get_save_file (LrgFileBrowser *self)
{
	g_autofree gchar *name = NULL;
	g_autofree gchar *ext = NULL;
	const gchar      *base;
	const gchar      *dot;

	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), NULL);

	name = g_strstrip (g_strdup (lrg_file_browser_get_current_name (self)));
	if (name[0] == '\0' || self->location == NULL)
		return NULL;

	/* Add the filter's extension to a bare name ("save1" -> "save1.dat");
	 * a leading dot does not count as an extension */
	base = strrchr (name, '/');
	base = base != NULL ? base + 1 : name;
	dot = strrchr (base, '.');
	if ((dot == NULL || dot == base) && self->filter != NULL &&
	    (ext = lrg_file_filter_get_default_extension (self->filter)) != NULL)
	{
		gchar *with = g_strconcat (name, ".", ext, NULL);

		g_free (name);
		name = with;
	}
	return expand_path (self, name);
}

guint
lrg_file_browser_get_n_entries (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), 0);

	return n_view (self);
}

GFileInfo *
lrg_file_browser_get_entry_info (LrgFileBrowser *self,
                                 guint           index)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), NULL);

	if (index >= n_view (self))
		return NULL;
	return entry_at (self, index)->info;
}

GFile *
lrg_file_browser_get_entry_file (LrgFileBrowser *self,
                                 guint           index)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), NULL);

	if (index >= n_view (self) || self->location == NULL)
		return NULL;
	return entry_file (self, entry_at (self, index));
}

gint
lrg_file_browser_get_cursor (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), -1);

	return self->cursor;
}

gboolean
lrg_file_browser_select_file (LrgFileBrowser *self,
                              GFile          *file)
{
	g_autoptr(GFile)  parent = NULL;
	g_autofree gchar *base = NULL;
	gint              i;

	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	parent = g_file_get_parent (file);
	if (parent == NULL || self->location == NULL || !g_file_equal (parent, self->location))
		return FALSE;
	base = g_file_get_basename (file);
	i = view_index_of_name (self, base);
	if (i >= 0)
	{
		if (self->mode == LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE)
		{
			Entry *e = entry_at (self, i);

			if (!e->selected)
			{
				e->selected = TRUE;
				emit_selection_changed (self);
			}
			self->anchor = i;
			set_cursor (self, i, TRUE);
		}
		else
			select_only (self, i);
		return TRUE;
	}
	if (!self->loading)
		return FALSE;

	/* Not listed yet: select it when it arrives */
	if (self->mode != LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE)
		g_hash_table_remove_all (self->pending);
	g_hash_table_add (self->pending, g_strdup (base));
	g_free (self->pending_cursor);
	self->pending_cursor = g_steal_pointer (&base);
	return TRUE;
}

void
lrg_file_browser_select_all (LrgFileBrowser *self)
{
	gboolean changed = FALSE;
	guint    i;

	g_return_if_fail (LRG_IS_FILE_BROWSER (self));

	if (self->mode != LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE)
		return;
	for (i = 0; i < n_view (self); i++)
	{
		if (!entry_at (self, i)->selected)
		{
			entry_at (self, i)->selected = TRUE;
			changed = TRUE;
		}
	}
	if (changed)
		emit_selection_changed (self);
}

void
lrg_file_browser_unselect_all (LrgFileBrowser *self)
{
	g_return_if_fail (LRG_IS_FILE_BROWSER (self));

	if (clear_selection (self))
		emit_selection_changed (self);
}

GListModel *
lrg_file_browser_get_selected_files (LrgFileBrowser *self)
{
	GListStore *store;
	guint       i;

	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), NULL);

	store = g_list_store_new (G_TYPE_FILE);
	for (i = 0; i < n_view (self); i++)
	{
		Entry *e = entry_at (self, i);

		if (e->selected)
		{
			g_autoptr(GFile) f = entry_file (self, e);

			g_list_store_append (store, f);
		}
	}
	return G_LIST_MODEL (store);
}

GFile *
lrg_file_browser_get_selected_file (LrgFileBrowser *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), NULL);

	for (i = 0; i < n_view (self); i++)
	{
		if (entry_at (self, i)->selected)
			return entry_file (self, entry_at (self, i));
	}
	return NULL;
}

void
lrg_file_browser_add_place (LrgFileBrowser *self,
                            const gchar    *label,
                            GFile          *location,
                            LrgIcon         icon)
{
	guint i;
	guint n_app = 0;
	Place *p;

	g_return_if_fail (LRG_IS_FILE_BROWSER (self));
	g_return_if_fail (label != NULL);
	g_return_if_fail (G_IS_FILE (location));

	/* Replace an existing application place for the same folder */
	for (i = 0; i < self->places->len; i++)
	{
		p = place_at (self, i);
		if (p->section != SECTION_APP)
			break;
		n_app++;
		if (g_file_equal (p->location, location))
		{
			g_free (p->label);
			p->label = g_strdup (label);
			p->icon = icon;
			return;
		}
	}

	/* New ones go after the other application places */
	p = g_new0 (Place, 1);
	p->label = g_strdup (label);
	p->location = g_object_ref (location);
	p->icon = icon;
	p->section = SECTION_APP;
	g_ptr_array_insert (self->places, (gint) n_app, p);
	self->hover_place = -1;
	self->press_place = -1;
	layout_sidebar (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_file_browser_remove_place (LrgFileBrowser *self,
                               GFile          *location)
{
	guint i;

	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);
	g_return_val_if_fail (G_IS_FILE (location), FALSE);

	for (i = 0; i < self->places->len; i++)
	{
		Place *p = place_at (self, i);

		if (p->section == SECTION_APP && g_file_equal (p->location, location))
		{
			g_ptr_array_remove_index (self->places, i);
			self->hover_place = -1;
			self->press_place = -1;
			layout_sidebar (self);
			lrg_widget_queue_layout (LRG_WIDGET (self));
			return TRUE;
		}
	}
	return FALSE;
}

guint
lrg_file_browser_get_n_places (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), 0);

	return self->places->len;
}

const gchar *
lrg_file_browser_get_place_label (LrgFileBrowser *self,
                                  guint           index)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), NULL);

	return index < self->places->len ? place_at (self, index)->label : NULL;
}

GFile *
lrg_file_browser_get_place_location (LrgFileBrowser *self,
                                     guint           index)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), NULL);

	return index < self->places->len ? place_at (self, index)->location : NULL;
}

gboolean
lrg_file_browser_get_show_sidebar (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), TRUE);

	return self->show_sidebar;
}

void
lrg_file_browser_set_show_sidebar (LrgFileBrowser *self,
                                   gboolean        show_sidebar)
{
	g_return_if_fail (LRG_IS_FILE_BROWSER (self));

	show_sidebar = !!show_sidebar;
	if (self->show_sidebar == show_sidebar)
		return;
	self->show_sidebar = show_sidebar;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_SIDEBAR]);
}

gboolean
lrg_file_browser_get_show_system_places (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), TRUE);

	return self->show_system_places;
}

void
lrg_file_browser_set_show_system_places (LrgFileBrowser *self,
                                         gboolean        show_system_places)
{
	g_return_if_fail (LRG_IS_FILE_BROWSER (self));

	show_system_places = !!show_system_places;
	if (self->show_system_places == show_system_places)
		return;
	self->show_system_places = show_system_places;
	if (show_system_places && self->volumes == NULL)
	{
		self->volumes = g_volume_monitor_get ();
		g_signal_connect_object (self->volumes, "mount-added", G_CALLBACK (on_mounts_changed), self, 0);
		g_signal_connect_object (self->volumes, "mount-removed", G_CALLBACK (on_mounts_changed), self, 0);
	}
	refresh_places (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_SYSTEM_PLACES]);
}

gboolean
lrg_file_browser_get_editing_location (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);

	return self->editing;
}

void
lrg_file_browser_set_editing_location (LrgFileBrowser *self,
                                       gboolean        editing)
{
	LrgWidget *root;

	g_return_if_fail (LRG_IS_FILE_BROWSER (self));

	editing = !!editing;
	if (self->editing == editing)
		return;
	self->editing = editing;
	root = lrg_widget_get_root (LRG_WIDGET (self));

	if (editing)
	{
		/* Fill the field with the current path and focus it */
		g_autofree gchar *path = self->location != NULL ? g_file_get_parse_name (self->location) : g_strdup ("");
		g_autofree gchar *text = (path[0] != '\0' && !g_str_has_suffix (path, G_DIR_SEPARATOR_S))
		                         ? g_strconcat (path, G_DIR_SEPARATOR_S, NULL) : g_strdup (path);

		lrg_text_input_set_text (self->path_entry, text);
		lrg_text_input_set_cursor_position (self->path_entry, (gint) g_utf8_strlen (text, -1));
		lrg_widget_set_visible (LRG_WIDGET (self->path_entry), TRUE);
		lrg_widget_set_visible (LRG_WIDGET (self->crumb), FALSE);
		if (LRG_IS_CANVAS (root))
			lrg_canvas_set_focused_widget (LRG_CANVAS (root), LRG_WIDGET (self->path_entry));
	}
	else
	{
		gboolean had_focus = LRG_IS_CANVAS (root) &&
		                     lrg_canvas_get_focused_widget (LRG_CANVAS (root)) == LRG_WIDGET (self->path_entry);

		lrg_widget_set_visible (LRG_WIDGET (self->path_entry), FALSE);
		lrg_widget_set_visible (LRG_WIDGET (self->crumb), TRUE);
		if (had_focus)
			lrg_canvas_set_focused_widget (LRG_CANVAS (root), LRG_WIDGET (self));
	}
	update_nav_zones (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EDITING_LOCATION]);
}

gboolean
lrg_file_browser_activate_cursor (LrgFileBrowser *self)
{
	g_return_val_if_fail (LRG_IS_FILE_BROWSER (self), FALSE);

	if (self->cursor < 0 || (guint) self->cursor >= n_view (self))
		return FALSE;
	activate_row (self, self->cursor);
	return TRUE;
}
