/* lrg-platform.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Platform preparation: the portable parts (data root, asset extraction)
 * and, on Android, the glue that applies them to the app's internal
 * storage and APK, plus a GLib log writer for logcat.  Also the rest of
 * what a phone needs from the platform: the external storage directory,
 * the display density, the soft keyboard with a character queue that
 * works there, and the app lifecycle (LrgPlatform's paused/resumed),
 * which on Android comes from handlers chained in front of raylib's.
 */

/* The NDK headers used below define plain `inline' helpers that call APIs newer
 * than libregnum's minimum (ANativeWindow_clearFrameRate, API 31).  Under
 * gnu89 a plain inline function is emitted out of line in every file, so
 * its reference to the newer API would be a link error.  This is the NDK's
 * own switch for "use newer APIs only when present": such references
 * become weak (null on older devices), and nothing here calls them.  It
 * must come before the first system header, hence before config.h. */
#ifdef LRG_PLATFORM_ANDROID
#define __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ 1
#endif

#include "config.h"

#include <errno.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <graylib.h>

#include "lrg-platform.h"
#include "lrg-platform-private.h"

#ifdef LRG_PLATFORM_ANDROID
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <jni.h>

/* raylib's PLATFORM_ANDROID (rcore_android.c) keeps the app its
 * android_main() was given and exposes it without a header */
extern struct android_app *GetAndroidApp (void);
#endif

/* ==========================================================================
 * Data root
 * ========================================================================== */

/* set_dir: one environment variable pointing at a created directory */
static gboolean
set_dir (const gchar  *variable,
         const gchar  *path,
         GError      **error)
{
	if (g_mkdir_with_parents (path, 0700) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot create %s (%s): %s", path, variable, g_strerror (saved));
		return FALSE;
	}
	if (!g_setenv (variable, path, TRUE))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot set %s", variable);
		return FALSE;
	}
	return TRUE;
}

/* lrg_platform_set_data_root: documented in lrg-platform.h */
gboolean
lrg_platform_set_data_root (const gchar  *root,
                            GError      **error)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *config = NULL;
	g_autofree gchar *cache = NULL;
	g_autofree gchar *tmp = NULL;

	g_return_val_if_fail (root != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	data = g_build_filename (root, "data", NULL);
	config = g_build_filename (root, "config", NULL);
	cache = g_build_filename (root, "cache", NULL);
	tmp = g_build_filename (root, "tmp", NULL);

	return set_dir ("HOME", root, error) &&
	       set_dir ("XDG_DATA_HOME", data, error) &&
	       set_dir ("XDG_CONFIG_HOME", config, error) &&
	       set_dir ("XDG_CACHE_HOME", cache, error) &&
	       set_dir ("TMPDIR", tmp, error);
}

/* ==========================================================================
 * Asset extraction
 * ========================================================================== */

#define STAMP_FILE ".lrg-assets-stamp"
#define MANIFEST_MAGIC "# lrg-assets "

/* parse_manifest: the stamp and the file list of a manifest, every path
 * checked before anything is written */
static gboolean
parse_manifest (GBytes      *bytes,
                gchar      **out_stamp,
                GPtrArray  **out_paths,
                GError     **error)
{
	g_autofree gchar *text = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GPtrArray) paths = NULL;
	gsize size;
	const gchar *data;
	guint i;

	data = g_bytes_get_data (bytes, &size);
	text = g_strndup (data, size);
	lines = g_strsplit (text, "\n", -1);
	if (lines[0] == NULL || !g_str_has_prefix (lines[0], MANIFEST_MAGIC) ||
	    g_strstrip (lines[0] + strlen (MANIFEST_MAGIC))[0] == '\0')
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		             "%s does not start with \"%s<stamp>\"",
		             LRG_PLATFORM_ASSET_MANIFEST, MANIFEST_MAGIC);
		return FALSE;
	}

	paths = g_ptr_array_new_with_free_func (g_free);
	for (i = 1; lines[i] != NULL; i++)
	{
		gchar *line = g_strstrip (lines[i]);

		if (line[0] == '\0' || line[0] == '#')
			continue;
		if (!lrg_platform_asset_path_is_safe (line))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			             "%s line %u: unsafe path \"%s\"",
			             LRG_PLATFORM_ASSET_MANIFEST, i + 1, line);
			return FALSE;
		}
		g_ptr_array_add (paths, g_strdup (line));
	}

	*out_stamp = g_strdup (g_strstrip (lines[0] + strlen (MANIFEST_MAGIC)));
	*out_paths = g_steal_pointer (&paths);
	return TRUE;
}

