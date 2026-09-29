# SDK "uci" -- automake fragment, included from bin/Makefile.am.
# Paths are relative to bin/, like the rest of that file.
if SDK_UCI
libtr098_la_SOURCES +=	\
	../sdk/uci/dmplatform_uci.c		\
	../tr098/root.c						\
	../tr098/deviceinfo.c				\
	../tr098/times.c						\
	../tr098/upnp.c						\
	../tr098/voice_services.c 			\
	../tr098/x_iopsys_eu_ice.c 			\
	../tr098/x_iopsys_eu_igmp.c 			\
	../tr098/x_iopsys_eu_ipacccfg.c		\
	../tr098/x_iopsys_eu_logincfg.c		\
	../tr098/x_iopsys_eu_power_mgmt.c	\
	../tr098/x_iopsys_eu_syslog.c			\
	../tr098/x_iopsys_eu_dropbear.c		\
	../tr098/x_iopsys_eu_owsd.c			\
	../tr098/x_iopsys_eu_buttons.c		\
	../tr098/x_iopsys_eu_wifilife.c		\
	../tr098/lan_interfaces.c				\
	../tr098/landevice.c					\
	../tr098/layer_2_bridging.c			\
	../tr098/wandevice.c					\
	../tr098/x_iopsys_eu_wifi.c			\
	../tr098/ippingdiagnostics.c			\
	../tr098/downloaddiagnostic.c			\
	../tr098/uploaddiagnostic.c			\
	../tr098/deviceconfig.c				\
	../tr098/layer_3_forwarding.c			\
	../tr098/xmpp.c

libtr098_la_CFLAGS += -I../sdk/uci/
endif
