/* lrg-controller-rumble.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Controller vibration through Linux force feedback (evdev).
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_INPUT

#include "lrg-controller-rumble.h"
#include "../lrg-log.h"

#include <gio/gio.h>

#include <string.h>

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#endif

struct _LrgControllerRumble
{
	GObject parent_instance;

	gchar *name;       /* the controller's name */
	gchar *sysfs_root;
	gchar *dev_root;
	gint   fd;         /* -1 while closed */
	gint   effect_id;  /* the uploaded effect, -1 for none */
	gboolean failed;   /* opening failed; do not retry every hit */
};

G_DEFINE_FINAL_TYPE (LrgControllerRumble, lrg_controller_rumble, G_TYPE_OBJECT)

static void
lrg_controller_rumble_dispose (GObject *object)
{
	LrgControllerRumble *self = LRG_CONTROLLER_RUMBLE (object);

#ifdef __linux__
	if (self->fd >= 0)
	{
		lrg_controller_rumble_stop (self);
		close (self->fd);
	}
#endif
	self->fd = -1;

	G_OBJECT_CLASS (lrg_controller_rumble_parent_class)->dispose (object);
}

static void
lrg_controller_rumble_finalize (GObject *object)
{
	LrgControllerRumble *self = LRG_CONTROLLER_RUMBLE (object);

	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->sysfs_root, g_free);
	g_clear_pointer (&self->dev_root, g_free);

	G_OBJECT_CLASS (lrg_controller_rumble_parent_class)->finalize (object);
}

static void
lrg_controller_rumble_class_init (LrgControllerRumbleClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_controller_rumble_dispose;
	object_class->finalize = lrg_controller_rumble_finalize;
}

static void
lrg_controller_rumble_init (LrgControllerRumble *self)
{
	self->fd = -1;
	self->effect_id = -1;
}

LrgControllerRumble *
lrg_controller_rumble_new_with_roots (const gchar *name,
                                      const gchar *sysfs_root,
                                      const gchar *dev_root)
{
	LrgControllerRumble *self;

	g_return_val_if_fail (name != NULL, NULL);

	self = g_object_new (LRG_TYPE_CONTROLLER_RUMBLE, NULL);
	self->name = g_strdup (name);
	self->sysfs_root = g_strdup (sysfs_root != NULL ? sysfs_root : "/sys");
	self->dev_root = g_strdup (dev_root != NULL ? dev_root : "/dev/input");

	return self;
}

LrgControllerRumble *
lrg_controller_rumble_new (const gchar *name)
{
	return lrg_controller_rumble_new_with_roots (name, NULL, NULL);
}

/*
 * lrg_controller_rumble_find_node:
 *
 * Walks class/input/input*: the one whose "name" is ours has an "eventN"
 * child directory, and /dev/input/eventN is the node to open.
 */
gchar *
lrg_controller_rumble_find_node (LrgControllerRumble *self)
{
	g_autofree gchar *input_dir = NULL;
	g_autoptr(GDir) dir = NULL;
	const gchar *entry;

	g_return_val_if_fail (LRG_IS_CONTROLLER_RUMBLE (self), NULL);

	input_dir = g_build_filename (self->sysfs_root, "class", "input", NULL);
	dir = g_dir_open (input_dir, 0, NULL);
	if (dir == NULL)
		return NULL;

	while ((entry = g_dir_read_name (dir)) != NULL)
	{
		g_autofree gchar *name_path = NULL;
		g_autofree gchar *dev_name = NULL;
		g_autofree gchar *device_dir = NULL;
		g_autoptr(GDir) children = NULL;
		const gchar *child;

		if (!g_str_has_prefix (entry, "input"))
			continue;

		name_path = g_build_filename (input_dir, entry, "name", NULL);
		if (!g_file_get_contents (name_path, &dev_name, NULL, NULL))
			continue;
		g_strstrip (dev_name);
		if (g_strcmp0 (dev_name, self->name) != 0)
			continue;

		/* the eventN directory sits next to name inside inputN */
		device_dir = g_build_filename (input_dir, entry, NULL);
		children = g_dir_open (device_dir, 0, NULL);
		if (children == NULL)
			continue;

		while ((child = g_dir_read_name (children)) != NULL)
		{
			if (g_str_has_prefix (child, "event"))
				return g_build_filename (self->dev_root, child, NULL);
		}
	}

	return NULL;
}

