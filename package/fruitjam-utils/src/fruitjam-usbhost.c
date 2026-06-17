// SPDX-License-Identifier: MIT
/*
 * Tiny Fruit Jam USB host power/status helper.
 *
 * Fruit Jam routes USB host D+/D- through GPIO1/GPIO2 and controls host 5 V
 * power with GPIO11. This helper manages power and reports line-level
 * readiness for the future PIO USB HID bridge; it is not the protocol stack.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef GPIO_ROOT
#define GPIO_ROOT "/sys/class/gpio"
#endif
#ifndef BRIDGE_DEV
#define BRIDGE_DEV "/dev/fruitjam-usbhost"
#endif

#define GPIO_DP 1u
#define GPIO_DM 2u
#define GPIO_POWER 11u
#define POLL_US 100000
#define MAX_SECONDS 600u
#define DEFAULT_RESET_MS 50u
#define MAX_RESET_MS 1000u
#define POST_RESET_US 100000u
#define GPIO_PATH_MAX 192
#define BRIDGE_BUF_MAX 1024
#define RX_HEX_MAX 65

struct usbhost_status {
	int power;
	int dp;
	int dm;
	int pio_ready;
	int pio_configured;
	int packets;
	int tx_errors;
	int last_tx_result;
	int last_tx_len;
	int rx_attempts;
	int rx_errors;
	int last_rx_result;
	int last_rx_len;
	int last_rx_pid;
	char last_rx_hex[RX_HEX_MAX];
	const char *device;
	int present;
	bool kernel_bridge;
};

static const char *sysfs_stack_text =
	"sysfs line-state only; PIO USB host/HID report polling not implemented yet";
static const char *kernel_stack_text =
	"kernel bridge line-state; PIO USB host/HID report polling not implemented yet";
static const char *kernel_pio_stack_text =
	"kernel bridge line-state; PIO2 host program staged; HID report polling not implemented yet";

static void usage(FILE *out)
{
	fprintf(out,
		"usage: fruitjam-usbhost {status|json|on|off|reset [ms]|pio-init|tx-test|self-rx|sof-burst|in-token|setup-token-self-rx|setup-data-self-rx|setup-data-self-rx-noeop|setup-data-self-rx-cpu|setup-data-self-rx-drain|data-len-sweep|get-device-8|in-token-gated|get-device-8-gated|get-device-8-gated-cpu|get-device-8-combo|get-device-8-fast|get-device-8-tight|get-device-8-burst|get-device-8-stream|reset-get-device-8|reset-get-device-8-gated|reset-get-device-8-combo|reset-get-device-8-fast|reset-get-device-8-tight|reset-get-device-8-burst|reset-get-device-8-stream|wait [seconds]|monitor [seconds]}\n");
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

static int root_path(const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "%s/%s", GPIO_ROOT, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int gpio_path(unsigned int gpio, const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "%s/gpio%u/%s", GPIO_ROOT, gpio, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int gpio_export(unsigned int gpio)
{
	char path[GPIO_PATH_MAX];
	char text[12];
	int fd;
	ssize_t ret;
	int i;

	if (gpio_path(gpio, "value", path, sizeof(path)) == 0 &&
	    access(path, F_OK) == 0)
		return 0;
	if (root_path("export", path, sizeof(path)) < 0)
		return -1;
	fd = open(path, O_WRONLY);
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
	return -1;
}

static int gpio_direction(unsigned int gpio, const char *direction)
{
	char path[GPIO_PATH_MAX];

	if (gpio_export(gpio) < 0)
		return -1;
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
	char path[GPIO_PATH_MAX];

	if (gpio_direction(gpio, "out") < 0)
		return -1;
	if (gpio_path(gpio, "value", path, sizeof(path)) < 0)
		return -1;
	return write_file(path, value ? "1" : "0");
}

static int gpio_read(unsigned int gpio)
{
	char path[GPIO_PATH_MAX];
	char text[8];

	if (gpio_export(gpio) < 0)
		return -1;
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

static int bridge_read_raw(char *buf, size_t len)
{
	int fd;
	ssize_t ret;

	if (!len)
		return -1;
	fd = open(BRIDGE_DEV, O_RDONLY);
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
	return 0;
}

static int bridge_parse_int(const char *text, const char *key, int *value)
{
	size_t key_len = strlen(key);
	const char *p = text;
	char *end;
	long v;

	while (*p) {
		const char *line_end = strchr(p, '\n');
		size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

		if (line_len > key_len && !strncmp(p, key, key_len) &&
		    p[key_len] == ' ') {
			errno = 0;
			v = strtol(p + key_len + 1, &end, 0);
			if (errno || end == p + key_len + 1)
				return -1;
			*value = (int)v;
			return 0;
		}

		if (!line_end)
			break;
		p = line_end + 1;
	}

	return -1;
}

static int bridge_parse_string(const char *text, const char *key,
			       char *value, size_t value_len)
{
	size_t key_len = strlen(key);
	const char *p = text;

	if (!value_len)
		return -1;
	while (*p) {
		const char *line_end = strchr(p, '\n');
		size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

		if (line_len > key_len && !strncmp(p, key, key_len) &&
		    p[key_len] == ' ') {
			size_t n = line_len - key_len - 1;

			if (n >= value_len)
				n = value_len - 1;
			memcpy(value, p + key_len + 1, n);
			value[n] = '\0';
			return 0;
		}

		if (!line_end)
			break;
		p = line_end + 1;
	}

	value[0] = '\0';
	return -1;
}

static int bridge_read_status(struct usbhost_status *st)
{
	char text[BRIDGE_BUF_MAX];

	if (bridge_read_raw(text, sizeof(text)) < 0)
		return -1;
	if (bridge_parse_int(text, "power", &st->power) < 0 ||
	    bridge_parse_int(text, "dp", &st->dp) < 0 ||
	    bridge_parse_int(text, "dm", &st->dm) < 0)
		return -1;

	st->device = usb_state(st->power, st->dp, st->dm);
	st->present = st->power > 0 &&
		(!strcmp(st->device, "full-speed-device") ||
		 !strcmp(st->device, "low-speed-device"));
	if (bridge_parse_int(text, "pio_ready", &st->pio_ready) < 0)
		st->pio_ready = 0;
	if (bridge_parse_int(text, "pio_configured", &st->pio_configured) < 0)
		st->pio_configured = 0;
	if (bridge_parse_int(text, "packets", &st->packets) < 0)
		st->packets = 0;
	if (bridge_parse_int(text, "tx_errors", &st->tx_errors) < 0)
		st->tx_errors = 0;
	if (bridge_parse_int(text, "last_tx_result", &st->last_tx_result) < 0)
		st->last_tx_result = 0;
	if (bridge_parse_int(text, "last_tx_len", &st->last_tx_len) < 0)
		st->last_tx_len = 0;
	if (bridge_parse_int(text, "rx_attempts", &st->rx_attempts) < 0)
		st->rx_attempts = 0;
	if (bridge_parse_int(text, "rx_errors", &st->rx_errors) < 0)
		st->rx_errors = 0;
	if (bridge_parse_int(text, "last_rx_result", &st->last_rx_result) < 0)
		st->last_rx_result = 0;
	if (bridge_parse_int(text, "last_rx_len", &st->last_rx_len) < 0)
		st->last_rx_len = 0;
	if (bridge_parse_int(text, "last_rx_pid", &st->last_rx_pid) < 0)
		st->last_rx_pid = 0;
	if (bridge_parse_string(text, "last_rx_hex", st->last_rx_hex,
				sizeof(st->last_rx_hex)) < 0)
		st->last_rx_hex[0] = '\0';
	st->kernel_bridge = true;
	return 0;
}

static int bridge_write_command(const char *command)
{
	int fd;
	ssize_t ret;
	size_t len = strlen(command);

	fd = open(BRIDGE_DEV, O_WRONLY);
	if (fd < 0)
		return -1;
	ret = write(fd, command, len);
	close(fd);
	return ret == (ssize_t)len ? 0 : -1;
}

static void read_status(struct usbhost_status *st)
{
	if (bridge_read_status(st) == 0)
		return;

	st->power = gpio_read(GPIO_POWER);
	st->dp = gpio_read_input(GPIO_DP);
	st->dm = gpio_read_input(GPIO_DM);
	st->device = usb_state(st->power, st->dp, st->dm);
	st->present = st->power > 0 &&
		(!strcmp(st->device, "full-speed-device") ||
		 !strcmp(st->device, "low-speed-device"));
	st->pio_ready = 0;
	st->pio_configured = 0;
	st->packets = 0;
	st->tx_errors = 0;
	st->last_tx_result = 0;
	st->last_tx_len = 0;
	st->rx_attempts = 0;
	st->rx_errors = 0;
	st->last_rx_result = 0;
	st->last_rx_len = 0;
	st->last_rx_pid = 0;
	st->last_rx_hex[0] = '\0';
	st->kernel_bridge = false;
}

static void print_status_line(const struct usbhost_status *st)
{
	printf("usbhost power %s gpio%u=%d", st->power ? "on" : "off",
	       GPIO_POWER, st->power);
	if (st->dp >= 0 && st->dm >= 0)
		printf(" gpio%u(dp)=%d gpio%u(dm)=%d",
		       GPIO_DP, st->dp, GPIO_DM, st->dm);
	putchar('\n');
}

static void print_human(const struct usbhost_status *st)
{
	print_status_line(st);
	printf("usbhost device %s\n", st->device);
	printf("usbhost hid-ready %s\n", st->present ? "line-detected" : "no");
	printf("usbhost stack %s\n",
	       st->kernel_bridge ?
	       (st->pio_ready > 0 ? kernel_pio_stack_text : kernel_stack_text) :
	       sysfs_stack_text);
	if (st->kernel_bridge) {
		printf("usbhost pio-ready %s\n", st->pio_ready > 0 ? "yes" : "no");
		printf("usbhost pio-configured %s\n",
		       st->pio_configured > 0 ? "yes" : "no");
		printf("usbhost packets %d tx-errors %d last-tx-result %d len %d\n",
		       st->packets, st->tx_errors, st->last_tx_result,
		       st->last_tx_len);
		printf("usbhost rx-attempts %d rx-errors %d last-rx-result %d pid 0x%02x len %d\n",
		       st->rx_attempts, st->rx_errors, st->last_rx_result,
		       st->last_rx_pid, st->last_rx_len);
		if (st->last_rx_hex[0])
			printf("usbhost last-rx-hex %s\n", st->last_rx_hex);
	}
	puts("usbhost next pio-packet-io first=boot-protocol-keyboard");
}

static void print_json(const struct usbhost_status *st)
{
	printf("{\"ok\":%s,\"power\":%d,\"dp\":%d,\"dm\":%d,\"device\":\"%s\",",
	       st->power >= 0 ? "true" : "false",
	       st->power, st->dp, st->dm, st->device);
	printf("\"present\":%s,\"hid\":false,\"driver\":\"%s\",",
	       st->present ? "true" : "false",
	       st->kernel_bridge ? "kernel-line-state" : "sysfs-line-state");
	printf("\"stack\":\"%s\",",
	       st->kernel_bridge ?
	       (st->pio_ready > 0 ? kernel_pio_stack_text : kernel_stack_text) :
	       sysfs_stack_text);
	printf("\"pio_ready\":%s,", st->pio_ready > 0 ? "true" : "false");
	printf("\"pio_configured\":%s,", st->pio_configured > 0 ? "true" : "false");
	printf("\"packets\":%d,\"tx_errors\":%d,", st->packets, st->tx_errors);
	printf("\"last_tx_result\":%d,\"last_tx_len\":%d,",
	       st->last_tx_result, st->last_tx_len);
	printf("\"rx_attempts\":%d,\"rx_errors\":%d,",
	       st->rx_attempts, st->rx_errors);
	printf("\"last_rx_result\":%d,\"last_rx_len\":%d,",
	       st->last_rx_result, st->last_rx_len);
	printf("\"last_rx_pid\":%d,\"last_rx_hex\":\"%s\",",
	       st->last_rx_pid, st->last_rx_hex);
	printf("\"next\":\"pio-packet-io\",");
	printf("\"first_milestone\":\"boot-protocol-keyboard\"}\n");
}

static int bridge_action(const char *command, const char *label)
{
	struct usbhost_status st;
	int saved_errno;

	if (access(BRIDGE_DEV, W_OK) != 0) {
		fprintf(stderr, "fruitjam-usbhost: %s requires %s\n",
			label, BRIDGE_DEV);
		return 1;
	}
	if (bridge_write_command(command) < 0) {
		saved_errno = errno;
		fprintf(stderr, "fruitjam-usbhost: bridge %s failed: %s\n",
			label, strerror(saved_errno));
		read_status(&st);
		print_human(&st);
		return 1;
	}
	read_status(&st);
	print_human(&st);
	return st.power >= 0 ? 0 : 1;
}

static int parse_seconds(const char *s, unsigned int default_value,
			 unsigned int *seconds)
{
	char *end;
	unsigned long value;

	if (!s || !*s) {
		*seconds = default_value;
		return 0;
	}
	errno = 0;
	value = strtoul(s, &end, 10);
	if (errno || *end || value > MAX_SECONDS)
		return -1;
	*seconds = (unsigned int)value;
	return 0;
}

static int parse_reset_ms(const char *s, unsigned int *ms)
{
	char *end;
	unsigned long value;

	if (!s || !*s) {
		*ms = DEFAULT_RESET_MS;
		return 0;
	}
	errno = 0;
	value = strtoul(s, &end, 10);
	if (errno || *end || value < 10 || value > MAX_RESET_MS)
		return -1;
	*ms = (unsigned int)value;
	return 0;
}

static int bus_reset(unsigned int reset_ms)
{
	int ret = 0;

	if (gpio_write(GPIO_POWER, 1) < 0)
		ret = -1;
	usleep(POST_RESET_US);
	if (gpio_write(GPIO_DP, 0) < 0)
		ret = -1;
	if (gpio_write(GPIO_DM, 0) < 0)
		ret = -1;
	usleep(reset_ms * 1000u);
	if (gpio_input(GPIO_DP) < 0)
		ret = -1;
	if (gpio_input(GPIO_DM) < 0)
		ret = -1;
	usleep(POST_RESET_US);
	return ret;
}

static int wait_for_device(unsigned int seconds)
{
	struct usbhost_status st;
	unsigned int loops = seconds * 10u;
	unsigned int i;

	for (i = 0; i <= loops; i++) {
		read_status(&st);
		if (st.present) {
			print_human(&st);
			return 0;
		}
		if (i < loops)
			usleep(POLL_US);
	}
	print_human(&st);
	return 2;
}

static int monitor_device(unsigned int seconds)
{
	struct usbhost_status st;
	char prev[32] = "";
	unsigned int loops = seconds * 10u;
	unsigned int i;

	for (i = 0; i <= loops; i++) {
		read_status(&st);
		if (strcmp(prev, st.device)) {
			printf("t=%u00ms ", i);
			print_status_line(&st);
			printf("usbhost device %s\n", st.device);
			snprintf(prev, sizeof(prev), "%s", st.device);
		}
		if (i < loops)
			usleep(POLL_US);
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *cmd = argc > 1 ? argv[1] : "status";
	struct usbhost_status st;
	unsigned int seconds;
	unsigned int reset_ms;

	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
		usage(stdout);
		return 0;
	}
	if (!strcmp(cmd, "on")) {
		if (access(BRIDGE_DEV, W_OK) == 0) {
			if (bridge_write_command("on") < 0) {
				fprintf(stderr, "fruitjam-usbhost: bridge power on failed: %s\n",
					strerror(errno));
				return 1;
			}
		} else if (gpio_write(GPIO_POWER, 1) < 0) {
			fprintf(stderr, "fruitjam-usbhost: power on failed: %s\n",
				strerror(errno));
			return 1;
		}
		read_status(&st);
		print_human(&st);
		return st.power >= 0 ? 0 : 1;
	}
	if (!strcmp(cmd, "off")) {
		if (access(BRIDGE_DEV, W_OK) == 0) {
			if (bridge_write_command("off") < 0) {
				fprintf(stderr, "fruitjam-usbhost: bridge power off failed: %s\n",
					strerror(errno));
				return 1;
			}
		} else if (gpio_write(GPIO_POWER, 0) < 0) {
			fprintf(stderr, "fruitjam-usbhost: power off failed: %s\n",
				strerror(errno));
			return 1;
		}
		read_status(&st);
		print_human(&st);
		return st.power >= 0 ? 0 : 1;
	}
	if (!strcmp(cmd, "reset")) {
		if (parse_reset_ms(argc > 2 ? argv[2] : NULL, &reset_ms) < 0) {
			usage(stderr);
			return 1;
		}
		if (access(BRIDGE_DEV, W_OK) == 0) {
			char bridge_cmd[32];

			snprintf(bridge_cmd, sizeof(bridge_cmd), "reset %u", reset_ms);
			if (bridge_write_command(bridge_cmd) < 0) {
				fprintf(stderr, "fruitjam-usbhost: bridge USB bus reset failed: %s\n",
					strerror(errno));
				return 1;
			}
		} else if (bus_reset(reset_ms) < 0) {
			fprintf(stderr, "fruitjam-usbhost: USB bus reset failed: %s\n",
				strerror(errno));
			return 1;
		}
		printf("usbhost reset %ums\n", reset_ms);
		read_status(&st);
		print_human(&st);
		return st.power >= 0 ? 0 : 1;
	}
	if (!strcmp(cmd, "pio-init"))
		return bridge_action("pio-init", "PIO init");
	if (!strcmp(cmd, "tx-test"))
		return bridge_action("tx-test", "PIO TX test");
	if (!strcmp(cmd, "self-rx"))
		return bridge_action("self-rx", "PIO self-RX probe");
	if (!strcmp(cmd, "sof-burst"))
		return bridge_action("sof-burst", "PIO SOF burst");
	if (!strcmp(cmd, "in-token"))
		return bridge_action("in-token", "PIO IN token probe");
	if (!strcmp(cmd, "setup-token-self-rx"))
		return bridge_action("setup-token-self-rx",
				     "PIO SETUP token self-RX probe");
	if (!strcmp(cmd, "setup-data-self-rx"))
		return bridge_action("setup-data-self-rx",
				     "PIO SETUP DATA0 self-RX probe");
	if (!strcmp(cmd, "setup-data-self-rx-noeop"))
		return bridge_action("setup-data-self-rx-noeop",
				     "PIO SETUP DATA0 no-EOP-stop self-RX probe");
	if (!strcmp(cmd, "setup-data-self-rx-cpu"))
		return bridge_action("setup-data-self-rx-cpu",
				     "PIO CPU-TX SETUP DATA0 self-RX probe");
	if (!strcmp(cmd, "setup-data-self-rx-drain"))
		return bridge_action("setup-data-self-rx-drain",
				     "PIO DMA-TX live-drain SETUP DATA0 self-RX probe");
	if (!strcmp(cmd, "data-len-sweep"))
		return bridge_action("data-len-sweep",
				     "PIO DATA0 payload length self-RX sweep");
	if (!strcmp(cmd, "get-device-8"))
		return bridge_action("get-device-8", "PIO GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "in-token-gated"))
		return bridge_action("in-token-gated", "PIO gated IN token probe");
	if (!strcmp(cmd, "get-device-8-gated"))
		return bridge_action("get-device-8-gated",
				     "PIO gated GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-gated-cpu"))
		return bridge_action("get-device-8-gated-cpu",
				     "PIO CPU-TX gated GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-combo"))
		return bridge_action("get-device-8-combo",
				     "PIO combined SETUP GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-fast"))
		return bridge_action("get-device-8-fast",
				     "PIO fast gated GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-tight"))
		return bridge_action("get-device-8-tight",
				     "PIO tight SETUP GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-burst"))
		return bridge_action("get-device-8-burst",
				     "PIO burst SETUP/IN GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "get-device-8-stream"))
		return bridge_action("get-device-8-stream",
				     "PIO streamed SETUP/IN GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8"))
		return bridge_action("reset-get-device-8",
				     "PIO reset/SOF GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-gated"))
		return bridge_action("reset-get-device-8-gated",
				     "PIO reset/SOF gated GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-combo"))
		return bridge_action("reset-get-device-8-combo",
				     "PIO reset/SOF combined SETUP GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-fast"))
		return bridge_action("reset-get-device-8-fast",
				     "PIO reset/SOF fast gated GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-tight"))
		return bridge_action("reset-get-device-8-tight",
				     "PIO reset/SOF tight SETUP GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-burst"))
		return bridge_action("reset-get-device-8-burst",
				     "PIO reset/SOF burst SETUP/IN GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "reset-get-device-8-stream"))
		return bridge_action("reset-get-device-8-stream",
				     "PIO reset/SOF streamed SETUP/IN GET_DESCRIPTOR probe");
	if (!strcmp(cmd, "status")) {
		read_status(&st);
		print_human(&st);
		return st.power >= 0 ? 0 : 1;
	}
	if (!strcmp(cmd, "json")) {
		read_status(&st);
		print_json(&st);
		return st.power >= 0 ? 0 : 1;
	}
	if (!strcmp(cmd, "wait")) {
		if (parse_seconds(argc > 2 ? argv[2] : NULL, 5, &seconds) < 0) {
			usage(stderr);
			return 1;
		}
		return wait_for_device(seconds);
	}
	if (!strcmp(cmd, "monitor")) {
		if (parse_seconds(argc > 2 ? argv[2] : NULL, 10, &seconds) < 0) {
			usage(stderr);
			return 1;
		}
		return monitor_device(seconds);
	}

	usage(stderr);
	return 1;
}
