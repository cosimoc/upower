/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 David Zeuthen <davidz@redhat.com>
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>

#include "up-native.h"
#include "up-device.h"
#include "up-history.h"
#include "up-history-item.h"
#include "up-stats-item.h"
#include "up-marshal.h"
#include "up-device-generated.h"

struct UpDevicePrivate
{
	gchar			*object_path;
	UpExportedDevice	*skeleton;
	UpDaemon		*daemon;
	UpHistory		*history;
	GObject			*native;
	gboolean		 has_ever_refresh;
};

static gboolean	up_device_register_device	(UpDevice *device);

enum {
	PROP_0,
	PROP_NATIVE_PATH,
	PROP_VENDOR,
	PROP_MODEL,
	PROP_SERIAL,
	PROP_UPDATE_TIME,
	PROP_TYPE,
	PROP_ONLINE,
	PROP_POWER_SUPPLY,
	PROP_CAPACITY,
	PROP_IS_PRESENT,
	PROP_IS_RECHARGEABLE,
	PROP_HAS_HISTORY,
	PROP_HAS_STATISTICS,
	PROP_STATE,
	PROP_ENERGY,
	PROP_ENERGY_EMPTY,
	PROP_ENERGY_FULL,
	PROP_ENERGY_FULL_DESIGN,
	PROP_ENERGY_RATE,
	PROP_VOLTAGE,
	PROP_LUMINOSITY,
	PROP_TIME_TO_EMPTY,
	PROP_TIME_TO_FULL,
	PROP_PERCENTAGE,
	PROP_TEMPERATURE,
	PROP_TECHNOLOGY,
	PROP_WARNING_LEVEL,
	PROP_ICON_NAME,
	PROP_LAST
};

G_DEFINE_TYPE (UpDevice, up_device, G_TYPE_OBJECT)
#define UP_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), UP_TYPE_DEVICE, UpDevicePrivate))

#define UP_DEVICES_DBUS_PATH "/org/freedesktop/UPower/devices"

/* This needs to be called when one of those properties changes:
 * state
 * power_supply
 * percentage
 * time_to_empty
 *
 * type should not change for non-display devices
 */
static void
update_warning_level (UpDevice *device)
{
	UpDeviceLevel warning_level;

	/* Not finished setting up the object? */
	if (device->priv->daemon == NULL)
		return;

	warning_level = up_daemon_compute_warning_level (device->priv->daemon,
							 up_exported_device_get_state (device->priv->skeleton),
							 up_exported_device_get_type_ (device->priv->skeleton),
							 up_exported_device_get_power_supply (device->priv->skeleton),
							 up_exported_device_get_percentage (device->priv->skeleton),
							 up_exported_device_get_time_to_empty (device->priv->skeleton));

	up_exported_device_set_warning_level (device->priv->skeleton, warning_level);
	g_object_notify (G_OBJECT (device), "warning-level");
}

static const gchar *
get_device_charge_icon (gdouble  percentage,
			gboolean charging)
{
	if (percentage < 10)
		return charging ? "battery-caution-charging-symbolic" : "battery-caution-symbolic";
	else if (percentage < 30)
		return charging ? "battery-low-charging-symbolic" : "battery-low-symbolic";
	else if (percentage < 60)
		return charging ? "battery-good-charging-symbolic" : "battery-good-symbolic";
	return charging ? "battery-full-charging-symbolic" : "battery-full-symbolic";
}

/* This needs to be called when one of those properties changes:
 * type
 * state
 * percentage
 * is-present
 */
