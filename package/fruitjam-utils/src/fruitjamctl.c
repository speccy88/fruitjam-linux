// SPDX-License-Identifier: MIT
/*
 * Minimal Adafruit Fruit Jam RP2350B board-control helper.
 *
 * The no-MMU Linux image exposes the RP2350 SIO GPIOs through sysfs. Keep this
 * helper on that path so status/control works without direct /dev/mem access.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define FRUITJAM_BUTTON1_GPIO        0u
#define FRUITJAM_BUTTON2_GPIO        4u
#define FRUITJAM_BUTTON3_GPIO        5u
#define FRUITJAM_USB_HOST_POWER_GPIO 11u
#define FRUITJAM_PERIPH_RESET_GPIO   22u
#define FRUITJAM_LED_GPIO            29u

struct button {
	const char *name;
	unsigned int gpio;
};

static const struct button buttons[] = {
	{ "button1", FRUITJAM_BUTTON1_GPIO },
	{ "button2", FRUITJAM_BUTTON2_GPIO },
	{ "button3", FRUITJAM_BUTTON3_GPIO },
};

static void usage(FILE *out)
{
	fprintf(out,
		"Usage: fruitjamctl <command> [argument]\n"
		"\n"
		"Commands:\n"
		"  init                  configure LED/buttons/reset/USB-power GPIOs\n"
		"  status                print LED, button, reset, and USB-power state\n"
		"  buttons               print button pressed/released state\n"
		"  bootsel [delay-ms]    reboot into the RP2350 BOOTSEL USB loader\n"
		"  led on|off|toggle     control active-low red LED on GPIO29\n"
		"  usb-power on|off      control USB host 5V power GPIO11\n"
		"  periph-reset assert|deassert|pulse\n"
		"                        control shared TLV320/ESP32-C6 reset GPIO22\n");
}

static int write_text_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_TRUNC);
	ssize_t ret;

	if (fd < 0) {
		fprintf(stderr, "fruitjamctl: open %s: %s\n", path, strerror(errno));
		return -1;
	}
	ret = write(fd, text, strlen(text));
	close(fd);
	if (ret != (ssize_t)strlen(text)) {
		fprintf(stderr, "fruitjamctl: write %s: %s\n", path,
			ret < 0 ? strerror(errno) : "short write");
		return -1;
	}
	return 0;
}

static int export_gpio(unsigned int gpio)
{
	char buf[16];
	int fd = open("/sys/class/gpio/export", O_WRONLY);
	ssize_t ret;

	if (fd < 0) {
		fprintf(stderr, "fruitjamctl: open /sys/class/gpio/export: %s\n",
			strerror(errno));
		return -1;
	}

	snprintf(buf, sizeof(buf), "%u", gpio);
	ret = write(fd, buf, strlen(buf));
	close(fd);
	if (ret < 0 && errno != EBUSY) {
		fprintf(stderr, "fruitjamctl: export gpio%u: %s\n", gpio, strerror(errno));
		return -1;
	}
	return 0;
}

static int gpio_path(unsigned int gpio, const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "/sys/class/gpio/gpio%u/%s", gpio, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int gpio_direction(unsigned int gpio, const char *direction)
{
	char path[64];

	if (export_gpio(gpio) < 0)
		return -1;
	if (gpio_path(gpio, "direction", path, sizeof(path)) < 0)
		return -1;
	return write_text_file(path, direction);
}

static int gpio_output(unsigned int gpio, int high)
{
	char path[64];

	if (gpio_direction(gpio, "out") < 0)
		return -1;
	if (gpio_path(gpio, "value", path, sizeof(path)) < 0)
		return -1;
	return write_text_file(path, high ? "1" : "0");
}

static int gpio_input(unsigned int gpio)
{
	return gpio_direction(gpio, "in");
}

static int gpio_read(unsigned int gpio)
{
	char path[64];
	char c;
	int fd;

	if (export_gpio(gpio) < 0)
		return -1;
	if (gpio_path(gpio, "value", path, sizeof(path)) < 0)
		return -1;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "fruitjamctl: open %s: %s\n", path, strerror(errno));
		return -1;
	}
	if (read(fd, &c, 1) != 1) {
		fprintf(stderr, "fruitjamctl: read %s: %s\n", path, strerror(errno));
		close(fd);
		return -1;
	}
	close(fd);
	return c == '0' ? 0 : 1;
}

static int fruitjam_init(void)
{
	size_t i;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(buttons); i++)
		ret |= gpio_input(buttons[i].gpio);

	ret |= gpio_output(FRUITJAM_LED_GPIO, 1); /* active-low LED off */
	ret |= gpio_output(FRUITJAM_USB_HOST_POWER_GPIO, 1);
	ret |= gpio_output(FRUITJAM_PERIPH_RESET_GPIO, 1);
	return ret ? -1 : 0;
}

