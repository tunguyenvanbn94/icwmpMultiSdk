# Makefile to build libmicroxml (microxml fork of Mini-XML 2.x used by icwmp).
# Source lives in-tree under ./microxml (no tarball) so it can be patched/developed
# directly.  The autoconf "configure" script is generated at build time.

APP = microxml

# microxml's configure.in embeds "-rpath,$(libdir)" into DSOFLAGS/LDFLAGS.  That
# would bake a host build path into the target .so, so strip it after configure.
check_config:
	@if [ ! -e $(APP)/Makefile ]; then \
	   cd "$(APP)" && { [ -f configure ] || autoconf -f; } && ./configure \
		--host="$(TOOLCHAIN_PREFIX)" \
		--prefix="$(BCM_FSBUILD_DIR)/public" \
		--enable-shared --disable-static \
		CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" && \
	   sed -i -e 's/,-rpath,\$$(libdir)//g' -e 's/-Wl,-rpath,\$$(libdir)//g' Makefile; \
	   echo "$(APP) is configured"; \
	   cd ..; \
	fi;

install: check_config
	$(MAKE) -C $(APP)
	$(MAKE) -C $(APP) install

clean:
	-[ ! -e $(APP)/Makefile ] || $(MAKE) -C $(APP) uninstall
	-[ ! -e $(APP)/Makefile ] || $(MAKE) -C $(APP) distclean
	rm -f $(APP)/configure $(APP)/config.log $(APP)/config.status $(APP)/config.h $(APP)/microxml.pc $(APP)/microxml.list
	rm -rf $(APP)/autom4te.cache
