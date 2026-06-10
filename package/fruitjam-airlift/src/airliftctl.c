// SPDX-License-Identifier: MIT
#define _DEFAULT_SOURCE
/*
 * Fruit Jam AirLift bring-up helper.
 *
 * SPI bytes go through the kernel PL022/spidev path.  The board-specific
 * AirLift control pins live behind /dev/airlift-gpio because direct /dev/mem
 * GPIO writes are not reliable on the current no-MMU RP2350 Linux port.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BIT(n) (1u << (n))

#define AIRLIFT_GPIO_DEV_DEFAULT "/dev/airlift-gpio"
#define AIRLIFT_SPI_DEV_DEFAULT "/dev/spidev0.0"
#define AIRLIFT_SPI_HZ 8000000u
#define AIRLIFT_RESET_GPIO 22u
#define AIRLIFT_READY_GPIO 3u
#define AIRLIFT_IRQ_GPIO 23u
#define AIRLIFT_MISO_GPIO 28u
#define AIRLIFT_SCK_GPIO 30u
#define AIRLIFT_MOSI_GPIO 31u
#define AIRLIFT_CS_GPIO 46u

#define NINA_START 0xe0u
#define NINA_END 0xeeu
#define NINA_ERR 0xefu
#define NINA_REPLY 0x80u

#define CMD_SET_PASSPHRASE 0x11u
#define CMD_GET_STATUS 0x20u
#define CMD_GET_IPADDR 0x21u
#define CMD_GET_MACADDR 0x22u
#define CMD_SCAN_NETWORKS 0x27u
#define CMD_DATA_SENT_TCP 0x2au
#define CMD_AVAIL_DATA_TCP 0x2bu
#define CMD_START_CLIENT_TCP 0x2du
#define CMD_STOP_CLIENT_TCP 0x2eu
#define CMD_GET_CLIENT_STATE_TCP 0x2fu
#define CMD_REQ_HOST_BY_NAME 0x34u
#define CMD_GET_HOST_BY_NAME 0x35u
#define CMD_START_SCAN 0x36u
#define CMD_GET_FW_VERSION 0x37u
#define CMD_GET_SOCKET 0x3fu
#define CMD_SEND_DATA_TCP 0x44u
#define CMD_GET_DATABUF_TCP 0x45u

#define NINA_TCP_MODE 0u
#define NINA_NO_SOCKET 255u

#define WL_IDLE_STATUS 0
#define WL_NO_SSID_AVAIL 1
#define WL_SCAN_COMPLETED 2
#define WL_CONNECTED 3
#define WL_CONNECT_FAILED 4
#define WL_CONNECTION_LOST 5
#define WL_DISCONNECTED 6
#define WL_AP_LISTENING 7
#define WL_AP_CONNECTED 8
#define WL_AP_FAILED 9
#define WL_NO_MODULE 255

enum nina_tcp_state {
	CLOSED = 0,
	LISTEN = 1,
	SYN_SENT = 2,
	SYN_RCVD = 3,
	ESTABLISHED = 4,
	FIN_WAIT_1 = 5,
	FIN_WAIT_2 = 6,
	CLOSE_WAIT = 7,
	CLOSING = 8,
	LAST_ACK = 9,
	TIME_WAIT = 10,
};

struct response {
	uint8_t count;
	uint8_t len[16];
	uint8_t data[16][257];
};

struct gpio_state {
	int ready;
	int irq;
	int cs;
	int reset;
	char raw[128];
};

struct airlift {
	int gpio_fd;
	int spi_fd;
	const char *spi_path;
	uint32_t speed_hz;
	int verbose;
};

static struct response response_buf;
static uint8_t command_buf[768];

static void usage(FILE *out)
{
	fprintf(out,
		"Usage: airliftctl [--spidev PATH] [--verbose] <command> [args]\n"
		"\n"
		"Commands:\n"
		"  pins                  configure pins and print READY/BUSY/CS state\n"
		"  probe                 reset ESP32-C6, print firmware, MAC, status, IP\n"
		"  fw                    print firmware version\n"
		"  mac                   print ESP32-C6 MAC address\n"
		"  status                print WiFi connection status\n"
		"  ip                    print IP, netmask, and gateway\n"
		"  scan                  scan and print visible SSIDs\n"
		"  join SSID PASSPHRASE  join a WPA/WPA2 network and print assigned IP\n"
		"  tcp-get HOST [PATH]   fetch an HTTP path through the NINA TCP socket API\n"
		"  mqtt-pub HOST PORT TOPIC MESSAGE [CLIENTID]\n"
		"                        publish one MQTT 3.1.1 QoS 0 message over AirLift\n");
}

static long now_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (long)tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

static int gpio_command(struct airlift *air, const char *cmd)
{
	char buf[32];
	int len;
	ssize_t ret;

	len = snprintf(buf, sizeof(buf), "%s\n", cmd);
	if (len < 0 || (size_t)len >= sizeof(buf)) {
		fprintf(stderr, "airliftctl: GPIO command too long\n");
		return -1;
	}

	ret = write(air->gpio_fd, buf, len);
	if (ret != len) {
		fprintf(stderr, "airliftctl: write %s: %s\n", AIRLIFT_GPIO_DEV_DEFAULT,
			ret < 0 ? strerror(errno) : "short write");
		return -1;
	}
	return 0;
}

static int parse_gpio_state(struct gpio_state *state)
{
	char *line;
	char *saveptr = NULL;

	state->ready = -1;
	state->irq = -1;
	state->cs = -1;
	state->reset = -1;

	for (line = strtok_r(state->raw, "\n", &saveptr); line;
	     line = strtok_r(NULL, "\n", &saveptr)) {
		char key[16];
		int value;

		if (sscanf(line, "%15s %d", key, &value) != 2)
			continue;
		if (!strcmp(key, "ready"))
			state->ready = value;
		else if (!strcmp(key, "irq"))
			state->irq = value;
		else if (!strcmp(key, "busy") && state->ready < 0)
			state->ready = value;
		else if (!strcmp(key, "cs"))
			state->cs = value;
		else if (!strcmp(key, "reset"))
			state->reset = value;
	}

	if (state->ready < 0 || state->cs < 0 || state->reset < 0) {
		fprintf(stderr, "airliftctl: malformed %s state\n",
			AIRLIFT_GPIO_DEV_DEFAULT);
		return -1;
	}
	return 0;
}

static int read_gpio_state(struct airlift *air, struct gpio_state *state)
{
	ssize_t ret;

	memset(state, 0, sizeof(*state));
	if (lseek(air->gpio_fd, 0, SEEK_SET) < 0) {
		fprintf(stderr, "airliftctl: seek %s: %s\n",
			AIRLIFT_GPIO_DEV_DEFAULT, strerror(errno));
		return -1;
	}
	ret = read(air->gpio_fd, state->raw, sizeof(state->raw) - 1);
	if (ret < 0) {
		fprintf(stderr, "airliftctl: read %s: %s\n",
			AIRLIFT_GPIO_DEV_DEFAULT, strerror(errno));
		return -1;
	}
	state->raw[ret] = '\0';
	return parse_gpio_state(state);
}

static void airlift_reset(struct airlift *air)
{
	gpio_command(air, "reset");
}

static void print_pin_state(struct airlift *air)
{
	struct gpio_state state;

	if (read_gpio_state(air, &state) < 0)
		return;
	printf("spidev %s speed %u\n", air->spi_path ? air->spi_path : "(not open)",
	       air->speed_hz);
	printf("sck gpio%u spi\n", AIRLIFT_SCK_GPIO);
	printf("mosi gpio%u spi\n", AIRLIFT_MOSI_GPIO);
	printf("miso gpio%u spi\n", AIRLIFT_MISO_GPIO);
	printf("cs gpio%u %s\n", AIRLIFT_CS_GPIO, state.cs ? "selected" : "idle");
	printf("ready gpio%u %d\n", AIRLIFT_READY_GPIO, state.ready);
	if (state.irq >= 0)
		printf("irq gpio%u %d\n", AIRLIFT_IRQ_GPIO, state.irq);
	printf("reset gpio%u %s\n", AIRLIFT_RESET_GPIO,
	       state.reset ? "deasserted" : "asserted");
}

static int wait_ready_level(struct airlift *air, int level, int timeout_ms)
{
	long deadline = now_ms() + timeout_ms;
	struct gpio_state state;
	int last = -1;

	while (now_ms() < deadline) {
		if (read_gpio_state(air, &state) < 0)
			return -1;
		last = state.ready;
		if (state.ready == level)
			return 0;
		usleep(1000);
	}

	fprintf(stderr, "airliftctl: timeout waiting for READY gpio%u == %d (last %d)\n",
		AIRLIFT_READY_GPIO, level, last);
	return -1;
}

static void chip_select(struct airlift *air, int selected)
{
	gpio_command(air, selected ? "cs 1" : "cs 0");
}

static int spi_xfer(struct airlift *air, const uint8_t *tx, uint8_t *rx, size_t len)
{
	struct spi_ioc_transfer tr;
	int ret;

	memset(&tr, 0, sizeof(tr));
	tr.tx_buf = (uintptr_t)tx;
	tr.rx_buf = (uintptr_t)rx;
	tr.len = len;
	tr.speed_hz = air->speed_hz;
	tr.bits_per_word = 8;

	ret = ioctl(air->spi_fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 0)
		fprintf(stderr, "airliftctl: SPI_IOC_MESSAGE: %s\n", strerror(errno));
	return ret < 0 ? -1 : 0;
}

static int spi_write_bytes(struct airlift *air, const uint8_t *buf, size_t len)
{
	return spi_xfer(air, buf, NULL, len);
}

static int spi_read_byte(struct airlift *air, uint8_t *value)
{
	uint8_t tx = 0xff;
	uint8_t rx = 0xff;

	if (spi_xfer(air, &tx, &rx, 1) < 0)
		return -1;
	*value = rx;
	if (air->verbose)
		printf("rx 0x%02x\n", rx);
	return 0;
}

static int wait_select_ready(struct airlift *air)
{
	if (wait_ready_level(air, 0, 10000) < 0)
		return -1;

	chip_select(air, 1);
	if (wait_ready_level(air, 1, 1000) < 0) {
		chip_select(air, 0);
		return -1;
	}

	return 0;
}

static int send_command(struct airlift *air, uint8_t cmd,
			const uint8_t *params[], const size_t lens[], size_t nparams)
{
	uint8_t *buf = command_buf;
	size_t i, pos = 0;

	if (nparams > 8) {
		fprintf(stderr, "airliftctl: too many command params\n");
		return -1;
	}

	buf[pos++] = NINA_START;
	buf[pos++] = cmd & ~NINA_REPLY;
	buf[pos++] = (uint8_t)nparams;
	for (i = 0; i < nparams; i++) {
		if (lens[i] > 255 || pos + 1 + lens[i] + 1 > sizeof(command_buf)) {
			fprintf(stderr, "airliftctl: command param too large\n");
			return -1;
		}
		buf[pos++] = (uint8_t)lens[i];
		memcpy(&buf[pos], params[i], lens[i]);
		pos += lens[i];
	}
	buf[pos++] = NINA_END;
	while (pos % 4)
		buf[pos++] = 0;

	if (air->verbose) {
		printf("tx cmd 0x%02x len %lu\n", cmd, (unsigned long)pos);
	}

	if (wait_select_ready(air) < 0)
		return -1;
	if (spi_write_bytes(air, buf, pos) < 0) {
		chip_select(air, 0);
		return -1;
	}
	chip_select(air, 0);
	return 0;
}

/* NINA SPI sendBuffer lengths and sendParam(uint16_t) values are MSB first. */
static void put_be16(uint8_t *buf, uint16_t value)
{
	buf[0] = value >> 8;
	buf[1] = value & 0xffu;
}

