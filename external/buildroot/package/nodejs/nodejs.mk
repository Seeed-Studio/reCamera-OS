################################################################################
#
# nodejs
#
################################################################################

NODEJS_VERSION = 26.13.0
NODEJS_SOURCE = node-v$(NODEJS_VERSION).tar.xz
NODEJS_SITE = http://nodejs.org/dist/v$(NODEJS_VERSION)
# NODEJS_DL_SUBDIR = nodejs

NODEJS_LICENSE = MIT (core code); MIT, Apache and BSD family licenses (Bundled components)
NODEJS_LICENSE_FILES = LICENSE
NODEJS_CPE_ID_VENDOR = nodejs
NODEJS_CPE_ID_PRODUCT = node.js

# HOST_NODEJS_PROVIDES = host-nodejs

# ---------------------------------------------------------------------------
# Node >= 24 requires GCC >= 12.2 (C++20) while the BSP Xuantie toolchain is
# GCC 10.2. The target nodejs is therefore compiled with the riscv-collab
# riscv64-linux-musl toolchain instead, and bundles its own copies of
# openssl/libuv/c-ares/nghttp2/zlib (the 2021.05 stack is far too old and
# node's configure version gates would reject it). The toolchain C++ runtime
# libs (libstdc++.so.6/libgcc_s.so.1/libatomic.so.1) must be shipped to the
# target next to the node binary.
NODEJS_TC_ROOT ?= /opt/toolchains/riscv64-musl
NODEJS_TC_PREFIX = riscv64-unknown-linux-musl
NODEJS_TC_LIBDIR = $(NODEJS_TC_ROOT)/$(NODEJS_TC_PREFIX)/lib
NODEJS_TC_BIN = $(NODEJS_TC_ROOT)/bin/$(NODEJS_TC_PREFIX)
# The toolchain (GCC 16) defaults to -mfence-tso, emitting fence.tso (Ztso)
# instructions the C906 does not implement -> SIGILL at runtime. The prebuilt
# libstdc++/libgcc_s shipped with the toolchain still contains them and is
# binary-patched post-install instead (patch_fencetso.py).
NODEJS_TC_ARCH_FLAGS = -mno-fence-tso
# qemu-user lookup dir for the target binaries built by this toolchain: musl's
# dynamic loader IS libc.so; expose it under the PT_INTERP name there.
NODEJS_QEMU_LIBS ?= /opt/toolchains/qemu-libs

# Route the toolchain compilers through buildroot's ccache when enabled:
# the NODEJS_TC_* override points directly at the riscv-collab gcc, which
# otherwise bypasses buildroot's TARGET_CC ccache wrapper entirely (and the
# V8/nodejs compile is exactly the part that needs caching on slow hosts).
ifeq ($(BR2_CCACHE),y)
NODEJS_DEPENDENCIES += host-ccache
NODEJS_TC_CC = $(HOST_DIR)/bin/ccache $(NODEJS_TC_BIN)-gcc
NODEJS_TC_CXX = $(HOST_DIR)/bin/ccache $(NODEJS_TC_BIN)-g++
else
NODEJS_TC_CC = $(NODEJS_TC_BIN)-gcc
NODEJS_TC_CXX = $(NODEJS_TC_BIN)-g++
endif

NODEJS_TC_CONFIGURE_OPTS = \
	AR="$(NODEJS_TC_BIN)-ar" \
	AS="$(NODEJS_TC_BIN)-as" \
	CC="$(NODEJS_TC_CC)" \
	CPP="$(NODEJS_TC_BIN)-cpp" \
	CXX="$(NODEJS_TC_CXX)" \
	LD="$(NODEJS_TC_BIN)-g++" \
	NM="$(NODEJS_TC_BIN)-nm" \
	RANLIB="$(NODEJS_TC_BIN)-ranlib" \
	STRIP="$(NODEJS_TC_BIN)-strip" \
	OBJCOPY="$(NODEJS_TC_BIN)-objcopy" \
	CFLAGS="$(TARGET_CFLAGS) $(NODEJS_TC_ARCH_FLAGS)" \
	CXXFLAGS="$(TARGET_CXXFLAGS) $(NODEJS_TC_ARCH_FLAGS)"
