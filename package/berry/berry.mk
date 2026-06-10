################################################################################
#
# berry
#
################################################################################

BERRY_VERSION = bd9c93b65dfadddc27e3203fc04e60e986a0fa5f
BERRY_SITE = https://github.com/berry-lang/berry.git
BERRY_SITE_METHOD = git
BERRY_LICENSE = MIT
BERRY_LICENSE_FILES = LICENSE
BERRY_DEPENDENCIES = host-python3

define BERRY_CONFIGURE_CMDS
	$(SED) 's/#define BE_USE_SHARED_LIB[[:space:]].*/#define BE_USE_SHARED_LIB               0/' \
		-e 's/#define BE_USE_OS_MODULE[[:space:]].*/#define BE_USE_OS_MODULE                0/' \
		-e 's/#define BE_USE_BYTECODE_SAVER[[:space:]].*/#define BE_USE_BYTECODE_SAVER        0/' \
		-e 's/#define BE_USE_BYTECODE_LOADER[[:space:]].*/#define BE_USE_BYTECODE_LOADER       0/' \
		-e 's/#define BE_USE_SOLIDIFY_MODULE[[:space:]].*/#define BE_USE_SOLIDIFY_MODULE       0/' \
		$(@D)/default/berry_conf.h
endef

define BERRY_BUILD_CMDS
	mkdir -p $(@D)/generate
	cd $(@D) && $(HOST_DIR)/bin/python3 ./tools/coc/coc -o generate src default -c default/berry_conf.h
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-std=c99 -Wall -Wextra \
		-I$(@D)/src -I$(@D)/default -I$(@D)/generate \
		$(@D)/src/*.c $(@D)/default/be_port.c $(@D)/default/be_modtab.c \
		$(BERRY_PKGDIR)/berry_main.c \
		-lm -o $(@D)/berry
endef

define BERRY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/berry $(TARGET_DIR)/usr/bin/berry
endef

$(eval $(generic-package))
