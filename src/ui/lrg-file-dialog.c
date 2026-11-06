/* lrg-file-dialog.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFileDialog: asynchronous open / save / select-folder dialogs.
 *
 * Modelled on GTK 4's GtkFileDialog: the object only describes the
 * dialog (title, filters, initial folder ...); each operation is a
 * GTask that ends when the user picks something or backs out.
 *
 * Two backends:
 *
 *  - In-app (default): an LrgDialog presented on the canvas of the
 *    parent widget, holding an LrgFileBrowser and a Cancel / accept
 *    button row. The buttons live inside the dialog content rather than
 *    in LrgDialog's own button row because LrgDialog closes as soon as
 *    one of its buttons is clicked, and SAVE has to stay open while it
 *    asks before replacing a file (a second LrgDialog stacked on top).
 *
 *  - XDG desktop portal (prefer-native): OpenFile / SaveFile on
 *    org.freedesktop.portal.FileChooser over the session bus. The
 *    Response signal of the portal's Request object carries the result;
 *    we subscribe to the predicted request path before calling, so the
 *    answer cannot race past us. Any failure to reach the portal falls
 *    back to the in-app dialog.
 *
 * Every request keeps one reference on its GTask until it returns, and
 * returns exactly once ("done").
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <string.h>

#include "lrg-file-dialog.h"
#include "lrg-dialog.h"
#include "lrg-button.h"
#include "lrg-hbox.h"
#include "lrg-vbox.h"
#include "lrg-canvas.h"
#include "lrg-text-input.h"
#include "../lrg-log.h"

#define PORTAL_BUS_NAME     "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH  "/org/freedesktop/portal/desktop"
#define PORTAL_CHOOSER_IFACE "org.freedesktop.portal.FileChooser"
#define PORTAL_REQUEST_IFACE "org.freedesktop.portal.Request"

/* Response id of the "Replace" button of the overwrite question */
#define RESPONSE_REPLACE (1)

/* Largest in-app browser, and the room left around it on the canvas */
#define BROWSER_MAX_WIDTH  (780.0f)
#define BROWSER_MAX_HEIGHT (470.0f)
#define CANVAS_MARGIN_X    (96.0f)
#define CANVAS_MARGIN_Y    (200.0f)

G_DEFINE_QUARK (lrg-file-dialog-error-quark, lrg_file_dialog_error)

