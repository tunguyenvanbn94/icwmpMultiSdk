# SDK "bdk" -- Broadcom BDK.  m4_included unconditionally: guard on $with_sdk.
AM_CONDITIONAL([ICWMP_SDK_BDK], [test "x$with_sdk" = "xbdk" && test "x$enable_icwmp_tr098" = "xyes"])
AS_IF([test "x$with_sdk" = "xbdk"], [
  AC_DEFINE([ICWMP_BDK], [1], [Broadcom BDK SDK])
])
AC_ARG_VAR([BDK_LIBS], [Broadcom libraries to link with --with-sdk=bdk (cms_core, mdm_cbk_tr69, bcm_generic_hal, cms_msg, cms_util...)])
