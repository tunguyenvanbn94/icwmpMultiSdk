# SDK "bdk" -- automake fragment, included from bin/Makefile.am.
# bin/Makefile.am defines icwmp_tr098d_* only inside "if ICWMP_TR098", so every
# "+=" here has to sit under that same condition.  automake checks this
# statically over EVERY combination of conditions, not just the one configure
# actually selects:
#   error: cannot apply '+=' because 'icwmp_tr098d_SOURCES' is not defined
#          in the following conditions: ICWMP_SDK_<X> and !ICWMP_TR098
if ICWMP_TR098
if ICWMP_SDK_BDK
# C glue instead of the /usr/sbin/icwmp shell backend
icwmp_tr098d_SOURCES +=	\
	../sdk/bdk/icwmp_bdk.c		\
	../sdk/bdk/external_bdk.c

# read-only squashfs rootfs: everything persistent lives in /data/icwmp.
# Pass the path with -D here instead of reassigning CWMP_BKP_FILE (set
# unconditionally in bin/Makefile.am): automake does not reliably let a
# conditional reassignment override it and the binary kept /etc/icwmpd, where
# fopen() fails and bkp_tree stays NULL (crash in mxmlSaveFile, 2026-09-19).
# sdk/bdk/icwmp_bdk.h forces the same value in backupSession.c anyway.
icwmp_tr098d_CFLAGS += -DCWMP_BKP_FILE=\"/data/icwmp/.icwmpd_backup_session.xml\"
icwmp_tr098d_CFLAGS += -DICWMP_BOOT_FLAG_FILE=\"/data/icwmp/.icwmpd_boot\"
icwmp_tr098d_CFLAGS += -DICWMP_SDK_HEADER='"icwmp_bdk.h"'
icwmp_tr098d_CFLAGS += -I../sdk/bdk -g
# -rdynamic: backtrace_symbols_fd() of the crash handler (sdk/bdk/icwmp_bdk.c)
# can name the functions of the executable, -g: addr2line on the build binary
icwmp_tr098d_LDFLAGS += -rdynamic
icwmp_tr098d_LDADD += $(BDK_LIBS)
endif
endif