static void
update_icon_name (UpDevice *device)
{
	const gchar *icon_name = NULL;

	/* get the icon from some simple rules */
	if (up_exported_device_get_type_ (device->priv->skeleton) == UP_DEVICE_KIND_LINE_POWER) {
		icon_name = "ac-adapter-symbolic";
	} else {

		if (!up_exported_device_get_is_present (device->priv->skeleton)) {
			icon_name = "battery-missing-symbolic";

		} else {
			switch (up_exported_device_get_state (device->priv->skeleton)) {
			case UP_DEVICE_STATE_EMPTY:
				icon_name = "battery-empty-symbolic";
				break;
			case UP_DEVICE_STATE_FULLY_CHARGED:
				icon_name = "battery-full-charged-symbolic";
				break;
			case UP_DEVICE_STATE_CHARGING:
			case UP_DEVICE_STATE_PENDING_CHARGE:
				icon_name = get_device_charge_icon (up_exported_device_get_percentage (device->priv->skeleton), TRUE);
				break;
			case UP_DEVICE_STATE_DISCHARGING:
			case UP_DEVICE_STATE_PENDING_DISCHARGE:
				icon_name = get_device_charge_icon (up_exported_device_get_percentage (device->priv->skeleton), FALSE);
				break;
			default:
				icon_name = "battery-missing-symbolic";
			}
		}
	}

	up_exported_device_set_icon_name (device->priv->skeleton, icon_name);
	g_object_notify (G_OBJECT (device), "icon-name");
}

/**
 * up_device_get_property:
 **/
static void
up_device_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	UpDevice *device = UP_DEVICE (object);
	g_object_get_property (G_OBJECT (device->priv->skeleton), pspec->name, value);
}

/**
 * up_device_set_property:
 **/
static void
up_device_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	UpDevice *device = UP_DEVICE (object);

	g_object_set_property (G_OBJECT (device->priv->skeleton), pspec->name, value);

	switch (prop_id) {
	case PROP_TYPE:
		update_icon_name (device);
		break;
	case PROP_POWER_SUPPLY:
		update_warning_level (device);
	case PROP_IS_PRESENT:
		update_icon_name (device);
		break;
	case PROP_STATE:
		update_warning_level (device);
		update_icon_name (device);
		break;
	case PROP_TIME_TO_EMPTY:
		update_warning_level (device);
		break;
	case PROP_PERCENTAGE:
		update_warning_level (device);
		update_icon_name (device);
		break;
	default:
		break;
	}
}

/**
 * up_device_get_on_battery:
 *
 * Note: Only implement for system devices, i.e. ones supplying the system
 **/
gboolean
up_device_get_on_battery (UpDevice *device, gboolean *on_battery)
{
	UpDeviceClass *klass = UP_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (UP_IS_DEVICE (device), FALSE);

	/* no support */
	if (klass->get_on_battery == NULL)
		return FALSE;

	return klass->get_on_battery (device, on_battery);
}

/**
 * up_device_get_online:
 *
 * Note: Only implement for system devices, i.e. devices supplying the system
 **/
gboolean
up_device_get_online (UpDevice *device, gboolean *online)
{
	UpDeviceClass *klass = UP_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (UP_IS_DEVICE (device), FALSE);

	/* no support */
	if (klass->get_online == NULL)
		return FALSE;

	return klass->get_online (device, online);
}

/**
 * up_device_get_id:
 **/
static gchar *
up_device_get_id (UpDevice *device)
{
	GString *string;
	gchar *id = NULL;
	gdouble energy_full_design;
	const char *model;
	const char *serial;
	const char *vendor;
	UpDeviceKind type;

	type = up_exported_device_get_type_ (device->priv->skeleton);
	energy_full_design = up_exported_device_get_energy_full_design (device->priv->skeleton);
	model = up_exported_device_get_model (device->priv->skeleton);
	serial = up_exported_device_get_serial (device->priv->skeleton);
	vendor = up_exported_device_get_vendor (device->priv->skeleton);

	/* line power */
	if (type == UP_DEVICE_KIND_LINE_POWER) {
		goto out;

	/* batteries */
	} else if (type == UP_DEVICE_KIND_BATTERY) {
		/* we don't have an ID if we are not present */
		if (!up_exported_device_get_is_present (device->priv->skeleton))
			goto out;

		string = g_string_new ("");

		/* in an ideal world, model-capacity-serial */
		if (model != NULL && strlen (model) > 2) {
			g_string_append (string, model);
			g_string_append_c (string, '-');
		}
		if (energy_full_design > 0) {
			/* FIXME: this may not be stable if we are using voltage_now */
			g_string_append_printf (string, "%i", (guint) energy_full_design);
			g_string_append_c (string, '-');
		}
		if (serial != NULL && strlen (serial) > 2) {
			g_string_append (string, serial);
			g_string_append_c (string, '-');
		}

		/* make sure we are sane */
		if (string->len == 0) {
			/* just use something generic */
			g_string_append (string, "generic_id");
		} else {
			/* remove trailing '-' */
			g_string_set_size (string, string->len - 1);
		}

		/* the id may have invalid chars that need to be replaced */
		id = g_string_free (string, FALSE);

	} else {
		/* generic fallback, get what data we can */
		string = g_string_new ("");
		if (vendor != NULL) {
			g_string_append (string, vendor);
			g_string_append_c (string, '-');
		}
		if (model != NULL) {
			g_string_append (string, model);
			g_string_append_c (string, '-');
		}
		if (serial != NULL) {
			g_string_append (string, serial);
			g_string_append_c (string, '-');
		}

		/* make sure we are sane */
		if (string->len == 0) {
			/* just use something generic */
			g_string_append (string, "generic_id");
		} else {
			/* remove trailing '-' */
			g_string_set_size (string, string->len - 1);
		}

		/* the id may have invalid chars that need to be replaced */
		id = g_string_free (string, FALSE);
	}

	g_strdelimit (id, "\\\t\"?' /,.", '_');

out:
	return id;
}

