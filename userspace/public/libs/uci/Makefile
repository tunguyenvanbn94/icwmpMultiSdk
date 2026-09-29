LIB = uci
CMAKE_BUILD_DIR = uci-build

all dynamic install: conditional_build

#
# Set our CommEngine directory (by splitting the pwd into two words
# at /userspace and taking the first word only).
# Then include the common defines under CommEngine.
# You do not need to modify this part.
#
CURR_DIR := $(shell pwd)
BUILD_DIR:=$(subst /userspace, /userspace,$(CURR_DIR))
BUILD_DIR:=$(word 1, $(BUILD_DIR))
include $(BUILD_DIR)/make.common

BINDIR=$(INSTALL_DIR)/bin

clean:
	-[ ! -e $(CMAKE_BUILD_DIR) ] || $(MAKE) -C $(CMAKE_BUILD_DIR) clean;
	rm -rf $(BINDIR)/uci
	rm -rf $(INSTALL_DIR)/lib/libuci*
	rm -rf $(BCM_FSBUILD_DIR)/public/lib/libuci*
	rm -rf $(BCM_FSBUILD_DIR)/public/bin/uci
	rm -rf $(BCM_FSBUILD_DIR)/public/include/uci.h $(BCM_FSBUILD_DIR)/public/include/uci_config.h \
	       $(BCM_FSBUILD_DIR)/public/include/uci_blob.h $(BCM_FSBUILD_DIR)/public/include/ucimap.h
	rm -rf $(CURR_DIR)/$(CMAKE_BUILD_DIR)
	rm -f $(LIB)/uci_config.h


# The next line is a hint to to our release scripts
# GLOBAL_RELEASE_SCRIPT_CALL_DISTCLEAN
distclean: clean

CFLAGS=-I$(BCM_FSBUILD_DIR)/public/include -I$(BCM_FSBUILD_DIR)/public/include/libubox
ifeq ($(strip $(DESKTOP_LINUX)),y)
BCM_BLT32_FLAGS := $(BCM_LD_FLAGS)
CFLAGS+=$(BCM_BLT32_FLAGS)
endif

ifeq ($(strip $(BUILD_LIBUBOX)),y)
LDFLAGS+=-L$(INSTALL_DIR)/lib -lubox
endif

.PHONY: build check_cmake sanity_check conditional_build

ifneq ($(strip $(BUILD_LIBUCI)),)
conditional_build: build
else
conditional_build:
	@echo "skipping $(LIB) (not configured)"
endif

# Source is kept in-tree under ./uci (pinned upstream commit + BDK-CHANGES.md),
# built out-of-tree in $(CMAKE_BUILD_DIR) following the ubus/libubox packages.
check_cmake: sanity_check
	mkdir -p $(CURR_DIR)/$(CMAKE_BUILD_DIR)
	export PKG_CONFIG_LIBDIR=$(BCM_FSBUILD_DIR)/public/lib
	export PKG_CONFIG_PATH=$(BCM_FSBUILD_DIR)/public/lib/pkgconfig
	cd $(CURR_DIR)/$(CMAKE_BUILD_DIR); \
	CFLAGS="$(CFLAGS)" \
	cmake -DBUILD_LUA=off -DBUILD_STATIC=off \
	-DCMAKE_RANLIB=$(RANLIB) \
	-DCMAKE_STRIP=$(STRIP) \
	-DCMAKE_NM:FILEPATH=$(NM) \
	-DCMAKE_OBJCOPY:FILEPATH=$(OBJCOPY) \
	-DCMAKE_OBJDUMP=$(OBJDUMP) \
	-DCMAKE_LINKER:FILEPATH="$(LD)" \
	-DCMAKE_AR:FILEPATH=$(AR) \
	-DCMAKE_FIND_ROOT_PATH:PATH="$(BCM_FSBUILD_DIR);$(BCM_FSBUILD_DIR)/public" \
	-DCMAKE_PREFIX_PATH:PATH="$(BCM_FSBUILD_DIR)/public;$(BCM_FSBUILD_DIR)/public/include;$(BCM_FSBUILD_DIR)/public/lib" \
	-DCMAKE_LIBRARY_PATH:PATH="$(BCM_FSBUILD_DIR)/public/lib" \
	-DCMAKE_INSTALL_PREFIX:PATH=$(BCM_FSBUILD_DIR)/public \
	$(CURR_DIR)/$(LIB)

build: check_cmake
	@echo "Building $(LIB)"
	cd $(CURR_DIR)/$(CMAKE_BUILD_DIR); $(MAKE)
	cd $(CURR_DIR)/$(CMAKE_BUILD_DIR); $(MAKE) install
	# libuci.so + uci CLI (CLI is handy for debugging /data/icwmp/config on the board)
	mkdir -p $(BINDIR)
	chmod 755 $(BCM_FSBUILD_DIR)/public/lib/libuci.so
	cp -d $(BCM_FSBUILD_DIR)/public/lib/libuci.so* $(INSTALL_DIR)/lib/
	cp -d $(BCM_FSBUILD_DIR)/public/bin/uci $(BINDIR)/
