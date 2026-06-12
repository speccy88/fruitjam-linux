// SPDX-License-Identifier: MIT
/*
 * Tiny Fruit Jam USB host power/status helper.
 *
 * Fruit Jam routes USB host D+/D- through GPIO1/GPIO2 and controls host 5 V
 * power with GPIO11. This helper only manages power and reports line levels;
 * it is not a USB host stack.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GPIO_DP 1u
#define GPIO_DM 2u
#define GPIO_POWER 11u

static void usage(FILE *out)
{
	fprintf(out, "usage: fruitjam-usbhost {status|on|off}\n");
}

static int write_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY);
	ssize_t ret;

	if (fd < 0)
		return -1;
	ret = write(fd, text, strlen(text));
	close(fd);
	return ret == (ssize_t)strlen(text) ? 0 : -1;
}

static int read_file(const char *path, char *buf, size_t len)
{
	int fd;
	ssize_t ret;

	if (!len)
		return -1;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		buf[0] = '\0';
		return -1;
	}
	ret = read(fd, buf, len - 1);
	close(fd);
	if (ret < 0) {
		buf[0] = '\0';
		return -1;
	}
	buf[ret] = '\0';
	while (ret > 0 && (buf[ret - 1] == '\n' || buf[ret - 1] == '\r' ||
			   buf[ret - 1] == ' ' || buf[ret - 1] == '\t'))
		buf[--ret] = '\0';
	return 0;
}

static int gpio_path(unsigned int gpio, const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "/sys/class/gpio/gpio%u/%s", gpio, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int gpio_export(unsigned int gpio)
{
	char path[64];
	char text[12];
	int fd;
	ssize_t ret;
	int i;

	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd < 0)
		return -1;
	snprintf(text, sizeof(text), "%u", gpio);
	ret = write(fd, text, strlen(text));
	close(fd);
	if (ret < 0 && errno != EBUSY)
		return -1;
	if (gpio_path(gpio, "value", path, sizeof(path)) < 0)
		return -1;
	for (i = 0; i < 50; i++) {
		if (access(path, F_OK) == 0)
			return 0;
		usleep(2000);
	}
	return 0;
}

static int gpio_direction(unsigned int gpio, const char *direction)
{
	char path[64];

	gpio_export(gpio);
	if (gpio_path(gpio, "direction", path, sizeof(path)) < 0)
		return -1;
	return write_file(path, direction);
}

static int gpio_input(unsigned int gpio)
{
	return gpio_direction(gpio, "in");
}

static int gpio_write(unsigned int gpio, int value)
{
	char path[64];

	if (gpio_direction(gpio, "out") < 0)
		return -1;
	if (gpio_path(gpio, "value", path, sizeof(path)) < 0)
		return -1;
	return write_file(path, value ? "1" : "0");
}

static int gpio_read(unsigned int gpio)
{
	char path[64];
	char text[8];

	gpio_export(gpio);
	if (gpio_path(gpio, "value", path, sizeof(path)) < 0)
		return -1;
	if (read_file(path, text, sizeof(text)) < 0)
		return -1;
	return text[0] == '0' ? 0 : 1;
}

static int gpio_read_input(unsigned int gpio)
{
	if (gpio_input(gpio) < 0)
		return -1;
	return gpio_read(gpio);
}

static const char *usb_state(int power, int dp, int dm)
{
	if (power < 0)
		return "unknown";
	if (power == 0)
		return "power-off";
	if (dp < 0 || dm < 0)
		return "unknown";
	if (!dp && !dm)
		return "no-device-or-reset";
	if (dp && !dm)
		return "full-speed-device";
	if (!dp && dm)
		return "low-speed-device";
	return "invalid-both-lines-high";
}

static int print_status(void)
{
	int power = gpio_read(GPIO_POWER);
	int dp = gpio_read_input(GPIO_DP);
	int dm = gpio_read_input(GPIO_DM);

	if (power < 0) {
		fprintf(stderr, "fruitjam-usbhost: cannot read GPIO%u power\n",
			GPIO_POWER);
		return 1;
	}

	printf("usbhost power %s gpio%u=%d", power ? "on" : "off",
	       GPIO_POWER, power);
	if (dp >= 0 && dm >= 0)
		printf(" gpio%u(dp)=%d gpio%u(dm)=%d", GPIO_DP, dp, GPIO_DM, dm);
	putchar('\n');
	printf("usbhost device %s\n", usb_state(power, dp, dm));
	puts("usbhost stack unavailable: Linux image has USB gadget support only");
	return 0;
}

int main(int argc, char **argv)
{
	const char *cmd = argc > 1 ? argv[1] : "status";

	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
		usage(stdout);
		return 0;
	}
	if (!strcmp(cmd, "on")) {
		if (gpio_write(GPIO_POWER, 1) < 0) {
			fprintf(stderr, "fruitjam-usbhost: power on failed: %s\n",
				strerror(errno));
			return 1;
		}
		return print_status();
	}
	if (!strcmp(cmd, "off")) {
		if (gpio_write(GPIO_POWER, 0) < 0) {
			fprintf(stderr, "fruitjam-usbhost: power off failed: %s\n",
				strerror(errno));
			return 1;
		}
		return print_status();
	}
	if (!strcmp(cmd, "status"))
		return print_status();

	usage(stderr);
	return 1;
}
