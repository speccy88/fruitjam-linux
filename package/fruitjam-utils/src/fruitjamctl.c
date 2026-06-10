// SPDX-License-Identifier: MIT
/*
 * Minimal Adafruit Fruit Jam RP2350B board-control helper.
 *
 * This intentionally stays in userspace and uses /dev/mem because the current
 * RP2350 no-MMU Linux port does not yet provide upstreamable GPIO, pinctrl,
 * LED, input, USB-host, I2S, or HSTX DVI kernel drivers. It is a small bridge
 * for bring-up diagnostics: keep UART booting, make the red LED/buttons useful,
 * and let testers enable the USB-host 5V switch while proper drivers are built.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/reboot.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BIT(n) (1u << (n))

#define SIO_BASE        0xd0000000u
#define SIO_SIZE        0x1000u
#define SIO_GPIO_IN     0x004u
#define SIO_GPIO_OUT    0x010u
#define SIO_GPIO_OUT_SET 0x014u
#define SIO_GPIO_OUT_CLR 0x018u
#define SIO_GPIO_OE_SET 0x038u
#define SIO_GPIO_OE_CLR 0x03cu

#define IO_BANK0_BASE   0x40028000u
#define IO_BANK0_SIZE   0x1000u
#define IO_GPIO_CTRL(gpio) (0x004u + ((uint32_t)(gpio) * 8u))

#define PADS_BANK0_BASE 0x40038000u
#define PADS_BANK0_SIZE 0x1000u
#define PADS_GPIO(gpio) (0x004u + ((uint32_t)(gpio) * 4u))

#define GPIO_FUNC_SIO   5u
#define PADS_IE         BIT(6)
#define PADS_PUE        BIT(3)
#define PADS_DRIVE_4MA  BIT(4)

#define FRUITJAM_BUTTON1_GPIO       0u
#define FRUITJAM_BUTTON2_GPIO       4u
#define FRUITJAM_BUTTON3_GPIO       5u
#define FRUITJAM_USB_HOST_POWER_GPIO 11u
#define FRUITJAM_PERIPH_RESET_GPIO  22u
#define FRUITJAM_LED_GPIO           29u

struct maps {
	int fd;
	volatile uint32_t *sio;
	volatile uint32_t *io;
	volatile uint32_t *pads;
};

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
		"  bootsel               reboot into the RP2350 BOOTSEL USB loader\n"
		"  led on|off|toggle     control active-low red LED on GPIO29\n"
		"  usb-power on|off      control USB host 5V power GPIO11\n"
		"  periph-reset assert|deassert|pulse\n"
		"                        control shared TLV320/ESP32-C6 reset GPIO22\n");
}

static volatile uint32_t *map_regs(int fd, uint32_t base, uint32_t size)
{
	void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);

	if (map == MAP_FAILED)
		return NULL;

	return (volatile uint32_t *)map;
}

static int maps_open(struct maps *maps)
{
	memset(maps, 0, sizeof(*maps));
	maps->fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (maps->fd < 0) {
		fprintf(stderr, "fruitjamctl: open /dev/mem: %s\n", strerror(errno));
		return -1;
	}

	maps->sio = map_regs(maps->fd, SIO_BASE, SIO_SIZE);
	maps->io = map_regs(maps->fd, IO_BANK0_BASE, IO_BANK0_SIZE);
	maps->pads = map_regs(maps->fd, PADS_BANK0_BASE, PADS_BANK0_SIZE);
	if (!maps->sio || !maps->io || !maps->pads) {
		fprintf(stderr, "fruitjamctl: mmap RP2350 registers failed: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static void gpio_func_sio(struct maps *maps, unsigned int gpio)
{
	maps->io[IO_GPIO_CTRL(gpio) / 4] = GPIO_FUNC_SIO;
}

static void gpio_input_pullup(struct maps *maps, unsigned int gpio)
{
	maps->pads[PADS_GPIO(gpio) / 4] = PADS_IE | PADS_PUE;
	gpio_func_sio(maps, gpio);
	maps->sio[SIO_GPIO_OE_CLR / 4] = BIT(gpio);
}

static void gpio_output(struct maps *maps, unsigned int gpio, int high)
{
	maps->pads[PADS_GPIO(gpio) / 4] = PADS_DRIVE_4MA;
	gpio_func_sio(maps, gpio);
	maps->sio[high ? SIO_GPIO_OUT_SET / 4 : SIO_GPIO_OUT_CLR / 4] = BIT(gpio);
	maps->sio[SIO_GPIO_OE_SET / 4] = BIT(gpio);
}

static int gpio_read(struct maps *maps, unsigned int gpio)
{
	return !!(maps->sio[SIO_GPIO_IN / 4] & BIT(gpio));
}

static int gpio_out_read(struct maps *maps, unsigned int gpio)
{
	return !!(maps->sio[SIO_GPIO_OUT / 4] & BIT(gpio));
}

static void fruitjam_init(struct maps *maps)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(buttons); i++)
		gpio_input_pullup(maps, buttons[i].gpio);

	gpio_output(maps, FRUITJAM_LED_GPIO, 1); /* active-low LED off */
	gpio_output(maps, FRUITJAM_USB_HOST_POWER_GPIO, 1);
	gpio_output(maps, FRUITJAM_PERIPH_RESET_GPIO, 1);
}