static uint16_t get_be16(const uint8_t *buf)
{
	return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static void put_le16(uint8_t *buf, uint16_t value)
{
	buf[0] = value & 0xffu;
	buf[1] = value >> 8;
}

static uint16_t get_le16(const uint8_t *buf)
{
	return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static int send_command16(struct airlift *air, uint8_t cmd,
			  const uint8_t *params[], const size_t lens[], size_t nparams)
{
	uint8_t *buf = command_buf;
	size_t i, pos = 0;

	if (nparams > 8) {
		fprintf(stderr, "airliftctl: too many command params\n");
		return -1;
	}

	buf[pos++] = NINA_START;
	buf[pos++] = cmd & ~NINA_REPLY;
	buf[pos++] = (uint8_t)nparams;
	for (i = 0; i < nparams; i++) {
		if (lens[i] > 65535 || pos + 2 + lens[i] + 1 > sizeof(command_buf)) {
			fprintf(stderr, "airliftctl: command param too large\n");
			return -1;
		}
		put_be16(&buf[pos], (uint16_t)lens[i]);
		pos += 2;
		memcpy(&buf[pos], params[i], lens[i]);
		pos += lens[i];
	}
	buf[pos++] = NINA_END;
	while (pos % 4)
		buf[pos++] = 0;

	if (air->verbose)
		printf("tx data cmd 0x%02x len %lu\n", cmd, (unsigned long)pos);

	if (wait_select_ready(air) < 0)
		return -1;
	if (spi_write_bytes(air, buf, pos) < 0) {
		chip_select(air, 0);
		return -1;
	}
	chip_select(air, 0);
	return 0;
}

static int wait_spi_char(struct airlift *air, uint8_t desired)
{
	uint8_t value;
	int i;

	for (i = 0; i < 10; i++) {
		if (spi_read_byte(air, &value) < 0)
			return -1;
		if (value == NINA_ERR) {
			fprintf(stderr, "airliftctl: ESP32-C6 returned NINA error byte\n");
			return -1;
		}
		if (value == desired)
			return 0;
		usleep(10000);
	}

	fprintf(stderr, "airliftctl: expected SPI byte 0x%02x\n", desired);
	return -1;
}

static int read_checked_byte(struct airlift *air, uint8_t expected)
{
	uint8_t value;

	if (spi_read_byte(air, &value) < 0)
		return -1;
	if (value != expected) {
		fprintf(stderr, "airliftctl: expected 0x%02x, got 0x%02x\n", expected, value);
		return -1;
	}
	return 0;
}

static int read_response(struct airlift *air, uint8_t cmd, int expected_count,
			 struct response *resp)
{
	uint8_t value;
	size_t i, j;

	memset(resp, 0, sizeof(*resp));

	if (wait_select_ready(air) < 0)
		return -1;

	if (wait_spi_char(air, NINA_START) < 0)
		goto fail;
	if (read_checked_byte(air, cmd | NINA_REPLY) < 0)
		goto fail;
	if (spi_read_byte(air, &value) < 0)
		goto fail;

	resp->count = value;
	if (expected_count >= 0 && resp->count != (uint8_t)expected_count) {
		fprintf(stderr, "airliftctl: expected %d response params, got %u\n",
			expected_count, resp->count);
		goto fail;
	}
	if (resp->count > ARRAY_SIZE(resp->len)) {
		fprintf(stderr, "airliftctl: too many response params: %u\n", resp->count);
		goto fail;
	}

	for (i = 0; i < resp->count; i++) {
		if (spi_read_byte(air, &resp->len[i]) < 0)
			goto fail;
		for (j = 0; j < resp->len[i]; j++) {
			if (spi_read_byte(air, &resp->data[i][j]) < 0)
				goto fail;
		}
		resp->data[i][resp->len[i]] = 0;
	}

	if (read_checked_byte(air, NINA_END) < 0)
		goto fail;

	chip_select(air, 0);
	return 0;

fail:
	chip_select(air, 0);
	return -1;
}

static int read_response_data16(struct airlift *air, uint8_t cmd, uint8_t *data,
				uint16_t *len)
{
	uint8_t value;
	uint8_t len_bytes[2];
	uint16_t got;
	size_t i;

	if (wait_select_ready(air) < 0)
		return -1;

	if (wait_spi_char(air, NINA_START) < 0)
		goto fail;
	if (read_checked_byte(air, cmd | NINA_REPLY) < 0)
		goto fail;
	if (spi_read_byte(air, &value) < 0)
		goto fail;
	if (value != 1) {
		fprintf(stderr, "airliftctl: expected 1 data response param, got %u\n", value);
		goto fail;
	}
	if (spi_read_byte(air, &len_bytes[0]) < 0 ||
	    spi_read_byte(air, &len_bytes[1]) < 0)
		goto fail;
	got = get_be16(len_bytes);
	if (got > *len) {
		fprintf(stderr, "airliftctl: data response too large: %u\n", got);
		goto fail;
	}

	for (i = 0; i < got; i++) {
		if (spi_read_byte(air, &data[i]) < 0)
			goto fail;
	}

	if (read_checked_byte(air, NINA_END) < 0)
		goto fail;

	chip_select(air, 0);
	*len = got;
	return 0;

fail:
	chip_select(air, 0);
	return -1;
}

static int nina_command(struct airlift *air, uint8_t cmd,
			const uint8_t *params[], const size_t lens[], size_t nparams,
			int expected_count, struct response *resp)
{
	if (send_command(air, cmd, params, lens, nparams) < 0)
		return -1;
	return read_response(air, cmd, expected_count, resp);
}

static int nina_no_param(struct airlift *air, uint8_t cmd, int expected_count,
			 struct response *resp)
{
	return nina_command(air, cmd, NULL, NULL, 0, expected_count, resp);
}

static int nina_command16_response8(struct airlift *air, uint8_t cmd,
				    const uint8_t *params[], const size_t lens[],
				    size_t nparams, int expected_count,
				    struct response *resp)
{
	if (send_command16(air, cmd, params, lens, nparams) < 0)
		return -1;
	return read_response(air, cmd, expected_count, resp);
}

static int nina_command16_response16(struct airlift *air, uint8_t cmd,
				     const uint8_t *params[], const size_t lens[],
				     size_t nparams, uint8_t *data, uint16_t *len)
{
	if (send_command16(air, cmd, params, lens, nparams) < 0)
		return -1;
	return read_response_data16(air, cmd, data, len);
}

static const char *status_name(unsigned int status)
{
	switch (status) {
	case WL_IDLE_STATUS:
		return "idle";
	case WL_NO_SSID_AVAIL:
		return "no-ssid";
	case WL_SCAN_COMPLETED:
		return "scan-completed";
	case WL_CONNECTED:
		return "connected";
	case WL_CONNECT_FAILED:
		return "connect-failed";
	case WL_CONNECTION_LOST:
		return "connection-lost";
	case WL_DISCONNECTED:
		return "disconnected";
	case WL_AP_LISTENING:
		return "ap-listening";
	case WL_AP_CONNECTED:
		return "ap-connected";
	case WL_AP_FAILED:
		return "ap-failed";
	case WL_NO_MODULE:
		return "no-module";
	default:
		return "unknown";
	}
}

static int get_status(struct airlift *air, unsigned int *status)
{
	struct response *resp = &response_buf;

	if (nina_no_param(air, CMD_GET_STATUS, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1) {
		fprintf(stderr, "airliftctl: empty status response\n");
		return -1;
	}
	*status = resp->data[0][0];
	return 0;
}

static int print_fw(struct airlift *air)
{
	struct response *resp = &response_buf;

	if (nina_no_param(air, CMD_GET_FW_VERSION, 1, resp) < 0)
		return -1;
	printf("firmware %s\n", (char *)resp->data[0]);
	return 0;
}

static int print_mac(struct airlift *air)
{
	static const uint8_t dummy = 0xff;
	const uint8_t *params[] = { &dummy };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_GET_MACADDR, params, lens, 1, 1, resp) < 0)
		return -1;
	if (resp->len[0] != 6) {
		fprintf(stderr, "airliftctl: MAC response length %u\n", resp->len[0]);
		return -1;
	}
	printf("mac %02x:%02x:%02x:%02x:%02x:%02x\n",
	       resp->data[0][5], resp->data[0][4], resp->data[0][3],
	       resp->data[0][2], resp->data[0][1], resp->data[0][0]);
	return 0;
}

static int print_status(struct airlift *air)
{
	unsigned int status;

	if (get_status(air, &status) < 0)
		return -1;
	printf("status %u %s\n", status, status_name(status));
	return 0;
}

static void print_ip_bytes(const char *name, const uint8_t *ip)
{
	printf("%s %u.%u.%u.%u\n", name, ip[0], ip[1], ip[2], ip[3]);
}

static int print_ip(struct airlift *air)
{
	static const uint8_t dummy = 0xff;
	const uint8_t *params[] = { &dummy };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_GET_IPADDR, params, lens, 1, 3, resp) < 0)
		return -1;
	if (resp->len[0] < 4 || resp->len[1] < 4 || resp->len[2] < 4) {
		fprintf(stderr, "airliftctl: short IP response\n");
		return -1;
	}
	print_ip_bytes("ip", resp->data[0]);
	print_ip_bytes("netmask", resp->data[1]);
	print_ip_bytes("gateway", resp->data[2]);
	return 0;
}

