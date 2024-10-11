################################################################################
#
# sscma-app
#
################################################################################

SSCMA_APP_VERSION = 0.0.1
SSCMA_APP_SITE = https://github.com/Seeed-Studio/sscma-example-sg200x.git
SSCMA_APP_SITE_METHOD = git
SSCMA_APP_GIT_SUBMODULES = YES
SSCMA_APP_LICENSE = Apache-2.0
SSCMA_APP_DEPENDENCIES = mosquitto

define SSCMA_APP_BUILD_CMDS
	(cd $(@D); \
		$(TARGET_CONFIGURE_OPTS) \
		SG200X_SDK_PATH=$(BASE_DIR)/../../../ \
		./build.sh \
	)
endef

define SSCMA_APP_INSTALL_TARGET_CMDS
	@cp -rf $(@D)/install/* $(TARGET_DIR)/
endef

$(eval $(generic-package))