# ---------------------------------------------------------------------------

# Native addons built through $(NPM) must use the same toolchain as node
# itself: socketcan >= 4.3 needs C++20 headers (e.g. <source_location>) that
# the BSP GCC 10.2 does not provide, and -mno-fence-tso keeps fence.tso out
# of the generated .node files.
NODEJS_BIN_ENV = $(NODEJS_TC_CONFIGURE_OPTS) \
	LD="$(NODEJS_TC_BIN)-g++" \
	PATH="$(HOST_DIR)/bin:$(BR_PATH)" \
	npm_config_arch=$(NODEJS_CPU) \
	npm_config_target_arch=$(NODEJS_CPU) \
	npm_config_build_from_source=true \
	npm_config_nodedir=$(BUILD_DIR)/nodejs-$(NODEJS_VERSION) \
	npm_config_prefix=$(TARGET_DIR)/usr \
	npm_config_cache=$(BUILD_DIR)/.npm-cache

# Define various packaging tools for other packages to use
NPM = $(NODEJS_BIN_ENV) $(HOST_DIR)/bin/npm
ifeq ($(BR2_PACKAGE_HOST_NODEJS_COREPACK),y)
COREPACK = $(NODEJS_BIN_ENV) $(HOST_DIR)/bin/corepack
PNPM = $(NODEJS_BIN_ENV) $(HOST_DIR)/bin/pnpm
YARN = $(NODEJS_BIN_ENV) $(HOST_DIR)/bin/yarn
endif

NODEJS_DEPENDENCIES = \
	host-ninja \
	host-pkgconf \
	host-python3 \
	host-qemu \
	$(call qstrip,$(BR2_PACKAGE_NODEJS_MODULES_ADDITIONAL_DEPS))

HOST_NODEJS_DEPENDENCIES = \
	host-icu \
	host-libopenssl \
	host-ninja \
	host-pkgconf \
	host-python3 \
	host-zlib
NODEJS_INSTALL_STAGING = YES

# All third-party libs (openssl 3, libuv 1.52, c-ares, nghttp2, zlib) are
# bundled: the 2021.05 package versions are incompatible with node 24+ and
# sharing them adds version-gate failures without real benefit.
# Temporal (Rust) is disabled: node's gyp builds the cargo crates for the
# build host and links them into the target binary, which breaks
# cross-builds on hosts that happen to have a rust toolchain installed
# (e.g. GitHub runners: x86-64 objects fed to the riscv64 linker).
NODEJS_CONF_OPTS = \
	--v8-disable-temporal-support \
	--cross-compiling \
	--dest-os=linux \
	--ninja

HOST_NODEJS_MAKE_OPTS = \
	$(HOST_CONFIGURE_OPTS) \
	CXXFLAGS="$(HOST_NODEJS_CXXFLAGS)" \
	LDFLAGS.host="$(HOST_LDFLAGS)" \
	NO_LOAD=cctest.target.mk \
	PATH=$(@D)/bin:$(BR_PATH) \
	JOBS=$(PARALLEL_JOBS)

NODEJS_MAKE_OPTS = \
	$(NODEJS_TC_CONFIGURE_OPTS) \
	NO_LOAD=cctest.target.mk \
	PATH=$(@D)/bin:$(BR_PATH) \
	LDFLAGS="$(NODEJS_LDFLAGS)" \
	LD="$(NODEJS_TC_BIN)-g++" \
	JOBS=$(PARALLEL_JOBS)

# nodejs's build system uses python which can be a symlink to an unsupported
# python version (e.g. python 3.10 with nodejs 14.18.1). We work around this by
# forcing host-python3 early in the PATH, via a python->python3 symlink.
define NODEJS_PYTHON3_SYMLINK
	mkdir -p $(@D)/bin
	ln -sf $(HOST_DIR)/bin/python3 $(@D)/bin/python
