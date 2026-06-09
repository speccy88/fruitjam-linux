################################################################################
#
# fruitjam-utils
#
################################################################################

FRUITJAM_UTILS_VERSION = 1.0
FRUITJAM_UTILS_SITE = $(FRUITJAM_UTILS_PKGDIR)/src
FRUITJAM_UTILS_SITE_METHOD = local
FRUITJAM_UTILS_LICENSE = MIT

define FRUITJAM_UTILS_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjamctl $(@D)/fruitjamctl.c
endef

define FRUITJAM_UTILS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/fruitjamctl $(TARGET_DIR)/usr/bin/fruitjamctl
endef

$(eval $(generic-package))
