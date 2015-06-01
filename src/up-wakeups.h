/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __UP_WAKEUPS_H
#define __UP_WAKEUPS_H

#include <dbus/up-wakeups-generated.h>

G_BEGIN_DECLS

#define UP_TYPE_WAKEUPS		(up_wakeups_get_type ())
#define UP_WAKEUPS(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), UP_TYPE_WAKEUPS, UpWakeups))
#define UP_WAKEUPS_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), UP_TYPE_WAKEUPS, UpWakeupsClass))
#define UP_IS_WAKEUPS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), UP_TYPE_WAKEUPS))
#define UP_IS_WAKEUPS_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), UP_TYPE_WAKEUPS))
#define UP_WAKEUPS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), UP_TYPE_WAKEUPS, UpWakeupsClass))

typedef struct UpWakeupsPrivate UpWakeupsPrivate;

typedef struct
{
	UpExportedWakeupsSkeleton parent;
	UpWakeupsPrivate	*priv;
} UpWakeups;

typedef struct
{
	UpExportedWakeupsSkeletonClass parent_class;
} UpWakeupsClass;

UpWakeups	*up_wakeups_new			(void);
GType		 up_wakeups_get_type		(void);
void		 up_wakeups_register            (UpWakeups *wakeups,
						 GDBusConnection *connection);

G_END_DECLS

#endif	/* __UP_WAKEUPS_H */

