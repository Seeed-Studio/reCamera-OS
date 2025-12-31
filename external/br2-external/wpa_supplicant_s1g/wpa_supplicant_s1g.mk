################################################################################
#
# wpa_supplicant_s1g
#
################################################################################

WPA_SUPPLICANT_S1G_VERSION = 1.9.4
WPA_SUPPLICANT_S1G_SITE = $(BR2_EXTERNAL_BR2EXT_PATH)/wpa_supplicant_s1g
WPA_SUPPLICANT_S1G_SITE_METHOD = file
WPA_SUPPLICANT_S1G_SITE_SOURCE = wpa_supplicant_s1g-$(WPA_SUPPLICANT_S1G_VERSION).tar.gz

WPA_SUPPLICANT_S1G_CONFIG = $(WPA_SUPPLICANT_S1G_DIR)/wpa_supplicant/.config
WPA_SUPPLICANT_S1G_SUBDIR = wpa_supplicant
WPA_SUPPLICANT_S1G_CFLAGS = -Os -I$(STAGING_DIR)/usr/include/libnl3/ -I$(STAGING_DIR)/usr/include/
WPA_SUPPLICANT_S1G_LDFLAGS = $(TARGET_LDFLAGS) -lpthread -lm

WPA_SUPPLICANT_S1G_LIBS += -lnl-3 -lm -lpthread

WPA_SUPPLICANT_S1G_CONFIG_ENABLE = CONFIG_EAP_FAST

WPA_SUPPLICANT_S1G_MAKE_ENV = \
	PKG_CONFIG_SYSROOT_DIR="$(STAGING_DIR)" \
	PKG_CONFIG_PATH="$(STAGING_DIR)/usr/lib/pkgconfig"

WPA_SUPPLICANT_S1G_DEPENDENCIES += host-pkgconf libnl libopenssl

# Configure step: prepare the build environment and run CMake to configure the build
define WPA_SUPPLICANT_S1G_CONFIGURE_CMDS
    cp $(@D)/wpa_supplicant/defconfig $(WPA_SUPPLICANT_S1G_CONFIG)

	sed -i \
		$(patsubst %,-e 's/^#\(%\)/\1/',$(WPA_SUPPLICANT_S1G_CONFIG_ENABLE)) \
		$(WPA_SUPPLICANT_S1G_CONFIG)
endef

# Build step: compile the package using the Makefile in the build directory
define WPA_SUPPLICANT_S1G_BUILD_CMDS
    $(WPA_SUPPLICANT_S1G_MAKE_ENV) $(TARGET_MAKE_ENV) \
		CFLAGS="$(WPA_SUPPLICANT_S1G_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)" \
        LIBS="$(WPA_SUPPLICANT_S1G_LIBS)" LIBS_c="$(WPA_SUPPLICANT_S1G_LIBS)" \
        LIBS_p="$(WPA_SUPPLICANT_S1G_LIBS)" \
        $(MAKE) CC="$(TARGET_CC)" -C $(@D)/$(WPA_SUPPLICANT_S1G_SUBDIR)
endef

# Install step: copy the built files to the target directory
define WPA_SUPPLICANT_S1G_INSTALL_TARGET_CMDS
    $(INSTALL) -m 0755 -D $(@D)/$(WPA_SUPPLICANT_SUBDIR)/wpa_supplicant_s1g \
        $(TARGET_DIR)/usr/sbin/wpa_supplicant_s1g
	$(INSTALL) -m 0755 -D $(@D)/$(WPA_SUPPLICANT_SUBDIR)/wpa_cli_s1g \
        $(TARGET_DIR)/usr/sbin/wpa_cli_s1g
	$(INSTALL) -m 644 -D $(BR2_EXTERNAL_BR2EXT_PATH)/wpa_supplicant_s1g/wpa_supplicant_s1g.conf \
		$(TARGET_DIR)/etc/wpa_supplicant_s1g.conf
endef

$(eval $(generic-package))