#ifdef __linux__

/* opens the node and checks it can rumble; FALSE with @error otherwise */
static gboolean
rumble_open (LrgControllerRumble  *self,
             GError              **error)
{
	g_autofree gchar *node = NULL;
	unsigned long bits[(FF_MAX + 8 * sizeof (unsigned long)) / (8 * sizeof (unsigned long))];
	gint fd;

	if (self->fd >= 0)
		return TRUE;

	if (self->failed)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "rumble unavailable (earlier open failed)");
		return FALSE;
	}

	/* chunk 1: the node */
	node = lrg_controller_rumble_find_node (self);
	if (node == NULL)
	{
		self->failed = TRUE;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		             "no input device named '%s'", self->name);
		return FALSE;
	}

	/* chunk 2: open it for writing (force feedback is written as events) */
	fd = open (node, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
	{
		self->failed = TRUE;
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
		             "cannot open %s: %s", node, g_strerror (errno));
		return FALSE;
	}

	/* chunk 3: the device must advertise the rumble effect */
	memset (bits, 0, sizeof (bits));
	if (ioctl (fd, EVIOCGBIT (EV_FF, sizeof (bits)), bits) < 0 ||
	    (bits[FF_RUMBLE / (8 * sizeof (unsigned long))] &
	     (1ul << (FF_RUMBLE % (8 * sizeof (unsigned long))))) == 0)
	{
		close (fd);
		self->failed = TRUE;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "%s has no rumble force feedback", node);
		return FALSE;
	}

	self->fd = fd;
	return TRUE;
}

#endif /* __linux__ */

gboolean
lrg_controller_rumble_play (LrgControllerRumble  *self,
                            gdouble               strong,
                            gdouble               weak,
                            guint                 duration_ms,
                            GError              **error)
{
	g_return_val_if_fail (LRG_IS_CONTROLLER_RUMBLE (self), FALSE);

#ifdef __linux__
	{
		struct ff_effect effect;
		struct input_event play;

		if (!rumble_open (self, error))
			return FALSE;

		/* chunk 1: describe the effect; a reused id replaces the old one */
		memset (&effect, 0, sizeof (effect));
		effect.type = FF_RUMBLE;
		effect.id = (gint16)self->effect_id;
		effect.replay.length = (guint16)MIN (duration_ms, 65535u);
		effect.u.rumble.strong_magnitude = (guint16)(CLAMP (strong, 0.0, 1.0) * 65535.0);
		effect.u.rumble.weak_magnitude = (guint16)(CLAMP (weak, 0.0, 1.0) * 65535.0);

		if (ioctl (self->fd, EVIOCSFF, &effect) < 0)
		{
			g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
			             "cannot upload the rumble effect: %s", g_strerror (errno));
			return FALSE;
		}
		self->effect_id = effect.id;

		/* chunk 2: play it once */
		memset (&play, 0, sizeof (play));
		play.type = EV_FF;
		play.code = (guint16)self->effect_id;
		play.value = 1;
		if (write (self->fd, &play, sizeof (play)) != (gssize)sizeof (play))
		{
			g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
			             "cannot start the rumble effect: %s", g_strerror (errno));
			return FALSE;
		}

		return TRUE;
	}
#else
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	                     "rumble through evdev needs Linux");
	return FALSE;
#endif
}

void
lrg_controller_rumble_stop (LrgControllerRumble *self)
{
	g_return_if_fail (LRG_IS_CONTROLLER_RUMBLE (self));

#ifdef __linux__
	if (self->fd >= 0 && self->effect_id >= 0)
	{
		/* removing the effect stops it and frees the slot */
		if (ioctl (self->fd, EVIOCRMFF, self->effect_id) < 0)
			g_debug ("rumble: EVIOCRMFF failed: %s", g_strerror (errno));
		self->effect_id = -1;
	}
#endif
}

gboolean
lrg_controller_rumble_is_open (LrgControllerRumble *self)
{
	g_return_val_if_fail (LRG_IS_CONTROLLER_RUMBLE (self), FALSE);

	return self->fd >= 0;
}
