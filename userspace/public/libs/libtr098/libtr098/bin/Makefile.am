TR098_VERSION = 3.0.0
LIB_TR098_VERSION = 3:0:0

lib_LTLIBRARIES = libtr098.la

# Engine and storage helpers.  No SDK name appears in this file: the sources,
# the CFLAGS and the extra libraries of the selected SDK come from
# sdk/<name>/sdk.mk, pulled in by the generated sdk/enabled.mk.
libtr098_la_SOURCES =	\
	../md5.c					\
	../dmtr098.c			\
	../dmentry.c			\
	../dmmem.c			\
	../dmubus.c			\
	../dmjson.c			\
	../dmuci.c			\
	../dmcommon.c		\
	../dm_registry.c		\
	../wepkey.c

# Portable data model modules: no SDK specific call, every SDK may link them.
# Which ones a build actually uses is decided in sdk/<name>/sdk.mk, except
# these two, which every SDK has needed so far.
libtr098_la_SOURCES +=	\
	../tr098/managementserver.c			\
	../tr098/softwaremodules.c			\
	../tr098/common/icwmpcfg.c

libtr098_la_CFLAGS =	\
	$(AM_CFLAGS)			\
	$(LIBUCI_CFLAGS)		\
	$(LIBUBOX_CFLAGS)		\
	$(LIBUBUS_CFLAGS)

libtr098_la_LDFLAGS = 	\
	$(AM_LDFLAGS)			\
	$(LIBUCI_LDFLAGS)		\
	$(LIBUBOX_LDFLAGS)		\
	$(LIBUBUS_LDFLAGS)		\
	-share					\
	-version-info $(LIB_TR098_VERSION)

libtr098_la_LIBADD =	\
	$(AM_LIBS)				\
	$(LIBUCI_LIBS)			\
	$(LIBUBOX_LIBS)			\
	$(LIBUBUS_LIBS)			\
	$(LIBJSON_LIBS)			\
	$(LBLOBMSG_LIBS)		\
	-lpthread

libtr098_la_CFLAGS+=-DTR098_VERSION=\"$(TR098_VERSION)\"
libtr098_la_LDFLAGS+=-DTR098_VERSION=\"$(TR098_VERSION)\"

libtr098_la_CFLAGS+=-I../
libtr098_la_CFLAGS+=-I../tr098/
libtr098_la_CFLAGS+=-I../tr098/common/
libtr098_la_CFLAGS+=-I../upnp

if UPNP_TR064
libtr098_la_SOURCES +=							\
	../upnp/upnp_deviceinfo.c	            \
	../upnp/upnp_configuration.c			\
	../upnp/upnp_monitoring.c				\
	../upnp/upnp_common.c
endif

# One include line per SDK directory that exists; regenerate after adding or
# deleting one:  ./tools/sdk-scan.sh
include $(top_srcdir)/sdk/enabled.mk
