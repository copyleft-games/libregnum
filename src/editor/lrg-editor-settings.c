/* lrg-editor-settings.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Per-user editor preferences, persisted as YAML.
 *
 * Persistence is reflection-driven: every readable+writable property is a
 * top-level key.  Scalars use the obvious YAML scalar, string arrays are
 * sequences, enums use their nick.  The keybinding overrides are the one
 * structured key (a mapping of action id -> shortcut).  Keys this version
 * does not know about are kept and written back unchanged, so a newer
 * editor's settings survive a round trip through an older one.
 */

#include "lrg-editor-settings.h"
#include <yaml-glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <errno.h>

#define KEYBINDINGS_KEY "keybindings"

struct _LrgEditorSettings
{
	GObject parent_instance;

	gchar      *loaded_path;
	GHashTable *keybindings;  /* action -> shortcut */
	GHashTable *unknown;      /* key -> YamlNode* (ref) */

	/* External editor */
	gchar      *external_editor;
	gchar      *external_editor_command;
	gchar      *terminal_command;
	gchar      *editor_in_terminal;
	gboolean    emacs_integration;
	gchar      *emacs_server_name;

	/* Appearance */
	gchar      *theme;
	gdouble     ui_scale;
	gint        font_size;
	gchar      *font_path;

	/* Behaviour */
	guint       autosave_interval;
	guint       history_limit;
	gboolean    reload_on_external_change;
	gboolean    confirm_on_close;
	gboolean    restore_last_session;

	/* Viewport */
	gboolean    snap_enabled;
	gdouble     snap_translate;
	gdouble     snap_rotate_degrees;
	gdouble     snap_scale;
	gboolean    grid_visible;
	gdouble     grid_size;
	guint       grid_extent;
	gdouble     camera_fly_speed;
	gdouble     camera_orbit_speed;
	gboolean    camera_invert_y;
	gchar      *camera_navigation;
	gdouble     camera_fov;

	/* Server */
	gboolean    server_enabled;
	gchar      *server_socket;

	/* Session */
	gchar     **recent_projects;
	gchar     **recent_levels;
	gchar      *layout;
};