static int reboot_bootsel(void)
{
	sync();
	return syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		       LINUX_REBOOT_CMD_RESTART2, "bootsel");
}

static void print_buttons(struct maps *maps)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(buttons); i++) {
		int released = gpio_read(maps, buttons[i].gpio);

		printf("%s gpio%u %s\n", buttons[i].name, buttons[i].gpio,
		       released ? "released" : "pressed");
	}
}

static void print_status(struct maps *maps)
{
	printf("red-led gpio%u %s\n", FRUITJAM_LED_GPIO,
	       gpio_out_read(maps, FRUITJAM_LED_GPIO) ? "off" : "on");
	printf("usb-host-5v gpio%u %s\n", FRUITJAM_USB_HOST_POWER_GPIO,
	       gpio_out_read(maps, FRUITJAM_USB_HOST_POWER_GPIO) ? "on" : "off");
	printf("periph-reset gpio%u %s\n", FRUITJAM_PERIPH_RESET_GPIO,
	       gpio_out_read(maps, FRUITJAM_PERIPH_RESET_GPIO) ? "deasserted" : "asserted");
	print_buttons(maps);
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
	struct maps maps;

	if (argc < 2) {
		usage(stderr);
		return 2;
	}

	if (!strcmp(argv[1], "bootsel")) {
		if (reboot_bootsel() < 0) {
			fprintf(stderr, "fruitjamctl: reboot bootsel: %s\n", strerror(errno));
			return 1;
		}
		return 1;
	}

	if (maps_open(&maps) < 0)
		return 1;

	if (!strcmp(argv[1], "init")) {
		fruitjam_init(&maps);
		return 0;
	}
	if (!strcmp(argv[1], "status")) {
		print_status(&maps);
		return 0;
	}
	if (!strcmp(argv[1], "buttons")) {
		print_buttons(&maps);
		return 0;
	}
	if (!strcmp(argv[1], "led")) {
		if (require_arg(argc, argv) < 0)
			return 2;
		if (!strcmp(argv[2], "on"))
			gpio_output(&maps, FRUITJAM_LED_GPIO, 0);
		else if (!strcmp(argv[2], "off"))
			gpio_output(&maps, FRUITJAM_LED_GPIO, 1);
		else if (!strcmp(argv[2], "toggle"))
			gpio_output(&maps, FRUITJAM_LED_GPIO,
				    !gpio_out_read(&maps, FRUITJAM_LED_GPIO));
		else
			return usage(stderr), 2;
		return 0;
	}
	if (!strcmp(argv[1], "usb-power")) {
		if (require_arg(argc, argv) < 0)
			return 2;
		if (!strcmp(argv[2], "on"))
			gpio_output(&maps, FRUITJAM_USB_HOST_POWER_GPIO, 1);
		else if (!strcmp(argv[2], "off"))
			gpio_output(&maps, FRUITJAM_USB_HOST_POWER_GPIO, 0);
		else
			return usage(stderr), 2;
		return 0;
	}
	if (!strcmp(argv[1], "periph-reset")) {
		if (require_arg(argc, argv) < 0)
			return 2;
		if (!strcmp(argv[2], "assert"))
			gpio_output(&maps, FRUITJAM_PERIPH_RESET_GPIO, 0);
		else if (!strcmp(argv[2], "deassert"))
			gpio_output(&maps, FRUITJAM_PERIPH_RESET_GPIO, 1);
		else if (!strcmp(argv[2], "pulse")) {
			gpio_output(&maps, FRUITJAM_PERIPH_RESET_GPIO, 0);
			usleep(10000);
			gpio_output(&maps, FRUITJAM_PERIPH_RESET_GPIO, 1);
		} else {
			return usage(stderr), 2;
		}
		return 0;
	}

	usage(stderr);
	fprintf(stderr, "fruitjamctl: unknown command: %s\n", argv[1]);
	return 2;
}