GType
lrg_file_dialog_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_FILE_DIALOG_ERROR_FAILED, "LRG_FILE_DIALOG_ERROR_FAILED", "failed" },
			{ LRG_FILE_DIALOG_ERROR_CANCELLED, "LRG_FILE_DIALOG_ERROR_CANCELLED", "cancelled" },
			{ LRG_FILE_DIALOG_ERROR_DISMISSED, "LRG_FILE_DIALOG_ERROR_DISMISSED", "dismissed" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgFileDialogError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* An application place for the in-app sidebar */
typedef struct
{
	gchar   *label;
	GFile   *location;
	LrgIcon  icon;
} DialogPlace;

struct _LrgFileDialog
{
	GObject         parent_instance;

	gchar          *title;
	gchar          *accept_label;
	GFile          *initial_folder;
	gchar          *initial_name;
	GListModel     *filters;
	LrgFileFilter  *default_filter;
	gboolean        modal;
	gboolean        prefer_native;
	gboolean        show_system_places;
	GPtrArray      *places;     /* DialogPlace * */
};

G_DEFINE_FINAL_TYPE (LrgFileDialog, lrg_file_dialog, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_TITLE,
	PROP_ACCEPT_LABEL,
	PROP_INITIAL_FOLDER,
	PROP_INITIAL_NAME,
	PROP_FILTERS,
	PROP_DEFAULT_FILTER,
	PROP_MODAL,
	PROP_PREFER_NATIVE,
	PROP_SHOW_SYSTEM_PLACES,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Requests
 * ========================================================================== */

/* One running operation, the task data of its GTask */
typedef struct
{
	GTask              *task;          /* the owning task (not a reference);
	                                    * every signal handler uses it as data */
	LrgFileDialog      *dialog;        /* ref */
	LrgFileBrowserMode  mode;
	LrgWidget          *parent;        /* ref, nullable */
	gboolean            done;
	gulong              cancel_id;
	guint               cancel_idle;

	/* In-app */
	LrgCanvas          *canvas;        /* weak */
	LrgDialog          *sheet;         /* ref */
	LrgFileBrowser     *browser;       /* owned by the sheet */
	LrgButton          *accept_button; /* owned by the sheet */
	LrgButton          *cancel_button; /* owned by the sheet */
	LrgDialog          *confirm;       /* ref, the overwrite question */
	GFile              *confirm_file;

	/* Portal */
	GDBusConnection    *bus;           /* ref */
	gchar              *handle;
	guint               response_sub;
} Request;

static void
dialog_place_free (gpointer data)
{
	DialogPlace *p = data;

	g_free (p->label);
	g_clear_object (&p->location);
	g_free (p);
}

/* sheet_disconnect: stop listening to the in-app widgets */
static void
sheet_disconnect (Request *req)
{
	if (req->sheet != NULL)
		g_signal_handlers_disconnect_by_data (req->sheet, req->task);
	if (req->browser != NULL)
		g_signal_handlers_disconnect_by_data (req->browser, req->task);
	if (req->accept_button != NULL)
		g_signal_handlers_disconnect_by_data (req->accept_button, req->task);
	if (req->cancel_button != NULL)
		g_signal_handlers_disconnect_by_data (req->cancel_button, req->task);
	if (req->confirm != NULL)
		g_signal_handlers_disconnect_by_data (req->confirm, req->task);
}

static void
request_free (gpointer data)
{
	Request *req = data;

	sheet_disconnect (req);
	if (req->bus != NULL && req->response_sub != 0)
		g_dbus_connection_signal_unsubscribe (req->bus, req->response_sub);
	g_clear_handle_id (&req->cancel_idle, g_source_remove);
	g_clear_weak_pointer (&req->canvas);
	g_clear_object (&req->sheet);
	g_clear_object (&req->confirm);
	g_clear_object (&req->confirm_file);
	g_clear_object (&req->parent);
	g_clear_object (&req->bus);
	g_clear_object (&req->dialog);
	g_free (req->handle);
	g_free (req);
}

/*
 * request_end:
 *
 * Common tail of every outcome: closes whatever is still showing,
 * stops listening, then drops the request's own reference on @task
 * (taken in start_request()) after the caller returned its result.
 * Returns FALSE when the request had already ended.
 */
static gboolean
request_end (GTask *task)
{
	Request      *req = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);

	if (req->done)
		return FALSE;
	req->done = TRUE;

	if (req->cancel_id != 0 && cancellable != NULL)
		g_cancellable_disconnect (cancellable, req->cancel_id);
	req->cancel_id = 0;
	sheet_disconnect (req);

	/* Close the overwrite question first, then the file dialog */
	if (req->confirm != NULL && lrg_dialog_get_presented (req->confirm))
		lrg_dialog_close (req->confirm);
	if (req->sheet != NULL && lrg_dialog_get_presented (req->sheet))
		lrg_dialog_close (req->sheet);

	if (req->bus != NULL && req->response_sub != 0)
	{
		g_dbus_connection_signal_unsubscribe (req->bus, req->response_sub);
		req->response_sub = 0;
	}
	return TRUE;
}

/* request_fail: end with an error in our domain */
static void
request_fail (GTask       *task,
              gint         code,
              const gchar *message)
{
	if (!request_end (task))
		return;
	g_task_return_new_error (task, LRG_FILE_DIALOG_ERROR, code, "%s", message);
	g_object_unref (task);
}

/* request_succeed_file: end with one file */
static void
request_succeed_file (GTask *task,
                      GFile *file)
{
	if (!request_end (task))
		return;
	g_task_return_pointer (task, g_object_ref (file), g_object_unref);
	g_object_unref (task);
}

/* request_succeed_list: end with a list of files */
static void
request_succeed_list (GTask      *task,
                      GListModel *files)
{
	if (!request_end (task))
		return;
	g_task_return_pointer (task, g_object_ref (files), g_object_unref);
	g_object_unref (task);
}

/* ==========================================================================
 * Cancellation
 * ========================================================================== */

/* portal_close_request: ask the portal to take its dialog down */
static void
portal_close_request (Request *req)
{
	if (req->bus == NULL || req->handle == NULL)
		return;
	g_dbus_connection_call (req->bus, PORTAL_BUS_NAME, req->handle, PORTAL_REQUEST_IFACE, "Close",
	                        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static gboolean
cancel_idle_cb (gpointer user_data)
{
	GTask   *task = user_data;
	Request *req = g_task_get_task_data (task);

	req->cancel_idle = 0;
	portal_close_request (req);
	request_fail (task, LRG_FILE_DIALOG_ERROR_CANCELLED, "The file dialog was cancelled");
	return G_SOURCE_REMOVE;
}

/*
 * on_cancelled:
 *
 * The GCancellable fired. Disconnecting from inside this handler would
 * deadlock, so the request ends from an idle callback on the task's
 * context.
 */
static void
on_cancelled (GCancellable *cancellable,
              gpointer      user_data)
{
	GTask   *task = user_data;
	Request *req = g_task_get_task_data (task);
	GSource *source;

	if (req->done || req->cancel_idle != 0)
		return;
	source = g_idle_source_new ();
	g_source_set_callback (source, cancel_idle_cb, task, NULL);
	req->cancel_idle = g_source_attach (source, g_task_get_context (task));
	g_source_unref (source);
}

/* ==========================================================================
 * In-app backend
 * ========================================================================== */

static const gchar *
default_title (LrgFileBrowserMode mode)
{
	switch (mode)
	{
	case LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE:
		return "Open Files";
	case LRG_FILE_BROWSER_MODE_SAVE:
		return "Save File";
	case LRG_FILE_BROWSER_MODE_SELECT_FOLDER:
		return "Select Folder";
	case LRG_FILE_BROWSER_MODE_OPEN:
	default:
		return "Open File";
	}
}

static const gchar *
default_accept (LrgFileBrowserMode mode)
{
	switch (mode)
	{
	case LRG_FILE_BROWSER_MODE_SAVE:
		return "Save";
	case LRG_FILE_BROWSER_MODE_SELECT_FOLDER:
		return "Select";
	case LRG_FILE_BROWSER_MODE_OPEN:
	case LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE:
	default:
		return "Open";
	}
}

/* is_folder: local, synchronous type check (stat) */
static gboolean
is_folder (GFile *file)
{
	return g_file_query_file_type (file, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY;
}

static void on_confirm_response (LrgDialog *confirm, gint response, gpointer user_data);

/*
 * save_with_confirm:
 *
 * SAVE: a folder name enters the folder; an existing file asks before
 * replacing it (a second dialog stacked on the file dialog); anything
 * else is the answer.
 */
static void
save_with_confirm (GTask *task,
                   GFile *file)
{
	Request          *req = g_task_get_task_data (task);
	g_autoptr(GFile)  parent = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *folder = NULL;
	g_autofree gchar *title = NULL;
	g_autofree gchar *body = NULL;
	LrgButton        *replace;

	if (is_folder (file))
	{
		lrg_file_browser_set_current_name (req->browser, "");
		lrg_file_browser_set_location (req->browser, file);
		return;
	}
	if (!g_file_query_exists (file, NULL) || req->canvas == NULL)
	{
		request_succeed_file (task, file);
		return;
	}

	/* Ask; the file dialog stays open underneath */
	base = g_file_get_basename (file);
	parent = g_file_get_parent (file);
	folder = parent != NULL ? g_file_get_basename (parent) : g_strdup ("");
	title = g_strdup_printf ("Replace \xe2\x80\x9c%s\xe2\x80\x9d?", base);
	body = g_strdup_printf ("A file with this name already exists in \xe2\x80\x9c%s\xe2\x80\x9d. "
	                        "Replacing it overwrites its contents.", folder);
	if (req->confirm != NULL)
		g_signal_handlers_disconnect_by_data (req->confirm, task);
	g_clear_object (&req->confirm);
	g_set_object (&req->confirm_file, file);
	req->confirm = lrg_dialog_new (title, body);
	lrg_widget_set_name (LRG_WIDGET (req->confirm), "file-dialog-confirm");
	lrg_dialog_add_button (req->confirm, "Cancel", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	replace = lrg_dialog_add_button (req->confirm, "Replace", RESPONSE_REPLACE, TRUE);
	lrg_widget_add_class (LRG_WIDGET (replace), "danger");
	lrg_widget_set_name (LRG_WIDGET (replace), "replace-button");
	g_signal_connect (req->confirm, "response", G_CALLBACK (on_confirm_response), task);
	lrg_dialog_present (req->confirm, req->canvas);
}

static void
on_confirm_response (LrgDialog *confirm,
                     gint       response,
                     gpointer   user_data)
{
	GTask   *task = user_data;
	Request *req = g_task_get_task_data (task);

	if (response == RESPONSE_REPLACE && req->confirm_file != NULL)
	{
		g_autoptr(GFile) file = g_object_ref (req->confirm_file);

		request_succeed_file (task, file);
	}
	/* Otherwise back to the file dialog, which is still open */
}

/*
 * accept:
 *
 * The accept button. What counts as an answer depends on the mode; a
 * selected folder in an open mode is entered instead.
 */
static void
accept (GTask *task)
{
	Request          *req = g_task_get_task_data (task);
	g_autoptr(GFile)  file = NULL;
	g_autoptr(GListModel) selected = NULL;
	g_autoptr(GListStore) files = NULL;
	guint             i;
	guint             n;

	switch (req->mode)
	{
	case LRG_FILE_BROWSER_MODE_OPEN:
		file = lrg_file_browser_get_selected_file (req->browser);
		if (file == NULL)
			return;
		if (is_folder (file))
			lrg_file_browser_set_location (req->browser, file);
		else
			request_succeed_file (task, file);
		return;

	case LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE:
		selected = lrg_file_browser_get_selected_files (req->browser);
		n = g_list_model_get_n_items (selected);
		files = g_list_store_new (G_TYPE_FILE);
		for (i = 0; i < n; i++)
		{
			g_autoptr(GFile) f = g_list_model_get_item (selected, i);

			if (!is_folder (f))
				g_list_store_append (files, f);
		}
		if (g_list_model_get_n_items (G_LIST_MODEL (files)) > 0)
		{
			request_succeed_list (task, G_LIST_MODEL (files));
			return;
		}
		/* Only folders: enter the one under the cursor */
		if (n == 1)
		{
			g_autoptr(GFile) f = g_list_model_get_item (selected, 0);

			lrg_file_browser_set_location (req->browser, f);
		}
		return;

	case LRG_FILE_BROWSER_MODE_SAVE:
		file = lrg_file_browser_get_save_file (req->browser);
		if (file != NULL)
			save_with_confirm (task, file);
		return;

	case LRG_FILE_BROWSER_MODE_SELECT_FOLDER:
		file = lrg_file_browser_get_selected_file (req->browser);
		if (file == NULL)
			file = g_object_ref (lrg_file_browser_get_location (req->browser));
		request_succeed_file (task, file);
		return;

	default:
		return;
	}
}

static void
on_accept_clicked (LrgButton *button,
                   gpointer   user_data)
{
	accept (G_TASK (user_data));
}

static void
on_cancel_clicked (LrgButton *button,
                   gpointer   user_data)
{
	request_fail (G_TASK (user_data), LRG_FILE_DIALOG_ERROR_DISMISSED, "The file dialog was dismissed");
}

/* on_file_activated: double click / Enter on a file, Enter in the name field */
static void
on_file_activated (LrgFileBrowser *browser,
                   GFile          *file,
                   gpointer        user_data)
{
	GTask   *task = user_data;
	Request *req = g_task_get_task_data (task);

	switch (req->mode)
	{
	case LRG_FILE_BROWSER_MODE_OPEN:
		request_succeed_file (task, file);
		break;

	case LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE:
		{
			g_autoptr(GListModel) selected = lrg_file_browser_get_selected_files (browser);
			g_autoptr(GListStore) files = g_list_store_new (G_TYPE_FILE);
			gboolean              inside = FALSE;
			guint                 i;

			/* The activated file plus the rest of the selection it is part of */
			for (i = 0; i < g_list_model_get_n_items (selected); i++)
			{
				g_autoptr(GFile) f = g_list_model_get_item (selected, i);

				inside |= g_file_equal (f, file);
				if (!is_folder (f))
					g_list_store_append (files, f);
			}
			if (!inside)
			{
				g_list_store_remove_all (files);
				g_list_store_append (files, file);
			}
			request_succeed_list (task, G_LIST_MODEL (files));
			break;
		}

	case LRG_FILE_BROWSER_MODE_SAVE:
		save_with_confirm (task, file);
		break;

	case LRG_FILE_BROWSER_MODE_SELECT_FOLDER:
	default:
		break;
	}
}

/* on_sheet_response: Escape / gamepad B on the dialog */
static void
on_sheet_response (LrgDialog *sheet,
                   gint       response,
                   gpointer   user_data)
{
	request_fail (G_TASK (user_data), LRG_FILE_DIALOG_ERROR_DISMISSED, "The file dialog was dismissed");
}

/* on_sheet_presented: closed some other way (the canvas dismissed it) */
static void
on_sheet_presented (GObject    *sheet,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
	if (!lrg_dialog_get_presented (LRG_DIALOG (sheet)))
		request_fail (G_TASK (user_data), LRG_FILE_DIALOG_ERROR_DISMISSED, "The file dialog was dismissed");
}

/* find_canvas: the canvas @parent lives on, or NULL */
static LrgCanvas *
find_canvas (LrgWidget *parent)
{
	LrgWidget *root;

	if (parent == NULL)
		return NULL;
	root = lrg_widget_get_root (parent);
	return LRG_IS_CANVAS (root) ? LRG_CANVAS (root) : NULL;
}

/*
 * show_in_app:
 *
 * Builds and presents the in-app dialog: a browser configured from the
 * dialog's properties above a Cancel / accept row.
 */
static void
show_in_app (GTask *task)
{
	Request          *req = g_task_get_task_data (task);
	LrgFileDialog    *self = req->dialog;
	LrgCanvas        *canvas = find_canvas (req->parent);
	LrgVBox          *content;
	LrgHBox          *row;
	LrgButton        *cancel;
	LrgButton        *ok;
	LrgWidget        *focus;
	gfloat            bw;
	gfloat            bh;
	guint             i;

	if (canvas == NULL)
	{
		request_fail (task, LRG_FILE_DIALOG_ERROR_FAILED,
		              "No canvas to show the file dialog on (pass a widget inside an LrgCanvas)");
		return;
	}
	g_set_weak_pointer (&req->canvas, canvas);

	/* 1. The browser, sized to the canvas */
	req->browser = g_object_new (LRG_TYPE_FILE_BROWSER,
	                             "mode", req->mode,
	                             "show-system-places", self->show_system_places,
	                             "location", self->initial_folder,
	                             NULL);
	lrg_widget_set_name (LRG_WIDGET (req->browser), "file-browser");
	for (i = 0; i < self->places->len; i++)
	{
		DialogPlace *p = g_ptr_array_index (self->places, i);

		lrg_file_browser_add_place (req->browser, p->label, p->location, p->icon);
	}
	if (self->filters != NULL)
	{
		for (i = 0; i < g_list_model_get_n_items (self->filters); i++)
		{
			g_autoptr(LrgFileFilter) f = g_list_model_get_item (self->filters, i);

			lrg_file_browser_add_filter (req->browser, f);
		}
	}
	if (self->default_filter != NULL)
		lrg_file_browser_set_filter (req->browser, self->default_filter);
	if (req->mode == LRG_FILE_BROWSER_MODE_SAVE && self->initial_name != NULL)
		lrg_file_browser_set_current_name (req->browser, self->initial_name);
	bw = MIN (BROWSER_MAX_WIDTH, MAX (320.0f, lrg_widget_get_width (LRG_WIDGET (canvas)) - CANVAS_MARGIN_X));
	bh = MIN (BROWSER_MAX_HEIGHT, MAX (220.0f, lrg_widget_get_height (LRG_WIDGET (canvas)) - CANVAS_MARGIN_Y));
	lrg_widget_set_min_size (LRG_WIDGET (req->browser), bw, bh);
	lrg_widget_set_max_size (LRG_WIDGET (req->browser), bw, bh);
	g_signal_connect (req->browser, "file-activated", G_CALLBACK (on_file_activated), task);

	/* 2. Cancel / accept row at the end */
	row = lrg_hbox_new ();
	lrg_widget_set_halign (LRG_WIDGET (row), LRG_ALIGN_END);
	lrg_container_set_spacing (LRG_CONTAINER (row), 8.0f);
	cancel = lrg_button_new ("Cancel");
	lrg_widget_set_name (LRG_WIDGET (cancel), "cancel-button");
	lrg_widget_set_min_size (LRG_WIDGET (cancel), 96.0f, 0.0f);
	ok = lrg_button_new (self->accept_label != NULL ? self->accept_label : default_accept (req->mode));
	lrg_widget_set_name (LRG_WIDGET (ok), "accept-button");
	lrg_widget_add_class (LRG_WIDGET (ok), "primary");
	lrg_widget_set_min_size (LRG_WIDGET (ok), 96.0f, 0.0f);
	g_signal_connect (cancel, "clicked", G_CALLBACK (on_cancel_clicked), task);
	g_signal_connect (ok, "clicked", G_CALLBACK (on_accept_clicked), task);
	req->cancel_button = cancel;
	req->accept_button = ok;
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (cancel));
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (ok));
	g_object_unref (cancel);
	g_object_unref (ok);

	content = lrg_vbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (content), 12.0f);
	lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (req->browser));
	lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (row));
	g_object_unref (req->browser);
	g_object_unref (row);

	/* 3. The sheet */
	req->sheet = lrg_dialog_new (self->title != NULL ? self->title : default_title (req->mode), NULL);
	lrg_widget_set_name (LRG_WIDGET (req->sheet), "file-dialog");
	lrg_dialog_set_content (req->sheet, LRG_WIDGET (content));
	g_object_unref (content);
	g_signal_connect (req->sheet, "response", G_CALLBACK (on_sheet_response), task);
	lrg_dialog_present (req->sheet, canvas);
	g_signal_connect (req->sheet, "notify::presented", G_CALLBACK (on_sheet_presented), task);

	/* 4. Typing goes to the name field when saving, else to the list */
	focus = req->mode == LRG_FILE_BROWSER_MODE_SAVE ? lrg_widget_find (LRG_WIDGET (req->browser), "name-entry")
	                                                : LRG_WIDGET (req->browser);
	if (focus != NULL)
		lrg_canvas_set_focused_widget (canvas, focus);

	/* Saving: the base name is selected, so typing replaces it and
	 * keeps the extension */
	if (req->mode == LRG_FILE_BROWSER_MODE_SAVE && focus != NULL && LRG_IS_TEXT_INPUT (focus))
	{
		const gchar *name = lrg_text_input_get_text (LRG_TEXT_INPUT (focus));
		const gchar *dot = name != NULL ? strrchr (name, '.') : NULL;
		glong        len = name != NULL ? g_utf8_strlen (name, -1) : 0;

		if (dot != NULL && dot != name)
			len = g_utf8_pointer_to_offset (name, dot);
		lrg_text_input_set_selection (LRG_TEXT_INPUT (focus), 0, (gint) len);
	}
}

