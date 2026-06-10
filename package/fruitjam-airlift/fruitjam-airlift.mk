################################################################################
#
# fruitjam-airlift
#
################################################################################

FRUITJAM_AIRLIFT_VERSION = 1.0
FRUITJAM_AIRLIFT_SITE = $(FRUITJAM_AIRLIFT_PKGDIR)/src
FRUITJAM_AIRLIFT_SITE_METHOD = local
FRUITJAM_AIRLIFT_LICENSE = MIT

define FRUITJAM_AIRLIFT_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-std=c99 -Wall -Wextra -Os -o $(@D)/airliftctl $(@D)/airliftctl.c
endef

define FRUITJAM_AIRLIFT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/airliftctl $(TARGET_DIR)/usr/bin/airliftctl
endef

$(eval $(generic-package))
