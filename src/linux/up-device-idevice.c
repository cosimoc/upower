/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2005-2010 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2004 Sergey V. Udaltsov <svu@gnome.org>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <math.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <gudev/gudev.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <plist/plist.h>

#include "sysfs-utils.h"
#include "up-types.h"
#include "up-device-idevice.h"

struct UpDeviceIdevicePrivate
{
	guint			 start_id;
	idevice_t		 dev;
	lockdownd_client_t	 client;
};

G_DEFINE_TYPE (UpDeviceIdevice, up_device_idevice, UP_TYPE_DEVICE)
#define UP_DEVICE_IDEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), UP_TYPE_DEVICE_IDEVICE, UpDeviceIdevicePrivate))

static gboolean		 up_device_idevice_refresh		(UpDevice *device);

/**
 * up_device_idevice_poll_cb:
 **/
static gboolean
up_device_idevice_poll_cb (UpDeviceIdevice *idevice)
{
	UpDevice *device = UP_DEVICE (idevice);

	g_debug ("Polling: %s", up_device_get_object_path (device));
	up_device_idevice_refresh (device);

	/* always continue polling */
	return TRUE;
}

static gboolean
start_poll_cb (UpDeviceIdevice *idevice)
{
	UpDevice *device = UP_DEVICE (idevice);
	idevice_t dev = NULL;
	lockdownd_client_t client = NULL;
	char *uuid;

	g_object_get (G_OBJECT (idevice), "serial", &uuid, NULL);
	g_assert (uuid);

	/* Connect to the device */
	if (idevice_new (&dev, uuid) != IDEVICE_E_SUCCESS)
		goto out;

	g_free (uuid);

	if (LOCKDOWN_E_SUCCESS != lockdownd_client_new_with_handshake (dev, &client, "upower"))
		goto out;

	/* coldplug */
	idevice->priv->client = client;
	if (up_device_idevice_refresh (device) == FALSE) {
		idevice->priv->client = NULL;
		goto out;
	}
	idevice->priv->dev = dev;

	/* _refresh will reopen a new lockdownd client */
	idevice->priv->client = NULL;
	g_clear_pointer (&client, lockdownd_client_free);

	g_object_set (G_OBJECT (idevice), "is-present", TRUE, NULL);

	/* set up a poll */
	up_daemon_start_poll (G_OBJECT (idevice), (GSourceFunc) up_device_idevice_poll_cb);

	idevice->priv->start_id = 0;
	return G_SOURCE_REMOVE;

out:
	g_clear_pointer (&client, lockdownd_client_free);
	g_clear_pointer (&dev, idevice_free);
	g_free (uuid);
	return G_SOURCE_CONTINUE;
}

/**
 * up_device_idevice_coldplug:
 *
 * Return %TRUE on success, %FALSE if we failed to get data and should be removed
 **/
static gboolean
up_device_idevice_coldplug (UpDevice *device)
{
	UpDeviceIdevice *idevice = UP_DEVICE_IDEVICE (device);
	GUdevDevice *native;
	const gchar *uuid;
	const gchar *model;
	UpDeviceKind kind;

	/* Is it an iDevice? */
	native = G_UDEV_DEVICE (up_device_get_native (device));
	if (g_udev_device_get_property_as_boolean (native, "USBMUX_SUPPORTED") == FALSE)
		return FALSE;

	/* Get the UUID */
	uuid = g_udev_device_get_property (native, "ID_SERIAL_SHORT");
	if (uuid == NULL)
		return FALSE;

	/* find the kind of device */
	model = g_udev_device_get_property (native, "ID_MODEL");
	kind = UP_DEVICE_KIND_PHONE;
	if (model != NULL && g_strstr_len (model, -1, "iPad")) {
		kind = UP_DEVICE_KIND_COMPUTER;
	} else if (model != NULL && g_strstr_len (model, -1, "iPod")) {
		kind = UP_DEVICE_KIND_MEDIA_PLAYER;
	}

	/* hardcode some values */
	g_object_set (device,
		      "type", kind,
		      "serial", uuid,
		      "vendor", g_udev_device_get_property (native, "ID_VENDOR"),
		      "model", g_udev_device_get_property (native, "ID_MODEL"),
		      "power-supply", FALSE,
		      "is-present", FALSE,
		      "is-rechargeable", TRUE,
		      "has-history", TRUE,
		      NULL);

	idevice->priv->start_id = g_timeout_add_seconds (1, (GSourceFunc) start_poll_cb, idevice);

	return TRUE;
}

