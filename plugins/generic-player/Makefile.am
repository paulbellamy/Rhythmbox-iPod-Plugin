plugindir = $(PLUGINDIR)
plugin_LTLIBRARIES = libgeneric-player.la

libgeneric_player_la_SOURCES = \
	rb-generic-player-plugin.c			\
	$(top_srcdir)/sources/rb-generic-player-source.c \
	$(top_srcdir)/sources/rb-generic-player-source.h \
	$(top_srcdir)/sources/rb-psp-source.c		 \
	$(top_srcdir)/sources/rb-psp-source.h

if HAVE_HAL
libgeneric_player_la_SOURCES +=			\
	$(top_srcdir)/sources/rb-nokia770-source.c	\
	$(top_srcdir)/sources/rb-nokia770-source.h
endif

	
libgeneric_player_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/player                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/iradio                    	\
	-I$(top_srcdir)/podcast                    	\
	-I$(top_srcdir)/remote				\
	-I$(top_builddir)/remote			\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(HAL_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)				\
	$(WARN_CFLAGS)					\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

gladedir = $(datadir)/rhythmbox/glade
glade_DATA =

plugin_in_files = generic-player.rb-plugin.desktop.in

%.rb-plugin: %.rb-plugin.desktop.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.rb-plugin.desktop.in=.rb-plugin)

EXTRA_DIST = $(glade_DATA) $(plugin_in_files)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
