# Build icwmpd (TR-069 client, TR-098 data model via libtr098, Broadcom BDK
# glue) with autotools, in-tree, the same way obuspa is built.  Invoked from
# Bcmbuild.mk.

SRC_DIR := icwmp

# SDK glue selection (icwmp/sdk/<name>).  ICWMP_PLATFORM is the old name.
ICWMP_SDK ?= $(if $(ICWMP_PLATFORM),$(ICWMP_PLATFORM),bdk)

# --enable-debug defines WITH_CWMP_DEBUG: without it inc/log.h compiles every
# CWMP_LOG() away and /var/log/icwmpd.log is never written (board 19/09: 7
# failed sessions and no log).  Output is still gated at runtime by
# cwmp.cpe.log_severity / log_to_file / log_to_console.

CPPFLAGS += $(ALLOWED_INCLUDE_PATHS) $(CMS_COMPILE_FLAGS)
LDFLAGS  += $(BCM_RPATH_LINK_OPTION) $(BCM_LIB_PATH)

# what tr69c links on BDK (Makefile.fullsrc): cms_core stack without the
# per-component callback libs, plus the tr69 callback lib and the generic HAL
BDK_LIBS := -ltr098 $(MDM_CORE_LIBS) -lmdm_cbk_tr69 -lbcm_generic_hal -lcms_msg $(CMS_COMMON_LIBS)

check_config:
	@if [ ! -e $(SRC_DIR)/Makefile ]; then \
	   export PKG_CONFIG_LIBDIR=$${PKG_CONFIG_LIBDIR:-$(BCM_FSBUILD_DIR)/public/lib}; \
	   export PKG_CONFIG_PATH=$${PKG_CONFIG_PATH:-$(BCM_FSBUILD_DIR)/public/lib/pkgconfig}; \
	   cd $(SRC_DIR) && \
	   ./tools/sdk-scan.sh && \
	   autoreconf --force --install && \
	   ./configure --host=$(TOOLCHAIN_PREFIX) \
	      --prefix=$(BCM_FSBUILD_DIR)/public \
	      --enable-icwmp_tr098 --with-sdk=$(ICWMP_SDK) --enable-http=curl --enable-debug \
	      CPPFLAGS="$(CPPFLAGS)" LDFLAGS="$(LDFLAGS)" BDK_LIBS="$(BDK_LIBS)"; \
	fi;

install: check_config
	$(MAKE) -C $(SRC_DIR)
	# bin/Makefile.am names the TR-098 build icwmp_tr098d; install it as icwmpd,
	# the name tr69_md launches (comp_tr69_md.c, SUPPORT_ICWMP)
	mkdir -p $(EXE_INSTALL_DIR)
	install -m 0755 $(SRC_DIR)/bin/icwmp_tr098d $(EXE_INSTALL_DIR)/$(EXE)

clean:
	@if [ -e $(SRC_DIR)/Makefile ]; then \
	    $(MAKE) -C $(SRC_DIR) distclean; \
	fi;
	rm -rf $(SRC_DIR)/m4 $(SRC_DIR)/autom4te.cache
	rm -f $(SRC_DIR)/aclocal.m4 $(SRC_DIR)/compile $(SRC_DIR)/configure $(SRC_DIR)/config.guess \
	      $(SRC_DIR)/config.sub $(SRC_DIR)/depcomp $(SRC_DIR)/install-sh $(SRC_DIR)/ltmain.sh \
	      $(SRC_DIR)/missing $(SRC_DIR)/Makefile.in $(SRC_DIR)/bin/Makefile.in
	rm -f $(EXE_INSTALL_DIR)/$(EXE)
