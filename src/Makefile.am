glib_enum_prefix = gst_dreamsource

include $(top_srcdir)/common/glib-gen.mak

built_sources = gstdreamsource-marshal.c
built_headers = gstdreamsource-marshal.h
BUILT_SOURCES = $(built_sources) $(built_headers)
# plugindir is set in configure

CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = gstdreamsource-marshal.list

plugin_LTLIBRARIES = libgstdreamsource.la

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed

libgstdreamsource_la_SOURCES = gstdreamaudiosource.c gstdreamvideosource.c gstdreamtssource.c gstdreamsource.c $(built_sources)
libgstdreamsource_la_CFLAGS = $(GST_CFLAGS)
libgstdreamsource_la_LIBADD =  $(GST_LIBS) -lgstbase-1.0
libgstdreamsource_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstdreamaudiosource.h gstdreamvideosource.h gstdreamtssource.h gstdreamsource.h
