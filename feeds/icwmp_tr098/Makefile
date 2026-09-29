#
# Copyright (C) 2020-2025 IOPSYS Software Solutions AB
#
# This is free software, licensed under the BSD-3-Clause
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/target.mk

PKG_NAME:=icwmp_tr098
PKG_VERSION:=3
PKG_RELEASE:=2

PKG_SOURCE:=$(APP_HNI_ICWMP_TR098_DIR)
# the data model scripts and the ACS config of record come from the
# easycwmp client this package replaces
CWMPCLIENT_EXT:=$(TRUNK_DIR)/apps/hni/cwmpclient/ext/openwrt
PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)

PKG_FIXUP:=autoreconf
PKG_INSTALL:=1

include $(INCLUDE_DIR)/package.mk

define Package/icwmp_tr098
  SECTION:=utils
  CATEGORY:=Utilities
  SUBMENU:=TRx69
  TITLE:=TR069 CWMP client (icwmp + libtr098, replaces cwmpclient)
  # +zlib: bin/Makefile.am links icwmp_tr098d with $(LIBZ_LIBS) for zlib.c (SOAP
  # compression).  The cwmpclient package this replaces never linked it, so the
  # dependency is new -- without it ipkg-build fails with "missing dependencies
  # for the following libraries: libz.so.1".
  # +libubox: the binary NEEDs libubox.so directly.  It happens to be installed
  # anyway as a dependency of libblobmsg-json (same source package), which is
  # why ipkg-build never complained -- libtr098 lists it explicitly and so does
  # this now.  -lcrypto/-lssl are on the link line too but --as-needed drops
  # them: nothing here calls OpenSSL, curl does, so no +libopenssl.
  DEPENDS:=+libubus +libuci +libubox +libcurl +libmicroxml +libjson-c +libblobmsg-json +libpthread +zlib +libtr098
  CONFLICTS:=cwmpclient
endef

define Package/icwmp_tr098/description
 TR069 client implementation with libtr098 for TR098 support.  On this
 product (--with-sdk=mtk) the data model is native C in libtr098
 (sdk/mtk/dm098); the objects not ported yet are still served by the
 product's easycwmp function library, installed here under
 /usr/share/easycwmp/functions at its original path.  /etc/config/easycwmp
 stays the ACS config of record and /etc/init.d/easycwmpd forwards to
 /etc/init.d/icwmpd, so the WebUI, hal_gateway and stuncd keep working.
endef

define Build/Prepare
	@echo "Build/Prepare"
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(PKG_SOURCE)/. $(PKG_BUILD_DIR)
	@echo "Finish Build/Prepare"
endef

TARGET_CFLAGS += -D_GNU_SOURCE

define Build/Configure
	cd $(PKG_BUILD_DIR) && \
	./tools/sdk-scan.sh && \
	autoreconf -i && \
	./configure \
		--host=$(GNU_TARGET_NAME) \
		--enable-icwmp_tr098 \
		--enable-http=curl \
		--enable-debug \
		--with-sdk=mtk
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)
endef

define Package/icwmp_tr098/conffiles
/etc/config/cwmp
/etc/config/easycwmp
/usr/share/easycwmp/defaults
endef

define Package/icwmp_tr098/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/bin/icwmp_tr098d $(1)/usr/sbin/icwmp_tr098d
	# external action backend (downloads, firmware apply, reboot, factory reset)
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sdk/mtk/scripts/icwmp.sh $(1)/usr/sbin/icwmp
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sdk/mtk/files/wan_interface_up $(1)/usr/sbin/wan_interface_up
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sdk/mtk/files/value_monitoring $(1)/usr/sbin/value_monitoring
	$(INSTALL_BIN) $(CWMPCLIENT_EXT)/scripts/easycwmp_firewall.sh $(1)/usr/sbin/easycwmp_firewall.sh
	$(INSTALL_BIN) $(CWMPCLIENT_EXT)/scripts/easycwmp_uci_trigger.sh $(1)/usr/sbin/easycwmp_uci_trigger.sh
	$(INSTALL_BIN) $(CWMPCLIENT_EXT)/scripts/hni_wan_reload.sh $(1)/usr/sbin/hni_wan_reload.sh
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sdk/mtk/files/icwmpd.init $(1)/etc/init.d/icwmpd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sdk/mtk/files/easycwmpd $(1)/etc/init.d/easycwmpd
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/sdk/mtk/files/cwmp $(1)/etc/config/cwmp
	$(INSTALL_CONF) $(CWMPCLIENT_EXT)/config/easycwmp $(1)/etc/config/easycwmp
	$(INSTALL_DIR) $(1)/etc/easycwmp
	$(INSTALL_CONF) $(CWMPCLIENT_EXT)/config/ssl_client* $(1)/etc/easycwmp
	$(INSTALL_DIR) $(1)/etc/icwmpd
	# the TR-098 data model: easycwmp function library, unchanged
	$(INSTALL_DIR) $(1)/usr/share/easycwmp/functions
	$(CP) $(CWMPCLIENT_EXT)/scripts/defaults $(1)/usr/share/easycwmp
	$(CP) $(CWMPCLIENT_EXT)/scripts/functions/common/* $(1)/usr/share/easycwmp/functions/
	$(CP) $(CWMPCLIENT_EXT)/scripts/functions/tr098/* $(1)/usr/share/easycwmp/functions/
	$(CP) $(CWMPCLIENT_EXT)/scripts/functions/tr143/* $(1)/usr/share/easycwmp/functions/
ifeq ($(TCSUPPORT_VOIP),y)
	$(CP) $(CWMPCLIENT_EXT)/scripts/functions/tr104/* $(1)/usr/share/easycwmp/functions/
endif
	chmod +x $(1)/usr/share/easycwmp/functions/*
endef

define Build/Clean
	echo "Build/clean"
	rm -rf $(PKG_BUILD_DIR)
	rm -rf $(KERNEL_BUILD_DIR)/$(PKG_NAME)
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
