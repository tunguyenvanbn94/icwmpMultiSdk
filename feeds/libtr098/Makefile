include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/target.mk

# TRUNK_DIR lives in include/ecnt-trunkdir.mk, written by
# airoha_script/airoha-compile.sh.  Nothing in the OpenWrt tree includes that
# file, so in a shell that has not exported TRUNK_DIR it is empty -- pull it in
# here.  "-include": a tree without the file still parses.
-include $(TOPDIR)/include/ecnt-trunkdir.mk

PKG_NAME:=libtr098
PKG_RELEASE:=3

PKG_FIXUP:=autoreconf
PKG_INSTALL:=1

# Source name is model-neutral, package/SONAME remain libtr098 during A1.
PKG_SOURCE := $(TRUNK_DIR)/apps/hni/libicwmp_dm
PKG_BUILD_DIR := $(KERNEL_BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/libtr098
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=icwmp Data Model Library (transitional libtr098 ABI)
  DEPENDS:=+libubus +libuci +libjson-c +libubox +libblobmsg-json +libpthread
endef

define Package/libtr098/description
 Shared data model engine of icwmp_tr098d. Source: libicwmp_dm.
 With --with-sdk=mtk the whole TR-098 model is native C in sdk/mtk/dm098/.
 The easycwmp shell bridge (sdk/mtk/compat, /usr/share/icwmp/icwmp_dm.sh)
 is left out: --disable-dm-script-compat.  Drop that flag and put the
 install lines of icwmp_dm.sh back to build the bridge again.
endef

define Build/Prepare
	@echo "Build/Prepare"
	@test -n "$(strip $(PKG_SOURCE))" -a -d "$(PKG_SOURCE)" || { \
		echo "ERROR: PKG_SOURCE is empty or not a directory: '$(PKG_SOURCE)'"; \
		echo "Without this guard the next line copies all of / into $(PKG_BUILD_DIR)."; \
		exit 1; }
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(PKG_SOURCE)/. $(PKG_BUILD_DIR)
	# X_AIS_CPEagent.SecretKey reads back encrypted with the operator's key:
	# taken from the product's shell library, never kept in this source
	$(PKG_BUILD_DIR)/tools/mtk-cpeagent-key.sh "$(TRUNK_DIR)" \
		$(PKG_BUILD_DIR)/sdk/mtk/dm098/cpeagent_key_mtk.h
endef

TARGET_CFLAGS += -D_GNU_SOURCE

define Build/Configure
	cd $(PKG_BUILD_DIR) && \
	./tools/sdk-scan.sh && \
	autoreconf -i && \
	./configure \
		--host=$(GNU_TARGET_NAME) \
		--prefix=/usr \
		--with-sdk=mtk \
		--disable-dm-script-compat
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)
endef

define Build/InstallDev
	$(PKG_BUILD_DIR)/tools/install-headers.sh "$(STAGING_DIR)/usr/include" mtk

	$(INSTALL_DIR) $(STAGING_DIR)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/bin/.libs/libtr098.so* $(STAGING_DIR)/usr/lib/
endef

define Package/libtr098/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/bin/.libs/libtr098.so* $(1)/usr/lib/
	# no /usr/share/icwmp/icwmp_dm.sh: built --disable-dm-script-compat.
	# With the bridge compiled in it needs, here:
	#   $(INSTALL_DIR) $(1)/usr/share/icwmp
	#   $(INSTALL_BIN) $(PKG_BUILD_DIR)/sdk/mtk/compat/icwmp_dm.sh $(1)/usr/share/icwmp/icwmp_dm.sh
endef

define Build/Clean
	echo "Build/clean"
	rm -rf $(PKG_BUILD_DIR)
	rm -rf $(KERNEL_BUILD_DIR)/$(PKG_NAME)
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
