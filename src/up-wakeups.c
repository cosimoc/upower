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

#include "config.h"

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "up-wakeups.h"
#include "up-daemon.h"
#include "up-wakeup-item.h"

static void     up_wakeups_finalize   (GObject		*object);
static gboolean	up_wakeups_timerstats_enable (UpWakeups *wakeups);

#define UP_WAKEUPS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), UP_TYPE_WAKEUPS, UpWakeupsPrivate))

#define UP_WAKEUPS_POLL_INTERVAL_KERNEL	2 /* seconds */
#define UP_WAKEUPS_POLL_INTERVAL_USERSPACE	2 /* seconds */
#define UP_WAKEUPS_DISABLE_INTERVAL		30 /* seconds */
#define UP_WAKEUPS_SOURCE_KERNEL		"/proc/interrupts"
#define UP_WAKEUPS_SOURCE_USERSPACE		"/proc/timer_stats"
#define UP_WAKEUPS_SMALLEST_VALUE		0.1f /* seconds */
#define UP_WAKEUPS_TOTAL_SMOOTH_FACTOR		0.125f

struct UpWakeupsPrivate
{
	GPtrArray		*data;
	guint			 total_old;
	guint			 total_ave;
	guint			 poll_userspace_id;
	guint			 poll_kernel_id;
	guint			 disable_id;
	gboolean		 polling_enabled;
};

G_DEFINE_TYPE (UpWakeups, up_wakeups, UP_TYPE_EXPORTED_WAKEUPS_SKELETON)

/**
 * up_wakeups_get_cmdline:
 **/
