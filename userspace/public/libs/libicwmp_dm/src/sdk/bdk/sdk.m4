# SDK "bdk" -- Broadcom BDK Distributed MDM through libbcm_generic_hal.
# m4_included unconditionally by sdk/enabled.m4, so every AM_CONDITIONAL here
# is reached on every configure run and the SDK specific part is guarded on
# $with_sdk.  config.status refuses to substitute a conditional it never saw.
AM_CONDITIONAL([SDK_BDK], [test "x$with_sdk" = "xbdk"])
AS_IF([test "x$with_sdk" = "xbdk"], [
  AC_DEFINE([DM_SDK_BDK], [1], [Broadcom BDK backend])
  AC_DEFINE([DM_PLATFORM_BDK], [1], [Broadcom BDK backend (compatibility name)])
])