G_DEFINE_FINAL_TYPE (LrgEditorSettings, lrg_editor_settings, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_EXTERNAL_EDITOR,
	PROP_EXTERNAL_EDITOR_COMMAND,
	PROP_TERMINAL_COMMAND,
	PROP_EDITOR_IN_TERMINAL,
	PROP_EMACS_INTEGRATION,
	PROP_EMACS_SERVER_NAME,
	PROP_THEME,
	PROP_UI_SCALE,
	PROP_FONT_SIZE,
	PROP_FONT_PATH,
	PROP_AUTOSAVE_INTERVAL,
	PROP_HISTORY_LIMIT,
	PROP_RELOAD_ON_EXTERNAL_CHANGE,
	PROP_CONFIRM_ON_CLOSE,
	PROP_RESTORE_LAST_SESSION,
	PROP_SNAP_ENABLED,
	PROP_SNAP_TRANSLATE,
	PROP_SNAP_ROTATE_DEGREES,
	PROP_SNAP_SCALE,
	PROP_GRID_VISIBLE,
	PROP_GRID_SIZE,
	PROP_GRID_EXTENT,
	PROP_CAMERA_FLY_SPEED,
	PROP_CAMERA_ORBIT_SPEED,
	PROP_CAMERA_INVERT_Y,
	PROP_CAMERA_NAVIGATION,
	PROP_CAMERA_FOV,
	PROP_SERVER_ENABLED,
	PROP_SERVER_SOCKET,
	PROP_RECENT_PROJECTS,
	PROP_RECENT_LEVELS,
	PROP_LAYOUT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_KEYBINDINGS_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_settings_finalize (GObject *object)
{
	LrgEditorSettings *self = LRG_EDITOR_SETTINGS (object);

	g_free (self->loaded_path);
	g_hash_table_unref (self->keybindings);
	g_hash_table_unref (self->unknown);
	g_free (self->external_editor);
	g_free (self->external_editor_command);
	g_free (self->terminal_command);
	g_free (self->editor_in_terminal);
	g_free (self->camera_navigation);
	g_free (self->emacs_server_name);
	g_free (self->theme);
	g_free (self->font_path);
	g_free (self->server_socket);
	g_strfreev (self->recent_projects);
	g_strfreev (self->recent_levels);
	g_free (self->layout);

	G_OBJECT_CLASS (lrg_editor_settings_parent_class)->finalize (object);
}

static void
lrg_editor_settings_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgEditorSettings *self = LRG_EDITOR_SETTINGS (object);

	switch (prop_id)
	{
	case PROP_EXTERNAL_EDITOR:          g_value_set_string (value, self->external_editor); break;
	case PROP_EXTERNAL_EDITOR_COMMAND:  g_value_set_string (value, self->external_editor_command); break;
	case PROP_TERMINAL_COMMAND:         g_value_set_string (value, self->terminal_command); break;
	case PROP_EDITOR_IN_TERMINAL:       g_value_set_string (value, self->editor_in_terminal); break;
	case PROP_EMACS_INTEGRATION:        g_value_set_boolean (value, self->emacs_integration); break;
	case PROP_EMACS_SERVER_NAME:        g_value_set_string (value, self->emacs_server_name); break;
	case PROP_THEME:                    g_value_set_string (value, self->theme); break;
	case PROP_UI_SCALE:                 g_value_set_double (value, self->ui_scale); break;
	case PROP_FONT_SIZE:                g_value_set_int (value, self->font_size); break;
	case PROP_FONT_PATH:                g_value_set_string (value, self->font_path); break;
	case PROP_AUTOSAVE_INTERVAL:        g_value_set_uint (value, self->autosave_interval); break;
	case PROP_HISTORY_LIMIT:            g_value_set_uint (value, self->history_limit); break;
	case PROP_RELOAD_ON_EXTERNAL_CHANGE: g_value_set_boolean (value, self->reload_on_external_change); break;
	case PROP_CONFIRM_ON_CLOSE:         g_value_set_boolean (value, self->confirm_on_close); break;
	case PROP_RESTORE_LAST_SESSION:     g_value_set_boolean (value, self->restore_last_session); break;
	case PROP_SNAP_ENABLED:             g_value_set_boolean (value, self->snap_enabled); break;
	case PROP_SNAP_TRANSLATE:           g_value_set_double (value, self->snap_translate); break;
	case PROP_SNAP_ROTATE_DEGREES:      g_value_set_double (value, self->snap_rotate_degrees); break;
	case PROP_SNAP_SCALE:               g_value_set_double (value, self->snap_scale); break;
	case PROP_GRID_VISIBLE:             g_value_set_boolean (value, self->grid_visible); break;
	case PROP_GRID_SIZE:                g_value_set_double (value, self->grid_size); break;
	case PROP_GRID_EXTENT:              g_value_set_uint (value, self->grid_extent); break;
	case PROP_CAMERA_FLY_SPEED:         g_value_set_double (value, self->camera_fly_speed); break;
	case PROP_CAMERA_ORBIT_SPEED:       g_value_set_double (value, self->camera_orbit_speed); break;
	case PROP_CAMERA_INVERT_Y:          g_value_set_boolean (value, self->camera_invert_y); break;
	case PROP_CAMERA_NAVIGATION:        g_value_set_string (value, self->camera_navigation); break;
	case PROP_CAMERA_FOV:               g_value_set_double (value, self->camera_fov); break;
	case PROP_SERVER_ENABLED:           g_value_set_boolean (value, self->server_enabled); break;
	case PROP_SERVER_SOCKET:            g_value_set_string (value, self->server_socket); break;
	case PROP_RECENT_PROJECTS:          g_value_set_boxed (value, self->recent_projects); break;
	case PROP_RECENT_LEVELS:            g_value_set_boxed (value, self->recent_levels); break;
	case PROP_LAYOUT:                   g_value_set_string (value, self->layout); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_str (gchar        **field,
         const GValue  *value)
{
	g_free (*field);
	*field = g_value_dup_string (value);
}

static void
set_strv (gchar        ***field,
          const GValue   *value)
{
	g_strfreev (*field);
	*field = g_value_dup_boxed (value);
	if (*field == NULL)
		*field = g_new0 (gchar *, 1);
}

static void
lrg_editor_settings_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgEditorSettings *self = LRG_EDITOR_SETTINGS (object);

	switch (prop_id)
	{
	case PROP_EXTERNAL_EDITOR:          set_str (&self->external_editor, value); break;
	case PROP_EXTERNAL_EDITOR_COMMAND:  set_str (&self->external_editor_command, value); break;
	case PROP_TERMINAL_COMMAND:         set_str (&self->terminal_command, value); break;
	case PROP_EDITOR_IN_TERMINAL:       set_str (&self->editor_in_terminal, value); break;
	case PROP_EMACS_INTEGRATION:        self->emacs_integration = g_value_get_boolean (value); break;
	case PROP_EMACS_SERVER_NAME:        set_str (&self->emacs_server_name, value); break;
	case PROP_THEME:                    set_str (&self->theme, value); break;
	case PROP_UI_SCALE:                 self->ui_scale = g_value_get_double (value); break;
	case PROP_FONT_SIZE:                self->font_size = g_value_get_int (value); break;
	case PROP_FONT_PATH:                set_str (&self->font_path, value); break;
	case PROP_AUTOSAVE_INTERVAL:        self->autosave_interval = g_value_get_uint (value); break;
	case PROP_HISTORY_LIMIT:            self->history_limit = g_value_get_uint (value); break;
	case PROP_RELOAD_ON_EXTERNAL_CHANGE: self->reload_on_external_change = g_value_get_boolean (value); break;
	case PROP_CONFIRM_ON_CLOSE:         self->confirm_on_close = g_value_get_boolean (value); break;
	case PROP_RESTORE_LAST_SESSION:     self->restore_last_session = g_value_get_boolean (value); break;
	case PROP_SNAP_ENABLED:             self->snap_enabled = g_value_get_boolean (value); break;
	case PROP_SNAP_TRANSLATE:           self->snap_translate = g_value_get_double (value); break;
	case PROP_SNAP_ROTATE_DEGREES:      self->snap_rotate_degrees = g_value_get_double (value); break;
	case PROP_SNAP_SCALE:               self->snap_scale = g_value_get_double (value); break;
	case PROP_GRID_VISIBLE:             self->grid_visible = g_value_get_boolean (value); break;
	case PROP_GRID_SIZE:                self->grid_size = g_value_get_double (value); break;
	case PROP_GRID_EXTENT:              self->grid_extent = g_value_get_uint (value); break;
	case PROP_CAMERA_FLY_SPEED:         self->camera_fly_speed = g_value_get_double (value); break;
	case PROP_CAMERA_ORBIT_SPEED:       self->camera_orbit_speed = g_value_get_double (value); break;
	case PROP_CAMERA_INVERT_Y:          self->camera_invert_y = g_value_get_boolean (value); break;
	case PROP_CAMERA_NAVIGATION:        set_str (&self->camera_navigation, value); break;
	case PROP_CAMERA_FOV:               self->camera_fov = g_value_get_double (value); break;
	case PROP_SERVER_ENABLED:           self->server_enabled = g_value_get_boolean (value); break;
	case PROP_SERVER_SOCKET:            set_str (&self->server_socket, value); break;
	case PROP_RECENT_PROJECTS:          set_strv (&self->recent_projects, value); break;
	case PROP_RECENT_LEVELS:            set_strv (&self->recent_levels, value); break;
	case PROP_LAYOUT:                   set_str (&self->layout, value); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

#define RW (G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS)

static void
lrg_editor_settings_class_init (LrgEditorSettingsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_editor_settings_finalize;
	object_class->get_property = lrg_editor_settings_get_property;
	object_class->set_property = lrg_editor_settings_set_property;

	/**
	 * LrgEditorSettings:external-editor:
	 *
	 * External code editor preset: "auto" follows `$VISUAL`/`$EDITOR`,
	 * "custom" uses #LrgEditorSettings:external-editor-command, anything
	 * else names a preset (see lrg_external_editor_list_presets()).
	 */
	properties[PROP_EXTERNAL_EDITOR] =
		g_param_spec_string ("external-editor", "External Editor",
		                     "External code editor preset id", "auto", RW);

	/**
	 * LrgEditorSettings:external-editor-command:
	 *
	 * Command template for the "custom" preset, split shell-style, with
	 * `{file}`, `{line}`, `{col}`, `{project}` and `{dir}` substituted per
	 * argument (Godot-compatible placeholders). A template without
	 * `{file}` gets the file appended.
	 */
	properties[PROP_EXTERNAL_EDITOR_COMMAND] =
		g_param_spec_string ("external-editor-command", "External Editor Command",
		                     "Custom external editor command template", NULL, RW);

	/**
	 * LrgEditorSettings:terminal-command:
	 *
	 * Terminal wrapper for terminal editors (vim, nvim, nano, helix...),
	 * e.g. "gst -e" or "foot". Empty means detect `$TERMINAL` and common
	 * terminals.
	 */
	properties[PROP_TERMINAL_COMMAND] =
		g_param_spec_string ("terminal-command", "Terminal Command",
		                     "Terminal used to run terminal editors", NULL, RW);

	/**
	 * LrgEditorSettings:editor-in-terminal:
	 *
	 * "auto" (decide from the preset), "always" or "never".
	 */
	properties[PROP_EDITOR_IN_TERMINAL] =
		g_param_spec_string ("editor-in-terminal", "Editor In Terminal",
		                     "Whether to wrap the editor in a terminal", "auto", RW);

	/**
	 * LrgEditorSettings:emacs-integration:
	 *
	 * Prefer an Emacs connected to the editor server (libregnum.el) for
	 * opening files, falling back to launching a process.
	 */
	properties[PROP_EMACS_INTEGRATION] =
		g_param_spec_boolean ("emacs-integration", "Emacs Integration",
		                      "Open files in a connected Emacs first", TRUE, RW);

	/**
	 * LrgEditorSettings:emacs-server-name:
	 *
	 * Emacs server name passed to emacsclient as `-s` (empty = default).
	 */
	properties[PROP_EMACS_SERVER_NAME] =
		g_param_spec_string ("emacs-server-name", "Emacs Server Name",
		                     "emacsclient --socket-name", NULL, RW);

	/**
	 * LrgEditorSettings:theme:
	 *
	 * UI colour theme ("mocha", "macchiato", "frappe", "latte").
	 */
	properties[PROP_THEME] =
		g_param_spec_string ("theme", "Theme", "UI theme", "mocha", RW);

	/**
	 * LrgEditorSettings:ui-scale:
	 *
	 * UI scale factor.
	 */
	properties[PROP_UI_SCALE] =
		g_param_spec_double ("ui-scale", "UI Scale", "UI scale factor",
		                     0.5, 4.0, 1.0, RW);

	/**
	 * LrgEditorSettings:font-size:
	 *
	 * Base UI font size in pixels (before scaling).
	 */
	properties[PROP_FONT_SIZE] =
		g_param_spec_int ("font-size", "Font Size", "UI font size",
		                  8, 48, 16, RW);

	/**
	 * LrgEditorSettings:font-path:
	 *
	 * Optional TTF/OTF font file for the UI (empty = built-in font).
	 */
	properties[PROP_FONT_PATH] =
		g_param_spec_string ("font-path", "Font Path", "UI font file", NULL, RW);

	/**
	 * LrgEditorSettings:autosave-interval:
	 *
	 * Seconds between autosaves of a dirty level (0 disables).
	 */
	properties[PROP_AUTOSAVE_INTERVAL] =
		g_param_spec_uint ("autosave-interval", "Autosave Interval",
		                   "Seconds between autosaves (0 = off)",
		                   0, 86400, 120, RW);

	/**
	 * LrgEditorSettings:history-limit:
	 *
	 * Maximum undo steps (0 = unlimited).
	 */
	properties[PROP_HISTORY_LIMIT] =
		g_param_spec_uint ("history-limit", "History Limit",
		                   "Maximum undo steps", 0, G_MAXUINT, 1000, RW);

	/**
	 * LrgEditorSettings:reload-on-external-change:
	 *
	 * Reload the open level when its file changes on disk and there are no
	 * unsaved edits (e.g. after editing the YAML in Emacs).
	 */
	properties[PROP_RELOAD_ON_EXTERNAL_CHANGE] =
		g_param_spec_boolean ("reload-on-external-change", "Reload On External Change",
		                      "Reload files changed outside the editor", TRUE, RW);

	/**
	 * LrgEditorSettings:confirm-on-close:
	 *
	 * Ask before discarding unsaved changes.
	 */
	properties[PROP_CONFIRM_ON_CLOSE] =
		g_param_spec_boolean ("confirm-on-close", "Confirm On Close",
		                      "Ask before discarding unsaved changes", TRUE, RW);

	/**
	 * LrgEditorSettings:restore-last-session:
	 *
	 * Reopen the last project and level on start-up.
	 */
	properties[PROP_RESTORE_LAST_SESSION] =
		g_param_spec_boolean ("restore-last-session", "Restore Last Session",
		                      "Reopen the last project on start-up", TRUE, RW);

	/**
	 * LrgEditorSettings:snap-enabled:
	 *
	 * Whether gizmo snapping starts enabled.
	 */
	properties[PROP_SNAP_ENABLED] =
		g_param_spec_boolean ("snap-enabled", "Snap Enabled",
		                      "Gizmo snapping on by default", FALSE, RW);

	/**
	 * LrgEditorSettings:snap-translate:
	 *
	 * Translation snap increment in world units.
	 */
	properties[PROP_SNAP_TRANSLATE] =
		g_param_spec_double ("snap-translate", "Snap Translate",
		                     "Translation snap increment", 0.0, 1000.0, 1.0, RW);

	/**
	 * LrgEditorSettings:snap-rotate-degrees:
	 *
	 * Rotation snap increment in degrees.
	 */
	properties[PROP_SNAP_ROTATE_DEGREES] =
		g_param_spec_double ("snap-rotate-degrees", "Snap Rotate Degrees",
		                     "Rotation snap increment", 0.0, 360.0, 15.0, RW);

	/**
	 * LrgEditorSettings:snap-scale:
	 *
	 * Scale snap increment.
	 */
	properties[PROP_SNAP_SCALE] =
		g_param_spec_double ("snap-scale", "Snap Scale",
		                     "Scale snap increment", 0.0, 100.0, 0.1, RW);

	/**
	 * LrgEditorSettings:grid-visible:
	 *
	 * Whether the viewport grid is shown.
	 */
	properties[PROP_GRID_VISIBLE] =
		g_param_spec_boolean ("grid-visible", "Grid Visible",
		                      "Show the viewport grid", TRUE, RW);

	/**
	 * LrgEditorSettings:grid-size:
	 *
	 * Grid cell size in world units.
	 */
	properties[PROP_GRID_SIZE] =
		g_param_spec_double ("grid-size", "Grid Size", "Grid cell size",
		                     0.01, 1000.0, 1.0, RW);

	/**
	 * LrgEditorSettings:grid-extent:
	 *
	 * Number of grid cells on each side of the origin.
	 */
	properties[PROP_GRID_EXTENT] =
		g_param_spec_uint ("grid-extent", "Grid Extent", "Grid half extent in cells",
		                   1, 1000, 50, RW);

	/**
	 * LrgEditorSettings:camera-fly-speed:
	 *
	 * Fly-mode speed in world units per second (Shift doubles it).
	 */
	properties[PROP_CAMERA_FLY_SPEED] =
		g_param_spec_double ("camera-fly-speed", "Camera Fly Speed",
		                     "Fly speed in units per second", 0.1, 1000.0, 8.0, RW);

	/**
	 * LrgEditorSettings:camera-orbit-speed:
	 *
	 * Orbit/look sensitivity in degrees per pixel.
	 */
	properties[PROP_CAMERA_ORBIT_SPEED] =
		g_param_spec_double ("camera-orbit-speed", "Camera Orbit Speed",
		                     "Degrees per pixel", 0.01, 5.0, 0.35, RW);

	/**
	 * LrgEditorSettings:camera-invert-y:
	 *
	 * Invert vertical mouse look.
	 */
	properties[PROP_CAMERA_INVERT_Y] =
		g_param_spec_boolean ("camera-invert-y", "Camera Invert Y",
		                      "Invert vertical look", FALSE, RW);

	/**
	 * LrgEditorSettings:camera-navigation:
	 *
	 * Viewport mouse scheme: "default" (right drag flies, middle drag
	 * pans, Alt + left drag orbits), "blender" (middle drag orbits,
	 * Shift + middle pans, Ctrl + middle zooms) or "trackpad" (two-finger
	 * scroll orbits, Shift + scroll pans, Ctrl + scroll zooms). The
	 * orientation gizmo and the numpad work in every scheme.
	 */
	properties[PROP_CAMERA_NAVIGATION] =
		g_param_spec_string ("camera-navigation", "Camera Navigation",
		                     "Viewport navigation scheme", "default", RW);

	/**
	 * LrgEditorSettings:camera-fov:
	 *
	 * Viewport vertical field of view in degrees.
	 */
	properties[PROP_CAMERA_FOV] =
		g_param_spec_double ("camera-fov", "Camera FOV", "Vertical field of view",
		                     10.0, 150.0, 50.0, RW);

	/**
	 * LrgEditorSettings:server-enabled:
	 *
	 * Whether the editor listens on its IPC socket (needed by libregnum.el
	 * and other tools).
	 */
	properties[PROP_SERVER_ENABLED] =
		g_param_spec_boolean ("server-enabled", "Server Enabled",
		                      "Listen on the editor IPC socket", TRUE, RW);

	/**
	 * LrgEditorSettings:server-socket:
	 *
	 * IPC socket path (empty = `$XDG_RUNTIME_DIR/libregnum/editor-PID.sock`).
	 */
	properties[PROP_SERVER_SOCKET] =
		g_param_spec_string ("server-socket", "Server Socket",
		                     "IPC socket path", NULL, RW);

	/**
	 * LrgEditorSettings:recent-projects:
	 *
	 * Recently opened project directories, newest first.
	 */
	properties[PROP_RECENT_PROJECTS] =
		g_param_spec_boxed ("recent-projects", "Recent Projects",
		                    "Recently opened projects", G_TYPE_STRV, RW);

	/**
	 * LrgEditorSettings:recent-levels:
	 *
	 * Recently opened level files, newest first.
	 */
	properties[PROP_RECENT_LEVELS] =
		g_param_spec_boxed ("recent-levels", "Recent Levels",
		                    "Recently opened levels", G_TYPE_STRV, RW);

	/**
	 * LrgEditorSettings:layout:
	 *
	 * Serialized dock layout of the editor window (opaque to this class).
	 */
	properties[PROP_LAYOUT] =
		g_param_spec_string ("layout", "Layout", "Saved panel layout", NULL, RW);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgEditorSettings::keybindings-changed:
	 * @self: the settings
	 *
	 * Emitted when a keybinding override is set, cleared or loaded.
	 */
	signals[SIGNAL_KEYBINDINGS_CHANGED] =
		g_signal_new ("keybindings-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
lrg_editor_settings_init (LrgEditorSettings *self)
{
	self->keybindings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->unknown = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                                       (GDestroyNotify) yaml_node_unref);
}

LrgEditorSettings *
lrg_editor_settings_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_SETTINGS, NULL);
}

gchar *
lrg_editor_settings_get_default_path (void)
{
	const gchar *env = g_getenv ("LRG_EDITOR_CONFIG");

	if (env != NULL && *env != '\0')
		return g_strdup (env);

	return g_build_filename (g_get_user_config_dir (), "libregnum", "editor.yaml", NULL);
}

void
lrg_editor_settings_reset (LrgEditorSettings *self)
{
	guint i;

	g_return_if_fail (LRG_IS_EDITOR_SETTINGS (self));

	g_object_freeze_notify (G_OBJECT (self));
	for (i = 1; i < N_PROPS; i++)
	{
		const GValue *def = g_param_spec_get_default_value (properties[i]);

		g_object_set_property (G_OBJECT (self), properties[i]->name, def);
	}
	g_object_thaw_notify (G_OBJECT (self));

	g_hash_table_remove_all (self->keybindings);
	g_signal_emit (self, signals[SIGNAL_KEYBINDINGS_CHANGED], 0);
}

/* ==========================================================================
 * YAML <-> properties
 * ========================================================================== */

static gboolean
parse_bool (const gchar *s,
            gboolean    *out)
{
	if (s == NULL)
		return FALSE;
	if (g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "yes") == 0 ||
	    g_ascii_strcasecmp (s, "on") == 0 || g_strcmp0 (s, "1") == 0)
		*out = TRUE;
	else if (g_ascii_strcasecmp (s, "false") == 0 || g_ascii_strcasecmp (s, "no") == 0 ||
	         g_ascii_strcasecmp (s, "off") == 0 || g_strcmp0 (s, "0") == 0)
		*out = FALSE;
	else
		return FALSE;

	return TRUE;
}

/*
 * node_to_value:
 *
 * Converts a YAML node to a GValue of @pspec's type, validating ranges.
 * Returns FALSE (leaving @value unset) when the node does not fit.
 */
static gboolean
node_to_value (YamlNode   *node,
               GParamSpec *pspec,
               GValue     *value)
{
	GType        type = pspec->value_type;
	const gchar *scalar = NULL;
	gchar       *end = NULL;

	if (type == G_TYPE_STRV)
	{
		YamlSequence *seq;
		GPtrArray    *arr;
		guint         i, n;

		if (yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
			return FALSE;
		seq = yaml_node_get_sequence (node);
		n = yaml_sequence_get_length (seq);
		arr = g_ptr_array_new ();
		for (i = 0; i < n; i++)
		{
			YamlNode *e = yaml_sequence_get_element (seq, i);

			if (e != NULL && yaml_node_get_node_type (e) == YAML_NODE_SCALAR)
				g_ptr_array_add (arr, g_strdup (yaml_node_get_scalar (e)));
		}
		g_ptr_array_add (arr, NULL);
		g_value_init (value, G_TYPE_STRV);
		g_value_take_boxed (value, g_ptr_array_free (arr, FALSE));
		return TRUE;
	}

	if (yaml_node_get_node_type (node) == YAML_NODE_NULL)
	{
		if (type != G_TYPE_STRING)
			return FALSE;
		g_value_init (value, G_TYPE_STRING);
		return TRUE;
	}
	if (yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return FALSE;
	scalar = yaml_node_get_scalar (node);

	g_value_init (value, type);
	switch (G_TYPE_FUNDAMENTAL (type))
	{
	case G_TYPE_STRING:
		g_value_set_string (value, scalar);
		break;
	case G_TYPE_BOOLEAN:
		{
			gboolean b;

			if (!parse_bool (scalar, &b))
				goto bad;
			g_value_set_boolean (value, b);
		}
		break;
	case G_TYPE_INT:
		{
			gint64 v = g_ascii_strtoll (scalar, &end, 10);

			if (end == scalar || *end != '\0' || v < G_MININT || v > G_MAXINT)
				goto bad;
			g_value_set_int (value, (gint) v);
		}
		break;
	case G_TYPE_UINT:
		{
			guint64 v;

			if (scalar[0] == '-')
				goto bad;
			v = g_ascii_strtoull (scalar, &end, 10);
			if (end == scalar || *end != '\0' || v > G_MAXUINT)
				goto bad;
			g_value_set_uint (value, (guint) v);
		}
		break;
	case G_TYPE_DOUBLE:
		{
			gdouble v = g_ascii_strtod (scalar, &end);

			if (end == scalar || *end != '\0')
				goto bad;
			g_value_set_double (value, v);
		}
		break;
	default:
		goto bad;
	}

	/* Out-of-range values are rejected rather than silently clamped. */
	if (g_param_value_validate (pspec, value))
		goto bad;

	return TRUE;

bad:
	g_value_unset (value);
	return FALSE;
}

static void
value_to_mapping (YamlMapping  *map,
                  const gchar  *key,
                  const GValue *value)
{
	GType type = G_VALUE_TYPE (value);

	if (type == G_TYPE_STRV)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();
		const gchar * const    *v = g_value_get_boxed (value);
		guint                   i;

		for (i = 0; v != NULL && v[i] != NULL; i++)
			yaml_sequence_add_string_element (seq, v[i]);
		yaml_mapping_set_sequence_member (map, key, seq);
		return;
	}

	switch (G_TYPE_FUNDAMENTAL (type))
	{
	case G_TYPE_STRING:
		if (g_value_get_string (value) == NULL)
			yaml_mapping_set_null_member (map, key);
		else
			yaml_mapping_set_string_member (map, key, g_value_get_string (value));
		break;
	case G_TYPE_BOOLEAN:
		yaml_mapping_set_boolean_member (map, key, g_value_get_boolean (value));
		break;
	case G_TYPE_INT:
		yaml_mapping_set_int_member (map, key, g_value_get_int (value));
		break;
	case G_TYPE_UINT:
		yaml_mapping_set_int_member (map, key, g_value_get_uint (value));
		break;
	case G_TYPE_DOUBLE:
		yaml_mapping_set_double_member (map, key, g_value_get_double (value));
		break;
	default:
		break;
	}
}

static gboolean
apply_root (LrgEditorSettings  *self,
            YamlNode           *root,
            GError            **error)
{
	YamlMapping *map;
	GList       *keys;
	GList       *l;

	if (root == NULL || yaml_node_get_node_type (root) == YAML_NODE_NULL)
		return TRUE;  /* an empty file means "all defaults" */

	if (yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     "Editor settings must be a YAML mapping");
		return FALSE;
	}

	map = yaml_node_get_mapping (root);
	keys = yaml_mapping_get_members (map);

	g_object_freeze_notify (G_OBJECT (self));
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar *key = l->data;
		YamlNode    *node = yaml_mapping_get_member (map, key);
		GParamSpec  *pspec;
		GValue       value = G_VALUE_INIT;

		if (g_strcmp0 (key, KEYBINDINGS_KEY) == 0)
		{
			YamlMapping *kb;
			GList       *actions, *a;

			if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
			{
				g_message ("editor settings: '%s' must be a mapping; ignored", key);
				continue;
			}
			kb = yaml_node_get_mapping (node);
			actions = yaml_mapping_get_members (kb);
			g_hash_table_remove_all (self->keybindings);
			for (a = actions; a != NULL; a = a->next)
			{
				YamlNode *s = yaml_mapping_get_member (kb, a->data);

				if (s != NULL && yaml_node_get_node_type (s) == YAML_NODE_SCALAR)
					g_hash_table_insert (self->keybindings, g_strdup (a->data),
					                     g_strdup (yaml_node_get_scalar (s)));
				else if (s != NULL && yaml_node_get_node_type (s) == YAML_NODE_NULL)
					g_hash_table_insert (self->keybindings, g_strdup (a->data), g_strdup (""));
			}
			g_list_free (actions);
			continue;
		}

		pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (self), key);
		if (pspec == NULL)
		{
			/* Preserve keys from newer editors / other tools. */
			g_hash_table_insert (self->unknown, g_strdup (key), yaml_node_ref (node));
			continue;
		}

		if (!node_to_value (node, pspec, &value))
		{
			g_message ("editor settings: invalid value for '%s'; keeping default", key);
			continue;
		}
		g_object_set_property (G_OBJECT (self), key, &value);
		g_value_unset (&value);
	}
	g_object_thaw_notify (G_OBJECT (self));
	g_list_free (keys);

	g_signal_emit (self, signals[SIGNAL_KEYBINDINGS_CHANGED], 0);
	return TRUE;
}

