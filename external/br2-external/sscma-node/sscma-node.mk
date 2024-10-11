################################################################################
#
# sscma-node
#
################################################################################


SSCMA_NODE_VERSION = 4c6b2268e0cefb2e7aaea8da12d856a47e67c78e
SSCMA_NODE_SITE = https://github.com/Seeed-Studio/sscma-example-sg200x
SSCMA_NODE_SITE_METHOD = git
SSCMA_NODE_GIT_SUBMODULES = YES
SSCMA_NODE_LICENSE = Apache-2.0
SSCMA_NODE_DEPENDENCIES = host-nodejs mosquitto

# Configure
define SSCMA_NODE_CONFIGURE_CMDS
	mkdir -p $(@D)/solutions/sscma-node/build && \
	cd $(@D)/solutions/sscma-node/build && \
	SG200X_SDK_PATH=`realpath $(BUILD_DIR)/../../../../` $(BR2_CMAKE) -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=$(TARGET_DIR) ..
endef

# Build
define SSCMA_NODE_BUILD_CMDS
    $(MAKE) -C $(@D)/solutions/sscma-node/build
endef

# Install
define SSCMA_NODE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/solutions/sscma-node/build/sscma-node $(TARGET_DIR)/usr/local/bin/sscma-node
	mkdir -p $(TARGET_DIR)/home/recamera/.node-red/node_modules
	$(NPM) install --no-audit --no-update-notifier --no-fund --save --save-prefix=~ --production --engine-strict --prefix $(TARGET_DIR)/home/recamera/.node-red node-red-contrib-sscma@0.0.5
endef

define SSCMA_NODE_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 package/sscma-node/S91sscma-node \
		$(TARGET_DIR)/etc/init.d/S91sscma-node
endef

$(eval $(generic-package))
