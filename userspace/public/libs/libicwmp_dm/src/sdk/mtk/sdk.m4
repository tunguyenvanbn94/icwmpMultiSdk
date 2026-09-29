# SDK "mtk" -- MediaTek/Airoha OpenWrt, HNI product tree.
# m4_included unconditionally by sdk/enabled.m4, so every AM_CONDITIONAL here
# is reached on every configure run and the SDK specific part is guarded on
# $with_sdk.  config.status refuses to substitute a conditional it never saw.
AM_CONDITIONAL([SDK_MTK], [test "x$with_sdk" = "xmtk"])

# Migration scaffolding: while objects are still being ported from the
# product's easycwmp shell library to C, sdk/mtk/compat/ answers the paths no
# C module claims.  The end state is --disable-dm-script-compat, which drops
# the child shell entirely; sdk/mtk/compat/ can then be deleted.
AC_ARG_ENABLE([dm-script-compat],
  [AS_HELP_STRING([--disable-dm-script-compat],
    [mtk: do not fall back to the easycwmp shell library for objects not ported to C yet])],
  [], [enable_dm_script_compat=yes])

AS_IF([test "x$with_sdk" = "xmtk"], [
  AC_DEFINE([DM_SDK_MTK], [1], [MTK/Airoha OpenWrt backend])
  AC_DEFINE([DM_PLATFORM_MTK], [1], [MTK/Airoha OpenWrt backend (compatibility name)])
  AS_IF([test "x$enable_dm_script_compat" = "xyes"],
    [AC_DEFINE([DM_MTK_SCRIPT_COMPAT], [1], [mtk: easycwmp shell fallback compiled in])])
])
AM_CONDITIONAL([DM_MTK_SCRIPT_COMPAT],
  [test "x$with_sdk" = "xmtk" && test "x$enable_dm_script_compat" = "xyes"])
