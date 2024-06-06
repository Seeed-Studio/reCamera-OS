################################################################################
#
# reCamera
#
################################################################################

RECAMERA_SOURCE = reCamera.zip
RECAMERA_SITE = https://github.com/Seeed-Studio/sscma-example-sg200x/archive
BR_NO_CHECK_HASH_FOR += $(RECAMERA_SOURCE)

define RECAMERA_EXTRACT_CMDS
	$(UNZIP) -d $(@D) $(RECAMERA_DL_DIR)/$(RECAMERA_SOURCE)
    @mv -fv $(@D)/* $(@D)/source
endef

define RECAMERA_BUILD_CMDS
    $(TARGET_MAKE_ENV) $(BR2_MAKE) -j1 recamera -C $(@D)/source
    $(TARGET_MAKE_ENV) $(BR2_MAKE) -j1 recamera install -C $(@D)/source
endef

define RECAMERA_INSTALL_TARGET_CMDS
    @cp -rf $(@D)/source/out/install/* $(TARGET_DIR)/
endef

$(eval $(generic-package))