/* ==========================================================================
 * Portal backend
 * ========================================================================== */

/* filter_to_variant: (sa(us)) — name and (0 glob | 1 mime) rules */
static GVariant *
filter_to_variant (LrgFileFilter *filter)
{
	GVariantBuilder      rules;
	const gchar * const *v;
	const gchar         *name = lrg_file_filter_get_name (filter);
	guint                i;

	g_variant_builder_init (&rules, G_VARIANT_TYPE ("a(us)"));
	v = lrg_file_filter_get_patterns (filter);
	for (i = 0; v[i] != NULL; i++)
		g_variant_builder_add (&rules, "(us)", 0u, v[i]);
	v = lrg_file_filter_get_suffixes (filter);
	for (i = 0; v[i] != NULL; i++)
	{
		g_autofree gchar *glob = g_strconcat ("*.", v[i], NULL);

		g_variant_builder_add (&rules, "(us)", 0u, glob);
	}
	v = lrg_file_filter_get_mime_types (filter);
	for (i = 0; v[i] != NULL; i++)
		g_variant_builder_add (&rules, "(us)", 1u, v[i]);
	return g_variant_new ("(sa(us))", name != NULL ? name : "", &rules);
}

/* portal_options: the a{sv} for OpenFile / SaveFile */
static GVariant *
portal_options (Request     *req,
                const gchar *token)
{
	LrgFileDialog   *self = req->dialog;
	GVariantBuilder  opts;
	gboolean         folder = req->mode == LRG_FILE_BROWSER_MODE_SELECT_FOLDER;
	guint            i;

	g_variant_builder_init (&opts, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&opts, "{sv}", "handle_token", g_variant_new_string (token));
	g_variant_builder_add (&opts, "{sv}", "modal", g_variant_new_boolean (self->modal));
	if (self->accept_label != NULL)
		g_variant_builder_add (&opts, "{sv}", "accept_label", g_variant_new_string (self->accept_label));
	if (req->mode == LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE)
		g_variant_builder_add (&opts, "{sv}", "multiple", g_variant_new_boolean (TRUE));
	if (folder)
		g_variant_builder_add (&opts, "{sv}", "directory", g_variant_new_boolean (TRUE));

	if (!folder && self->filters != NULL && g_list_model_get_n_items (self->filters) > 0)
	{
		GVariantBuilder list;

		g_variant_builder_init (&list, G_VARIANT_TYPE ("a(sa(us))"));
		for (i = 0; i < g_list_model_get_n_items (self->filters); i++)
		{
			g_autoptr(LrgFileFilter) f = g_list_model_get_item (self->filters, i);

			g_variant_builder_add_value (&list, filter_to_variant (f));
		}
		g_variant_builder_add (&opts, "{sv}", "filters", g_variant_builder_end (&list));
		if (self->default_filter != NULL)
			g_variant_builder_add (&opts, "{sv}", "current_filter", filter_to_variant (self->default_filter));
	}
	if (req->mode == LRG_FILE_BROWSER_MODE_SAVE && self->initial_name != NULL)
		g_variant_builder_add (&opts, "{sv}", "current_name", g_variant_new_string (self->initial_name));
	if (self->initial_folder != NULL)
	{
		g_autofree gchar *path = g_file_get_path (self->initial_folder);

		/* A NUL-terminated byte string, as the portal expects */
		if (path != NULL)
			g_variant_builder_add (&opts, "{sv}", "current_folder", g_variant_new_bytestring (path));
	}
	return g_variant_builder_end (&opts);
}

