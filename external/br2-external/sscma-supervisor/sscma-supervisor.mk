################################################################################
#
# sscma-supervisor
#
################################################################################

SSCMA_SUPERVISOR_VERSION = 0a62f1816c7fd2effb217f117915676ff70fb369
SSCMA_SUPERVISOR_SITE = https://github.com/Seeed-Studio/sscma-example-sg200x
SSCMA_SUPERVISOR_SITE_METHOD = git
SSCMA_SUPERVISOR_GIT_SUBMODULES = YES
SSCMA_SUPERVISOR_LICENSE = Apache-2.0

# Configure step: prepare the build environment and run CMake to configure the build
define SSCMA_SUPERVISOR_CONFIGURE_CMDS
	mkdir -p $(@D)/solutions/supervisor/build && \
	cd $(@D)/solutions/supervisor/build && \
	SG200X_SDK_PATH=$(shell realpath $(BUILD_DIR)/../../../../) $(BR2_CMAKE) -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=$(TARGET_DIR) ..
endef

# Build step: compile the package using the Makefile in the build directory
define SSCMA_SUPERVISOR_BUILD_CMDS
    $(MAKE) -C $(@D)/solutions/supervisor/build
endef

# Install step: copy the built files to the target directory
define SSCMA_SUPERVISOR_INSTALL_TARGET_CMDS
	# Install the executable file
	$(INSTALL) -D -m 0755 $(@D)/solutions/supervisor/build/supervisor $(TARGET_DIR)/usr/local/bin/supervisor

	# Copy other files from the source directory to the target directory
	cp -r $(@D)/solutions/supervisor/files/* $(TARGET_DIR)/
endef

$(eval $(generic-package))
