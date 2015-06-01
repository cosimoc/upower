/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 *               2010 Alex Murray <murray.alex@gmail.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include "up-kbd-backlight.h"
#include "up-daemon.h"
#include "up-types.h"

static void     up_kbd_backlight_finalize   (GObject	*object);

#define UP_KBD_BACKLIGHT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), UP_TYPE_KBD_BACKLIGHT, UpKbdBacklightPrivate))

struct UpKbdBacklightPrivate
{
	gint			 fd;
	gint			 brightness;
	gint			 max_brightness;
};

G_DEFINE_TYPE (UpKbdBacklight, up_kbd_backlight, UP_TYPE_EXPORTED_KBD_BACKLIGHT_SKELETON)

/**
 * up_kbd_backlight_brightness_write:
 **/
static gboolean
up_kbd_backlight_brightness_write (UpKbdBacklight *kbd_backlight, gint value)
{
	gchar *text = NULL;
	gint retval;
	gint length;
	gboolean ret = TRUE;

	/* write new values to backlight */
	if (kbd_backlight->priv->fd < 0) {
		g_warning ("cannot write to kbd_backlight as file not open");
		ret = FALSE;
		goto out;
	}

	/* limit to between 0 and max */
	value = CLAMP (value, 0, kbd_backlight->priv->max_brightness);

	/* convert to text */
	text = g_strdup_printf ("%i", value);
	length = strlen (text);

	/* write to file */
	lseek (kbd_backlight->priv->fd, 0, SEEK_SET);
	retval = write (kbd_backlight->priv->fd, text, length);
	if (retval != length) {
		g_warning ("writing '%s' to device failed", text);
		ret = FALSE;
		goto out;
	}

	/* emit signal */
	kbd_backlight->priv->brightness = value;
	up_exported_kbd_backlight_emit_brightness_changed (UP_EXPORTED_KBD_BACKLIGHT (kbd_backlight),
							   kbd_backlight->priv->brightness);

out:
	g_free (text);
	return ret;
}

/**
 * up_kbd_backlight_get_brightness:
 *
 * Gets the current brightness
 **/
static gboolean
up_kbd_backlight_get_brightness (UpExportedKbdBacklight *skeleton,
				 GDBusMethodInvocation *invocation,
				 UpKbdBacklight *kbd_backlight)
{
	up_exported_kbd_backlight_complete_get_brightness (skeleton, invocation,
							   kbd_backlight->priv->brightness);
	return TRUE;
}

/**
 * up_kbd_backlight_get_max_brightness:
 *
 * Gets the max brightness
 **/
static gboolean
up_kbd_backlight_get_max_brightness (UpExportedKbdBacklight *skeleton,
				     GDBusMethodInvocation *invocation,
				     UpKbdBacklight *kbd_backlight)
{
	up_exported_kbd_backlight_complete_get_max_brightness (skeleton, invocation,
							       kbd_backlight->priv->max_brightness);
	return TRUE;
}

/**
 * up_kbd_backlight_set_brightness:
 **/
static gboolean
up_kbd_backlight_set_brightness (UpExportedKbdBacklight *skeleton,
				 GDBusMethodInvocation *invocation,
				 gint value,
				 UpKbdBacklight *kbd_backlight)
{
	gboolean ret = FALSE;

	g_debug ("setting brightness to %i", value);
	ret = up_kbd_backlight_brightness_write (kbd_backlight, value);

	if (ret) {
		up_exported_kbd_backlight_complete_set_brightness (skeleton, invocation);
	} else {
		g_dbus_method_invocation_return_error (invocation,
						       UP_DAEMON_ERROR, UP_DAEMON_ERROR_GENERAL,
						       "error writing brightness %d", value);
	}

	return TRUE;
}

/**
 * up_kbd_backlight_class_init:
 **/
static void
up_kbd_backlight_class_init (UpKbdBacklightClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = up_kbd_backlight_finalize;

	g_type_class_add_private (klass, sizeof (UpKbdBacklightPrivate));
}

/**
 * up_kbd_backlight_find:
 **/