/**
 * up_device_get_daemon:
 *
 * Returns a refcounted #UpDaemon instance, or %NULL
 **/
UpDaemon *
up_device_get_daemon (UpDevice *device)
{
	if (device->priv->daemon == NULL)
		return NULL;
	return g_object_ref (device->priv->daemon);
}

/**
 * up_device_coldplug:
 *
 * Return %TRUE on success, %FALSE if we failed to get data and should be removed
 **/
gboolean
up_device_coldplug (UpDevice *device, UpDaemon *daemon, GObject *native)
{
	gboolean ret;
	const gchar *native_path;
	UpDeviceClass *klass = UP_DEVICE_GET_CLASS (device);
	gchar *id = NULL;

	g_return_val_if_fail (UP_IS_DEVICE (device), FALSE);

	/* save */
	device->priv->native = g_object_ref (native);
	device->priv->daemon = g_object_ref (daemon);

	native_path = up_native_get_native_path (native);
	up_exported_device_set_native_path (device->priv->skeleton, native_path);
	g_object_notify (G_OBJECT (device), "native-path");

	/* coldplug source */
	if (klass->coldplug != NULL) {
		ret = klass->coldplug (device);
		if (!ret) {
			g_debug ("failed to coldplug %s", native_path);
			goto bail;
		}
	}

	/* force a refresh, although failure isn't fatal */
	ret = up_device_refresh_internal (device);
	if (!ret) {
		g_debug ("failed to refresh %s", native_path);

		/* TODO: refresh should really have separate
		 *       success _and_ changed parameters */
		goto out;
	}

	/* get the id so we can load the old history */
	id = up_device_get_id (device);
	if (id != NULL) {
		up_history_set_id (device->priv->history, id);
		g_free (id);
	}
out:
	/* only put on the bus if we succeeded */
	ret = up_device_register_device (device);
	if (!ret) {
		g_warning ("failed to register device %s", native_path);
		goto out;
	}
bail:
	return ret;
}

/**
 * up_device_unplug:
 *
 * Initiates destruction of %UpDevice, undoing the effects of
 * up_device_coldplug.
 */
void
up_device_unplug (UpDevice *device)
{
	/* break circular dependency */
	if (device->priv->daemon != NULL) {
		g_object_unref (device->priv->daemon);
		device->priv->daemon = NULL;
	}
}

/**
 * up_device_get_statistics:
 **/