endef
HOST_NODEJS_PRE_CONFIGURE_HOOKS += NODEJS_PYTHON3_SYMLINK
NODEJS_PRE_CONFIGURE_HOOKS += NODEJS_PYTHON3_SYMLINK

# Keep the bundled small-icu (default) even when there is no system ICU:
# --with-intl=none breaks Unicode property escapes in regex (e.g. /\P{ASCII}/u
# used by node-red), while small-icu adds no external dependency.
ifeq ($(BR2_PACKAGE_ICU),y)
NODEJS_DEPENDENCIES += icu
NODEJS_CONF_OPTS += --with-intl=system-icu
endif

ifneq ($(BR2_PACKAGE_NODEJS_NPM),y)
NODEJS_CONF_OPTS += --without-npm
endif

define HOST_NODEJS_CONFIGURE_CMDS
	cd $(@D); \
		$(HOST_CONFIGURE_OPTS) \
		PATH=$(@D)/bin:$(BR_PATH) \
		PYTHON=$(HOST_DIR)/bin/python3 \
		$(HOST_DIR)/bin/python3 configure.py \
		--prefix=$(HOST_DIR) \
		--no-cross-compiling \
		--with-intl=none \
		--ninja
endef

HOST_NODEJS_CXXFLAGS = $(HOST_CXXFLAGS)

define HOST_NODEJS_BUILD_CMDS
	$(HOST_MAKE_ENV) PYTHON=$(HOST_DIR)/bin/python3 \
		$(MAKE) -C $(@D) \
		$(HOST_NODEJS_MAKE_OPTS)
endef

ifeq ($(BR2_PACKAGE_HOST_NODEJS_COREPACK),y)
define HOST_NODEJS_ENABLE_COREPACK
	$(COREPACK) enable
endef
endif

define HOST_NODEJS_INSTALL_CMDS
	$(HOST_MAKE_ENV) PYTHON=$(HOST_DIR)/bin/python3 \
		$(MAKE) -C $(@D) install \
		$(HOST_NODEJS_MAKE_OPTS)
	$(HOST_NODEJS_ENABLE_COREPACK)
endef

ifeq ($(BR2_i386),y)
NODEJS_CPU = ia32
else ifeq ($(BR2_x86_64),y)
NODEJS_CPU = x64
else ifeq ($(BR2_mips),y)
NODEJS_CPU = mips
else ifeq ($(BR2_mipsel),y)
NODEJS_CPU = mipsel
else ifeq ($(BR2_arm),y)
NODEJS_CPU = arm
# V8 needs to know what floating point ABI the target is using.
NODEJS_ARM_FP = $(GCC_TARGET_FLOAT_ABI)
# it also wants to know which FPU to use, but only has support for
# vfp, vfpv3, vfpv3-d16 and neon.
ifeq ($(BR2_ARM_FPU_VFPV2),y)
NODEJS_ARM_FPU = vfp
# vfpv4 is a superset of vfpv3
else ifeq ($(BR2_ARM_FPU_VFPV3)$(BR2_ARM_FPU_VFPV4),y)
NODEJS_ARM_FPU = vfpv3
# vfpv4-d16 is a superset of vfpv3-d16
else ifeq ($(BR2_ARM_FPU_VFPV3D16)$(BR2_ARM_FPU_VFPV4D16),y)
NODEJS_ARM_FPU = vfpv3-d16
else ifeq ($(BR2_ARM_FPU_NEON),y)
NODEJS_ARM_FPU = neon
endif
else ifeq ($(BR2_aarch64),y)
NODEJS_CPU = arm64
else ifeq ($(BR2_RISCV_64),y)
NODEJS_CPU = riscv64
endif

# MIPS architecture specific options
ifeq ($(BR2_mips)$(BR2_mipsel),y)
ifeq ($(BR2_MIPS_CPU_MIPS32R6),y)
NODEJS_MIPS_ARCH_VARIANT = r6
NODEJS_MIPS_FPU_MODE = fp64
else ifeq ($(BR2_MIPS_CPU_MIPS32R2),y)
NODEJS_MIPS_ARCH_VARIANT = r2
else ifeq ($(BR2_MIPS_CPU_MIPS32),y)
NODEJS_MIPS_ARCH_VARIANT = r1
endif
endif

