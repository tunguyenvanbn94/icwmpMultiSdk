# SDK "uci" -- OpenWrt, stock iopsys layout.  Reference SDK: no vendor glue,
# the portable tr098/ modules on plain UCI.
#
# m4_included unconditionally by sdk/enabled.m4, so everything it
# does must be guarded on $with_sdk -- an AM_CONDITIONAL has to be reached on
# every configure run or config.status refuses to substitute it.
AM_CONDITIONAL([SDK_UCI], [test "x$with_sdk" = "xuci"])