gboolean
lrg_editor_settings_load_from_data (LrgEditorSettings  *self,
                                    const gchar        *data,
                                    GError            **error)
{
	g_autoptr(YamlParser) parser = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, -1, error))
		return FALSE;

	return apply_root (self, yaml_parser_get_root (parser), error);
}

gboolean
lrg_editor_settings_load (LrgEditorSettings  *self,
                          const gchar        *path,
                          GError            **error)
{
	g_autofree gchar *resolved = NULL;
	g_autofree gchar *data = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), FALSE);

	resolved = (path != NULL) ? g_strdup (path) : lrg_editor_settings_get_default_path ();
	g_free (self->loaded_path);
	self->loaded_path = g_strdup (resolved);

	if (!g_file_get_contents (resolved, &data, NULL, &local))
	{
		if (g_error_matches (local, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&local));
		return FALSE;
	}

	if (!lrg_editor_settings_load_from_data (self, data, &local))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		             "%s: %s", resolved, local->message);
		return FALSE;
	}

	return TRUE;
}

gchar *
lrg_editor_settings_to_data (LrgEditorSettings *self)
{
	g_autoptr(YamlMapping)   map = NULL;
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	GHashTableIter           iter;
	gpointer                 key;
	gpointer                 value;
	guint                    i;

	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), NULL);

	map = yaml_mapping_new ();
	for (i = 1; i < N_PROPS; i++)
	{
		GValue v = G_VALUE_INIT;

		g_value_init (&v, properties[i]->value_type);
		g_object_get_property (G_OBJECT (self), properties[i]->name, &v);
		value_to_mapping (map, properties[i]->name, &v);
		g_value_unset (&v);
	}

	/* Keybinding overrides, sorted for a stable file. */
	{
		g_autoptr(YamlMapping) kb = yaml_mapping_new ();
		GList                 *actions = lrg_editor_settings_list_keybindings (self);
		GList                 *l;

		for (l = actions; l != NULL; l = l->next)
			yaml_mapping_set_string_member (kb, l->data,
			                                g_hash_table_lookup (self->keybindings, l->data));
		g_list_free (actions);
		yaml_mapping_set_mapping_member (map, KEYBINDINGS_KEY, kb);
	}

	g_hash_table_iter_init (&iter, self->unknown);
	while (g_hash_table_iter_next (&iter, &key, &value))
		yaml_mapping_set_member (map, key, value);

	root = yaml_node_new_mapping (map);
	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, NULL, NULL);
}