/*
 * on_portal_response:
 *
 * org.freedesktop.portal.Request::Response (u response, a{sv} results):
 * 0 = success with "uris", 1 = the user cancelled, 2 = other failure.
 */
static void
on_portal_response (GDBusConnection *bus,
                    const gchar     *sender,
                    const gchar     *path,
                    const gchar     *iface,
                    const gchar     *signal,
                    GVariant        *parameters,
                    gpointer         user_data)
{
	GTask                *task = user_data;
	Request              *req = g_task_get_task_data (task);
	guint32               response = 2;
	g_autoptr(GVariant)   results = NULL;
	g_autofree const gchar **uris = NULL;
	g_autoptr(GListStore) files = NULL;
	guint                 i;

	if (req->done || !g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(ua{sv})")))
		return;
	g_variant_get (parameters, "(u@a{sv})", &response, &results);

	if (response == 1)
	{
		request_fail (task, LRG_FILE_DIALOG_ERROR_DISMISSED, "The file dialog was dismissed");
		return;
	}
	if (response != 0 || !g_variant_lookup (results, "uris", "^a&s", &uris) || uris == NULL || uris[0] == NULL)
	{
		request_fail (task, LRG_FILE_DIALOG_ERROR_FAILED, "The desktop file chooser failed");
		return;
	}

	files = g_list_store_new (G_TYPE_FILE);
	for (i = 0; uris[i] != NULL; i++)
	{
		g_autoptr(GFile) f = g_file_new_for_uri (uris[i]);

		g_list_store_append (files, f);
	}
	if (req->mode == LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE)
		request_succeed_list (task, G_LIST_MODEL (files));
	else
	{
		g_autoptr(GFile) first = g_list_model_get_item (G_LIST_MODEL (files), 0);

		request_succeed_file (task, first);
	}
}

/* subscribe_response: listen for Response on @handle */
static void
subscribe_response (GTask       *task,
                    const gchar *handle)
{
	Request *req = g_task_get_task_data (task);

	if (req->response_sub != 0)
		g_dbus_connection_signal_unsubscribe (req->bus, req->response_sub);
	g_free (req->handle);
	req->handle = g_strdup (handle);
	req->response_sub = g_dbus_connection_signal_subscribe (req->bus, NULL, PORTAL_REQUEST_IFACE, "Response",
	                                                        handle, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
	                                                        on_portal_response, task, NULL);
}

static void
on_portal_called (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
	GTask              *task = user_data;
	Request            *req = g_task_get_task_data (task);
	g_autoptr(GError)   error = NULL;
	g_autoptr(GVariant) reply = NULL;
	const gchar        *handle = NULL;

	reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
	if (req->done)
	{
		/* Ended meanwhile (cancelled); drop the call's reference */
		g_object_unref (task);
		return;
	}
	if (reply == NULL)
	{
		/* No portal (or it refused): use the in-app dialog */
		lrg_debug (LRG_LOG_DOMAIN_UI, "file dialog: portal unavailable (%s), using the in-app dialog",
		           error->message);
		if (req->response_sub != 0)
		{
			g_dbus_connection_signal_unsubscribe (req->bus, req->response_sub);
			req->response_sub = 0;
		}
		g_clear_pointer (&req->handle, g_free);
		show_in_app (task);
		g_object_unref (task);
		return;
	}

	/* Older portals pick their own request path */
	g_variant_get (reply, "(&o)", &handle);
	if (g_strcmp0 (handle, req->handle) != 0)
		subscribe_response (task, handle);
	g_object_unref (task);
}

/*
 * portal_start:
 *
 * Calls OpenFile / SaveFile with a fresh handle token after
 * subscribing to the Response signal of the request path the portal
 * will use (/org/freedesktop/portal/desktop/request/SENDER/TOKEN).
 */
static void
portal_start (GTask *task)
{
	Request           *req = g_task_get_task_data (task);
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *token = NULL;
	g_autofree gchar  *sender = NULL;
	g_autofree gchar  *handle = NULL;
	const gchar       *method;
	const gchar       *title;

	req->bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (req->bus == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN_UI, "file dialog: no session bus (%s), using the in-app dialog",
		           error->message);
		show_in_app (task);
		return;
	}

	token = g_strdup_printf ("lrg%u", g_random_int ());
	sender = g_strdup (g_dbus_connection_get_unique_name (req->bus) + 1);
	g_strdelimit (sender, ".", '_');
	handle = g_strdup_printf (PORTAL_OBJECT_PATH "/request/%s/%s", sender, token);
	subscribe_response (task, handle);

	method = req->mode == LRG_FILE_BROWSER_MODE_SAVE ? "SaveFile" : "OpenFile";
	title = req->dialog->title != NULL ? req->dialog->title : default_title (req->mode);
	g_dbus_connection_call (req->bus, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, PORTAL_CHOOSER_IFACE, method,
	                        g_variant_new ("(ss@a{sv})", "", title, portal_options (req, token)),
	                        G_VARIANT_TYPE ("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL,
	                        on_portal_called, g_object_ref (task));
}

/* ==========================================================================
 * Operations
 * ========================================================================== */

/*
 * start_request:
 *
 * Common start of the four operations. The task keeps one extra
 * reference until it returns (released in request_succeed / fail).
 */
static void
start_request (LrgFileDialog       *self,
               LrgFileBrowserMode   mode,
               LrgWidget           *parent,
               GCancellable        *cancellable,
               GAsyncReadyCallback  callback,
               gpointer             user_data,
               gpointer             source_tag)
{
	GTask   *task;
	Request *req;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, source_tag);
	/* Report cancellation in our own domain, not as G_IO_ERROR_CANCELLED */
	g_task_set_check_cancellable (task, FALSE);
	req = g_new0 (Request, 1);
	req->task = task;
	req->dialog = g_object_ref (self);
	req->mode = mode;
	req->parent = parent != NULL ? g_object_ref (parent) : NULL;
	g_task_set_task_data (task, req, request_free);

	/* Already cancelled: fail right away (returned from an idle) */
	if (cancellable != NULL && g_cancellable_is_cancelled (cancellable))
	{
		req->done = TRUE;
		g_task_return_new_error (task, LRG_FILE_DIALOG_ERROR, LRG_FILE_DIALOG_ERROR_CANCELLED,
		                         "The file dialog was cancelled");
		g_object_unref (task);
		return;
	}
	if (cancellable != NULL)
		req->cancel_id = g_cancellable_connect (cancellable, G_CALLBACK (on_cancelled), task, NULL);

	if (self->prefer_native)
		portal_start (task);
	else
		show_in_app (task);
}

