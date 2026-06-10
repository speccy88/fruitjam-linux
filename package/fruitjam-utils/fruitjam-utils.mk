################################################################################
#
# fruitjam-utils
#
################################################################################

FRUITJAM_UTILS_VERSION = 1.0
FRUITJAM_UTILS_SITE = $(FRUITJAM_UTILS_PKGDIR)/src
FRUITJAM_UTILS_SITE_METHOD = local
FRUITJAM_UTILS_LICENSE = MIT
FRUITJAM_UTILS_DEPENDENCIES = fruitjam-netbox
FRUITJAM_UTILS_TINY_CFLAGS = \
	$(TARGET_CFLAGS) -ffunction-sections -fdata-sections
FRUITJAM_UTILS_TINY_LDFLAGS = \
	$(TARGET_LDFLAGS) -Wl,--gc-sections

define FRUITJAM_UTILS_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjamctl $(@D)/fruitjamctl.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-env.cgi $(@D)/fruitjam-env-cgi.c
	$(TARGET_CC) $(FRUITJAM_UTILS_TINY_CFLAGS) $(FRUITJAM_UTILS_TINY_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-web.cgi $(@D)/fruitjam-web-cgi.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-i2c $(@D)/fruitjam-i2c.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-adc $(@D)/fruitjam-adc.c
	$(TARGET_CC) $(FRUITJAM_UTILS_TINY_CFLAGS) $(FRUITJAM_UTILS_TINY_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-services $(@D)/fruitjam-services.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/mosquitto_pub $(@D)/mosquitto_pub.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-buttons \
		$(@D)/fruitjam-buttons.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-buttonlog \
		$(@D)/fruitjam-buttonlog.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-rtttl \
		$(@D)/fruitjam-rtttl.c -lm
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-ftpd \
		$(@D)/fruitjam-ftpd.c
	$(TARGET_CC) $(FRUITJAM_UTILS_TINY_CFLAGS) $(FRUITJAM_UTILS_TINY_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-nc \
		$(@D)/fruitjam-nc.c
	$(TARGET_CC) $(FRUITJAM_UTILS_TINY_CFLAGS) $(FRUITJAM_UTILS_TINY_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-telnet \
		$(@D)/fruitjam-telnet.c
	$(TARGET_CC) $(FRUITJAM_UTILS_TINY_CFLAGS) $(FRUITJAM_UTILS_TINY_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-shell \
		$(@D)/fruitjam-shell.c
	$(TARGET_CC) $(FRUITJAM_UTILS_TINY_CFLAGS) $(FRUITJAM_UTILS_TINY_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-wget \
		$(@D)/fruitjam-wget.c
	$(TARGET_CC) $(FRUITJAM_UTILS_TINY_CFLAGS) $(FRUITJAM_UTILS_TINY_LDFLAGS) \
		-Wall -Wextra -Os -o $(@D)/fruitjam-telnetd \
		$(@D)/fruitjam-telnetd.c
	$(TARGET_CROSS)flthdr -s 4096 \
		$(@D)/fruitjamctl \
		$(@D)/fruitjam-env.cgi \
		$(@D)/fruitjam-web.cgi \
		$(@D)/fruitjam-i2c \
		$(@D)/fruitjam-adc \
		$(@D)/mosquitto_pub \
		$(@D)/fruitjam-buttons \
		$(@D)/fruitjam-buttonlog \
		$(@D)/fruitjam-rtttl \
		$(@D)/fruitjam-ftpd
	$(TARGET_CROSS)flthdr -s 1024 \
		$(@D)/fruitjam-nc \
		$(@D)/fruitjam-telnet \
		$(@D)/fruitjam-shell \
		$(@D)/fruitjam-wget \
		$(@D)/fruitjam-telnetd \
		$(@D)/fruitjam-services
endef

define FRUITJAM_UTILS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/fruitjamctl $(TARGET_DIR)/usr/bin/fruitjamctl
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-env.cgi $(TARGET_DIR)/www/cgi-bin/env.cgi
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-web.cgi $(TARGET_DIR)/www/cgi-bin/fruitjam.cgi
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-i2c $(TARGET_DIR)/usr/bin/fruitjam-i2c
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-adc $(TARGET_DIR)/usr/bin/fruitjam-adc
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-services $(TARGET_DIR)/usr/bin/fruitjam-services
	$(INSTALL) -D -m 0755 $(@D)/mosquitto_pub $(TARGET_DIR)/usr/bin/mosquitto_pub
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-buttons $(TARGET_DIR)/usr/bin/fruitjam-buttons
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-buttonlog $(TARGET_DIR)/usr/bin/fruitjam-buttonlog
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-rtttl $(TARGET_DIR)/usr/bin/fruitjam-rtttl
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-ftpd $(TARGET_DIR)/usr/sbin/fruitjam-ftpd
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-nc $(TARGET_DIR)/usr/bin/nc
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-telnet $(TARGET_DIR)/usr/bin/telnet
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-shell $(TARGET_DIR)/usr/bin/fruitjam-shell
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-wget $(TARGET_DIR)/usr/bin/wget
	$(INSTALL) -D -m 0755 $(@D)/fruitjam-telnetd $(TARGET_DIR)/usr/sbin/fruitjam-telnetd
	rm -f $(TARGET_DIR)/usr/bin/sqlite3 $(TARGET_DIR)/usr/lib/libsqlite3*
endef

$(eval $(generic-package))
