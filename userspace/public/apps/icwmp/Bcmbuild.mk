EXE := icwmpd

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
EXE_INSTALL_DIR       := $(BCM_FSBUILD_DIR)/public/bin

# Same include/lib surface as tr69c: CMS public+private headers, cms_core,
# generic HAL, plus the open source libs (uci, ubus, microxml, curl, json-c).
ALLOWED_INCLUDE_PATHS := -I. \
                         -I$(BCM_FSBUILD_DIR)/public/include \
                         -I$(BCM_FSBUILD_DIR)/public/include/libubox \
                         -I$(BCM_FSBUILD_DIR)/public/include/json-c \
                         -I$(BCM_FSBUILD_DIR)/private/include \
                         -I$(BCM_FSBUILD_DIR)/private/include/cms_core

ALLOWED_LIB_DIRS      := /lib:/private/lib:/public/lib

export ARCH CFLAGS BCM_LD_FLAGS BCM_RPATH_LINK_OPTION BCM_LIB_PATH
export EXE_INSTALL_DIR CMS_COMMON_LIBS CMS_COMPILE_FLAGS ALLOWED_INCLUDE_PATHS EXE
export TOOLCHAIN_PREFIX BUILD_LIBCURL_WITH_SSL
export PKG_CONFIG=$(TOOLCHAIN_TOP)/$(TOOLCHAIN_USR_DIR)/bin/pkg-config
export PKG_CONFIG_LIBDIR=$(BCM_FSBUILD_DIR)/public/lib
export PKG_CONFIG_PATH=$(BCM_FSBUILD_DIR)/public/lib/pkgconfig

# Remove all mdm_cbk_* and bcm_*_hal libs from CMS_CORE_LIBS; the local
# Makefile adds -lmdm_cbk_tr69 -lbcm_generic_hal (what tr69c links).
MDM_CORE_LIBS := $(patsubst -lmdm_cbk_%,,$(CMS_CORE_LIBS))
MDM_CORE_LIBS := $(patsubst -lbcm_%_hal,,$(MDM_CORE_LIBS))
export MDM_CORE_LIBS

# Final location of EXE for system image.  Only the BRCM build system needs to
# know about this.
FINAL_EXE_INSTALL_DIR := $(INSTALL_DIR)/bin
FINAL_ETC_INSTALL_DIR := $(INSTALL_DIR)/etc/icwmp

ifneq ($(strip $(BUILD_ICWMP)),)
conditional_build:
	$(MAKE) -f Makefile install
	mkdir -p $(FINAL_EXE_INSTALL_DIR) $(FINAL_ETC_INSTALL_DIR)
	cp -p $(EXE_INSTALL_DIR)/$(EXE) $(FINAL_EXE_INSTALL_DIR)
	# read-only seed of the UCI config, copied to /data/icwmp/config/cwmp at first start
	install -m 0644 files/cwmp $(FINAL_ETC_INSTALL_DIR)/cwmp
else
conditional_build:
	@echo "skipping $(EXE) (not configured)"
endif

clean:
	$(MAKE) -f Makefile clean
	rm -f $(FINAL_EXE_INSTALL_DIR)/$(EXE)
	rm -rf $(FINAL_ETC_INSTALL_DIR)

# The next line is a hint to our release scripts
# GLOBAL_RELEASE_SCRIPT_CALL_DISTCLEAN
distclean: clean

shell:
	@echo "Entering makefile debug shell (type exit to exit) >>>"
	@bash -i
	@echo "exiting debug shell."