static gboolean
up_kbd_backlight_find (UpKbdBacklight *kbd_backlight)
{
	gboolean ret;
	gboolean found = FALSE;
	GDir *dir;
	const gchar *filename;
	gchar *end = NULL;
	gchar *dir_path = NULL;
	gchar *path_max = NULL;
	gchar *path_now = NULL;
	gchar *buf_max = NULL;
	gchar *buf_now = NULL;
	GError *error = NULL;

	kbd_backlight->priv->fd = -1;

	/* open directory */
	dir = g_dir_open ("/sys/class/leds", 0, &error);
	if (dir == NULL) {
		if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			g_warning ("failed to open directory: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find a led device that is a keyboard device */
	while ((filename = g_dir_read_name (dir)) != NULL) {
		if (g_strstr_len (filename, -1, "kbd_backlight") != NULL) {
			dir_path = g_build_filename ("/sys/class/leds",
						    filename, NULL);
			break;
		}
	}

	/* nothing found */
	if (dir_path == NULL)
		goto out;

	/* read max brightness */
	path_max = g_build_filename (dir_path, "max_brightness", NULL);
	ret = g_file_get_contents (path_max, &buf_max, NULL, &error);
	if (!ret) {
		g_warning ("failed to get max brightness: %s", error->message);
		g_error_free (error);
		goto out;
	}
	kbd_backlight->priv->max_brightness = g_ascii_strtoull (buf_max, &end, 10);
	if (kbd_backlight->priv->max_brightness == 0 && end == buf_max) {
		g_warning ("failed to convert max brightness: %s", buf_max);
		goto out;
	}

	/* read brightness */
	path_now = g_build_filename (dir_path, "brightness", NULL);
	ret = g_file_get_contents (path_now, &buf_now, NULL, &error);
	if (!ret) {
		g_warning ("failed to get brightness: %s", error->message);
		g_error_free (error);
		goto out;
	}
	kbd_backlight->priv->brightness = g_ascii_strtoull (buf_now, &end, 10);
	if (kbd_backlight->priv->brightness == 0 && end == buf_now) {
		g_warning ("failed to convert brightness: %s", buf_now);
		goto out;
	}

	/* open the file for writing */
	kbd_backlight->priv->fd = open (path_now, O_RDWR);
	if (kbd_backlight->priv->fd < 0)
		goto out;

	/* success */
	found = TRUE;
out:
	if (dir != NULL)
		g_dir_close (dir);
	g_free (dir_path);
	g_free (path_max);
	g_free (path_now);
	g_free (buf_max);
	g_free (buf_now);
	return found;
}

/**
 * up_kbd_backlight_init:
 **/
static void
up_kbd_backlight_init (UpKbdBacklight *kbd_backlight)
{
	kbd_backlight->priv = UP_KBD_BACKLIGHT_GET_PRIVATE (kbd_backlight);

	g_signal_connect (kbd_backlight, "handle-get-brightness",
			  G_CALLBACK (up_kbd_backlight_get_brightness), kbd_backlight);
	g_signal_connect (kbd_backlight, "handle-get-max-brightness",
			  G_CALLBACK (up_kbd_backlight_get_max_brightness), kbd_backlight);
	g_signal_connect (kbd_backlight, "handle-set-brightness",
			  G_CALLBACK (up_kbd_backlight_set_brightness), kbd_backlight);
}

/**
 * up_kbd_backlight_finalize:
 **/
static void
up_kbd_backlight_finalize (GObject *object)
{
	UpKbdBacklight *kbd_backlight;

	g_return_if_fail (object != NULL);
	g_return_if_fail (UP_IS_KBD_BACKLIGHT (object));

	kbd_backlight = UP_KBD_BACKLIGHT (object);
	kbd_backlight->priv = UP_KBD_BACKLIGHT_GET_PRIVATE (kbd_backlight);

	/* close file */
	if (kbd_backlight->priv->fd >= 0)
		close (kbd_backlight->priv->fd);

	G_OBJECT_CLASS (up_kbd_backlight_parent_class)->finalize (object);
}

/**
 * up_kbd_backlight_new:
 **/
UpKbdBacklight *
up_kbd_backlight_new (void)
{
	return g_object_new (UP_TYPE_KBD_BACKLIGHT, NULL);
}

void
up_kbd_backlight_register (UpKbdBacklight *kbd_backlight,
			   GDBusConnection *connection)
{
	GError *error = NULL;

	/* find a kbd backlight in sysfs */
	if (!up_kbd_backlight_find (kbd_backlight)) {
		g_debug ("cannot find a keyboard backlight");
		return;
	}

	g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (kbd_backlight),
					  connection,
					  "/org/freedesktop/UPower/KbdBacklight",
					  &error);

	if (error != NULL) {
		g_warning ("Cannot export KbdBacklight object to bus: %s", error->message);
		g_error_free (error);
	}
}
