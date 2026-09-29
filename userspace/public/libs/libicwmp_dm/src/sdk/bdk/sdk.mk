# SDK "bdk" -- automake fragment, included from bin/Makefile.am.
# Paths are relative to bin/, like the rest of that file.
if SDK_BDK
libtr098_la_SOURCES +=	\
	../sdk/bdk/dmplatform_bdk.c			\
	../sdk/bdk/dmproxy_bdk.c				\
	../sdk/bdk/dm098/root_bdk.c			\
	../sdk/bdk/dm098/root181_bdk.c		\
	../sdk/bdk/dm098/mlo_bdk.c			\
	../sdk/bdk/dm098/sample_bdk.c			\
	../sdk/bdk/dm098/deviceinfo_bdk.c		\
	../sdk/bdk/dm098/landevice_bdk.c		\
	../sdk/bdk/dm098/wandevice_bdk.c		\
	../sdk/bdk/dm098/system_bdk.c

libtr098_la_CFLAGS += -I../sdk/bdk/ -I../sdk/bdk/dm098/ -g
# developer template object X_MARUSYS_COM_Sample (sdk/bdk/dm098/sample_bdk.c):
# drop the define for a production build
libtr098_la_CFLAGS += -DBDK_SAMPLE_OBJECT
# vendor prefix and persistent paths for the read-only BDK rootfs
libtr098_la_CFLAGS += -DCUSTOM_PREFIX=\"X_MARUSYS_COM_\"
libtr098_la_CFLAGS += -DTR098_CONFIG=\"/data/icwmp/tr098\"
libtr098_la_CFLAGS += -DTR098_SAVEDIR=\"/tmp/icwmp/.tr098\"
libtr098_la_CFLAGS += -DDM_ENABLED_NOTIFY=\"/data/icwmp/tr098/.dm_enabled_notify\"
libtr098_la_CFLAGS += -DDM_ENABLED_NOTIFY_TEMPORARY=\"/tmp/icwmp/.dm_enabled_notify_temporary\"
endif