static int parse_delay_ms(const char *s, unsigned int *delay_ms)
{
	char *end;
	unsigned long parsed;

	if (!s || !*s) {
		*delay_ms = 0;
		return 0;
	}
	errno = 0;
	parsed = strtoul(s, &end, 10);
	if (errno || *end || parsed > 60000ul)
		return -1;
	*delay_ms = (unsigned int)parsed;
	return 0;
}

static int reboot_bootsel(unsigned int delay_ms)
{
	if (delay_ms)
		usleep(delay_ms * 1000u);
	sync();
	return syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		       LINUX_REBOOT_CMD_RESTART2, "bootsel");
}

static int print_buttons(void)
{
	size_t i;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(buttons); i++) {
		int released = gpio_read(buttons[i].gpio);

		if (released < 0) {
			ret = -1;
			printf("%s gpio%u error\n", buttons[i].name, buttons[i].gpio);
			continue;
		}
		printf("%s gpio%u %s\n", buttons[i].name, buttons[i].gpio,
		       released ? "released" : "pressed");
	}
	return ret;
}

static int print_named_level(const char *name, unsigned int gpio,
			     const char *high_name, const char *low_name)
{
	int value = gpio_read(gpio);

	if (value < 0) {
		printf("%s gpio%u error\n", name, gpio);
		return -1;
	}
	printf("%s gpio%u %s\n", name, gpio, value ? high_name : low_name);
	return 0;
}

static int print_status(void)
{
	int ret = 0;

	ret |= print_named_level("red-led", FRUITJAM_LED_GPIO, "off", "on");
	ret |= print_named_level("usb-host-5v", FRUITJAM_USB_HOST_POWER_GPIO, "on", "off");
	ret |= print_named_level("periph-reset", FRUITJAM_PERIPH_RESET_GPIO,
				 "deasserted", "asserted");
	ret |= print_buttons();
	return ret ? -1 : 0;
}

static int require_arg(int argc, char **argv)
{
	if (argc >= 3)
		return 0;

	usage(stderr);
	fprintf(stderr, "fruitjamctl: missing argument for %s\n", argv[1]);
	return -1;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage(stderr);
		return 2;
	}

	if (!strcmp(argv[1], "bootsel")) {
		unsigned int delay_ms;

		if (parse_delay_ms(argc >= 3 ? argv[2] : NULL, &delay_ms) < 0) {
			fprintf(stderr, "fruitjamctl: bad bootsel delay: %s\n", argv[2]);
			return 2;
		}
		if (reboot_bootsel(delay_ms) < 0) {
			fprintf(stderr, "fruitjamctl: reboot bootsel: %s\n", strerror(errno));
			return 1;
		}
		return 0;
	}

	if (!strcmp(argv[1], "init"))
		return fruitjam_init() < 0 ? 1 : 0;
	if (!strcmp(argv[1], "status"))
		return print_status() < 0 ? 1 : 0;
	if (!strcmp(argv[1], "buttons"))
		return print_buttons() < 0 ? 1 : 0;

	if (!strcmp(argv[1], "led")) {
		if (require_arg(argc, argv) < 0)
			return 2;
		if (!strcmp(argv[2], "on"))
			return gpio_output(FRUITJAM_LED_GPIO, 0) < 0 ? 1 : 0;
		if (!strcmp(argv[2], "off"))
			return gpio_output(FRUITJAM_LED_GPIO, 1) < 0 ? 1 : 0;
		if (!strcmp(argv[2], "toggle")) {
			int value = gpio_read(FRUITJAM_LED_GPIO);

			if (value < 0)
				return 1;
			return gpio_output(FRUITJAM_LED_GPIO, !value) < 0 ? 1 : 0;
		}
		return usage(stderr), 2;
	}

	if (!strcmp(argv[1], "usb-power")) {
		if (require_arg(argc, argv) < 0)
			return 2;
		if (!strcmp(argv[2], "on"))
			return gpio_output(FRUITJAM_USB_HOST_POWER_GPIO, 1) < 0 ? 1 : 0;
		if (!strcmp(argv[2], "off"))
			return gpio_output(FRUITJAM_USB_HOST_POWER_GPIO, 0) < 0 ? 1 : 0;
		return usage(stderr), 2;
	}

	if (!strcmp(argv[1], "periph-reset")) {
		if (require_arg(argc, argv) < 0)
			return 2;
		if (!strcmp(argv[2], "assert"))
			return gpio_output(FRUITJAM_PERIPH_RESET_GPIO, 0) < 0 ? 1 : 0;
		if (!strcmp(argv[2], "deassert"))
			return gpio_output(FRUITJAM_PERIPH_RESET_GPIO, 1) < 0 ? 1 : 0;
		if (!strcmp(argv[2], "pulse")) {
			if (gpio_output(FRUITJAM_PERIPH_RESET_GPIO, 0) < 0)
				return 1;
			usleep(10000);
			return gpio_output(FRUITJAM_PERIPH_RESET_GPIO, 1) < 0 ? 1 : 0;
		}
		return usage(stderr), 2;
	}

	usage(stderr);
	fprintf(stderr, "fruitjamctl: unknown command: %s\n", argv[1]);
	return 2;
}