gboolean
lrg_editor_settings_save (LrgEditorSettings  *self,
                          const gchar        *path,
                          GError            **error)
{
	g_autofree gchar *resolved = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *data = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), FALSE);

	if (path != NULL)
		resolved = g_strdup (path);
	else if (self->loaded_path != NULL)
		resolved = g_strdup (self->loaded_path);
	else
		resolved = lrg_editor_settings_get_default_path ();

	dir = g_path_get_dirname (resolved);
	if (g_mkdir_with_parents (dir, 0700) != 0)
	{
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
		             "Cannot create %s: %s", dir, g_strerror (errno));
		return FALSE;
	}

	data = lrg_editor_settings_to_data (self);
	if (data == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     "Failed to serialize editor settings");
		return FALSE;
	}

	return g_file_set_contents (resolved, data, -1, error);
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

const gchar *
lrg_editor_settings_get_external_editor (LrgEditorSettings *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), NULL);

	return (self->external_editor != NULL && *self->external_editor != '\0')
	       ? self->external_editor : "auto";
}

const gchar *
lrg_editor_settings_get_external_editor_command (LrgEditorSettings *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), NULL);

	return self->external_editor_command;
}

const gchar *
lrg_editor_settings_get_terminal_command (LrgEditorSettings *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), NULL);

	return self->terminal_command;
}

