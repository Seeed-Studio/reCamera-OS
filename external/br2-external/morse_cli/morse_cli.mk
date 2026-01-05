################################################################################
#
# morse_cli
#
################################################################################

MORSE_CLI_VERSION = 1.9.4
MORSE_CLI_SITE = $(BR2_EXTERNAL_BR2EXT_PATH)/morse_cli
MORSE_CLI_SITE_METHOD = file
MORSE_CLI_SITE_SOURCE = morse_cli-$(MORSE_CLI_VERSION).tar.gz

MORSE_CLI_CFLAGS = -I$(STAGING_DIR)/usr/include/libnl3/

MORSE_CLI_DEPENDENCIES += host-pkgconf libnl

define MORSE_CLI_BUILD_CMDS
    $(TARGET_MAKE_ENV) CONFIG_MORSE_TRANS_NL80211=1 \
		CFLAGS="$(MORSE_CLI_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)" \
		$(MAKE) CC="$(TARGET_CC)" -C $(@D)
endef

define MORSE_CLI_INSTALL_TARGET_CMDS
    $(INSTALL) -m 0755 -D $(@D)/morse_cli $(TARGET_DIR)/usr/sbin/morse_cli
    $(INSTALL) -m 0755 -D $(@D)/morsectrl $(TARGET_DIR)/usr/sbin/morsectrl
endef

$(eval $(generic-package))