static int start_scan(struct airlift *air)
{
	struct response *resp = &response_buf;

	if (nina_no_param(air, CMD_START_SCAN, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1 || resp->data[0][0] != 1) {
		fprintf(stderr, "airliftctl: scan start failed\n");
		return -1;
	}
	return 0;
}

static int print_scan(struct airlift *air)
{
	struct response *resp = &response_buf;
	int attempt;
	size_t i;

	if (start_scan(air) < 0)
		return -1;

	for (attempt = 0; attempt < 10; attempt++) {
		sleep(2);
		if (nina_no_param(air, CMD_SCAN_NETWORKS, -1, resp) < 0)
			return -1;
		if (resp->count > 0)
			break;
	}

	printf("networks %u\n", resp->count);
	for (i = 0; i < resp->count; i++)
		printf("%lu %s\n", (unsigned long)i, (char *)resp->data[i]);
	return resp->count ? 0 : 1;
}

static int join_network(struct airlift *air, const char *ssid, const char *pass)
{
	const uint8_t *params[] = { (const uint8_t *)ssid, (const uint8_t *)pass };
	const size_t lens[] = { strlen(ssid), strlen(pass) };
	struct response *resp = &response_buf;
	unsigned int status = 0;
	unsigned int last_status = 256;
	int i;

	if (lens[0] == 0 || lens[0] > 32 || lens[1] < 8 || lens[1] > 64) {
		fprintf(stderr, "airliftctl: SSID must be 1..32 bytes and passphrase 8..64 bytes\n");
		return -1;
	}

	if (nina_command(air, CMD_SET_PASSPHRASE, params, lens, 2, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1 || resp->data[0][0] != 1) {
		fprintf(stderr, "airliftctl: join command was not acknowledged\n");
		return -1;
	}

	for (i = 0; i < 300; i++) {
		if (get_status(air, &status) < 0)
			return -1;
		if (status != last_status) {
			printf("status %u %s\n", status, status_name(status));
			fflush(stdout);
			last_status = status;
		}
		if (status == WL_CONNECTED) {
			usleep(500000);
			return print_ip(air);
		}
		if (status == WL_CONNECT_FAILED)
			return -1;
		usleep(100000);
	}

	fprintf(stderr, "airliftctl: timed out waiting for WiFi connection\n");
	return -1;
}

static int resolve_host(struct airlift *air, const char *host, uint8_t ip[4])
{
	const uint8_t *params[] = { (const uint8_t *)host };
	const size_t lens[] = { strlen(host) };
	struct response *resp = &response_buf;

	if (lens[0] == 0 || lens[0] > 255) {
		fprintf(stderr, "airliftctl: host name must be 1..255 bytes\n");
		return -1;
	}

	if (nina_command(air, CMD_REQ_HOST_BY_NAME, params, lens, 1, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1 || resp->data[0][0] != 1) {
		fprintf(stderr, "airliftctl: DNS request was not acknowledged\n");
		return -1;
	}
	if (nina_no_param(air, CMD_GET_HOST_BY_NAME, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 4) {
		fprintf(stderr, "airliftctl: DNS response was too short\n");
		return -1;
	}
	memcpy(ip, resp->data[0], 4);
	if (ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255) {
		fprintf(stderr, "airliftctl: DNS lookup failed for %s\n", host);
		return -1;
	}
	return 0;
}

static int tcp_start_client_ip(struct airlift *air, const uint8_t ip[4], uint16_t port,
			       uint8_t sock)
{
	uint8_t port_be[2];
	uint8_t mode = NINA_TCP_MODE;
	const uint8_t *params[] = { ip, port_be, &sock, &mode };
	const size_t lens[] = { 4, sizeof(port_be), 1, 1 };
	struct response *resp = &response_buf;

	put_be16(port_be, port);

	if (nina_command(air, CMD_START_CLIENT_TCP, params, lens, ARRAY_SIZE(params),
			 -1, resp) < 0)
		return -1;
	if (resp->count > 0 && (resp->len[0] < 1 || resp->data[0][0] == 0)) {
		fprintf(stderr, "airliftctl: TCP connect was not acknowledged\n");
		return -1;
	}
	return 0;
}

static int tcp_get_socket(struct airlift *air, uint8_t *sock)
{
	struct response *resp = &response_buf;

	if (nina_no_param(air, CMD_GET_SOCKET, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1)
		return -1;
	*sock = resp->data[0][0];
	if (*sock == NINA_NO_SOCKET) {
		fprintf(stderr, "airliftctl: no NINA sockets available\n");
		return -1;
	}
	return 0;
}

static int tcp_stop_client(struct airlift *air, uint8_t sock)
{
	const uint8_t *params[] = { &sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_STOP_CLIENT_TCP, params, lens, 1, 1, resp) < 0)
		return -1;
	return 0;
}

static int tcp_client_state(struct airlift *air, uint8_t sock, uint8_t *state)
{
	const uint8_t *params[] = { &sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_GET_CLIENT_STATE_TCP, params, lens, 1, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1)
		return -1;
	*state = resp->data[0][0];
	return 0;
}

static int tcp_wait_connected(struct airlift *air, uint8_t sock)
{
	uint8_t state = 0;
	int i;

	for (i = 0; i < 50; i++) {
		if (tcp_client_state(air, sock, &state) < 0)
			return -1;
		if (state == ESTABLISHED)
			return 0;
		if (state == CLOSED && i > 2)
			break;
		usleep(100000);
	}

	fprintf(stderr, "airliftctl: TCP socket %u did not connect, state %u\n",
		sock, state);
	return -1;
}

static int tcp_avail(struct airlift *air, uint8_t sock, uint16_t *avail)
{
	const uint8_t *params[] = { &sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_AVAIL_DATA_TCP, params, lens, 1, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 2)
		return -1;
	*avail = get_le16(resp->data[0]);
	return 0;
}

static int tcp_send(struct airlift *air, uint8_t sock, const uint8_t *data, uint16_t len)
{
	const uint8_t *params[] = { &sock, data };
	const size_t lens[] = { 1, len };
	struct response *resp = &response_buf;
	uint16_t sent;

	if (nina_command16_response8(air, CMD_SEND_DATA_TCP, params, lens, 2, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 2)
		return -1;
	sent = get_le16(resp->data[0]);
	if (sent != len) {
		fprintf(stderr, "airliftctl: TCP sent %u of %u bytes\n", sent, len);
		return -1;
	}
	return 0;
}

static int tcp_check_sent(struct airlift *air, uint8_t sock)
{
	const uint8_t *params[] = { &sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;
	int i;

	for (i = 0; i < 25; i++) {
		if (nina_command(air, CMD_DATA_SENT_TCP, params, lens, 1, 1, resp) < 0)
			return -1;
		if (resp->len[0] >= 1 && resp->data[0][0])
			return 0;
		usleep(100000);
	}
	fprintf(stderr, "airliftctl: timed out waiting for TCP send completion\n");
	return -1;
}

static int tcp_read_buf(struct airlift *air, uint8_t sock, uint8_t *data, uint16_t *len)
{
	uint8_t want[2];
	const uint8_t *params[] = { &sock, want };
	const size_t lens[] = { 1, sizeof(want) };

	put_le16(want, *len);
	return nina_command16_response16(air, CMD_GET_DATABUF_TCP, params, lens, 2,
					 data, len);
}

static int mqtt_put_remaining_length(uint8_t *buf, size_t cap, size_t *pos,
				     size_t len)
{
	do {
		uint8_t byte = (uint8_t)(len % 128);

		if (*pos >= cap)
			return -1;
		len /= 128;
		if (len)
			byte |= 0x80;
		buf[(*pos)++] = byte;
	} while (len);
	return 0;
}

static int mqtt_put_string(uint8_t *buf, size_t cap, size_t *pos, const char *s)
{
	size_t len = strlen(s);

	if (len > 65535 || *pos + 2 + len > cap)
		return -1;
	buf[(*pos)++] = (uint8_t)(len >> 8);
	buf[(*pos)++] = (uint8_t)len;
	memcpy(buf + *pos, s, len);
	*pos += len;
	return 0;
}

static int tcp_read_exact(struct airlift *air, uint8_t sock, uint8_t *data,
			  uint16_t want, long timeout_ms)
{
	uint16_t got = 0;
	long deadline = now_ms() + timeout_ms;

	while (got < want && now_ms() < deadline) {
		uint16_t avail = 0;
		uint16_t len;

		if (tcp_avail(air, sock, &avail) < 0)
			return -1;
		if (!avail) {
			usleep(100000);
			continue;
		}
		len = want - got;
		if (len > avail)
			len = avail;
		if (tcp_read_buf(air, sock, data + got, &len) < 0)
			return -1;
		got += len;
	}

	return got == want ? 0 : -1;
}

static int mqtt_send_connect(struct airlift *air, uint8_t sock,
			     const char *client_id)
{
	uint8_t pkt[256];
	uint8_t resp[4];
	size_t pos = 0;
	size_t rem_pos;
	size_t payload_start;
	size_t rem_len;

	pkt[pos++] = 0x10;
	rem_pos = pos++;
	if (mqtt_put_string(pkt, sizeof(pkt), &pos, "MQTT") < 0)
		return -1;
	pkt[pos++] = 4;
	pkt[pos++] = 2;
	pkt[pos++] = 0;
	pkt[pos++] = 60;
	payload_start = pos;
	if (mqtt_put_string(pkt, sizeof(pkt), &pos, client_id) < 0)
		return -1;
	rem_len = pos - payload_start + 10;
	if (rem_len > 127)
		return -1;
	pkt[rem_pos] = (uint8_t)rem_len;

	if (tcp_send(air, sock, pkt, (uint16_t)pos) < 0)
		return -1;
	if (tcp_check_sent(air, sock) < 0)
		return -1;
	if (tcp_read_exact(air, sock, resp, sizeof(resp), 8000) < 0) {
		fprintf(stderr, "airliftctl: MQTT CONNACK timeout\n");
		return -1;
	}
	if (resp[0] != 0x20 || resp[1] != 0x02 || resp[2] != 0x00 || resp[3] != 0x00) {
		fprintf(stderr, "airliftctl: MQTT connect refused (%02x %02x %02x %02x)\n",
			resp[0], resp[1], resp[2], resp[3]);
		return -1;
	}
	return 0;
}

static int mqtt_send_publish(struct airlift *air, uint8_t sock, const char *topic,
			     const char *message)
{
	uint8_t pkt[768];
	size_t topic_len = strlen(topic);
	size_t msg_len = strlen(message);
	size_t rem_len = 2 + topic_len + msg_len;
	size_t pos = 0;

	if (topic_len > 65535 || msg_len > 512) {
		fprintf(stderr, "airliftctl: MQTT topic/message too large\n");
		return -1;
	}

	pkt[pos++] = 0x30;
	if (mqtt_put_remaining_length(pkt, sizeof(pkt), &pos, rem_len) < 0)
		return -1;
	if (mqtt_put_string(pkt, sizeof(pkt), &pos, topic) < 0)
		return -1;
	if (pos + msg_len > sizeof(pkt))
		return -1;
	memcpy(pkt + pos, message, msg_len);
	pos += msg_len;

	if (tcp_send(air, sock, pkt, (uint16_t)pos) < 0)
		return -1;
	return tcp_check_sent(air, sock);
}

static int mqtt_publish(struct airlift *air, const char *host, uint16_t port,
			const char *topic, const char *message,
			const char *client_id)
{
	uint8_t sock = NINA_NO_SOCKET;
	uint8_t disconnect[] = { 0xe0, 0x00 };
	uint8_t ip[4];
	int ret = -1;

	if (resolve_host(air, host, ip) < 0)
		return -1;
	if (tcp_get_socket(air, &sock) < 0)
		return -1;

	if (tcp_start_client_ip(air, ip, port, sock) < 0)
		goto out;
	if (tcp_wait_connected(air, sock) < 0)
		goto out;
	if (mqtt_send_connect(air, sock, client_id) < 0)
		goto out;
	if (mqtt_send_publish(air, sock, topic, message) < 0)
		goto out;
	tcp_send(air, sock, disconnect, sizeof(disconnect));
	tcp_check_sent(air, sock);
	printf("mqtt published %s:%u %s\n", host, port, topic);
	ret = 0;

out:
	tcp_stop_client(air, sock);
	return ret;
}

static int tcp_get(struct airlift *air, const char *host, const char *path)
{
	uint8_t sock = NINA_NO_SOCKET;
	uint8_t buf[256];
	uint8_t ip[4];
	char request[384];
	uint16_t request_len;
	long quiet_deadline;

	if (!path || !*path)
		path = "/";
	if (path[0] != '/') {
		fprintf(stderr, "airliftctl: path must start with '/'\n");
		return -1;
	}

	request_len = (uint16_t)snprintf(request, sizeof(request),
					"GET %s HTTP/1.0\r\n"
					"Host: %s\r\n"
					"Connection: close\r\n"
					"\r\n",
					path, host);
	if (request_len >= sizeof(request)) {
		fprintf(stderr, "airliftctl: HTTP request too large\n");
		return -1;
	}

	if (resolve_host(air, host, ip) < 0)
		return -1;
	if (tcp_get_socket(air, &sock) < 0)
		return -1;

	if (tcp_start_client_ip(air, ip, 80, sock) < 0)
		return -1;
	if (tcp_wait_connected(air, sock) < 0)
		goto fail;
	if (tcp_send(air, sock, (const uint8_t *)request, request_len) < 0)
		goto fail;
	if (tcp_check_sent(air, sock) < 0)
		goto fail;

	quiet_deadline = now_ms() + 20000;
	while (now_ms() < quiet_deadline) {
		uint16_t avail = 0;
		uint8_t state = 0;

		if (tcp_avail(air, sock, &avail) < 0)
			goto fail;
		if (avail) {
			uint16_t len = avail > sizeof(buf) ? sizeof(buf) : avail;

			if (tcp_read_buf(air, sock, buf, &len) < 0)
				goto fail;
			if (len) {
				fwrite(buf, 1, len, stdout);
				fflush(stdout);
				quiet_deadline = now_ms() + 2000;
			}
			continue;
		}

		if (tcp_client_state(air, sock, &state) == 0 && state == 0)
			break;
		usleep(100000);
	}

	tcp_stop_client(air, sock);
	return 0;

fail:
	tcp_stop_client(air, sock);
	return -1;
}

static int find_spidev(char *buf, size_t len)
{
	DIR *dir;
	struct dirent *de;

	dir = opendir("/dev");
	if (!dir)
		return -1;
	while ((de = readdir(dir)) != NULL) {
		if (!strncmp(de->d_name, "spidev", 6)) {
			size_t name_len = strlen(de->d_name);

			if (name_len + 6 >= len)
				continue;
			memcpy(buf, "/dev/", 5);
			memcpy(buf + 5, de->d_name, name_len + 1);
			closedir(dir);
			return 0;
		}
	}
	closedir(dir);
	return -1;
}

static int open_spi(struct airlift *air, const char *requested_path)
{
	static char found[64];
	uint8_t mode = SPI_MODE_0;
	uint8_t bits = 8;

	if (requested_path)
		air->spi_path = requested_path;
	else if (find_spidev(found, sizeof(found)) == 0)
		air->spi_path = found;
	else
		air->spi_path = AIRLIFT_SPI_DEV_DEFAULT;

	air->spi_fd = open(air->spi_path, O_RDWR);
	if (air->spi_fd < 0) {
		fprintf(stderr, "airliftctl: open %s: %s\n", air->spi_path, strerror(errno));
		return -1;
	}

	if (ioctl(air->spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
	    ioctl(air->spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
	    ioctl(air->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &air->speed_hz) < 0) {
		fprintf(stderr, "airliftctl: configure %s: %s\n", air->spi_path, strerror(errno));
		return -1;
	}
	return 0;
}

static int airlift_open(struct airlift *air, const char *spidev, int verbose)
{
	memset(air, 0, sizeof(*air));
	air->gpio_fd = -1;
	air->spi_fd = -1;
	air->speed_hz = AIRLIFT_SPI_HZ;
	air->verbose = verbose;

	air->gpio_fd = open(AIRLIFT_GPIO_DEV_DEFAULT, O_RDWR);
	if (air->gpio_fd < 0) {
		fprintf(stderr, "airliftctl: open %s: %s\n",
			AIRLIFT_GPIO_DEV_DEFAULT, strerror(errno));
		return -1;
	}
	if (gpio_command(air, "init") < 0)
		return -1;
	return open_spi(air, spidev);
}

int main(int argc, char **argv)
{
	const char *spidev = NULL;
	struct airlift air;
	int verbose = 0;
	int arg = 1;
	int ret = 0;

	while (arg < argc && !strncmp(argv[arg], "--", 2)) {
		if (!strcmp(argv[arg], "--verbose")) {
			verbose = 1;
			arg++;
		} else if (!strcmp(argv[arg], "--spidev") && arg + 1 < argc) {
			spidev = argv[arg + 1];
			arg += 2;
		} else {
			usage(stderr);
			return 2;
		}
	}

	if (arg >= argc) {
		usage(stderr);
		return 2;
	}

	if (airlift_open(&air, spidev, verbose) < 0)
		return 1;

	if (!strcmp(argv[arg], "pins")) {
		print_pin_state(&air);
		return 0;
	}

	if (!strcmp(argv[arg], "probe"))
		airlift_reset(&air);

	if (!strcmp(argv[arg], "probe")) {
		print_pin_state(&air);
		ret |= print_fw(&air);
		ret |= print_mac(&air);
		ret |= print_status(&air);
		ret |= print_ip(&air);
		return ret ? 1 : 0;
	}
	if (!strcmp(argv[arg], "fw"))
		return print_fw(&air) < 0 ? 1 : 0;
	if (!strcmp(argv[arg], "mac"))
		return print_mac(&air) < 0 ? 1 : 0;
	if (!strcmp(argv[arg], "status"))
		return print_status(&air) < 0 ? 1 : 0;
	if (!strcmp(argv[arg], "ip"))
		return print_ip(&air) < 0 ? 1 : 0;
	if (!strcmp(argv[arg], "scan"))
		return print_scan(&air) < 0 ? 1 : 0;
	if (!strcmp(argv[arg], "join")) {
		if (arg + 2 >= argc) {
			usage(stderr);
			return 2;
		}
		return join_network(&air, argv[arg + 1], argv[arg + 2]) < 0 ? 1 : 0;
	}
	if (!strcmp(argv[arg], "tcp-get")) {
		if (arg + 1 >= argc) {
			usage(stderr);
			return 2;
		}
		return tcp_get(&air, argv[arg + 1], arg + 2 < argc ? argv[arg + 2] : "/") < 0 ? 1 : 0;
	}
	if (!strcmp(argv[arg], "mqtt-pub")) {
		unsigned long port;
		const char *client_id = "fruitjam-rp2350";

		if (arg + 4 >= argc) {
			usage(stderr);
			return 2;
		}
		port = strtoul(argv[arg + 2], NULL, 10);
		if (!port || port > 65535) {
			fprintf(stderr, "airliftctl: invalid MQTT port\n");
			return 2;
		}
		if (arg + 5 < argc)
			client_id = argv[arg + 5];
		return mqtt_publish(&air, argv[arg + 1], (uint16_t)port,
				    argv[arg + 3], argv[arg + 4],
				    client_id) < 0 ? 1 : 0;
	}

	usage(stderr);
	fprintf(stderr, "airliftctl: unknown command: %s\n", argv[arg]);
	return 2;
}