/* remove_tree: deletes @path and everything below it, without following
 * symbolic links (they are removed, not entered) */
static gboolean
remove_tree (const gchar  *path,
             GError      **error)
{
	g_autoptr(GDir) dir = NULL;
	const gchar *name;

	if (!g_file_test (path, G_FILE_TEST_EXISTS) && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
		return TRUE;
	if (g_file_test (path, G_FILE_TEST_IS_DIR) && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
	{
		dir = g_dir_open (path, 0, error);
		if (dir == NULL)
			return FALSE;
		while ((name = g_dir_read_name (dir)) != NULL)
		{
			g_autofree gchar *child = g_build_filename (path, name, NULL);

			if (!remove_tree (child, error))
				return FALSE;
		}
		if (g_rmdir (path) != 0)
		{
			gint saved = errno;

			g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
			             "Cannot remove %s: %s", path, g_strerror (saved));
			return FALSE;
		}
		return TRUE;
	}
	if (g_unlink (path) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot remove %s: %s", path, g_strerror (saved));
		return FALSE;
	}
	return TRUE;
}

/* lrg_platform_extract_assets: documented in lrg-platform.h */
gboolean
lrg_platform_extract_assets (LrgPlatformAssetReadFunc   read,
                             gpointer                   user_data,
                             const gchar               *dest,
                             GError                   **error)
{
	g_autoptr(GBytes) manifest = NULL;
	g_autoptr(GPtrArray) paths = NULL;
	g_autofree gchar *stamp = NULL;
	g_autofree gchar *stamp_file = NULL;
	g_autofree gchar *current = NULL;
	g_autofree gchar *staging = NULL;
	g_autofree gchar *retired = NULL;
	g_autofree gchar *staged_stamp = NULL;
	guint i;

	g_return_val_if_fail (read != NULL, FALSE);
	g_return_val_if_fail (dest != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* the manifest decides everything; a bad one writes nothing */
	manifest = read (LRG_PLATFORM_ASSET_MANIFEST, user_data, error);
	if (manifest == NULL)
		return FALSE;
	if (!parse_manifest (manifest, &stamp, &paths, error))
		return FALSE;

	/* up to date already? */
	stamp_file = g_build_filename (dest, STAMP_FILE, NULL);
	if (g_file_get_contents (stamp_file, &current, NULL, NULL) &&
	    g_strcmp0 (g_strstrip (current), stamp) == 0)
		return TRUE;

	/* extract into <dest>.new (left over from an interrupted run: start
	 * clean) */
	staging = g_strconcat (dest, ".new", NULL);
	retired = g_strconcat (dest, ".old", NULL);
	if (!remove_tree (staging, error) || !remove_tree (retired, error))
		return FALSE;
	if (g_mkdir_with_parents (staging, 0700) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot create %s: %s", staging, g_strerror (saved));
		return FALSE;
	}
	for (i = 0; i < paths->len; i++)
	{
		const gchar *path = g_ptr_array_index (paths, i);
		g_autoptr(GBytes) bytes = NULL;
		g_autofree gchar *target = NULL;
		g_autofree gchar *parent = NULL;
		gconstpointer data;
		gsize size;

		bytes = read (path, user_data, error);
		if (bytes == NULL)
			return FALSE;
		target = g_build_filename (staging, path, NULL);
		parent = g_path_get_dirname (target);
		if (g_mkdir_with_parents (parent, 0700) != 0)
		{
			gint saved = errno;

			g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
			             "Cannot create %s: %s", parent, g_strerror (saved));
			return FALSE;
		}
		data = g_bytes_get_data (bytes, &size);
		if (!g_file_set_contents (target, data, (gssize) size, error))
			return FALSE;
	}
	staged_stamp = g_build_filename (staging, STAMP_FILE, NULL);
	if (!g_file_set_contents (staged_stamp, stamp, -1, error))
		return FALSE;

	/* swap: the old tree steps aside, the new one takes its name */
	if (g_file_test (dest, G_FILE_TEST_EXISTS) && g_rename (dest, retired) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot move %s aside: %s", dest, g_strerror (saved));
		return FALSE;
	}
	if (g_rename (staging, dest) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot move %s into place: %s", staging, g_strerror (saved));
		return FALSE;
	}
	return remove_tree (retired, error);
}

/* ==========================================================================
 * Android
 * ========================================================================== */

#ifdef LRG_PLATFORM_ANDROID

/* read_apk_asset: LrgPlatformAssetReadFunc over the APK's assets/ */
static GBytes *
read_apk_asset (const gchar  *path,
                gpointer      user_data,
                GError      **error)
{
	AAssetManager *manager = user_data;
	AAsset *asset;
	const void *buffer;
	off_t length;
	GBytes *bytes;

	asset = AAssetManager_open (manager, path, AASSET_MODE_BUFFER);
	if (asset == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		             "The APK has no asset %s", path);
		return NULL;
	}
	length = AAsset_getLength (asset);
	buffer = AAsset_getBuffer (asset);
	if (buffer == NULL)
	{
		AAsset_close (asset);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot read asset %s", path);
		return NULL;
	}
	bytes = g_bytes_new (buffer, (gsize) length);
	AAsset_close (asset);
	return bytes;
}

