LIB := libtr098.so

all install: conditional_build

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

ARCH                  := $(PROFILE_ARCH)
LIB_INSTALL_DIR       := $(BCM_FSBUILD_DIR)/public/lib
HEADER_INSTALL_DIR    := $(BCM_FSBUILD_DIR)/public/include/libtr098

# The BDK platform layer (libtr098/platform/bdk) talks to the Distributed MDM
# through libbcm_generic_hal, so it needs the same include/lib paths as tr69c.
ALLOWED_INCLUDE_PATHS := -I. \
                         -I$(BCM_FSBUILD_DIR)/public/include \
                         -I$(BCM_FSBUILD_DIR)/public/include/libubox \
                         -I$(BCM_FSBUILD_DIR)/public/include/json-c \
                         -I$(BCM_FSBUILD_DIR)/private/include \
                         -I$(BCM_FSBUILD_DIR)/private/include/cms_core

ALLOWED_LIB_DIRS      := /lib:/private/lib:/public/lib

export ARCH CFLAGS BCM_LD_FLAGS BCM_RPATH_LINK_OPTION BCM_LIB_PATH
export LIB_INSTALL_DIR HEADER_INSTALL_DIR CMS_COMMON_LIBS CMS_COMPILE_FLAGS
export ALLOWED_INCLUDE_PATHS
export TOOLCHAIN_PREFIX
export PKG_CONFIG=$(TOOLCHAIN_TOP)/$(TOOLCHAIN_USR_DIR)/bin/pkg-config
export PKG_CONFIG_LIBDIR=$(BCM_FSBUILD_DIR)/public/lib
export PKG_CONFIG_PATH=$(BCM_FSBUILD_DIR)/public/lib/pkgconfig

# Remove all mdm_cbk_* and bcm_*_hal libs from CMS_CORE_LIBS; the local Makefile
# adds exactly what the BDK platform layer needs (same trick as tr69c/obuspa).
MDM_CORE_LIBS := $(patsubst -lmdm_cbk_%,,$(CMS_CORE_LIBS))
MDM_CORE_LIBS := $(patsubst -lbcm_%_hal,,$(MDM_CORE_LIBS))
export MDM_CORE_LIBS

# Final location of LIB for system image.  Only the BRCM build system needs to
# know about this.
FINAL_LIB_INSTALL_DIR := $(INSTALL_DIR)/lib$(BCM_INSTALL_SUFFIX_DIR)

ifneq ($(strip $(BUILD_LIBTR098)),)
conditional_build:
	$(MAKE) -f Makefile install
	mkdir -p $(FINAL_LIB_INSTALL_DIR)
	cp -d $(LIB_INSTALL_DIR)/libtr098.so* $(FINAL_LIB_INSTALL_DIR)
else
conditional_build:
	@echo "skipping $(LIB) (not configured)"
endif

clean:
	$(MAKE) -f Makefile clean
	rm -f $(FINAL_LIB_INSTALL_DIR)/libtr098.so*

# The next line is a hint to our release scripts
# GLOBAL_RELEASE_SCRIPT_CALL_DISTCLEAN
distclean: clean

shell:
	@echo "Entering makefile debug shell (type exit to exit) >>>"
	@bash -i
	@echo "exiting debug shell."
