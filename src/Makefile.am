## Process this file with automake to produce Makefile.in

plugindir = $(libexecdir)/xfce4/panel-plugins
plugin_PROGRAMS = xfce4-timer

# deprecated?
#plugin_LTLIBRARIES =							\
#	libxfce4_timer.la

xfce4_timer_SOURCES =						\
	xfcetimer.c xfcetimer.h

if HAVE_4UI
xfce4_timer_LDFLAGS =						\
	@LIBXFCE4PANEL_LIBS@ \
	@LIBXFCE4UTIL_LIBS@ \
	@LIBXFCE4UI_LIBS@
		
xfce4_timer_CFLAGS =						\
	-I$(top_srcdir) \
	@LIBXFCE4UI_CFLAGS@ \
	@LIBXFCE4UTIL_CFLAGS@ \
	@LIBXFCE4PANEL_CFLAGS@ \
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"
else
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
endif
# deprecated?	
#libxfce4_timer_la_LIBADD = 						\
#	@XFCE4_PANEL_LIBS@

#Old
desktopdir = $(datadir)/xfce4/panel-plugins
desktop_in_in_files = xfce4-timer.desktop.in.in
desktop_in_files = $(desktop_in_in_files:.desktop.in.in=.desktop.in)
%.desktop.in: %.desktop.in.in
	sed -e "s,\@libexecdir\@,$(libexecdir),g" < $< > $@	
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

#desktopdir = $(datadir)/xfce4/panel-plugins
#desktop_in_files = xfce4-timer.desktop.in
#desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
#@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = 				\
	$(desktop_in_in_files)

DISTCLEANFILES =			\
	$(desktop_DATA) $(desktop_in_files)
