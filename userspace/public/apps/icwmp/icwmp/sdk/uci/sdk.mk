# SDK "uci" -- automake fragment, included from bin/Makefile.am.
if ICWMP_SDK_UCI
icwmp_tr098d_SOURCES +=	\
	../sdk/uci/icwmp_uci.c	\
	../external.c

icwmp_tr098d_CFLAGS += -I../sdk/uci
endif