/* log_writer: GLib structured log -> logcat, tag = the log domain */
static GLogWriterOutput
log_writer (GLogLevelFlags   level,
            const GLogField *fields,
            gsize            n_fields,
            gpointer         user_data)
{
	const gchar *domain = "libregnum";
	const gchar *message = "";
	gsize i;

	for (i = 0; i < n_fields; i++)
	{
		if (g_strcmp0 (fields[i].key, "GLIB_DOMAIN") == 0 && fields[i].value != NULL)
			domain = fields[i].value;
		else if (g_strcmp0 (fields[i].key, "MESSAGE") == 0 && fields[i].value != NULL)
			message = fields[i].value;
	}
	__android_log_write (lrg_platform_android_priority (level), domain, message);
	return G_LOG_WRITER_HANDLED;
}

static gboolean
prepare_android (GError **error)
{
	struct android_app *app;
	g_autofree gchar *assets = NULL;
	const gchar *internal;

	/* logging first, so everything after reaches logcat */
	g_log_set_writer_func (log_writer, NULL, NULL);

	app = GetAndroidApp ();
	if (app == NULL || app->activity == NULL || app->activity->internalDataPath == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
		             "lrg_platform_prepare() runs before raylib's android_main()");
		return FALSE;
	}
	internal = app->activity->internalDataPath;

	if (!lrg_platform_set_data_root (internal, error))
		return FALSE;

	assets = g_build_filename (internal, "assets", NULL);
	if (!lrg_platform_extract_assets (read_apk_asset, app->activity->assetManager, assets, error))
		return FALSE;
	if (g_chdir (assets) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot change into %s: %s", assets, g_strerror (saved));
		return FALSE;
	}
	g_info ("Android: data root %s, assets in %s", internal, assets);
	return TRUE;
}

#endif /* LRG_PLATFORM_ANDROID */

/* lrg_platform_prepare: documented in lrg-platform.h */
gboolean
lrg_platform_prepare (GError **error)
{
#ifdef LRG_PLATFORM_ANDROID
	static gsize once = 0;
	static gboolean result = FALSE;
	static GError *first_error = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (g_once_init_enter (&once))
	{
		result = prepare_android (&first_error);
		g_once_init_leave (&once, 1);
	}
	if (!result && first_error != NULL)
		g_propagate_error (error, g_error_copy (first_error));
	return result;
#else
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return TRUE;
#endif
}

