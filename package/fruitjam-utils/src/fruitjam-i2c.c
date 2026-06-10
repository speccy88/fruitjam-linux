// SPDX-License-Identifier: MIT
/*
 * Tiny i2c-dev helper for the Fruit Jam GPIO20/GPIO21 I2C bus.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef I2C_RDWR
#define I2C_RDWR 0x0707
#endif

struct i2c_msg {
	unsigned short addr;
	unsigned short flags;
	unsigned short len;
	unsigned char *buf;
};

struct i2c_rdwr_ioctl_data {
	struct i2c_msg *msgs;
	unsigned int nmsgs;
};

static void usage(FILE *out)
{
	fprintf(out,
		"usage: fruitjam-i2c {scan [DEV]|ping ADDR [DEV]}\n"
		"       DEV defaults to /dev/i2c-0, ADDR may be hex or decimal\n");
}

static int parse_addr(const char *arg)
{
	char *end;
	long addr = strtol(arg, &end, 0);

	if (*end || addr < 0x03 || addr > 0x77)
		return -1;
	return (int)addr;
}

static int open_bus(const char *dev)
{
	int fd = open(dev, O_RDWR);

	if (fd < 0)
		fprintf(stderr, "fruitjam-i2c: open %s: %s\n", dev, strerror(errno));
	return fd;
}

static int ping_addr(int fd, int addr)
{
	unsigned char dummy = 0;
	struct i2c_msg msg = {
		.addr = (unsigned short)addr,
		.flags = 0,
		.len = 0,
		.buf = &dummy,
	};
	struct i2c_rdwr_ioctl_data data = {
		.msgs = &msg,
		.nmsgs = 1,
	};

	/*
	 * A zero-length I2C write message maps to an address-only transfer on
	 * i2c-gpio, which is enough for an ACK/NAK probe and avoids poking
	 * device registers.
	 */
	return ioctl(fd, I2C_RDWR, &data) == 1 ? 0 : -1;
}

static int cmd_ping(const char *dev, const char *addr_arg)
{
	int addr = parse_addr(addr_arg);
	int fd;
	int ret;

	if (addr < 0) {
		fprintf(stderr, "fruitjam-i2c: invalid address %s\n", addr_arg);
		return 2;
	}

	fd = open_bus(dev);
	if (fd < 0)
		return 1;
	ret = ping_addr(fd, addr);
	close(fd);

	if (ret == 0) {
		printf("0x%02x ack\n", addr);
		return 0;
	}
	printf("0x%02x no-ack\n", addr);
	return 1;
}

static int cmd_scan(const char *dev)
{
	int fd = open_bus(dev);
	int row;

	if (fd < 0)
		return 1;

	puts("     00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f");
	for (row = 0; row < 8; row++) {
		int col;

		printf("%02x: ", row << 4);
		for (col = 0; col < 16; col++) {
			int addr = (row << 4) | col;

			if (addr < 0x03 || addr > 0x77) {
				printf("   ");
				continue;
			}
			if (ping_addr(fd, addr) == 0)
				printf("%02x ", addr);
			else
				printf("-- ");
		}
		putchar('\n');
	}

	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	if ((argc == 2 || argc == 3) && !strcmp(argv[1], "scan"))
		return cmd_scan(argc == 3 ? argv[2] : "/dev/i2c-0");
	if ((argc == 3 || argc == 4) && !strcmp(argv[1], "ping"))
		return cmd_ping(argc == 4 ? argv[3] : "/dev/i2c-0", argv[2]);

	usage(stderr);
	return 2;
}
