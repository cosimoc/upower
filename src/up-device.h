/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 David Zeuthen <davidz@redhat.com>
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __UP_DEVICE_H__
#define __UP_DEVICE_H__

#include <dbus/up-device-generated.h>
#include "up-daemon.h"

G_BEGIN_DECLS

#define UP_TYPE_DEVICE		(up_device_get_type ())
#define UP_DEVICE(o)	   	(G_TYPE_CHECK_INSTANCE_CAST ((o), UP_TYPE_DEVICE, UpDevice))
#define UP_DEVICE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), UP_TYPE_DEVICE, UpDeviceClass))
#define UP_IS_DEVICE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), UP_TYPE_DEVICE))
#define UP_IS_DEVICE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), UP_TYPE_DEVICE))
#define UP_DEVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), UP_TYPE_DEVICE, UpDeviceClass))

typedef struct UpDevicePrivate UpDevicePrivate;

typedef struct
{
	UpExportedDeviceSkeleton parent;
	UpDevicePrivate	*priv;
} UpDevice;

typedef struct
{
	UpExportedDeviceSkeletonClass parent_class;

	/* vtable */
	gboolean	 (*coldplug)		(UpDevice	*device);
	gboolean	 (*refresh)		(UpDevice	*device);
	const gchar	*(*get_id)		(UpDevice	*device);
	gboolean	 (*get_on_battery)	(UpDevice	*device,
						 gboolean	*on_battery);
	gboolean	 (*get_online)		(UpDevice	*device,
						 gboolean	*online);
} UpDeviceClass;

GType		 up_device_get_type		(void);
UpDevice	*up_device_new			(void);

gboolean	 up_device_coldplug		(UpDevice	*device,
						 UpDaemon	*daemon,
						 GObject	*native);
void		 up_device_unplug		(UpDevice	*device);
gboolean	 up_device_register_display_device (UpDevice	*device,
						    UpDaemon	*daemon);
UpDaemon	*up_device_get_daemon		(UpDevice	*device);
GObject		*up_device_get_native		(UpDevice	*device);
const gchar	*up_device_get_object_path	(UpDevice	*device);
gboolean	 up_device_get_on_battery	(UpDevice	*device,
						 gboolean	*on_battery);
gboolean	 up_device_get_online		(UpDevice	*device,
						 gboolean	*online);
gboolean	 up_device_refresh_internal	(UpDevice	*device);

G_END_DECLS

#endif /* __UP_DEVICE_H__ */