NODEJS_LDFLAGS = $(TARGET_LDFLAGS) -latomic

# Ship the toolchain C++ runtime into the target from this package: the
# external-toolchain pseudo-packages do not reliably deliver libstdc++ (the
# cvitek sysroot keeps it in ABI-variant dirs the lib copy loop never
# searches), so a clean build would otherwise end up without a libstdc++ new
# enough for node. fence.tso in the prebuilt libstdc++ is rewritten to a full
# fence by patch_fencetso.py; node itself is built with -mno-fence-tso.
define NODEJS_INSTALL_TC_RUNTIME
	$(HOST_DIR)/bin/python3 $(NODEJS_PKGDIR)/patch_fencetso.py \
		$(NODEJS_TC_LIBDIR)/libstdc++.so.6.0.35 \
		$(TARGET_DIR)/lib/libstdc++.so.6.0.35
	$(INSTALL) -D -m 0755 $(NODEJS_TC_LIBDIR)/libatomic.so.1.2.0 \
		$(TARGET_DIR)/lib/libatomic.so.1.2.0
	$(INSTALL) -D -m 0755 $(NODEJS_TC_LIBDIR)/libgcc_s.so.1 \
		$(TARGET_DIR)/lib/libgcc_s.so.1
	ln -sf libstdc++.so.6.0.35 $(TARGET_DIR)/lib/libstdc++.so.6
	ln -sf libatomic.so.1.2.0 $(TARGET_DIR)/lib/libatomic.so.1
endef
NODEJS_POST_INSTALL_TARGET_HOOKS += NODEJS_INSTALL_TC_RUNTIME

# V8's JIT infrastructure requires binaries such as mksnapshot and
# mkpeephole to be run in the host during the build. However, these
# binaries must have the same bit-width as the target (e.g. a x86_64
# host targeting ARMv6 needs to produce a 32-bit binary). To work around this
# issue, cross-compile the binaries for the target and run them on the
# host with QEMU, much like gobject-introspection.
# The toolchain variant of the wrapper additionally resolves the dynamic
# loader and the C++ runtime from NODEJS_TC_SYSROOT first, because the
# node binaries are linked against a newer libstdc++ than the one staged
# by the BSP GCC 10.2 toolchain.
define NODEJS_INSTALL_V8_QEMU_WRAPPER
	$(INSTALL) -D -m 755 $(NODEJS_PKGDIR)/v8-qemu-wrapper-tc.in \
		$(@D)/out/Release/v8-qemu-wrapper
	$(SED) "s%@QEMU_USER@%$(QEMU_USER)%g" \
		$(@D)/out/Release/v8-qemu-wrapper
	$(SED) "s%@TOOLCHAIN_HEADERS_VERSION@%$(BR2_TOOLCHAIN_HEADERS_AT_LEAST)%g" \
		$(@D)/out/Release/v8-qemu-wrapper
	$(SED) "s%@QEMU_USERMODE_ARGS@%$(call qstrip,$(BR2_PACKAGE_HOST_QEMU_USER_MODE_ARGS))%g" \
		$(@D)/out/Release/v8-qemu-wrapper
	$(SED) "s%@NODEJS_TC_LIBDIR@%$(NODEJS_TC_LIBDIR)%g" \
		$(@D)/out/Release/v8-qemu-wrapper
	$(SED) "s%@NODEJS_QEMU_LIBS@%$(NODEJS_QEMU_LIBS)%g" \
		$(@D)/out/Release/v8-qemu-wrapper
endef
NODEJS_PRE_CONFIGURE_HOOKS += NODEJS_INSTALL_V8_QEMU_WRAPPER

