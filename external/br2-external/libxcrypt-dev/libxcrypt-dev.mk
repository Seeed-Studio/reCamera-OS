################################################################################
#
# libxcrypt
#
################################################################################

LIBXCRYPT_DEV_VERSION = 4.4.36
LIBXCRYPT_DEV_SITE = $(call github,besser82,libxcrypt,v$(LIBXCRYPT_DEV_VERSION))
LIBXCRYPT_DEV_LICENSE = LGPL-2.1+
LIBXCRYPT_DEV_LICENSE_FILES = LICENSING COPYING.LIB
LIBXCRYPT_DEV_INSTALL_STAGING = YES
LIBXCRYPT_DEV_AUTORECONF = YES

# Some warnings turn into errors with some sensitive compilers
LIBXCRYPT_DEV_CONF_OPTS = --disable-werror

# Disable obsolete and unsecure API
LIBXCRYPT_DEV_CONF_OPTS += --disable-obsolete_api

$(eval $(autotools-package))
