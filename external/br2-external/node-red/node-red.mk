################################################################################
#
# node-red
#
################################################################################

NODE_RED_VERSION = 4.0.0
NODE_RED_SITE = https://github.com/node-red/node-red.git
NODE_RED_SITE_METHOD = git
NODE_RED_LICENSE = Apache-2.0
NODE_RED_DEPENDENCIES = host-nodejs

define NODE_RED_INSTALL_TARGET_CMDS
	$(NPM) install --production -g $(@D)
endef

define NODE_RED_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 package/node-red/S03node-red \
		$(TARGET_DIR)/etc/init.d/S03node-red
endef

define NODE_RED_USERS
	node-red -1 nobody -1 /var/lib/node-red - - - Node-RED user
endef

$(eval $(generic-package))
