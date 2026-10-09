# SDK "mtk" -- automake fragment, included from bin/Makefile.am.
# Paths are relative to bin/, like the rest of that file.
if SDK_MTK
libtr098_la_SOURCES +=	\
	../sdk/mtk/dmplatform_mtk.c				\
	../sdk/mtk/dmmtk.c						\
	../sdk/mtk/input_contract_mtk.c				\
	../sdk/mtk/dm098/root_mtk.c				\
	../sdk/mtk/dm098/deviceinfo_mtk.c			\
	../sdk/mtk/dm098/time_mtk.c				\
	../sdk/mtk/dm098/managementserver_core_mtk.c	\
	../sdk/mtk/dm098/managementserver_mtk.c		\
	../sdk/mtk/dm098/lan_mtk.c				\
	../sdk/mtk/dm098/lanhosts_mtk.c				\
	../sdk/mtk/dm098/laneth_mtk.c				\
	../sdk/mtk/dm098/x_ais_mesh_mtk.c			\
	../sdk/mtk/dm098/wlan_mtk.c				\
	../sdk/mtk/dm098/wlanassoc_mtk.c			\
	../sdk/mtk/dm098/wlansec_mtk.c			\
	../sdk/mtk/dm098/wan_mtk.c			\
	../sdk/mtk/dm098/wanip_mtk.c	\
	../sdk/mtk/dm098/wanipv6_mtk.c	\
	../sdk/mtk/dm098/portmapping_mtk.c	\
	../sdk/mtk/dm098/servicelist_mtk.c	\
	../sdk/mtk/dm098/diag_mtk.c	\
	../sdk/mtk/dm098/ipping_mtk.c	\
	../sdk/mtk/dm098/traceroute_mtk.c	\
	../sdk/mtk/dm098/lookupdiag_mtk.c	\
	../sdk/mtk/dm098/tr143diag_mtk.c	\
	../sdk/mtk/dm098/layer3forwarding_mtk.c	\
	../sdk/mtk/dm098/root_hidden_mtk.c	\
	../sdk/mtk/dm098/account_mtk.c	\
	../sdk/mtk/dm098/x_ais_carrierlocking_mtk.c	\
	../sdk/mtk/dm098/x_ais_webuserinfo_mtk.c	\
	../sdk/mtk/dm098/xmpp_mtk.c	\
	../sdk/mtk/dm098/firewall_mtk.c	\
	../sdk/mtk/dm098/x_ais_upnp_mtk.c	\
	../sdk/mtk/dm098/x_ais_3rdagent_mtk.c	\
	../sdk/mtk/dm098/x_ais_cpeagent_mtk.c	\
	../sdk/mtk/dm098/x_ais_autowifiscan_mtk.c	\
	../sdk/mtk/dm098/x_ais_dhcpclient_mtk.c	\
	../sdk/mtk/dm098/x_ais_lanpolicy_mtk.c	\
	../sdk/mtk/dm098/x_ais_sshtelnet_mtk.c	\
	../sdk/mtk/dm098/x_ais_meshapi_mtk.c	\
	../sdk/mtk/dm098/x_ais_ddns_mtk.c	\
	../sdk/mtk/dm098/x_ais_conf_mtk.c	\
	../sdk/mtk/dm098/x_ais_logging_mtk.c	\
	../sdk/mtk/dm098/x_ais_uplinksetup_mtk.c	\
	../sdk/mtk/dm098/x_ais_wifistatus_mtk.c	\
	../sdk/mtk/dm098/x_ais_mlo_mtk.c	\
	../sdk/mtk/dm098/device_ip_mtk.c	\
	../sdk/mtk/dm098/device_traceroute_mtk.c	\
	../sdk/mtk/dm098/device_dhcpv6_mtk.c	\
	../sdk/mtk/dm098/docsis_mtk.c	\
	../sdk/mtk/dm098/device_ppp_mtk.c	\
	../sdk/mtk/dm098/device_ddns_mtk.c	\
	../sdk/mtk/dm098/device_ra_mtk.c	\
	../sdk/mtk/dm098/services_mtk.c	\
	../sdk/mtk/dm181/root181_mtk.c	\
	../sdk/mtk/dm181/stack181_mtk.c	\
	../sdk/mtk/dm181/ipv6_181_mtk.c

if DM_MTK_SCRIPT_COMPAT
libtr098_la_SOURCES +=	\
	../sdk/mtk/compat/dmscript.c
endif

libtr098_la_CFLAGS += -I../sdk/mtk/ -I../sdk/mtk/dm098/ -I../sdk/mtk/dm181/
# vendor prefix of the product tree (X_AIS_ is the operator's, X_HNI_ ours)
libtr098_la_CFLAGS += -DCUSTOM_PREFIX=\"X_HNI_\"
libtr098_la_CFLAGS += -DDMSCRIPT_PATH=\"/usr/share/icwmp/icwmp_dm.sh\"
libtr098_la_LIBADD += -lm
endif
