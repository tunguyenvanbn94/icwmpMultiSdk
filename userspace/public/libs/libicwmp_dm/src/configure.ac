AC_INIT([libtr098], [0.1], [omar.kallel@pivasoftware.com])


AM_INIT_AUTOMAKE([subdir-objects])

AC_ARG_ENABLE(tr064, [AS_HELP_STRING([--enable-tr064], [enable tr064 upnp feature])], AC_DEFINE(UPNP_TR064),)
AM_CONDITIONAL([UPNP_TR064],[test "x$enable_tr064" = "xyes"])

# SDK backend.  One directory per SDK under sdk/, each with its own sdk.m4 and
# sdk.mk.  sdk/enabled.m4 and sdk/enabled.mk are regenerated from whatever
# directories exist by ./tools/sdk-scan.sh, so an SDK can be added or removed
# (./tools/sdk-prune.sh) without editing this file.  See sdk/sdk.h.
m4_include([sdk/enabled.m4])

# checks for programs
AC_PROG_CC
AM_PROG_CC_C_O
LT_INIT
AC_ENABLE_SHARED

LIBJSON_LIBS='-ljson-c'
AC_SUBST([LIBJSON_LIBS])

AC_ARG_WITH([uci-include-path],
  [AS_HELP_STRING([--with-uci-include-path],
    [location of the uci library headers])],
  [LIBUCI_CFLAGS="-I$withval"])
AC_SUBST([LIBUCI_CFLAGS])

AC_ARG_WITH([uci-lib-path],
  [AS_HELP_STRING([--with-uci-lib-path], [location of the uci library])], [LIBUCI_LDFLAGS="-L$withval"])
AC_SUBST([LIBUCI_LDFLAGS])

LIBUCI_LIBS='-luci'
AC_SUBST([LIBUCI_LIBS])

LIBTRACE_LIBS='-ltrace'
AC_SUBST([LIBTRACE_LIBS])

AC_ARG_WITH([libubox-include-path],
  [AS_HELP_STRING([--with-libubox-include-path],
    [location of the libubox library headers])],
  [LIBUBOX_CFLAGS="-I$withval"])
AC_SUBST([LIBUBOX_CFLAGS])

AC_ARG_WITH([libubox-lib-path],
  [AS_HELP_STRING([--with-libubox-lib-path], [location of the libubox library])], [LIBUBOX_LDFLAGS="-L$withval"])
AC_SUBST([LIBUBOX_LDFLAGS])

LIBUBOX_LIBS='-lubox'
AC_SUBST([LIBUBOX_LIBS])

AC_ARG_WITH([libubus-include-path],
  [AS_HELP_STRING([--with-libubus-include-path],
    [location of the libubus library headers])],
  [LIBUBUS_CFLAGS="-I$withval"])
AC_SUBST([LIBUBUS_CFLAGS])

AC_ARG_WITH([libubus-lib-path],
  [AS_HELP_STRING([--with-libubus-lib-path], [location of the libubus library])], [LIBUBUS_LDFLAGS="-L$withval"])
AC_SUBST([LIBUBUS_LDFLAGS])

LIBUBUS_LIBS='-lubus'
AC_SUBST([LIBUBUS_LIBS])

LBLOBMSG_LIBS='-lblobmsg_json'
AC_SUBST([LBLOBMSG_LIBS])

# checks for header files
AC_CHECK_HEADERS([stdlib.h string.h])

# checks for typedefs, structures, and compiler characteristics
AC_TYPE_UINT8_T

# Makefiles
AC_CONFIG_FILES([
Makefile
bin/Makefile
])

AC_OUTPUT