/* ==========================================================================
 * Lifecycle object
 * ========================================================================== */

struct _LrgPlatform
{
	GObject parent_instance;

	gboolean running;   /* between resumed and paused: TRUE */
};

enum
{
	PROP_0,
	PROP_RUNNING,
	N_PROPS
};

enum
{
	SIGNAL_PAUSED,
	SIGNAL_RESUMED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgPlatform, lrg_platform, G_TYPE_OBJECT)

static void
lrg_platform_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgPlatform *self = LRG_PLATFORM (object);

	switch (prop_id)
	{
	case PROP_RUNNING:
		g_value_set_boolean (value, self->running);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_platform_class_init (LrgPlatformClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_platform_get_property;

	/**
	 * LrgPlatform:running:
	 *
	 * %FALSE between #LrgPlatform::paused and #LrgPlatform::resumed.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_RUNNING] =
		g_param_spec_boolean ("running", "Running",
		                      "Whether the app is shown and interactive",
		                      TRUE,
		                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgPlatform::paused:
	 * @self: the #LrgPlatform
	 *
	 * The app went to the background or lost its window: frames stop
	 * until #LrgPlatform::resumed.  Android may end a paused process
	 * without further notice, so save here.
	 *
	 * Since: 0.13.0
	 */
	signals[SIGNAL_PAUSED] =
		g_signal_new ("paused", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgPlatform::resumed:
	 * @self: the #LrgPlatform
	 * @gl_context_lost: %TRUE when the GL context did not survive the
	 *   pause: every texture, mesh, shader and framebuffer made before it
	 *   is gone and must be loaded again
	 *
	 * The app is shown and interactive again.
	 *
	 * Since: 0.13.0
	 */
	signals[SIGNAL_RESUMED] =
		g_signal_new ("resumed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
lrg_platform_init (LrgPlatform *self)
{
	self->running = TRUE;
}

/* lrg_platform_get_default: documented in lrg-platform.h */
LrgPlatform *
lrg_platform_get_default (void)
{
	static LrgPlatform *instance = NULL;

	if (g_once_init_enter_pointer (&instance))
	{
		LrgPlatform *created = g_object_new (LRG_TYPE_PLATFORM, NULL);

		g_once_init_leave_pointer (&instance, created);
	}
	return instance;
}

/* lrg_platform_is_running: documented in lrg-platform.h */
gboolean
lrg_platform_is_running (LrgPlatform *self)
{
	g_return_val_if_fail (LRG_IS_PLATFORM (self), FALSE);

	return self->running;
}

/* set_running: the one place that changes the state and emits; a
 * repeated state is not emitted twice */
static void
set_running (LrgPlatform *self,
             gboolean     running,
             gboolean     gl_context_lost)
{
	if (self->running == running)
		return;
	self->running = running;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RUNNING]);
	if (running)
		g_signal_emit (self, signals[SIGNAL_RESUMED], 0, gl_context_lost);
	else
		g_signal_emit (self, signals[SIGNAL_PAUSED], 0);
}

/* lrg_platform_simulate_pause: documented in lrg-platform.h */
void
lrg_platform_simulate_pause (LrgPlatform *self)
{
	g_return_if_fail (LRG_IS_PLATFORM (self));

	set_running (self, FALSE, FALSE);
}

/* lrg_platform_simulate_resume: documented in lrg-platform.h */
void
lrg_platform_simulate_resume (LrgPlatform *self,
                              gboolean     gl_context_lost)
{
	g_return_if_fail (LRG_IS_PLATFORM (self));

	set_running (self, TRUE, gl_context_lost);
}

/* ==========================================================================
 * Character queue (filled on Android by the input hook below)
 * ========================================================================== */

#ifdef LRG_PLATFORM_ANDROID
#define CHAR_QUEUE_MAX (64)

/* the main thread's typed characters, oldest first; raylib's own queue is
 * bounded the same way (it drops when full) */
static gunichar char_queue[CHAR_QUEUE_MAX];
static guint char_queue_len = 0;

static void
char_queue_push (gunichar c)
{
	if (char_queue_len < CHAR_QUEUE_MAX)
		char_queue[char_queue_len++] = c;
}
#endif

/* lrg_platform_get_char_pressed: documented in lrg-platform.h */
gunichar
lrg_platform_get_char_pressed (void)
{
#ifdef LRG_PLATFORM_ANDROID
	gunichar c;

	if (char_queue_len == 0)
		return 0;
	c = char_queue[0];
	char_queue_len--;
	memmove (char_queue, char_queue + 1, char_queue_len * sizeof (gunichar));
	return c;
#else
	gint c;

	c = grl_input_get_char_pressed ();
	return c > 0 ? (gunichar) c : 0;
#endif
}

/* ==========================================================================
 * Android: storage, density, soft keyboard, hooks
 * ========================================================================== */

static gboolean soft_keyboard_requested = FALSE;

#ifdef LRG_PLATFORM_ANDROID

/* What raylib installed, called first from ours */
static void    (*raylib_on_app_cmd)     (struct android_app *app, int32_t cmd) = NULL;
static int32_t (*raylib_on_input_event) (struct android_app *app, AInputEvent *event) = NULL;

/* Lifecycle state as the commands report it */
static gboolean app_focused = TRUE;
static gboolean app_has_window = TRUE;
static gboolean app_activity_paused = FALSE;
static gboolean window_was_terminated = FALSE;
static gboolean context_lost_pending = FALSE;
static EGLContext context_before_term = EGL_NO_CONTEXT;

/* KeyEvent's class and methods, looked up once (a global reference) */
static jclass key_event_class = NULL;
static jmethodID key_event_new = NULL;
static jmethodID key_event_get_unicode_char = NULL;

/* key_event_unicode: the character a key-down event types, through
 * android.view.KeyEvent.getUnicodeChar(metaState) on a Java copy of the
 * event (the NDK has no equivalent).  0 for keys that type nothing, dead
 * keys (combining accents) and control characters. */
static gunichar
key_event_unicode (struct android_app *app,
                   const AInputEvent  *event)
{
	JavaVM *vm = app->activity->vm;
	JNIEnv *env = NULL;
	gboolean attached = FALSE;
	jobject java_event;
	jint result = 0;

	if ((*vm)->GetEnv (vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK)
	{
		if ((*vm)->AttachCurrentThread (vm, &env, NULL) != JNI_OK)
			return 0;
		attached = TRUE;
	}

	if (key_event_class == NULL)
	{
		jclass local = (*env)->FindClass (env, "android/view/KeyEvent");

		if (local != NULL)
		{
			key_event_class = (jclass) (*env)->NewGlobalRef (env, local);
			(*env)->DeleteLocalRef (env, local);
			key_event_new = (*env)->GetMethodID (env, key_event_class, "<init>", "(JJIIIIIIII)V");
			key_event_get_unicode_char = (*env)->GetMethodID (env, key_event_class,
			                                                  "getUnicodeChar", "(I)I");
		}
		if ((*env)->ExceptionCheck (env))
			(*env)->ExceptionClear (env);
	}

	if (key_event_class != NULL && key_event_new != NULL && key_event_get_unicode_char != NULL)
	{
		/* NDK times are nanoseconds, Java's milliseconds */
		java_event = (*env)->NewObject (env, key_event_class, key_event_new,
		                                (jlong) (AKeyEvent_getDownTime (event) / 1000000),
		                                (jlong) (AKeyEvent_getEventTime (event) / 1000000),
		                                (jint) AKeyEvent_getAction (event),
		                                (jint) AKeyEvent_getKeyCode (event),
		                                (jint) AKeyEvent_getRepeatCount (event),
		                                (jint) AKeyEvent_getMetaState (event),
		                                (jint) AInputEvent_getDeviceId (event),
		                                (jint) AKeyEvent_getScanCode (event),
		                                (jint) AKeyEvent_getFlags (event),
		                                (jint) AInputEvent_getSource (event));
		if (java_event != NULL)
		{
			result = (*env)->CallIntMethod (env, java_event, key_event_get_unicode_char,
			                                (jint) AKeyEvent_getMetaState (event));
			(*env)->DeleteLocalRef (env, java_event);
		}
		if ((*env)->ExceptionCheck (env))
		{
			(*env)->ExceptionClear (env);
			result = 0;
		}
	}

	if (attached)
		(*vm)->DetachCurrentThread (vm);

	/* KeyCharacterMap.COMBINING_ACCENT: a dead key, not a character */
	if (((guint32) result & 0x80000000u) != 0)
		return 0;
	if (result < 0x20 || result == 0x7f)
		return 0;
	return (gunichar) result;
}

/* on_input_event: raylib's input handling, then characters for the
 * queue from key-down (and repeated) key events */
static int32_t
on_input_event (struct android_app *app,
                AInputEvent        *event)
{
	int32_t handled = 0;

	if (raylib_on_input_event != NULL)
		handled = raylib_on_input_event (app, event);

	if (AInputEvent_getType (event) == AINPUT_EVENT_TYPE_KEY)
	{
		int32_t action = AKeyEvent_getAction (event);

		/* ACTION_MULTIPLE with KEYCODE_UNKNOWN carries an IME string the
		 * NDK cannot read; with a real key code it is a key repeated */
		if (action == AKEY_EVENT_ACTION_DOWN ||
		    (action == AKEY_EVENT_ACTION_MULTIPLE && AKeyEvent_getKeyCode (event) != AKEYCODE_UNKNOWN))
		{
			gunichar c = key_event_unicode (app, event);
			gint repeat = action == AKEY_EVENT_ACTION_MULTIPLE ? AKeyEvent_getRepeatCount (event) : 1;
			gint i;

			for (i = 0; c != 0 && i < repeat; i++)
				char_queue_push (c);
		}
	}
	return handled;
}

/* update_running: emit when the combined state changes */
static void
update_running (void)
{
	gboolean running = app_focused && app_has_window && !app_activity_paused;
	gboolean lost = context_lost_pending;

	if (running)
		context_lost_pending = FALSE;
	set_running (lrg_platform_get_default (), running, lost);
}

/* on_app_cmd: raylib's command handling (it detaches the context on
 * TERM_WINDOW and re-attaches the same one on INIT_WINDOW), then the
 * lifecycle state and the context check */
static void
on_app_cmd (struct android_app *app,
            int32_t             cmd)
{
	if (cmd == APP_CMD_TERM_WINDOW)
		context_before_term = eglGetCurrentContext ();

	if (raylib_on_app_cmd != NULL)
		raylib_on_app_cmd (app, cmd);

	switch (cmd)
	{
	case APP_CMD_GAINED_FOCUS:
		app_focused = TRUE;
		break;
	case APP_CMD_LOST_FOCUS:
		app_focused = FALSE;
		break;
	case APP_CMD_PAUSE:
		app_activity_paused = TRUE;
		break;
	case APP_CMD_RESUME:
		app_activity_paused = FALSE;
		break;
	case APP_CMD_TERM_WINDOW:
		app_has_window = FALSE;
		window_was_terminated = TRUE;
		break;
	case APP_CMD_INIT_WINDOW:
		app_has_window = app->window != NULL;
		if (app_has_window && window_was_terminated)
		{
			/* raylib's eglMakeCurrent() was the last EGL call: its error
			 * first, then whether the old context is current again */
			EGLint error = eglGetError ();
			EGLContext now = eglGetCurrentContext ();

			if (error == EGL_CONTEXT_LOST || now == EGL_NO_CONTEXT || now != context_before_term)
				context_lost_pending = TRUE;
			window_was_terminated = FALSE;
		}
		break;
	default:
		break;
	}
	update_running ();
}

/* install_hooks: put ours in front of raylib's callbacks, once raylib
 * has installed them (InitWindow()) */
static void
install_hooks (void)
{
	struct android_app *app = GetAndroidApp ();

	if (app == NULL || app->onAppCmd == NULL || app->onInputEvent == NULL)
		return;
	if (app->onAppCmd != on_app_cmd)
	{
		raylib_on_app_cmd = app->onAppCmd;
		app->onAppCmd = on_app_cmd;
		app_has_window = app->window != NULL;
	}
	if (app->onInputEvent != on_input_event)
	{
		raylib_on_input_event = app->onInputEvent;
		app->onInputEvent = on_input_event;
	}
}

#endif /* LRG_PLATFORM_ANDROID */

/* lrg_platform_poll: documented in lrg-platform.h */
void
lrg_platform_poll (void)
{
#ifdef LRG_PLATFORM_ANDROID
	install_hooks ();
#endif
}

/* lrg_platform_get_external_data_dir: documented in lrg-platform.h */
const gchar *
lrg_platform_get_external_data_dir (void)
{
	g_autofree gchar *path = NULL;

#ifdef LRG_PLATFORM_ANDROID
	struct android_app *app = GetAndroidApp ();

	if (app == NULL || app->activity == NULL || app->activity->externalDataPath == NULL)
		return NULL;
	path = g_strdup (app->activity->externalDataPath);
#else
	const gchar *prgname = g_get_prgname ();
	g_autofree gchar *name = NULL;

	/* the program name may be a path (argv[0]): its last component */
	name = prgname != NULL && prgname[0] != '\0' ? g_path_get_basename (prgname) : NULL;
	if (name == NULL || name[0] == '\0' || g_str_equal (name, ".") || g_str_equal (name, G_DIR_SEPARATOR_S))
	{
		g_free (name);
		name = g_strdup ("libregnum");
	}
	path = g_build_filename (g_get_user_data_dir (), name, NULL);
#endif
	if (g_mkdir_with_parents (path, 0700) != 0)
	{
		g_debug ("Cannot create the external data directory %s: %s", path, g_strerror (errno));
		return NULL;
	}
	/* interned: the answer can change (GLib's user data dir in tests,
	 * the program name), and every answer stays valid */
	return g_intern_string (path);
}

/* lrg_platform_get_dp_scale: documented in lrg-platform.h */
gfloat
lrg_platform_get_dp_scale (void)
{
#ifdef LRG_PLATFORM_ANDROID
	struct android_app *app = GetAndroidApp ();
	int32_t density;

	if (app == NULL || app->config == NULL)
		return 1.0f;
	density = AConfiguration_getDensity (app->config);
	if (density <= 0 || density == ACONFIGURATION_DENSITY_ANY || density == ACONFIGURATION_DENSITY_NONE)
		return 1.0f;
	return MAX ((gfloat) density / 160.0f, 0.5f);
#else
	return 1.0f;
#endif
}

/* lrg_platform_show_soft_keyboard: documented in lrg-platform.h */
void
lrg_platform_show_soft_keyboard (void)
{
#ifdef LRG_PLATFORM_ANDROID
	struct android_app *app = GetAndroidApp ();

	if (app != NULL && app->activity != NULL)
		ANativeActivity_showSoftInput (app->activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED);
#endif
	soft_keyboard_requested = TRUE;
}

/* lrg_platform_hide_soft_keyboard: documented in lrg-platform.h */
void
lrg_platform_hide_soft_keyboard (void)
{
#ifdef LRG_PLATFORM_ANDROID
	struct android_app *app = GetAndroidApp ();

	if (app != NULL && app->activity != NULL)
		ANativeActivity_hideSoftInput (app->activity, 0);
#endif
	soft_keyboard_requested = FALSE;
}

/* lrg_platform_is_soft_keyboard_requested: documented in lrg-platform.h */
gboolean
lrg_platform_is_soft_keyboard_requested (void)
{
	return soft_keyboard_requested;
}
