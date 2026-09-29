# SDK "mtk" -- automake fragment, included from bin/Makefile.am.
if ICWMP_SDK_MTK
# easycwmp <-> cwmp config mirror + the shell backend for the actions
icwmp_tr098d_SOURCES +=	\
	../sdk/mtk/icwmp_mtk.c	\
	../external.c

icwmp_tr098d_CFLAGS += -DICWMP_SDK_HEADER='"icwmp_mtk.h"'
icwmp_tr098d_CFLAGS += -I../sdk/mtk
endif
