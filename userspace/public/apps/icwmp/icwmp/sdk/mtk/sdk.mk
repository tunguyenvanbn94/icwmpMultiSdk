# SDK "mtk" -- automake fragment, included from bin/Makefile.am.
# bin/Makefile.am defines icwmp_tr098d_* only inside "if ICWMP_TR098", so every
# "+=" here has to sit under that same condition.  automake checks this
# statically over EVERY combination of conditions, not just the one configure
# actually selects:
#   error: cannot apply '+=' because 'icwmp_tr098d_SOURCES' is not defined
#          in the following conditions: ICWMP_SDK_<X> and !ICWMP_TR098
if ICWMP_TR098
if ICWMP_SDK_MTK
# easycwmp <-> cwmp config mirror + the shell backend for the actions
icwmp_tr098d_SOURCES +=	\
	../sdk/mtk/icwmp_mtk.c	\
	../external.c

icwmp_tr098d_CFLAGS += -DICWMP_SDK_HEADER='"icwmp_mtk.h"'
icwmp_tr098d_CFLAGS += -I../sdk/mtk
endif
endif
