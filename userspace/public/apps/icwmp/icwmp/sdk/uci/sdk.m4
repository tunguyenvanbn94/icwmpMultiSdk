# SDK "uci" -- stock OpenWrt.  The cwmp UCI config is the config of record,
# there is nothing to mirror; actions go to the /usr/sbin/icwmp shell script.
# m4_included unconditionally by sdk/enabled.m4: every AM_CONDITIONAL must be
# reached on every configure run, the SDK specific part is guarded on $with_sdk.
AM_CONDITIONAL([ICWMP_SDK_UCI], [test "x$with_sdk" = "xuci" && test "x$enable_icwmp_tr098" = "xyes"])