/* finish_pointer: the common _finish() */
static gpointer
finish_pointer (LrgFileDialog  *self,
                GAsyncResult   *result,
                gpointer        source_tag,
                GError        **error)
{
	g_return_val_if_fail (LRG_IS_FILE_DIALOG (self), NULL);
	g_return_val_if_fail (g_task_is_valid (result, self), NULL);
	g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == source_tag, NULL);

	return g_task_propagate_pointer (G_TASK (result), error);
}

void
lrg_file_dialog_open (LrgFileDialog       *self,
                      LrgWidget           *parent,
                      GCancellable        *cancellable,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));
	g_return_if_fail (parent == NULL || LRG_IS_WIDGET (parent));

	start_request (self, LRG_FILE_BROWSER_MODE_OPEN, parent, cancellable, callback, user_data,
	               lrg_file_dialog_open);
}

GFile *
lrg_file_dialog_open_finish (LrgFileDialog  *self,
                             GAsyncResult   *result,
                             GError        **error)
{
	return finish_pointer (self, result, lrg_file_dialog_open, error);
}

void
lrg_file_dialog_open_multiple (LrgFileDialog       *self,
                               LrgWidget           *parent,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));
	g_return_if_fail (parent == NULL || LRG_IS_WIDGET (parent));

	start_request (self, LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE, parent, cancellable, callback, user_data,
	               lrg_file_dialog_open_multiple);
}

