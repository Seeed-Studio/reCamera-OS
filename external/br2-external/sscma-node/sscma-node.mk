################################################################################
#
# sscma-node
#
################################################################################

SSCMA_NODE_VERSION = 0.2.2
SSCMA_NODE_SITE = https://github.com/Seeed-Studio/sscma-example-sg200x
SSCMA_NODE_SITE_METHOD = git
SSCMA_NODE_GIT_SUBMODULES = YES
SSCMA_NODE_LICENSE = Apache-2.0
SSCMA_NODE_DEPENDENCIES = host-nodejs mosquitto libhv alsa-lib

# Configure step: prepare the build environment and run CMake to configure the build
define SSCMA_NODE_CONFIGURE_CMDS
	mkdir -p $(@D)/solutions/sscma-node/build && \
	cd $(@D)/solutions/sscma-node/build && \
	$(BR2_CMAKE) -DSG200X_SDK_PATH=$(shell realpath $(BUILD_DIR)/../../../../) -DSYSROOT=${STAGING_DIR} -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(TARGET_DIR) ..
endef

# Build step: compile the package using the Makefile in the build directory
define SSCMA_NODE_BUILD_CMDS
    $(MAKE) -C $(@D)/solutions/sscma-node/build
endef

# Install step: copy the built files to the target directory
define SSCMA_NODE_INSTALL_TARGET_CMDS

	# Create the necessary directories for node-red
	mkdir -p $(TARGET_DIR)/home/recamera/.node-red/node_modules

	# Install npm packages
	$(NPM) install --no-audit --no-update-notifier --no-fund --save --save-prefix=~ --production --engine-strict --prefix $(TARGET_DIR)/home/recamera/.node-red node-red-contrib-sscma
	$(NPM) install --no-audit --no-update-notifier --no-fund --save --save-prefix=~ --production --engine-strict --prefix $(TARGET_DIR)/home/recamera/.node-red node-red-contrib-os
	$(NPM) install --no-audit --no-update-notifier --no-fund --save --save-prefix=~ --production --engine-strict --prefix $(TARGET_DIR)/home/recamera/.node-red node-red-contrib-seeed-canbus
	$(NPM) install --no-audit --no-update-notifier --no-fund --save --save-prefix=~ --production --engine-strict --prefix $(TARGET_DIR)/home/recamera/.node-red node-red-contrib-seeed-recamera

	$(NPM) install --no-audit --no-update-notifier --no-fund --save --save-prefix=~ --production --engine-strict --prefix $(TARGET_DIR)/home/recamera/.node-red @flowfuse/node-red-dashboard@1.26.0
	$(NPM) install --no-audit --no-update-notifier --no-fund --save --save-prefix=~ --production --engine-strict --prefix $(TARGET_DIR)/home/recamera/.node-red socketcan@4.0.5

	# Install the executable file
	$(INSTALL) -D -m 0755 $(@D)/solutions/sscma-node/build/sscma-node $(TARGET_DIR)/usr/local/bin/sscma-node

	# Copy other files from the source directory to the target directory
	cp -r $(@D)/solutions/sscma-node/rootfs/* $(TARGET_DIR)/


endef

$(eval $(generic-package))
