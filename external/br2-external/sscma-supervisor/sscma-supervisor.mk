################################################################################
#
# sscma-supervisor
#
################################################################################

SSCMA_SUPERVISOR_SOURCE = supervisor.zip
SSCMA_SUPERVISOR_SITE = https://github.com/Seeed-Studio/sscma-example-sg200x/archive
BR_NO_CHECK_HASH_FOR += $(SSCMA_SUPERVISOR_SOURCE)

define SSCMA_SUPERVISOR_EXTRACT_CMDS
	$(UNZIP) -d $(@D) $(SSCMA_SUPERVISOR_DL_DIR)/$(SSCMA_SUPERVISOR_SOURCE)
    @mv -fv $(@D)/* $(@D)/source
endef

define SSCMA_SUPERVISOR_BUILD_CMDS
	cd $(@D)/source && ./build.sh
endef

define SSCMA_SUPERVISOR_INSTALL_TARGET_CMDS
    @cp -rf $(@D)/source/install/* $(TARGET_DIR)/
endef

$(eval $(generic-package))