GListModel *
lrg_file_dialog_open_multiple_finish (LrgFileDialog  *self,
                                      GAsyncResult   *result,
                                      GError        **error)
{
	return finish_pointer (self, result, lrg_file_dialog_open_multiple, error);
}

void
lrg_file_dialog_save (LrgFileDialog       *self,
                      LrgWidget           *parent,
                      GCancellable        *cancellable,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));
	g_return_if_fail (parent == NULL || LRG_IS_WIDGET (parent));

	start_request (self, LRG_FILE_BROWSER_MODE_SAVE, parent, cancellable, callback, user_data,
	               lrg_file_dialog_save);
}

GFile *
lrg_file_dialog_save_finish (LrgFileDialog  *self,
                             GAsyncResult   *result,
                             GError        **error)
{
	return finish_pointer (self, result, lrg_file_dialog_save, error);
}

void
lrg_file_dialog_select_folder (LrgFileDialog       *self,
                               LrgWidget           *parent,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));
	g_return_if_fail (parent == NULL || LRG_IS_WIDGET (parent));

	start_request (self, LRG_FILE_BROWSER_MODE_SELECT_FOLDER, parent, cancellable, callback, user_data,
	               lrg_file_dialog_select_folder);
}

GFile *
lrg_file_dialog_select_folder_finish (LrgFileDialog  *self,
                                      GAsyncResult   *result,
                                      GError        **error)
{
	return finish_pointer (self, result, lrg_file_dialog_select_folder, error);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_file_dialog_finalize (GObject *object)
{
	LrgFileDialog *self = LRG_FILE_DIALOG (object);

	g_free (self->title);
	g_free (self->accept_label);
	g_clear_object (&self->initial_folder);
	g_free (self->initial_name);
	g_clear_object (&self->filters);
	g_clear_object (&self->default_filter);
	g_ptr_array_unref (self->places);

	G_OBJECT_CLASS (lrg_file_dialog_parent_class)->finalize (object);
}

static void
lrg_file_dialog_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgFileDialog *self = LRG_FILE_DIALOG (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		g_value_set_string (value, self->title);
		break;
	case PROP_ACCEPT_LABEL:
		g_value_set_string (value, self->accept_label);
		break;
	case PROP_INITIAL_FOLDER:
		g_value_set_object (value, self->initial_folder);
		break;
	case PROP_INITIAL_NAME:
		g_value_set_string (value, self->initial_name);
		break;
	case PROP_FILTERS:
		g_value_set_object (value, self->filters);
		break;
	case PROP_DEFAULT_FILTER:
		g_value_set_object (value, self->default_filter);
		break;
	case PROP_MODAL:
		g_value_set_boolean (value, self->modal);
		break;
	case PROP_PREFER_NATIVE:
		g_value_set_boolean (value, self->prefer_native);
		break;
	case PROP_SHOW_SYSTEM_PLACES:
		g_value_set_boolean (value, self->show_system_places);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_file_dialog_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgFileDialog *self = LRG_FILE_DIALOG (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		lrg_file_dialog_set_title (self, g_value_get_string (value));
		break;
	case PROP_ACCEPT_LABEL:
		lrg_file_dialog_set_accept_label (self, g_value_get_string (value));
		break;
	case PROP_INITIAL_FOLDER:
		lrg_file_dialog_set_initial_folder (self, g_value_get_object (value));
		break;
	case PROP_INITIAL_NAME:
		lrg_file_dialog_set_initial_name (self, g_value_get_string (value));
		break;
	case PROP_FILTERS:
		lrg_file_dialog_set_filters (self, g_value_get_object (value));
		break;
	case PROP_DEFAULT_FILTER:
		lrg_file_dialog_set_default_filter (self, g_value_get_object (value));
		break;
	case PROP_MODAL:
		lrg_file_dialog_set_modal (self, g_value_get_boolean (value));
		break;
	case PROP_PREFER_NATIVE:
		lrg_file_dialog_set_prefer_native (self, g_value_get_boolean (value));
		break;
	case PROP_SHOW_SYSTEM_PLACES:
		lrg_file_dialog_set_show_system_places (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_file_dialog_class_init (LrgFileDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_file_dialog_finalize;
	object_class->get_property = lrg_file_dialog_get_property;
	object_class->set_property = lrg_file_dialog_set_property;

	/**
	 * LrgFileDialog:title:
	 *
	 * Heading of the dialog; %NULL uses a default per operation.
	 */
	properties[PROP_TITLE] =
		g_param_spec_string ("title", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileDialog:accept-label:
	 *
	 * Text of the accept button; %NULL uses "Open", "Save" or "Select".
	 */
	properties[PROP_ACCEPT_LABEL] =
		g_param_spec_string ("accept-label", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileDialog:initial-folder:
	 *
	 * The folder the dialog starts in; %NULL for the working directory.
	 */
	properties[PROP_INITIAL_FOLDER] =
		g_param_spec_object ("initial-folder", NULL, NULL, G_TYPE_FILE,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileDialog:initial-name:
	 *
	 * The file name suggested when saving.
	 */
	properties[PROP_INITIAL_NAME] =
		g_param_spec_string ("initial-name", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileDialog:filters:
	 *
	 * A #GListModel of #LrgFileFilter offered to the user.
	 */
	properties[PROP_FILTERS] =
		g_param_spec_object ("filters", NULL, NULL, G_TYPE_LIST_MODEL,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileDialog:default-filter:
	 *
	 * The filter selected when the dialog opens.
	 */
	properties[PROP_DEFAULT_FILTER] =
		g_param_spec_object ("default-filter", NULL, NULL, LRG_TYPE_FILE_FILTER,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileDialog:modal:
	 *
	 * Whether the portal dialog is modal. The in-app dialog always is.
	 */
	properties[PROP_MODAL] =
		g_param_spec_boolean ("modal", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileDialog:prefer-native:
	 *
	 * Whether the XDG desktop portal is tried before the in-app dialog.
	 */
	properties[PROP_PREFER_NATIVE] =
		g_param_spec_boolean ("prefer-native", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFileDialog:show-system-places:
	 *
	 * Whether the in-app sidebar lists home, user folders, bookmarks
	 * and mounts next to the application places.
	 */
	properties[PROP_SHOW_SYSTEM_PLACES] =
		g_param_spec_boolean ("show-system-places", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_file_dialog_init (LrgFileDialog *self)
{
	self->modal = TRUE;
	self->show_system_places = TRUE;
	self->places = g_ptr_array_new_with_free_func (dialog_place_free);
}

/* ==========================================================================
 * Public API: properties
 * ========================================================================== */

LrgFileDialog *
lrg_file_dialog_new (void)
{
	return g_object_new (LRG_TYPE_FILE_DIALOG, NULL);
}

const gchar *
lrg_file_dialog_get_title (LrgFileDialog *self)
{
	g_return_val_if_fail (LRG_IS_FILE_DIALOG (self), NULL);

	return self->title;
}

void
lrg_file_dialog_set_title (LrgFileDialog *self,
                           const gchar   *title)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));

	if (g_set_str (&self->title, title))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

const gchar *
lrg_file_dialog_get_accept_label (LrgFileDialog *self)
{
	g_return_val_if_fail (LRG_IS_FILE_DIALOG (self), NULL);

	return self->accept_label;
}

void
lrg_file_dialog_set_accept_label (LrgFileDialog *self,
                                  const gchar   *accept_label)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));

	if (g_set_str (&self->accept_label, accept_label))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACCEPT_LABEL]);
}

GFile *
lrg_file_dialog_get_initial_folder (LrgFileDialog *self)
{
	g_return_val_if_fail (LRG_IS_FILE_DIALOG (self), NULL);

	return self->initial_folder;
}

void
lrg_file_dialog_set_initial_folder (LrgFileDialog *self,
                                    GFile         *folder)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));
	g_return_if_fail (folder == NULL || G_IS_FILE (folder));

	if (g_set_object (&self->initial_folder, folder))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INITIAL_FOLDER]);
}

const gchar *
lrg_file_dialog_get_initial_name (LrgFileDialog *self)
{
	g_return_val_if_fail (LRG_IS_FILE_DIALOG (self), NULL);

	return self->initial_name;
}

void
lrg_file_dialog_set_initial_name (LrgFileDialog *self,
                                  const gchar   *name)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));

	if (g_set_str (&self->initial_name, name))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INITIAL_NAME]);
}

