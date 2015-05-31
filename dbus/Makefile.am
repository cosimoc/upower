noinst_LTLIBRARIES=libupower-dbus.la

AM_CPPFLAGS = \
	$(GIO_CFLAGS)		\
	$(GIO_UNIX_CFLAGS)	\
	$(GLIB_CFLAGS)

libupower_dbus_la_LIBADD = \
	$(GIO_LIBS)		\
	$(GIO_UNIX_LIBS)	\
	$(GLIB_LIBS)

BUILT_SOURCES =		\
	up-daemon-generated.h		\
	up-daemon-generated.c		\
	up-device-generated.h		\
	up-device-generated.c		\
	up-kbd-backlight-generated.h	\
	up-kbd-backlight-generated.c	\
	up-wakeups-generated.h		\
	up-wakeups-generated.c

libupower_dbus_la_SOURCES = $(BUILT_SOURCES)

up-daemon-generated.h: org.freedesktop.UPower.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.UPower. \
	--generate-c-code up-daemon-generated \
	--c-namespace Up \
	--annotate "org.freedesktop.UPower" "org.gtk.GDBus.C.Name" ExportedDaemon \
	$(srcdir)/org.freedesktop.UPower.xml
up-daemon-generated.c: up-daemon-generated.h

up-device-generated.h: org.freedesktop.UPower.Device.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.UPower.Device. \
	--generate-c-code up-device-generated \
	--c-namespace Up \
	--annotate "org.freedesktop.UPower.Device" "org.gtk.GDBus.C.Name" ExportedDevice \
	$(srcdir)/org.freedesktop.UPower.Device.xml
up-device-generated.c: up-device-generated.h

up-kbd-backlight-generated.h: org.freedesktop.UPower.KbdBacklight.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.UPower.KbdBacklight. \
	--generate-c-code up-kbd-backlight-generated \
	--c-namespace Up \
	--annotate "org.freedesktop.UPower.KbdBacklight" "org.gtk.GDBus.C.Name" ExportedKbdBacklight \
	$(srcdir)/org.freedesktop.UPower.KbdBacklight.xml
up-kbd-backlight-generated.c: up-kbd-backlight-generated.h

up-wakeups-generated.h: org.freedesktop.UPower.Wakeups.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.UPower.Wakeups. \
	--generate-c-code up-wakeups-generated \
	--c-namespace Up \
	--annotate "org.freedesktop.UPower.Wakeups" "org.gtk.GDBus.C.Name" ExportedWakeups \
	$(srcdir)/org.freedesktop.UPower.Wakeups.xml
up-wakeups-generated.c: up-wakeups-generated.h

dbusifdir = $(datadir)/dbus-1/interfaces
dist_dbusif_DATA =						\
	org.freedesktop.UPower.xml				\
	org.freedesktop.UPower.Device.xml			\
	org.freedesktop.UPower.KbdBacklight.xml			\
	org.freedesktop.UPower.Wakeups.xml

-include $(top_srcdir)/git.mk
