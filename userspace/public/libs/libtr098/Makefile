# Build libtr098 (TR-098 data model engine + BDK platform layer) with autotools,
# in-tree, the same way obuspa is built.  Invoked from Bcmbuild.mk.

SRC_DIR := libtr098

# SDK selection (libtr098/sdk/<name>): bdk = Broadcom Distributed MDM via
# libbcm_generic_hal, uci = stock OpenWrt/UCI, mtk = Airoha OpenWrt.
# TR098_PLATFORM is the old name of the variable, still accepted.
TR098_SDK ?= $(if $(TR098_PLATFORM),$(TR098_PLATFORM),bdk)

CPPFLAGS += $(ALLOWED_INCLUDE_PATHS) $(CMS_COMPILE_FLAGS)
LDFLAGS  += $(BCM_RPATH_LINK_OPTION) $(BCM_LIB_PATH)

ifeq ($(strip $(TR098_SDK)),bdk)
PLATFORM_LIBS := $(MDM_CORE_LIBS) -lmdm_cbk_tr69 -lbcm_generic_hal -lcms_msg $(CMS_COMMON_LIBS)
else
PLATFORM_LIBS :=
endif

check_config:
	@if [ ! -e $(SRC_DIR)/Makefile ]; then \
	   export PKG_CONFIG_LIBDIR=$${PKG_CONFIG_LIBDIR:-$(BCM_FSBUILD_DIR)/public/lib}; \
	   export PKG_CONFIG_PATH=$${PKG_CONFIG_PATH:-$(BCM_FSBUILD_DIR)/public/lib/pkgconfig}; \
	   cd $(SRC_DIR) && \
	   ./tools/sdk-scan.sh && \
	   autoreconf --force --install && \
	   ./configure --host=$(TOOLCHAIN_PREFIX) \
	      --prefix=$(BCM_FSBUILD_DIR)/public \
	      --with-sdk=$(TR098_SDK) \
	      CPPFLAGS="$(CPPFLAGS)" LDFLAGS="$(LDFLAGS)" LIBS="$(PLATFORM_LIBS)"; \
	fi;

install: check_config
	$(MAKE) -C $(SRC_DIR)
	$(MAKE) -C $(SRC_DIR) install
	# headers are consumed by icwmp as <libtr098/xxx.h>
	mkdir -p $(HEADER_INSTALL_DIR)
	cp -f $(SRC_DIR)/*.h $(SRC_DIR)/tr098/*.h $(SRC_DIR)/tr098/common/*.h $(HEADER_INSTALL_DIR)/
	mkdir -p $(HEADER_INSTALL_DIR)/sdk
	cp -f $(SRC_DIR)/sdk/sdk.h $(HEADER_INSTALL_DIR)/sdk/
	# headers of the selected SDK only; a pruned tree has no other directory
	-cp -f $(SRC_DIR)/sdk/$(TR098_SDK)/*.h $(SRC_DIR)/sdk/$(TR098_SDK)/dm098/*.h $(HEADER_INSTALL_DIR)/ 2>/dev/null

clean:
	@if [ -e $(SRC_DIR)/Makefile ]; then \
	    $(MAKE) -C $(SRC_DIR) uninstall; \
	    $(MAKE) -C $(SRC_DIR) distclean; \
	fi;
	rm -rf $(SRC_DIR)/m4 $(SRC_DIR)/autom4te.cache
	rm -f $(SRC_DIR)/aclocal.m4 $(SRC_DIR)/compile $(SRC_DIR)/configure $(SRC_DIR)/config.guess \
	      $(SRC_DIR)/config.sub $(SRC_DIR)/depcomp $(SRC_DIR)/install-sh $(SRC_DIR)/ltmain.sh \
	      $(SRC_DIR)/missing $(SRC_DIR)/Makefile.in $(SRC_DIR)/bin/Makefile.in
	rm -rf $(HEADER_INSTALL_DIR)