GListModel *
lrg_file_dialog_get_filters (LrgFileDialog *self)
{
	g_return_val_if_fail (LRG_IS_FILE_DIALOG (self), NULL);

	return self->filters;
}

void
lrg_file_dialog_set_filters (LrgFileDialog *self,
                             GListModel    *filters)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));
	g_return_if_fail (filters == NULL || G_IS_LIST_MODEL (filters));

	if (g_set_object (&self->filters, filters))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTERS]);
}

LrgFileFilter *
lrg_file_dialog_get_default_filter (LrgFileDialog *self)
{
	g_return_val_if_fail (LRG_IS_FILE_DIALOG (self), NULL);

	return self->default_filter;
}

void
lrg_file_dialog_set_default_filter (LrgFileDialog *self,
                                    LrgFileFilter *filter)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));
	g_return_if_fail (filter == NULL || LRG_IS_FILE_FILTER (filter));

	if (g_set_object (&self->default_filter, filter))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_FILTER]);
}

gboolean
lrg_file_dialog_get_modal (LrgFileDialog *self)
{
	g_return_val_if_fail (LRG_IS_FILE_DIALOG (self), TRUE);

	return self->modal;
}

void
lrg_file_dialog_set_modal (LrgFileDialog *self,
                           gboolean       modal)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));

	modal = !!modal;
	if (self->modal == modal)
		return;
	self->modal = modal;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODAL]);
}