static gboolean
up_device_get_statistics (UpExportedDevice *skeleton,
			  GDBusMethodInvocation *invocation,
			  const gchar *type,
			  UpDevice *device)
{
	GPtrArray *array = NULL;
	UpStatsItem *item;
	guint i;
	GVariantBuilder builder;

	/* doesn't even try to support this */
	if (!up_exported_device_get_has_statistics (skeleton)) {
		g_dbus_method_invocation_return_error_literal (invocation,
							       UP_DAEMON_ERROR, UP_DAEMON_ERROR_GENERAL,
							       "device does not support getting stats");
		goto out;
	}

	/* get the correct data */
	if (g_strcmp0 (type, "charging") == 0)
		array = up_history_get_profile_data (device->priv->history, TRUE);
	else if (g_strcmp0 (type, "discharging") == 0)
		array = up_history_get_profile_data (device->priv->history, FALSE);

	/* maybe the device doesn't support histories */
	if (array == NULL) {
		g_dbus_method_invocation_return_error_literal (invocation,
							       UP_DAEMON_ERROR, UP_DAEMON_ERROR_GENERAL,
							       "device has no statistics");
		goto out;
	}

	/* always 101 items of data */
	if (array->len != 101) {
		g_dbus_method_invocation_return_error (invocation,
						       UP_DAEMON_ERROR, UP_DAEMON_ERROR_GENERAL,
						       "statistics invalid as have %i items", array->len);
		goto out;
	}

	/* copy data to dbus struct */
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(dd)"));
	for (i = 0; i < array->len; i++) {
		item = (UpStatsItem *) g_ptr_array_index (array, i);
		g_variant_builder_add (&builder, "(dd)",
				       up_stats_item_get_value (item),
				       up_stats_item_get_accuracy (item));
	}

	up_exported_device_complete_get_statistics (skeleton, invocation,
						    g_variant_builder_end (&builder));
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return TRUE;
}

/**
 * up_device_get_history:
 **/
static gboolean
up_device_get_history (UpExportedDevice *skeleton,
		       GDBusMethodInvocation *invocation,
		       const gchar *type_string,
		       guint timespan,
		       guint resolution,
		       UpDevice *device)
{
	GPtrArray *array = NULL;
	UpHistoryItem *item;
	guint i;
	UpHistoryType type = UP_HISTORY_TYPE_UNKNOWN;
	GVariantBuilder builder;

	/* doesn't even try to support this */
	if (!up_exported_device_get_has_history (skeleton)) {
		g_dbus_method_invocation_return_error_literal (invocation,
							       UP_DAEMON_ERROR, UP_DAEMON_ERROR_GENERAL,
							       "device does not support getting history");
		goto out;
	}

	/* get the correct data */
	if (g_strcmp0 (type_string, "rate") == 0)
		type = UP_HISTORY_TYPE_RATE;
	else if (g_strcmp0 (type_string, "charge") == 0)
		type = UP_HISTORY_TYPE_CHARGE;
	else if (g_strcmp0 (type_string, "time-full") == 0)
		type = UP_HISTORY_TYPE_TIME_FULL;
	else if (g_strcmp0 (type_string, "time-empty") == 0)
		type = UP_HISTORY_TYPE_TIME_EMPTY;

	/* something recognised */
	if (type != UP_HISTORY_TYPE_UNKNOWN)
		array = up_history_get_data (device->priv->history, type, timespan, resolution);

	/* maybe the device doesn't have any history */
	if (array == NULL) {
		g_dbus_method_invocation_return_error_literal (invocation,
							       UP_DAEMON_ERROR, UP_DAEMON_ERROR_GENERAL,
							       "device has no history");
		goto out;
	}

	/* copy data to dbus struct */
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(udu)"));
	for (i = 0; i < array->len; i++) {
		item = (UpHistoryItem *) g_ptr_array_index (array, i);
		g_variant_builder_add (&builder, "(udu)",
				       up_history_item_get_time (item),
				       up_history_item_get_value (item),
				       up_history_item_get_state (item));
	}

	up_exported_device_complete_get_history (skeleton, invocation,
						 g_variant_builder_end (&builder));

out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return TRUE;
}

/**
 * up_device_refresh:
 *
 * Return %TRUE on success, %FALSE if we failed to refresh or no data
 **/
static gboolean
up_device_refresh (UpExportedDevice *skeleton,
		   GDBusMethodInvocation *invocation,
		   UpDevice *device)
{
	up_device_refresh_internal (device);
	up_exported_device_complete_refresh (skeleton, invocation);
	return TRUE;
}

static void
up_device_export_skeleton (UpDevice *device)
{
	GError *error = NULL;

	g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (device->priv->skeleton),
					  up_daemon_get_dbus_connection (device->priv->daemon),
					  device->priv->object_path,
					  &error);

	if (error != NULL) {
		g_critical ("error registering device on system bus: %s", error->message);
		g_error_free (error);
	}
}

/**
 * up_device_register_display_device:
 **/