gboolean
lrg_editor_settings_get_emacs_integration (LrgEditorSettings *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), FALSE);

	return self->emacs_integration;
}

/* Moves @path to the front of @list, deduplicated and capped. */
static void
push_recent (gchar       ***list,
             const gchar   *path)
{
	GPtrArray *arr = g_ptr_array_new ();
	guint      i;

	g_ptr_array_add (arr, g_strdup (path));
	for (i = 0; *list != NULL && (*list)[i] != NULL; i++)
	{
		if (arr->len >= LRG_EDITOR_SETTINGS_MAX_RECENT)
			break;
		if (g_strcmp0 ((*list)[i], path) != 0)
			g_ptr_array_add (arr, g_strdup ((*list)[i]));
	}
	g_ptr_array_add (arr, NULL);

	g_strfreev (*list);
	*list = (gchar **) g_ptr_array_free (arr, FALSE);
}

void
lrg_editor_settings_add_recent_project (LrgEditorSettings *self,
                                        const gchar       *path)
{
	g_return_if_fail (LRG_IS_EDITOR_SETTINGS (self));
	g_return_if_fail (path != NULL);

	push_recent (&self->recent_projects, path);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RECENT_PROJECTS]);
}

void
lrg_editor_settings_add_recent_level (LrgEditorSettings *self,
                                      const gchar       *path)
{
	g_return_if_fail (LRG_IS_EDITOR_SETTINGS (self));
	g_return_if_fail (path != NULL);

	push_recent (&self->recent_levels, path);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RECENT_LEVELS]);
}