gboolean
lrg_file_dialog_get_prefer_native (LrgFileDialog *self)
{
	g_return_val_if_fail (LRG_IS_FILE_DIALOG (self), FALSE);

	return self->prefer_native;
}

void
lrg_file_dialog_set_prefer_native (LrgFileDialog *self,
                                   gboolean       prefer_native)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));

	prefer_native = !!prefer_native;
	if (self->prefer_native == prefer_native)
		return;
	self->prefer_native = prefer_native;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PREFER_NATIVE]);
}

gboolean
lrg_file_dialog_get_show_system_places (LrgFileDialog *self)
{
	g_return_val_if_fail (LRG_IS_FILE_DIALOG (self), TRUE);

	return self->show_system_places;
}

void
lrg_file_dialog_set_show_system_places (LrgFileDialog *self,
                                        gboolean       show_system_places)
{
	g_return_if_fail (LRG_IS_FILE_DIALOG (self));

	show_system_places = !!show_system_places;
	if (self->show_system_places == show_system_places)
		return;
	self->show_system_places = show_system_places;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_SYSTEM_PLACES]);
}

void
lrg_file_dialog_add_place (LrgFileDialog *self,
                           const gchar   *label,
                           GFile         *location,
                           LrgIcon        icon)
{
	DialogPlace *p;

	g_return_if_fail (LRG_IS_FILE_DIALOG (self));
	g_return_if_fail (label != NULL);
	g_return_if_fail (G_IS_FILE (location));

	p = g_new0 (DialogPlace, 1);
	p->label = g_strdup (label);
	p->location = g_object_ref (location);
	p->icon = icon;
	g_ptr_array_add (self->places, p);
}
