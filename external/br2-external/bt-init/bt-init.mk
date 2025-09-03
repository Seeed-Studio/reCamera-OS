################################################################################
#
# bt-init
#
################################################################################

BT_INIT_VERSION = 0.2.1
BT_INIT_SITE = https://github.com/Seeed-Studio/sscma-example-sg200x
BT_INIT_SITE_METHOD = git
BT_INIT_GIT_SUBMODULES = YES
BT_INIT_LICENSE = Apache-2.0
BT_INIT_DEPENDENCIES = bluez5_utils

# Configure step: prepare the build environment and run CMake to configure the build
define BT_INIT_CONFIGURE_CMDS
	mkdir -p $(@D)/solutions/brcm-patchram-plus/build && \
	cd $(@D)/solutions/brcm-patchram-plus/build && \
	$(BR2_CMAKE) -DSG200X_SDK_PATH=$(shell realpath $(BUILD_DIR)/../../../../) -DSYSROOT=${STAGING_DIR} -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(TARGET_DIR) ..
endef

# Build step: compile the package using the Makefile in the build directory
define BT_INIT_BUILD_CMDS
    $(MAKE) -C $(@D)/solutions/brcm-patchram-plus/build
endef

# Install step: copy the built files to the target directory
define BT_INIT_INSTALL_TARGET_CMDS
	# Install the executable file
	$(INSTALL) -D -m 0755 $(@D)/solutions/brcm-patchram-plus/build/brcm_patchram_plus $(TARGET_DIR)/usr/local/bin/brcm_patchram_plus

	# Copy other files from the source directory to the target directory
	cp -r $(@D)/solutions/brcm-patchram-plus/rootfs/* $(TARGET_DIR)/

endef

$(eval $(generic-package))
