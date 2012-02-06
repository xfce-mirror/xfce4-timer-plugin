## Process this file with automake to produce Makefile.in

## Created by Anjuta - will be overwritten
## If you don't want it to overwrite it,
## 	Please disable it in the Anjuta project configuration


plugindir = $(libexecdir)/xfce4/panel-plugins
plugin_PROGRAMS = xfce4-timer

# deprecated?
#plugin_LTLIBRARIES =							\
#	libxfce4_timer.la

xfce4_timer_SOURCES =						\
	xfcetimer.c xfcetimer.h

xfce4_timer_LDFLAGS =						\
	@LIBXFCE4PANEL_LIBS@ \
	@LIBXFCE4UTIL_LIBS@ \
	@LIBXFCEGUI4_LIBS@
		
xfce4_timer_CFLAGS =						\
	-I$(top_srcdir) \
	@LIBXFCEGUI4_CFLAGS@ \
	@LIBXFCE4UTIL_CFLAGS@ \
	@LIBXFCE4PANEL_CFLAGS@ \
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"

# deprecated?	
#libxfce4_timer_la_LIBADD = 						\
#	@XFCE4_PANEL_LIBS@

desktopdir = $(datadir)/xfce4/panel-plugins
desktop_in_in_files = xfce4-timer.desktop.in.in
desktop_in_files = $(desktop_in_in_files:.desktop.in.in=.desktop.in)
%.desktop.in: %.desktop.in.in
	sed -e "s,\@libexecdir\@,$(libexecdir),g" < $< > $@	
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = 				\
	$(desktop_in_in_files)

DISTCLEANFILES =			\
	$(desktop_DATA) $(desktop_in_files)

