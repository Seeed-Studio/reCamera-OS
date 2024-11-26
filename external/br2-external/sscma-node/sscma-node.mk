################################################################################
#
# sscma-node
#
################################################################################

SSCMA_NODE_VERSION = 0a62f1816c7fd2effb217f117915676ff70fb369
SSCMA_NODE_SITE = https://github.com/Seeed-Studio/sscma-example-sg200x
SSCMA_NODE_SITE_METHOD = git
SSCMA_NODE_GIT_SUBMODULES = YES
SSCMA_NODE_LICENSE = Apache-2.0
SSCMA_NODE_DEPENDENCIES = host-nodejs mosquitto

# Configure step: prepare the build environment and run CMake to configure the build
define SSCMA_NODE_CONFIGURE_CMDS
	mkdir -p $(@D)/solutions/sscma-node/build && \
	cd $(@D)/solutions/sscma-node/build && \
	SG200X_SDK_PATH=$(shell realpath $(BUILD_DIR)/../../../../) $(BR2_CMAKE) -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=$(TARGET_DIR) ..
endef

# Build step: compile the package using the Makefile in the build directory
define SSCMA_NODE_BUILD_CMDS
    $(MAKE) -C $(@D)/solutions/sscma-node/build
endef

# Install step: copy the built files to the target directory
define SSCMA_NODE_INSTALL_TARGET_CMDS
	# Install the executable file
	$(INSTALL) -D -m 0755 $(@D)/solutions/sscma-node/build/sscma-node $(TARGET_DIR)/usr/local/bin/sscma-node

	# Copy other files from the source directory to the target directory
	cp -r $(@D)/solutions/sscma-node/files/* $(TARGET_DIR)/

	# Create the necessary directories for node-red
	mkdir -p $(TARGET_DIR)/home/recamera/.node-red/node_modules

	# Use npm to install the node-red-contrib-sscma package
	$(NPM) install --no-audit --no-update-notifier --no-fund --save --save-prefix=~ --production --engine-strict --prefix $(TARGET_DIR)/home/recamera/.node-red node-red-contrib-sscma@0.1.0
endef

$(eval $(generic-package))