gboolean
up_device_register_display_device (UpDevice *device,
				   UpDaemon *daemon)
{
	g_return_val_if_fail (UP_IS_DEVICE (device), FALSE);

	device->priv->daemon = g_object_ref (daemon);
	device->priv->object_path = g_build_filename (UP_DEVICES_DBUS_PATH, "DisplayDevice", NULL);
	up_device_export_skeleton (device);

	return TRUE;
}

/**
 * up_device_refresh_internal:
 *
 * NOTE: if you're calling this function you have to ensure you're doing the
 * the changed signals on the right interfaces, although by monitoring
 * notify::update-time this should be mostly done.
 **/
gboolean
up_device_refresh_internal (UpDevice *device)
{
	gboolean ret = FALSE;
	UpDeviceClass *klass = UP_DEVICE_GET_CLASS (device);

	/* not implemented */
	if (klass->refresh == NULL)
		goto out;

	/* do the refresh */
	ret = klass->refresh (device);
	if (!ret) {
		g_debug ("no changes");
		goto out;
	}

	/* the first time, print all properties */
	if (!device->priv->has_ever_refresh) {
		g_debug ("added native-path: %s\n", up_exported_device_get_native_path (device->priv->skeleton));
		device->priv->has_ever_refresh = TRUE;
		goto out;
	}
out:
	return ret;
}

/**
 * up_device_get_object_path:
 **/
const gchar *
up_device_get_object_path (UpDevice *device)
{
	g_return_val_if_fail (UP_IS_DEVICE (device), NULL);
	return device->priv->object_path;
}

GObject *
up_device_get_native (UpDevice *device)
{
	g_return_val_if_fail (UP_IS_DEVICE (device), NULL);
	return device->priv->native;
}

/**
 * up_device_compute_object_path:
 **/
static gchar *
up_device_compute_object_path (UpDevice *device)
{
	gchar *basename;
	gchar *id;
	gchar *object_path;
	const gchar *native_path;
	const gchar *type;
	guint i;

	type = up_device_kind_to_string (up_exported_device_get_type_ (device->priv->skeleton));
	native_path = up_exported_device_get_native_path (device->priv->skeleton);
	basename = g_path_get_basename (native_path);
	id = g_strjoin ("_", type, basename, NULL);

	/* make DBUS valid path */
	for (i=0; id[i] != '\0'; i++) {
		if (id[i] == '-')
			id[i] = '_';
		if (id[i] == '.')
			id[i] = 'x';
		if (id[i] == ':')
			id[i] = 'o';
	}
	object_path = g_build_filename (UP_DEVICES_DBUS_PATH, id, NULL);

	g_free (basename);
	g_free (id);

	return object_path;
}

/**
 * up_device_register_device:
 **/
static gboolean
up_device_register_device (UpDevice *device)
{
	device->priv->object_path = up_device_compute_object_path (device);
	g_debug ("object path = %s", device->priv->object_path);
	up_device_export_skeleton (device);

	return TRUE;
}

/**
 * up_device_perhaps_changed_cb:
 **/
static void
up_device_perhaps_changed_cb (GObject *object, GParamSpec *pspec, UpDevice *device)
{
	g_return_if_fail (UP_IS_DEVICE (device));

	/* save new history */
	up_history_set_state (device->priv->history, up_exported_device_get_state (device->priv->skeleton));
	up_history_set_charge_data (device->priv->history, up_exported_device_get_percentage (device->priv->skeleton));
	up_history_set_rate_data (device->priv->history, up_exported_device_get_energy_rate (device->priv->skeleton));
	up_history_set_time_full_data (device->priv->history, up_exported_device_get_time_to_full (device->priv->skeleton));
	up_history_set_time_empty_data (device->priv->history, up_exported_device_get_time_to_empty (device->priv->skeleton));
}

/**
 * up_device_init:
 **/
static void
up_device_init (UpDevice *device)
{
	device->priv = UP_DEVICE_GET_PRIVATE (device);
	device->priv->history = up_history_new ();

	g_signal_connect (device, "notify::update-time", G_CALLBACK (up_device_perhaps_changed_cb), device);

	device->priv->skeleton = up_exported_device_skeleton_new ();

	g_signal_connect (device->priv->skeleton, "handle-get-history",
			  G_CALLBACK (up_device_get_history), device);
	g_signal_connect (device->priv->skeleton, "handle-get-statistics",
			  G_CALLBACK (up_device_get_statistics), device);
	g_signal_connect (device->priv->skeleton, "handle-refresh",
			  G_CALLBACK (up_device_refresh), device);
}