/**
 * up_device_idevice_refresh:
 *
 * Return %TRUE on success, %FALSE if we failed to refresh or no data
 **/
static gboolean
up_device_idevice_refresh (UpDevice *device)
{
	UpDeviceIdevice *idevice = UP_DEVICE_IDEVICE (device);
	lockdownd_client_t client = NULL;
	plist_t dict, node;
	guint64 percentage;
	guint8 charging;
	UpDeviceState state;
	gboolean retval = FALSE;

	/* Open a lockdown port, or re-use the one we have */
	if (idevice->priv->client == NULL) {
		if (LOCKDOWN_E_SUCCESS != lockdownd_client_new_with_handshake (idevice->priv->dev, &client, "upower"))
			goto out;
	} else {
		client = idevice->priv->client;
	}

	if (lockdownd_get_value (client, "com.apple.mobile.battery", NULL, &dict) != LOCKDOWN_E_SUCCESS)
		goto out;

	/* get battery status */
	node = plist_dict_get_item (dict, "BatteryCurrentCapacity");
	if (!node) {
		plist_free (dict);
		goto out;
	}
	plist_get_uint_val (node, &percentage);

	g_object_set (device, "percentage", (double) percentage, NULL);
	g_debug ("percentage=%"G_GUINT64_FORMAT, percentage);

	/* get charging status */
	node = plist_dict_get_item (dict, "BatteryIsCharging");
	plist_get_bool_val (node, &charging);

	if (percentage == 100)
		state = UP_DEVICE_STATE_FULLY_CHARGED;
	else if (percentage == 0)
		state = UP_DEVICE_STATE_EMPTY;
	else if (charging)
		state = UP_DEVICE_STATE_CHARGING;
	else
		state = UP_DEVICE_STATE_DISCHARGING; /* upower doesn't have a "not charging" state */

	g_object_set (device,
		      "state", state,
		      NULL);
	g_debug ("state=%s", up_device_state_to_string (state));

	plist_free (dict);

	/* reset time */
	g_object_set (device, "update-time", (guint64) g_get_real_time () / G_USEC_PER_SEC, NULL);

	retval = TRUE;

out:
	/* Only free it if we opened it */
	if (idevice->priv->client == NULL && client != NULL)
		lockdownd_client_free (client);

	return retval;
}

/**
 * up_device_idevice_init:
 **/
static void
up_device_idevice_init (UpDeviceIdevice *idevice)
{
	idevice->priv = UP_DEVICE_IDEVICE_GET_PRIVATE (idevice);
}

/**
 * up_device_idevice_finalize:
 **/
static void
up_device_idevice_finalize (GObject *object)
{
	UpDeviceIdevice *idevice;

	g_return_if_fail (object != NULL);
	g_return_if_fail (UP_IS_DEVICE_IDEVICE (object));

	idevice = UP_DEVICE_IDEVICE (object);
	g_return_if_fail (idevice->priv != NULL);

	if (idevice->priv->start_id > 0) {
		g_source_remove (idevice->priv->start_id);
		idevice->priv->start_id = 0;
	}

	up_daemon_stop_poll (object);
	if (idevice->priv->client != NULL)
		lockdownd_client_free (idevice->priv->client);
	if (idevice->priv->dev != NULL)
		idevice_free (idevice->priv->dev);

	G_OBJECT_CLASS (up_device_idevice_parent_class)->finalize (object);
}

/**
 * up_device_idevice_class_init:
 **/
static void
up_device_idevice_class_init (UpDeviceIdeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	UpDeviceClass *device_class = UP_DEVICE_CLASS (klass);

	object_class->finalize = up_device_idevice_finalize;
	device_class->coldplug = up_device_idevice_coldplug;
	device_class->refresh = up_device_idevice_refresh;

	g_type_class_add_private (klass, sizeof (UpDeviceIdevicePrivate));
}

/**
 * up_device_idevice_new:
 **/
UpDeviceIdevice *
up_device_idevice_new (void)
{
	return g_object_new (UP_TYPE_DEVICE_IDEVICE, NULL);
}