static gchar *
up_wakeups_get_cmdline (guint pid)
{
	gboolean ret;
	gchar *filename = NULL;
	gchar *cmdline = NULL;
	GError *error = NULL;

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/cmdline", pid);
	ret = g_file_get_contents (filename, &cmdline, NULL, &error);
	if (!ret) {
		g_debug ("failed to get cmdline: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (filename);
	return cmdline;
}

/**
 * up_wakeups_data_item_compare:
 **/
static gint
up_wakeups_data_item_compare (UpWakeupItem **item1, UpWakeupItem **item2)
{
	gdouble val1;
	gdouble val2;
	val1 = up_wakeup_item_get_value (*item1);
	val2 = up_wakeup_item_get_value (*item2);
	if (val1 > val2)
		return -1;
	if (val1 < val2)
		return 1;
	return -0;
}

/**
 * up_wakeups_data_get_or_create:
 **/
static UpWakeupItem *
up_wakeups_data_get_or_create (UpWakeups *wakeups, guint id)
{
	guint i;
	UpWakeupItem *item;

	for (i=0; i<wakeups->priv->data->len; i++) {
		item = g_ptr_array_index (wakeups->priv->data, i);
		if (up_wakeup_item_get_id (item) == id)
			goto out;
	}
	item = up_wakeup_item_new ();
	up_wakeup_item_set_id (item, id);
	g_ptr_array_add (wakeups->priv->data, item);
out:
	return item;
}

/**
 * up_wakeups_data_get_total:
 **/
static guint
up_wakeups_data_get_total (UpWakeups *wakeups)
{
	guint i;
	gfloat total = 0;
	UpWakeupItem *item;

	for (i=0; i<wakeups->priv->data->len; i++) {
		item = g_ptr_array_index (wakeups->priv->data, i);
		total += up_wakeup_item_get_value (item);
	}
	return (guint) total;
}

/**
 * up_wakeups_get_total:
 *
 * Gets the current latency
 **/
static gboolean
up_wakeups_get_total (UpExportedWakeups *skeleton,
		      GDBusMethodInvocation *invocation,
		      UpWakeups *wakeups)
{
	gboolean ret;

	/* no capability */
	if (!up_exported_wakeups_get_has_capability (skeleton)) {
		g_dbus_method_invocation_return_error_literal (invocation,
							       UP_DAEMON_ERROR, UP_DAEMON_ERROR_GENERAL,
							       "no hardware support");
		return TRUE;
	}

	/* start if not already started */
	ret = up_wakeups_timerstats_enable (wakeups);

	/* no data */
	if (!ret) {
		g_dbus_method_invocation_return_error_literal (invocation,
							       UP_DAEMON_ERROR, UP_DAEMON_ERROR_GENERAL,
							       "cannot enable timerstats");
		return TRUE;
	}

	/* return total averaged */
	up_exported_wakeups_complete_get_total (skeleton, invocation, wakeups->priv->total_ave);
	return TRUE;
}

/**
 * up_wakeups_get_data:
 **/
static gboolean
up_wakeups_get_data (UpExportedWakeups *skeleton,
		     GDBusMethodInvocation *invocation,
		     UpWakeups *wakeups)
{
	guint i;
	GPtrArray *array;
	UpWakeupItem *item;
	GVariantBuilder builder;

	/* no capability */
	if (!up_exported_wakeups_get_has_capability (skeleton)) {
		g_dbus_method_invocation_return_error_literal (invocation,
							       UP_DAEMON_ERROR, UP_DAEMON_ERROR_GENERAL,
							       "no hardware support");
		return TRUE;
	}

	/* start if not already started */
	up_wakeups_timerstats_enable (wakeups);

	/* sort data */
	g_ptr_array_sort (wakeups->priv->data, (GCompareFunc) up_wakeups_data_item_compare);

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(budss)"));
	array = wakeups->priv->data;
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (up_wakeup_item_get_value (item) < UP_WAKEUPS_SMALLEST_VALUE)
			continue;

		g_variant_builder_add (&builder, "(budss)",
				       up_wakeup_item_get_is_userspace (item),
				       up_wakeup_item_get_id (item),
				       up_wakeup_item_get_value (item),
				       up_wakeup_item_get_cmdline (item),
				       up_wakeup_item_get_details (item));
	}

	up_exported_wakeups_complete_get_data (skeleton, invocation,
					       g_variant_builder_end (&builder));

	return TRUE;
}

/**
 * up_is_in:
 **/
static gboolean
up_is_in (gchar needle, const gchar *delimiters)
{
	guint i;
	for (i=0; delimiters[i] != '\0'; i++) {
		if (delimiters[i] == needle)
			return TRUE;
	}
	return FALSE;
}

/**
 * up_strsplit_complete_set:
 **/
static GPtrArray *
up_strsplit_complete_set (const gchar *string, const gchar *delimiters, guint max_tokens)
{
	guint i;
	gboolean ret;
	const gchar *start = NULL;
	gchar temp_data[100];
	guint len;
	guint tokens = 0;
	GPtrArray *array;

	/* find sections not delimited by space */
	array = g_ptr_array_new_with_free_func (g_free);
	for (i=0; string[i] != '\0'; i++) {
		ret = up_is_in (string[i], delimiters);
		if (ret) {
			/* no character data yet */
			if (start == NULL)
				continue;
			if (tokens == max_tokens - 1) {
				g_ptr_array_add (array, g_strdup (start));
				break;
			}

			/* find length of string */
			len = &string[i] - start;
			if (len > 99)
				len = 99;
			strncpy (temp_data, start, len);
			temp_data[len] = '\0';

			/* add to array */
			g_ptr_array_add (array, g_strdup (temp_data));
			tokens++;
			start = NULL;
			continue;
		}
		/* we've got character data */
		if (start == NULL)
			start = &string[i];
	}
	return array;
}

/**
 * up_wakeups_perhaps_data_changed:
 **/
static void
up_wakeups_perhaps_data_changed (UpWakeups *wakeups)
{
	guint total;

	/* total has changed */
	total = up_wakeups_data_get_total (wakeups);
	if (total != wakeups->priv->total_old) {
		/* no old data, assume this is true */
		if (wakeups->priv->total_old == 0)
			wakeups->priv->total_ave = total;
		else
			wakeups->priv->total_ave = UP_WAKEUPS_TOTAL_SMOOTH_FACTOR * (gfloat) (total - wakeups->priv->total_old);

		up_exported_wakeups_emit_total_changed (UP_EXPORTED_WAKEUPS (wakeups),
							wakeups->priv->total_ave);
	}

	/* unconditionally emit */
	up_exported_wakeups_emit_data_changed (UP_EXPORTED_WAKEUPS (wakeups));
}

/**
 * up_wakeups_poll_kernel_cb:
 **/
static gboolean
up_wakeups_poll_kernel_cb (UpWakeups *wakeups)
{
	guint i;
	guint j;
	gboolean ret;
	gboolean special_ipi;
	gchar *data = NULL;
	gchar **lines = NULL;
	GError *error = NULL;
	guint cpus = 0;
	const gchar *found;
	const gchar *found2;
	guint irq;
	guint interrupts;
	GPtrArray *sections;
	UpWakeupItem *item;

	g_debug ("event");

	/* set all kernel data objs to zero */
	for (i=0; i<wakeups->priv->data->len; i++) {
		item = g_ptr_array_index (wakeups->priv->data, i);
		if (!up_wakeup_item_get_is_userspace (item))
			up_wakeup_item_set_value (item, 0.0f);
	}

	/* get the data */
	ret = g_file_get_contents (UP_WAKEUPS_SOURCE_KERNEL, &data, NULL, &error);
	if (!ret) {
		g_warning ("failed to get data: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* split */
	lines = g_strsplit (data, "\n", 0);

	/* find out how many processors we have */
	sections = up_strsplit_complete_set (lines[0], " ", 0);
	cpus = sections->len;
	g_ptr_array_unref (sections);

	/* get the data from " 9:      29730        365   IO-APIC-fasteoi   acpi" */
	for (i=1; lines[i] != NULL; i++) {

		/* get sections and check correct length */
		sections = up_strsplit_complete_set (lines[i], " :", 2 + cpus);
		if (sections->len != 2 + cpus)
			goto skip;

		/* get irq */
		special_ipi = TRUE;
		found = g_ptr_array_index (sections, 0);
		if (strcmp (found, "NMI") == 0)
			irq = 0xff0;
		else if (strcmp (found, "LOC") == 0)
			irq = 0xff1;
		else if (strcmp (found, "RES") == 0)
			irq = 0xff2;
		else if (strcmp (found, "CAL") == 0)
			irq = 0xff3;
		else if (strcmp (found, "TLB") == 0)
			irq = 0xff4;
		else if (strcmp (found, "TRM") == 0)
			irq = 0xff5;
		else if (strcmp (found, "SPU") == 0)
			irq = 0xff6;
		else if (strcmp (found, "ERR") == 0)
			irq = 0xff7;
		else if (strcmp (found, "MIS") == 0)
			irq = 0xff8;
		else {
			irq = atoi (found);
			special_ipi = FALSE;
		}

		/* get the number of interrupts over all processors */
		interrupts = 0;
		for (j=1; j<cpus; j++) {
			found = g_ptr_array_index (sections, j);
			interrupts += atoi (found);
		}
		if (interrupts == 0)
			goto skip;

		/* get the detail string */
		found = g_ptr_array_index (sections, cpus+1);

		/* save in database */
		item = up_wakeups_data_get_or_create (wakeups, irq);
		if (up_wakeup_item_get_details (item) == NULL) {

			/* remove the interrupt type */
			found2 = strstr (found, "IO-APIC-fasteoi");
			if (found2 != NULL)
				found = g_strchug ((gchar*)found2+16);
			found2 = strstr (found, "IO-APIC-edge");
			if (found2 != NULL)
				found = g_strchug ((gchar*)found2+14);
			up_wakeup_item_set_details (item, found);

			/* we special */
			if (special_ipi)
				up_wakeup_item_set_cmdline (item, "kernel-ipi");
			else
				up_wakeup_item_set_cmdline (item, "interrupt");
			up_wakeup_item_set_is_userspace (item, FALSE);
		}
		/* we report this in minutes, not seconds */
		if (up_wakeup_item_get_old (item) > 0)
			up_wakeup_item_set_value (item, (interrupts - up_wakeup_item_get_old (item)) / (gfloat) UP_WAKEUPS_POLL_INTERVAL_KERNEL);
		up_wakeup_item_set_old (item, interrupts);
skip:
		g_ptr_array_unref (sections);
	}

	/* tell GUI we've changed */
	up_wakeups_perhaps_data_changed (wakeups);
out:
	g_free (data);
	g_strfreev (lines);
	return TRUE;
}

/**
 * up_wakeups_poll_userspace_cb:
 **/
static gboolean
up_wakeups_poll_userspace_cb (UpWakeups *wakeups)
{
	guint i;
	gboolean ret;
	GError *error = NULL;
	gchar *data = NULL;
	gchar **lines = NULL;
	const gchar *string;
	UpWakeupItem *item;
	GPtrArray *sections;
	guint pid;
	guint interrupts;
	gfloat interval = 5.0f;
	gchar *cmdline;

	g_debug ("event");

	/* set all userspace data objs to zero */
	for (i=0; i<wakeups->priv->data->len; i++) {
		item = g_ptr_array_index (wakeups->priv->data, i);
		if (up_wakeup_item_get_is_userspace (item))
			up_wakeup_item_set_value (item, 0.0f);
	}

	/* get the data */
	ret = g_file_get_contents (UP_WAKEUPS_SOURCE_USERSPACE, &data, NULL, &error);
	if (!ret) {
		g_warning ("failed to get data: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* split */
	lines = g_strsplit (data, "\n", 0);

	/* get the data from " 9:      29730        365   IO-APIC-fasteoi   acpi" */
	for (i=0; lines[i] != NULL; i++) {

		if (strstr (lines[i], "Timer Stats Version:") != NULL)
			continue;
		if (strstr (lines[i], "events/sec") != NULL)
			continue;

		/* get sections */
		sections = up_strsplit_complete_set (lines[i], " :", 4);

		/* get timeout */
		if (strstr (lines[i], "Sample period:") != NULL) {
			string = g_ptr_array_index (sections, 2);
			interval = atof (string);
			g_debug ("interval=%f", interval);
			goto skip;
		}

		/* check correct length */
		if (sections->len != 4)
			goto skip;

		/* if deferred */
		string = g_ptr_array_index (sections, 0);
		if (strstr (string, "D") != NULL)
			goto skip;
		interrupts = atoi (string);
		if (interrupts == 0)
			goto skip;

		/* get pid */
		string = g_ptr_array_index (sections, 1);
		pid = atoi (string);

		/* ignore scheduled */
		string = g_ptr_array_index (sections, 3);
		if (g_str_has_prefix (string, "tick_nohz_"))
			goto skip;
		if (g_str_has_prefix (string, "tick_setup_sched_timer"))
			goto skip;

		/* get details */

		/* save in database */
		item = up_wakeups_data_get_or_create (wakeups, pid);
		if (up_wakeup_item_get_details (item) == NULL) {
			/* get process name (truncated) */
			string = g_ptr_array_index (sections, 2);
			if (strcmp (string, "insmod") == 0 ||
			    strcmp (string, "modprobe") == 0 ||
			    strcmp (string, "swapper") == 0) {
				up_wakeup_item_set_cmdline (item, string);
				up_wakeup_item_set_is_userspace (item, FALSE);
			} else {
				/* try to get a better command line */
				cmdline = up_wakeups_get_cmdline (pid);
				up_wakeup_item_set_cmdline (item, cmdline);
				g_free (cmdline);
				if (up_wakeup_item_get_cmdline (item) == NULL ||
				    up_wakeup_item_get_cmdline (item)[0] == '\0')
					up_wakeup_item_set_cmdline (item, string);
				up_wakeup_item_set_is_userspace (item, TRUE);
			}
			string = g_ptr_array_index (sections, 3);
			up_wakeup_item_set_details (item, string);
		}
		/* we report this in minutes, not seconds */
		up_wakeup_item_set_value (item, (gfloat) interrupts / interval);
skip:
		g_ptr_array_unref (sections);

	}

	/* tell GUI we've changed */
	up_wakeups_perhaps_data_changed (wakeups);
out:
	g_free (data);
	g_strfreev (lines);
	return ret;
}

/**
 * up_wakeups_timerstats_disable:
 **/
static gboolean
up_wakeups_timerstats_disable (UpWakeups *wakeups)
{
	FILE *file;

	/* already same state */
	if (!wakeups->priv->polling_enabled)
		return TRUE;

	g_debug ("disabling timer stats");

	/* clear polling */
	if (wakeups->priv->poll_kernel_id != 0) {
		g_source_remove (wakeups->priv->poll_kernel_id);
		wakeups->priv->poll_kernel_id = 0;
	}
	if (wakeups->priv->poll_userspace_id != 0) {
		g_source_remove (wakeups->priv->poll_userspace_id);
		wakeups->priv->poll_userspace_id = 0;
	}
	if (wakeups->priv->disable_id != 0) {
		g_source_remove (wakeups->priv->disable_id);
		wakeups->priv->disable_id = 0;
	}

	file = fopen (UP_WAKEUPS_SOURCE_USERSPACE, "w");
	if (file == NULL)
		return FALSE;
	fprintf (file, "0\n");
	fclose (file);
	wakeups->priv->polling_enabled = FALSE;
	return TRUE;
}

/**
 * up_wakeups_disable_cb:
 **/
static gboolean
up_wakeups_disable_cb (UpWakeups *wakeups)
{
	g_debug ("disabling timer stats as we are idle");
	up_wakeups_timerstats_disable (wakeups);

	/* never repeat */
	return FALSE;
}

/**
 * up_wakeups_timerstats_enable:
 **/
static gboolean
up_wakeups_timerstats_enable (UpWakeups *wakeups)
{
	FILE *file;

	/* reset timeout */
	if (wakeups->priv->disable_id != 0)
		g_source_remove (wakeups->priv->disable_id);
	wakeups->priv->disable_id =
		g_timeout_add_seconds (UP_WAKEUPS_DISABLE_INTERVAL,
				       (GSourceFunc) up_wakeups_disable_cb, wakeups);
	g_source_set_name_by_id (wakeups->priv->disable_id, "[upower] up_wakeups_disable_cb");

	/* already same state */
	if (wakeups->priv->polling_enabled)
		return TRUE;

	g_debug ("enabling timer stats");

	/* setup polls */
	wakeups->priv->poll_kernel_id =
		g_timeout_add_seconds (UP_WAKEUPS_POLL_INTERVAL_KERNEL,
				       (GSourceFunc) up_wakeups_poll_kernel_cb, wakeups);
	g_source_set_name_by_id (wakeups->priv->poll_kernel_id, "[upower] up_wakeups_poll_kernel_cb");

	wakeups->priv->poll_userspace_id =
		g_timeout_add_seconds (UP_WAKEUPS_POLL_INTERVAL_USERSPACE,
				       (GSourceFunc) up_wakeups_poll_userspace_cb, wakeups);
	g_source_set_name_by_id (wakeups->priv->poll_userspace_id, "[upower] up_wakeups_poll_userspace_cb");

	file = fopen (UP_WAKEUPS_SOURCE_USERSPACE, "w");
	if (file == NULL)
		return FALSE;
	fprintf (file, "1\n");
	fclose (file);
	wakeups->priv->polling_enabled = TRUE;
	return TRUE;
}

/**
 * up_wakeups_class_init:
 **/
static void
up_wakeups_class_init (UpWakeupsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = up_wakeups_finalize;

	g_type_class_add_private (klass, sizeof (UpWakeupsPrivate));
}

/**
 * up_wakeups_init:
 **/
static void
up_wakeups_init (UpWakeups *wakeups)
{
	wakeups->priv = UP_WAKEUPS_GET_PRIVATE (wakeups);
	wakeups->priv->data = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* test if we have an interface */
	if (g_file_test (UP_WAKEUPS_SOURCE_KERNEL, G_FILE_TEST_EXISTS) ||
	    g_file_test (UP_WAKEUPS_SOURCE_KERNEL, G_FILE_TEST_EXISTS)) {
		up_exported_wakeups_set_has_capability (UP_EXPORTED_WAKEUPS (wakeups), TRUE);
	}

	g_signal_connect (wakeups, "handle-get-data",
			  G_CALLBACK (up_wakeups_get_data), wakeups);
	g_signal_connect (wakeups, "handle-get-total",
			  G_CALLBACK (up_wakeups_get_total), wakeups);
}

/**
 * up_wakeups_finalize:
 **/
static void
up_wakeups_finalize (GObject *object)
{
	UpWakeups *wakeups;

	g_return_if_fail (object != NULL);
	g_return_if_fail (UP_IS_WAKEUPS (object));

	wakeups = UP_WAKEUPS (object);
	wakeups->priv = UP_WAKEUPS_GET_PRIVATE (wakeups);

	/* stop timerstats */
	up_wakeups_timerstats_disable (wakeups);

	g_ptr_array_unref (wakeups->priv->data);

	G_OBJECT_CLASS (up_wakeups_parent_class)->finalize (object);
}

/**
 * up_wakeups_new:
 **/
UpWakeups *
up_wakeups_new (void)
{
	return g_object_new (UP_TYPE_WAKEUPS, NULL);
}

void
up_wakeups_register (UpWakeups *wakeups,
		     GDBusConnection *connection)
{
	GError *error = NULL;

	g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (wakeups),
					  connection,
					  "/org/freedesktop/UPower/Wakeups",
					  &error);

	if (error != NULL) {
		g_critical ("Cannot register wakeups on system bus: %s", error->message);
		g_error_free (error);
	}
}
