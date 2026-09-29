# SDK "mtk" -- automake fragment, included from bin/Makefile.am.
# Paths are relative to bin/, like the rest of that file.
if SDK_MTK
libtr098_la_SOURCES +=	\
	../sdk/mtk/dmplatform_mtk.c				\
	../sdk/mtk/dmmtk.c						\
	../sdk/mtk/dm098/root_mtk.c				\
	../sdk/mtk/dm098/deviceinfo_mtk.c			\
	../sdk/mtk/dm098/time_mtk.c				\
	../sdk/mtk/dm098/managementserver_core_mtk.c	\
	../sdk/mtk/dm098/managementserver_mtk.c		\
	../sdk/mtk/dm098/lan_mtk.c				\
	../sdk/mtk/dm098/lanhosts_mtk.c				\
	../sdk/mtk/dm098/laneth_mtk.c				\
	../sdk/mtk/dm098/x_ais_mesh_mtk.c			\
	../sdk/mtk/dm098/wlan_mtk.c				\
	../sdk/mtk/dm098/wlanassoc_mtk.c

if DM_MTK_SCRIPT_COMPAT
libtr098_la_SOURCES +=	\
	../sdk/mtk/compat/dmscript.c
endif

libtr098_la_CFLAGS += -I../sdk/mtk/ -I../sdk/mtk/dm098/
# vendor prefix of the product tree (X_AIS_ is the operator's, X_HNI_ ours)
libtr098_la_CFLAGS += -DCUSTOM_PREFIX=\"X_HNI_\"
libtr098_la_CFLAGS += -DDMSCRIPT_PATH=\"/usr/share/icwmp/icwmp_dm.sh\"
libtr098_la_LIBADD += -lm
endif
