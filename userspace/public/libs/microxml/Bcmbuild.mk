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

APP := microxml
LIBNAME := libmicroxml

# Final location of LIB for system image.  Only the BRCM build system needs to
# know about this.
FINAL_LIB_INSTALL_DIR := $(INSTALL_DIR)/lib$(BCM_INSTALL_SUFFIX_DIR)

.PHONY: conditional_build

ifneq ($(strip $(BUILD_LIBMICROXML)),)
conditional_build:
	$(MAKE) -f Makefile install
	mkdir -p $(FINAL_LIB_INSTALL_DIR)
	cp -a $(BCM_FSBUILD_DIR)/public/lib/$(LIBNAME).so* $(FINAL_LIB_INSTALL_DIR)
	echo "Done building $(APP)"
else
conditional_build:
	@echo "skipping $(APP) (not configured)"
endif

clean:
	$(MAKE) -f Makefile clean
	rm -f $(FINAL_LIB_INSTALL_DIR)/$(LIBNAME).so*

# The next line is a hint to our release scripts
# GLOBAL_RELEASE_SCRIPT_CALL_DISTCLEAN
distclean: clean

bcm_dorel_distclean: distclean