define NODEJS_WRAPPER_FIXUP
	$(SED) "s%@MAYBE_WRAPPER@%'<(PRODUCT_DIR)/v8-qemu-wrapper',%g" $(@D)/node.gyp
	$(SED) "s%@MAYBE_WRAPPER@%'<(PRODUCT_DIR)/v8-qemu-wrapper',%g" $(@D)/tools/v8_gypfiles/v8.gyp
	$(SED) "s%@MAYBE_WRAPPER@%'<(PRODUCT_DIR)/v8-qemu-wrapper',%g" $(@D)/tools/icu/icu-generic.gyp
endef
NODEJS_PRE_CONFIGURE_HOOKS += NODEJS_WRAPPER_FIXUP

# Do not run the qemu-wrapper for the host build.
define HOST_NODEJS_WRAPPER_FIXUP
	$(SED) "s%@MAYBE_WRAPPER@%%g" $(@D)/node.gyp
	$(SED) "s%@MAYBE_WRAPPER@%%g" $(@D)/tools/v8_gypfiles/v8.gyp
	$(SED) "s%@MAYBE_WRAPPER@%%g" $(@D)/tools/icu/icu-generic.gyp
endef
HOST_NODEJS_PRE_CONFIGURE_HOOKS += HOST_NODEJS_WRAPPER_FIXUP

define NODEJS_CONFIGURE_CMDS
	(cd $(@D); \
		$(NODEJS_TC_CONFIGURE_OPTS) \
		PATH=$(@D)/bin:$(BR_PATH) \
		LDFLAGS="$(NODEJS_LDFLAGS)" \
		LD="$(NODEJS_TC_BIN)-g++" \
		PYTHON=$(HOST_DIR)/bin/python3 \
		$(HOST_DIR)/bin/python3 configure.py \
		--prefix=/usr \
		--dest-cpu=$(NODEJS_CPU) \
		$(if $(NODEJS_ARM_FP),--with-arm-float-abi=$(NODEJS_ARM_FP)) \
		$(if $(NODEJS_ARM_FPU),--with-arm-fpu=$(NODEJS_ARM_FPU)) \
		$(if $(NODEJS_MIPS_ARCH_VARIANT),--with-mips-arch-variant=$(NODEJS_MIPS_ARCH_VARIANT)) \
		$(if $(NODEJS_MIPS_FPU_MODE),--with-mips-fpu-mode=$(NODEJS_MIPS_FPU_MODE)) \
		$(NODEJS_CONF_OPTS) \
	)
endef

define NODEJS_BUILD_CMDS
	$(TARGET_MAKE_ENV) PYTHON=$(HOST_DIR)/bin/python3 \
		$(MAKE) -C $(@D) \
		$(NODEJS_MAKE_OPTS)
endef

#
# Build the list of modules to install.
#
NODEJS_MODULES_LIST= $(call qstrip,\
	$(BR2_PACKAGE_NODEJS_MODULES_ADDITIONAL))

#
# We can only call NPM if there's something to install.
#
ifneq ($(NODEJS_MODULES_LIST),)
NODEJS_DEPENDENCIES += host-nodejs
define NODEJS_INSTALL_MODULES
	# If you're having trouble with module installation, adding -d to the
	# npm install call below and setting npm_config_rollback=false can both
	# help in diagnosing the problem.
	$(NPM) install --production -g $(NODEJS_MODULES_LIST)
endef

# Exclude prebuilt binaries with different architectures and OS from check
NODEJS_BIN_ARCH_EXCLUDE = /usr/lib/node_modules/
endif

define NODEJS_INSTALL_STAGING_CMDS
	$(TARGET_MAKE_ENV) PYTHON=$(HOST_DIR)/bin/python3 \
		$(MAKE) -C $(@D) install \
		DESTDIR=$(STAGING_DIR) \
		$(NODEJS_MAKE_OPTS)
endef

define NODEJS_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) PYTHON=$(HOST_DIR)/bin/python3 \
		$(MAKE) -C $(@D) install \
		DESTDIR=$(TARGET_DIR) \
		$(NODEJS_MAKE_OPTS)
	$(NODEJS_INSTALL_MODULES)
endef

# node.js configure is a Python script and does not use autotools
$(eval $(generic-package))
$(eval $(host-generic-package))