/**
 * up_device_finalize:
 **/
static void
up_device_finalize (GObject *object)
{
	UpDevice *device;

	g_return_if_fail (object != NULL);
	g_return_if_fail (UP_IS_DEVICE (object));

	device = UP_DEVICE (object);
	g_return_if_fail (device->priv != NULL);
	if (device->priv->native != NULL)
		g_object_unref (device->priv->native);
	if (device->priv->daemon != NULL)
		g_object_unref (device->priv->daemon);
	g_object_unref (device->priv->history);
	g_object_unref (device->priv->skeleton);
	g_free (device->priv->object_path);

	G_OBJECT_CLASS (up_device_parent_class)->finalize (object);
}

/**
 * up_device_class_init:
 **/
static void
up_device_class_init (UpDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = up_device_get_property;
	object_class->set_property = up_device_set_property;
	object_class->finalize = up_device_finalize;

	g_type_class_add_private (klass, sizeof (UpDevicePrivate));

	/**
	 * UpDevice:update-time:
	 */
	g_object_class_install_property (object_class,
					 PROP_UPDATE_TIME,
					 g_param_spec_uint64 ("update-time",
							      NULL, NULL,
							      0, G_MAXUINT64, 0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:vendor:
	 */
	g_object_class_install_property (object_class,
					 PROP_VENDOR,
					 g_param_spec_string ("vendor",
							      NULL, NULL,
							      NULL,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:model:
	 */
	g_object_class_install_property (object_class,
					 PROP_MODEL,
					 g_param_spec_string ("model",
							      NULL, NULL,
							      NULL,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:serial:
	 */
	g_object_class_install_property (object_class,
					 PROP_SERIAL,
					 g_param_spec_string ("serial",
							      NULL, NULL,
							      NULL,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:native-path:
	 */
	g_object_class_install_property (object_class,
					 PROP_NATIVE_PATH,
					 g_param_spec_string ("native-path",
							      NULL, NULL,
							      NULL,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:power-supply:
	 */
	g_object_class_install_property (object_class,
					 PROP_POWER_SUPPLY,
					 g_param_spec_boolean ("power-supply",
							       NULL, NULL,
							       FALSE,
							       G_PARAM_READWRITE));
	/**
	 * UpDevice:online:
	 */
	g_object_class_install_property (object_class,
					 PROP_ONLINE,
					 g_param_spec_boolean ("online",
							       NULL, NULL,
							       FALSE,
							       G_PARAM_READWRITE));
	/**
	 * UpDevice:is-present:
	 */
	g_object_class_install_property (object_class,
					 PROP_IS_PRESENT,
					 g_param_spec_boolean ("is-present",
							       NULL, NULL,
							       FALSE,
							       G_PARAM_READWRITE));
	/**
	 * UpDevice:is-rechargeable:
	 */
	g_object_class_install_property (object_class,
					 PROP_IS_RECHARGEABLE,
					 g_param_spec_boolean ("is-rechargeable",
							       NULL, NULL,
							       FALSE,
							       G_PARAM_READWRITE));
	/**
	 * UpDevice:has-history:
	 */
	g_object_class_install_property (object_class,
					 PROP_HAS_HISTORY,
					 g_param_spec_boolean ("has-history",
							       NULL, NULL,
							       FALSE,
							       G_PARAM_READWRITE));
	/**
	 * UpDevice:has-statistics:
	 */
	g_object_class_install_property (object_class,
					 PROP_HAS_STATISTICS,
					 g_param_spec_boolean ("has-statistics",
							       NULL, NULL,
							       FALSE,
							       G_PARAM_READWRITE));
	/**
	 * UpDevice:type:
	 */
	g_object_class_install_property (object_class,
					 PROP_TYPE,
					 g_param_spec_uint ("type",
							    NULL, NULL,
							    UP_DEVICE_KIND_UNKNOWN,
							    UP_DEVICE_KIND_LAST,
							    UP_DEVICE_KIND_UNKNOWN,
							    G_PARAM_READWRITE));
	/**
	 * UpDevice:state:
	 */
	g_object_class_install_property (object_class,
					 PROP_STATE,
					 g_param_spec_uint ("state",
							    NULL, NULL,
							    UP_DEVICE_STATE_UNKNOWN,
							    UP_DEVICE_STATE_LAST,
							    UP_DEVICE_STATE_UNKNOWN,
							    G_PARAM_READWRITE));
	/**
	 * UpDevice:technology:
	 */
	g_object_class_install_property (object_class,
					 PROP_TECHNOLOGY,
					 g_param_spec_uint ("technology",
							    NULL, NULL,
							    UP_DEVICE_TECHNOLOGY_UNKNOWN,
							    UP_DEVICE_TECHNOLOGY_LAST,
							    UP_DEVICE_TECHNOLOGY_UNKNOWN,
							    G_PARAM_READWRITE));
	/**
	 * UpDevice:capacity:
	 */
	g_object_class_install_property (object_class,
					 PROP_CAPACITY,
					 g_param_spec_double ("capacity", NULL, NULL,
							      0.0, 100.f, 100.0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:energy:
	 */
	g_object_class_install_property (object_class,
					 PROP_ENERGY,
					 g_param_spec_double ("energy", NULL, NULL,
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:energy-empty:
	 */
	g_object_class_install_property (object_class,
					 PROP_ENERGY_EMPTY,
					 g_param_spec_double ("energy-empty", NULL, NULL,
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:energy-full:
	 */
	g_object_class_install_property (object_class,
					 PROP_ENERGY_FULL,
					 g_param_spec_double ("energy-full", NULL, NULL,
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:energy-full-design:
	 */
	g_object_class_install_property (object_class,
					 PROP_ENERGY_FULL_DESIGN,
					 g_param_spec_double ("energy-full-design", NULL, NULL,
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:energy-rate:
	 */
	g_object_class_install_property (object_class,
					 PROP_ENERGY_RATE,
					 g_param_spec_double ("energy-rate", NULL, NULL,
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:voltage:
	 */
	g_object_class_install_property (object_class,
					 PROP_VOLTAGE,
					 g_param_spec_double ("voltage", NULL, NULL,
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:luminosity:
	 */
	g_object_class_install_property (object_class,
					 PROP_LUMINOSITY,
					 g_param_spec_double ("luminosity", NULL, NULL,
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:time-to-empty:
	 */
	g_object_class_install_property (object_class,
					 PROP_TIME_TO_EMPTY,
					 g_param_spec_int64 ("time-to-empty", NULL, NULL,
							      0, G_MAXINT64, 0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:time-to-full:
	 */
	g_object_class_install_property (object_class,
					 PROP_TIME_TO_FULL,
					 g_param_spec_int64 ("time-to-full", NULL, NULL,
							      0, G_MAXINT64, 0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:percentage:
	 */
	g_object_class_install_property (object_class,
					 PROP_PERCENTAGE,
					 g_param_spec_double ("percentage", NULL, NULL,
							      0.0, 100.f, 100.0,
							      G_PARAM_READWRITE));
	/**
	 * UpDevice:temperature:
	 */
	g_object_class_install_property (object_class,
					 PROP_TEMPERATURE,
					 g_param_spec_double ("temperature", NULL, NULL,
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));

	/**
	 * UpDevice:warning-level:
	 */
	g_object_class_install_property (object_class,
					 PROP_WARNING_LEVEL,
					 g_param_spec_uint ("warning-level",
							    NULL, NULL,
							    UP_DEVICE_LEVEL_UNKNOWN,
							    UP_DEVICE_LEVEL_LAST,
							    UP_DEVICE_LEVEL_UNKNOWN,
							    G_PARAM_READWRITE));

	/**
	 * UpDevice:icon:
	 */
	g_object_class_install_property (object_class,
					 PROP_ICON_NAME,
					 g_param_spec_string ("icon-name",
							      NULL, NULL, NULL,
							      G_PARAM_READABLE));
}

/**
 * up_device_new:
 **/
UpDevice *
up_device_new (void)
{
	return UP_DEVICE (g_object_new (UP_TYPE_DEVICE, NULL));
}