const gchar * const *
lrg_editor_settings_get_recent_projects (LrgEditorSettings *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), NULL);

	return (const gchar * const *) self->recent_projects;
}

const gchar * const *
lrg_editor_settings_get_recent_levels (LrgEditorSettings *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), NULL);

	return (const gchar * const *) self->recent_levels;
}

const gchar *
lrg_editor_settings_get_keybinding (LrgEditorSettings *self,
                                    const gchar       *action)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), NULL);
	g_return_val_if_fail (action != NULL, NULL);

	return g_hash_table_lookup (self->keybindings, action);
}

void
lrg_editor_settings_set_keybinding (LrgEditorSettings *self,
                                    const gchar       *action,
                                    const gchar       *shortcut)
{
	g_return_if_fail (LRG_IS_EDITOR_SETTINGS (self));
	g_return_if_fail (action != NULL);

	if (shortcut == NULL)
		g_hash_table_remove (self->keybindings, action);
	else
		g_hash_table_insert (self->keybindings, g_strdup (action), g_strdup (shortcut));

	g_signal_emit (self, signals[SIGNAL_KEYBINDINGS_CHANGED], 0);
}

GList *
lrg_editor_settings_list_keybindings (LrgEditorSettings *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (self), NULL);

	return g_list_sort (g_hash_table_get_keys (self->keybindings),
	                    (GCompareFunc) g_strcmp0);
}
