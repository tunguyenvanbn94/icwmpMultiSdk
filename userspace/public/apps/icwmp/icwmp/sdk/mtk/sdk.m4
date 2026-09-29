# SDK "mtk" -- MediaTek/Airoha OpenWrt, HNI product.
# m4_included unconditionally by sdk/enabled.m4: every AM_CONDITIONAL must be
# reached on every configure run, the SDK specific part is guarded on $with_sdk.
AM_CONDITIONAL([ICWMP_SDK_MTK], [test "x$with_sdk" = "xmtk" && test "x$enable_icwmp_tr098" = "xyes"])
AS_IF([test "x$with_sdk" = "xmtk"], [
  AC_DEFINE([ICWMP_MTK], [1], [MTK/Airoha OpenWrt SDK])
])
