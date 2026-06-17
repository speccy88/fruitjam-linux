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
#include <linux/reboot.h>
#include <linux/spi/spidev.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BIT(n) (1u << (n))
#if defined(__GNUC__)
#define MAYBE_UNUSED __attribute__((unused))
#else
#define MAYBE_UNUSED
#endif

#define AIRLIFT_GPIO_DEV_DEFAULT "/dev/airlift-gpio"
#define AIRLIFT_SPI_DEV_DEFAULT "/dev/spidev0.0"
#define USBHOST_DEV "/dev/fruitjam-usbhost"
#define USBHOST_BIN "/usr/bin/fruitjam-usbhost"
#define AIRLIFT_HEARTBEAT_PATH "/run/fruitjam-airlift-inbound.heartbeat"
#define AIRLIFT_HEARTBEAT_INTERVAL_MS 5000L
#define AIRLIFT_SPI_HZ 8000000u
#define AIRLIFT_RESET_GPIO 22u
#define AIRLIFT_READY_GPIO 3u
#define USBHOST_RESET_MS 50u
#define USBHOST_POST_RESET_US 100000u
#define USBHOST_KBD_WEB_SECONDS "3"
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
#define CMD_START_SERVER_TCP 0x28u
#define CMD_GET_STATE_TCP 0x29u
#define CMD_DATA_SENT_TCP 0x2au
#define CMD_AVAIL_DATA_TCP 0x2bu
#define CMD_START_CLIENT_TCP 0x2du
#define CMD_STOP_CLIENT_TCP 0x2eu
#define CMD_GET_CLIENT_STATE_TCP 0x2fu
#define CMD_REQ_HOST_BY_NAME 0x34u
#define CMD_GET_HOST_BY_NAME 0x35u
#define CMD_START_SCAN 0x36u
#define CMD_GET_FW_VERSION 0x37u
#define CMD_STOP_SERVER_TCP 0x38u
#define CMD_SEND_DATA_UDP 0x39u
#define CMD_GET_REMOTE_DATA 0x3au
#define CMD_GET_SOCKET 0x3fu
#define CMD_SEND_DATA_TCP 0x44u
#define CMD_GET_DATABUF_TCP 0x45u
#define CMD_INSERT_DATABUF 0x46u
#define CMD_SOCKET_SOCKET 0x70u
#define CMD_SOCKET_CLOSE 0x71u
#define CMD_SOCKET_BIND 0x73u
#define CMD_SOCKET_LISTEN 0x74u
#define CMD_SOCKET_ACCEPT 0x75u
#define CMD_SOCKET_CONNECT 0x76u
#define CMD_SOCKET_SEND 0x77u
#define CMD_SOCKET_RECV 0x78u
#define CMD_SOCKET_IOCTL 0x7bu
#define CMD_SOCKET_POLL 0x7cu

#define NINA_TCP_MODE 0u
#define NINA_UDP_MODE 1u
#define NINA_NO_SOCKET 255u
#define NINA_MAX_SOCKETS 6u
#define NINA_RAW_SOCKET_SCAN_FIRST 56u
#define NINA_RAW_SOCKET_SCAN_MAX 64u
#define NINA_SOCK_STREAM 1u
#define NINA_IPPROTO_TCP 6u
#define NINA_FIONREAD 0x4004667fu
#define NINA_FIONBIO 0x8004667eu
#define NINA_SOCKET_POLL_RD 0x01u
#define NINA_SOCKET_POLL_WR 0x02u
#define NINA_SOCKET_POLL_ERR 0x04u
#define NINA_SOCKET_POLL_FAIL 0x80u
#define DEFAULT_SHELL_PORT 23u
#define DEFAULT_HTTP_PORT 80u
#define DEFAULT_FTP_PORT 21u
#define DEFAULT_FTP_DATA_PORT 2121u
#define DEFAULT_TFTP_PORT 69u
#define DEFAULT_SHELL_PATH "/usr/bin/fruitjam-shell"
#define PLAYGROUND_ROOT "/www"
#define SD_WEB_ROOT "/mnt/sd/www"
#define PLAYGROUND_PREFIX "/playground"
#define FTP_ROOT "/mnt/sd"
#define TFTP_ROOT "/tmp/tftp"
#define TFTP_README_TEXT "Fruit Jam TFTP area\n"
#define BUTTON_FIFO "/run/fruitjam-buttons.fifo"
#define ADC_SYSFS "/sys/bus/platform/devices/400a0000.adc"
#define DVI_DEV "/dev/fruitjam-dvi"
#define DVI_BIN "/usr/bin/fruitjam-dvi"
#define WAV_BIN "/usr/bin/fruitjam-wavplay"
#define WAV_DIR "/mnt/sd/wavs"
#define ADC_BASE_GPIO 40u
#define ADC_TEMP_CH 8u
#define BERRY_BIN "/usr/bin/berry"
#define BERRY_DIR "/root/berry"
#define BERRY_SCRIPT_MAX 63
#define WAV_FILE_MAX 95
#define WAV_LIST_MAX 32
#define AIRLIFT_SHELL_IDLE_MS 300000L
#define AIRLIFT_HTTP_IDLE_MS 1200L
#define AIRLIFT_HTTP_HEADER_DRAIN_MS 10L
#define AIRLIFT_FTP_IDLE_MS 5000L
#define AIRLIFT_SEND_CHUNK 512u
#define AIRLIFT_SEND_RETRY_US 5000
#define AIRLIFT_SERVICE_POLL_US 5000
#define AIRLIFT_TELNET_DRAIN_LIMIT 4
#define AIRLIFT_WIFI_JOIN_POLLS 1500
#define AIRLIFT_LOCK_PATH "/run/airliftctl.lock"
#define AIRLIFT_START_LOG "/tmp/airlift-start.log"
#define FTP_READ_NEW_CONTROL (-2)
#define FTP_READ_SERVICE_WAITING (-3)

static const char *const berry_scripts[] = {
	"00-hello.be",
	"01-language-tour.be",
	"02-files-and-sd.be",
	"03-buttons.be",
	"04-adc-summary.be",
	"05-usbhost-status.be",
	"06-fruitjam-module.be",
	"08-usbhost-hid-decode.be",
	"09-mqtt-publish.be",
	"10-mqtt-subscribe.be",
	"11-i2c.be",
	"12-usbhost-keyboard.be",
	"13-airlift.be",
	"14-audio-wav.be",
	"15-board-control.be",
	"neopixels.be",
	"neopixel-colors.be",
	"neopixel-rainbow-10s.be",
	"run-all.be",
	"run-visual.be",
};

#define TELNET_IAC 255u
#define TELNET_DONT 254u
#define TELNET_DO 253u
#define TELNET_WONT 252u
#define TELNET_WILL 251u
#define TELNET_SB 250u
#define TELNET_SE 240u
#define TELNET_OPT_ECHO 1u
#define TELNET_OPT_SGA 3u
#define TELNET_OPT_NAWS 31u
#ifndef TIOCGPTN
#define TIOCGPTN 0x80045430
#endif
#ifndef TIOCSPTLCK
#define TIOCSPTLCK 0x40045431
#endif
#ifndef TIOCSCTTY
#define TIOCSCTTY 0x540E
#endif

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

struct telnet_session {
	uint8_t sock;
	pid_t pid;
	int child_in;
	int child_out;
	int child_alive;
	long idle_deadline;
	long idle_ms;
};

static struct response response_buf;
static uint8_t command_buf[768];
static volatile sig_atomic_t stop_requested;
static int inbound_heartbeat_active;
static long inbound_heartbeat_next_ms;
static int airlift_lock_fd = -1;

static void airlift_lock_release(void)
{
	int fd = airlift_lock_fd;

	if (fd < 0)
		return;
	airlift_lock_fd = -1;
	unlink(AIRLIFT_LOCK_PATH);
	close(fd);
}

static void request_stop(int sig)
{
	(void)sig;
	stop_requested = 1;
}

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
		"  serve-shell [PORT] [SHELL]\n"
		"                        listen through AirLift and bridge to a shell\n"
		"  serve-inbound         listen through AirLift on telnet/HTTP/FTP ports\n"
		"  tcp-get HOST [PATH]   fetch an HTTP path through the NINA TCP socket API\n"
		"  mqtt-pub HOST PORT TOPIC MESSAGE [CLIENTID [USERNAME PASSWORD]]\n"
		"                        publish one MQTT 3.1.1 QoS 0 message over AirLift\n"
		"  mqtt-sub HOST PORT TOPIC [CLIENTID USERNAME PASSWORD SECONDS COUNT VERBOSE]\n"
		"                        subscribe to MQTT 3.1.1 QoS 0 messages over AirLift\n"
		"\n"
		"AirLift SPI access is serialized with " AIRLIFT_LOCK_PATH ".\n");
}

static long now_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (long)tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

static void inbound_heartbeat_touch(void)
{
	char line[32];
	long now = now_ms();
	int fd;
	int len;

	if (!inbound_heartbeat_active)
		return;
	fd = open(AIRLIFT_HEARTBEAT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return;
	len = snprintf(line, sizeof(line), "%ld\n", now);
	if (len > 0 && (size_t)len < sizeof(line))
		write(fd, line, (size_t)len);
	close(fd);
	inbound_heartbeat_next_ms = now + AIRLIFT_HEARTBEAT_INTERVAL_MS;
}

static void inbound_heartbeat_poll(void)
{
	if (inbound_heartbeat_active && now_ms() >= inbound_heartbeat_next_ms)
		inbound_heartbeat_touch();
}

static int parse_port(const char *s, uint16_t *port)
{
	char *end;
	unsigned long value = strtoul(s, &end, 10);

	if (!*s || *end || value == 0 || value > 65535)
		return -1;
	*port = (uint16_t)value;
	return 0;
}

static int mkdir_p(const char *path)
{
	char tmp[128];
	char *p;

	if (strlen(path) >= sizeof(tmp)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	strcpy(tmp, path);

	for (p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
			return -1;
		*p = '/';
	}

	if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
		return -1;
	return 0;
}

static int copy_log_line(char *dst, size_t dst_len, const char *src)
{
	int ret;

	if (dst_len == 0)
		return -1;
	ret = snprintf(dst, dst_len, "%s", src);
	return ret > 0 && (size_t)ret < dst_len ? 0 : -1;
}

static int print_cached_airlift_info(const char *command)
{
	char firmware[96] = "";
	char mac[96] = "";
	char status[96] = "";
	char ip[96] = "";
	char netmask[96] = "";
	char gateway[96] = "";
	char line[128];
	FILE *fp;

	if (strcmp(command, "fw") && strcmp(command, "mac") &&
	    strcmp(command, "status") && strcmp(command, "ip"))
		return -1;

	fp = fopen(AIRLIFT_START_LOG, "r");
	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		if (!strncmp(line, "firmware ", 9))
			copy_log_line(firmware, sizeof(firmware), line);
		else if (!strncmp(line, "mac ", 4))
			copy_log_line(mac, sizeof(mac), line);
		else if (!strncmp(line, "status ", 7))
			copy_log_line(status, sizeof(status), line);
		else if (!strncmp(line, "ip ", 3))
			copy_log_line(ip, sizeof(ip), line);
		else if (!strncmp(line, "netmask ", 8))
			copy_log_line(netmask, sizeof(netmask), line);
		else if (!strncmp(line, "gateway ", 8))
			copy_log_line(gateway, sizeof(gateway), line);
	}
	fclose(fp);

	if (!strcmp(command, "fw") && *firmware) {
		fputs(firmware, stdout);
		return 0;
	}
	if (!strcmp(command, "mac") && *mac) {
		fputs(mac, stdout);
		return 0;
	}
	if (!strcmp(command, "status") && *status) {
		fputs(status, stdout);
		return 0;
	}
	if (!strcmp(command, "ip") && *ip) {
		fputs(ip, stdout);
		if (*netmask)
			fputs(netmask, stdout);
		if (*gateway)
			fputs(gateway, stdout);
		return 0;
	}
	return -1;
}

static int read_lock_owner(pid_t *pid, char *command, size_t command_len)
{
	char line[96];
	char *end;
	FILE *fp;
	long value;

	if (command_len)
		command[0] = '\0';
	fp = fopen(AIRLIFT_LOCK_PATH, "r");
	if (!fp)
		return -1;
	if (!fgets(line, sizeof(line), fp)) {
		fclose(fp);
		return -1;
	}
	fclose(fp);

	errno = 0;
	value = strtol(line, &end, 10);
	if (errno || value <= 0)
		return -1;
	while (*end == ' ' || *end == '\t')
		end++;
	if (command_len) {
		size_t len = strcspn(end, "\r\n");

		if (len >= command_len)
			len = command_len - 1;
		memcpy(command, end, len);
		command[len] = '\0';
	}
	*pid = (pid_t)value;
	return 0;
}

static int lock_owner_alive(pid_t pid)
{
	char path[64];
	char comm[32];
	FILE *fp;

	if (pid <= 0)
		return 0;
	snprintf(path, sizeof(path), "/proc/%ld/comm", (long)pid);
	fp = fopen(path, "r");
	if (fp) {
		int alive = 0;

		if (fgets(comm, sizeof(comm), fp)) {
			size_t len = strcspn(comm, "\r\n");

			comm[len] = '\0';
			alive = !strcmp(comm, "airliftctl");
		}
		fclose(fp);
		return alive;
	}
	if (kill(pid, 0) == 0 || errno == EPERM)
		return 1;
	return 0;
}

static int airlift_lock_acquire(const char *command)
{
	char buf[80];
	int attempt;

	for (attempt = 0; attempt < 2; attempt++) {
		pid_t owner_pid = -1;
		char owner_command[48];
		int fd;
		int len;

		fd = open(AIRLIFT_LOCK_PATH, O_WRONLY | O_CREAT | O_EXCL, 0644);
		if (fd >= 0) {
			fcntl(fd, F_SETFD, FD_CLOEXEC);
			len = snprintf(buf, sizeof(buf), "%ld %s\n",
				       (long)getpid(), command);
			if (len > 0) {
				size_t write_len = (size_t)len < sizeof(buf) ?
					(size_t)len : sizeof(buf) - 1;

				write(fd, buf, write_len);
			}
			airlift_lock_fd = fd;
			atexit(airlift_lock_release);
			return 0;
		}
		if (errno != EEXIST) {
			fprintf(stderr, "airliftctl: open %s: %s\n",
				AIRLIFT_LOCK_PATH, strerror(errno));
			return -1;
		}

		if (read_lock_owner(&owner_pid, owner_command,
				    sizeof(owner_command)) < 0 ||
		    !lock_owner_alive(owner_pid)) {
			if (unlink(AIRLIFT_LOCK_PATH) < 0 && errno != ENOENT) {
				fprintf(stderr, "airliftctl: remove stale %s: %s\n",
					AIRLIFT_LOCK_PATH, strerror(errno));
				return -1;
			}
			continue;
		}

		if (print_cached_airlift_info(command) == 0)
			return 1;
		if (owner_command[0]) {
			fprintf(stderr, "airliftctl: AirLift SPI is busy; "
				"airliftctl pid %ld owns %s for '%s'\n",
				(long)owner_pid, AIRLIFT_LOCK_PATH,
				owner_command);
		} else {
			fprintf(stderr, "airliftctl: AirLift SPI is busy; "
				"airliftctl pid %ld owns %s\n",
				(long)owner_pid, AIRLIFT_LOCK_PATH);
		}
		fprintf(stderr, "airliftctl: wait or run "
			"'fruitjam-services stop' before '%s'\n",
			command);
		return -1;
	}

	fprintf(stderr, "airliftctl: could not acquire %s after stale lock cleanup\n",
		AIRLIFT_LOCK_PATH);
	return -1;
}

static int safe_root_path(const char *root, const char *arg, char *out, size_t out_len)
{
	const char *p = arg;
	int ret;

	if (!p || !*p || !strcmp(p, "/"))
		p = ".";
	while (*p == '/')
		p++;
	if (strstr(p, "..") || strchr(p, '\\'))
		return -1;
	ret = snprintf(out, out_len, "%s/%s", root, p);
	return ret > 0 && (size_t)ret < out_len ? 0 : -1;
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

static void put_le32(uint8_t *buf, uint32_t value)
{
	buf[0] = value & 0xffu;
	buf[1] = (value >> 8) & 0xffu;
	buf[2] = (value >> 16) & 0xffu;
	buf[3] = (value >> 24) & 0xffu;
}

static uint16_t get_le16(const uint8_t *buf)
{
	return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t get_le32(const uint8_t *buf)
{
	return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
	       ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
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

static int nina_command8_response16(struct airlift *air, uint8_t cmd,
				    const uint8_t *params[], const size_t lens[],
				    size_t nparams, uint8_t *data, uint16_t *len)
{
	if (send_command(air, cmd, params, lens, nparams) < 0)
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

	for (i = 0; i < AIRLIFT_WIFI_JOIN_POLLS; i++) {
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
		usleep(20000);
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

static int start_client_ip_mode(struct airlift *air, const uint8_t ip[4],
				uint16_t port, uint8_t sock, uint8_t mode)
{
	uint8_t port_be[2];
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

static int tcp_start_client_ip(struct airlift *air, const uint8_t ip[4], uint16_t port,
			       uint8_t sock)
{
	return start_client_ip_mode(air, ip, port, sock, NINA_TCP_MODE);
}

static int udp_start_client_ip(struct airlift *air, const uint8_t ip[4], uint16_t port,
			       uint8_t sock)
{
	return start_client_ip_mode(air, ip, port, sock, NINA_UDP_MODE);
}

static int start_server_mode(struct airlift *air, uint16_t port, uint8_t sock,
			     uint8_t mode)
{
	uint8_t port_be[2];
	const uint8_t *params[] = { port_be, &sock, &mode };
	const size_t lens[] = { sizeof(port_be), 1, 1 };
	struct response *resp = &response_buf;

	put_be16(port_be, port);

	if (nina_command(air, CMD_START_SERVER_TCP, params, lens, ARRAY_SIZE(params),
			 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1 || resp->data[0][0] != 1) {
		fprintf(stderr, "airliftctl: NINA refused %s server socket %u port %u\n",
			mode == NINA_UDP_MODE ? "UDP" : "TCP", sock, port);
		return -1;
	}
	return 0;
}

static int tcp_start_server(struct airlift *air, uint16_t port, uint8_t sock)
{
	return start_server_mode(air, port, sock, NINA_TCP_MODE);
}

static void close_stale_raw_sockets(struct airlift *air);

static int MAYBE_UNUSED udp_start_server(struct airlift *air, uint16_t port, uint8_t sock)
{
	return start_server_mode(air, port, sock, NINA_UDP_MODE);
}

static int tcp_stop_server(struct airlift *air, uint8_t sock)
{
	const uint8_t *params[] = { &sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_STOP_SERVER_TCP, params, lens, 1, 1, resp) < 0)
		return -1;
	return 0;
}

static int tcp_stop_client(struct airlift *air, uint8_t sock);

static void stop_all_nina_sockets(struct airlift *air)
{
	uint8_t sock;

	for (sock = 0; sock < NINA_MAX_SOCKETS; sock++) {
		tcp_stop_client(air, sock);
		tcp_stop_server(air, sock);
	}
	usleep(250000);
}

static int nina_socket_create(struct airlift *air, uint8_t type, uint8_t proto,
			      uint8_t *sock)
{
	const uint8_t *params[] = { &type, &proto };
	const size_t lens[] = { 1, 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_SOCKET_SOCKET, params, lens, 2, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1 || resp->data[0][0] == NINA_NO_SOCKET)
		return -1;
	*sock = resp->data[0][0];
	return 0;
}

static int nina_socket_close(struct airlift *air, uint8_t sock)
{
	const uint8_t *params[] = { &sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (sock == NINA_NO_SOCKET)
		return 0;
	if (nina_command(air, CMD_SOCKET_CLOSE, params, lens, 1, 1, resp) < 0)
		return -1;
	return 0;
}

static void MAYBE_UNUSED close_stale_raw_sockets(struct airlift *air)
{
	uint8_t sock;

	for (sock = NINA_RAW_SOCKET_SCAN_FIRST; sock < NINA_RAW_SOCKET_SCAN_MAX; sock++)
		nina_socket_close(air, sock);
}

static int nina_socket_bind(struct airlift *air, uint8_t sock, uint16_t port)
{
	uint8_t port_be[2];
	const uint8_t *params[] = { &sock, port_be };
	const size_t lens[] = { 1, sizeof(port_be) };
	struct response *resp = &response_buf;

	put_be16(port_be, port);
	if (nina_command(air, CMD_SOCKET_BIND, params, lens, 2, 1, resp) < 0)
		return -1;
	return resp->len[0] >= 1 && resp->data[0][0] == 1 ? 0 : -1;
}

static int nina_socket_listen(struct airlift *air, uint8_t sock, uint8_t backlog)
{
	const uint8_t *params[] = { &sock, &backlog };
	const size_t lens[] = { 1, 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_SOCKET_LISTEN, params, lens, 2, 1, resp) < 0)
		return -1;
	return resp->len[0] >= 1 && resp->data[0][0] == 1 ? 0 : -1;
}

static int nina_socket_set_nonblock(struct airlift *air, uint8_t sock);

static int nina_socket_listen_tcp(struct airlift *air, uint16_t port, uint8_t *sock)
{
	uint8_t fd = NINA_NO_SOCKET;

	if (nina_socket_create(air, NINA_SOCK_STREAM, NINA_IPPROTO_TCP, &fd) < 0)
		return -1;
	if (nina_socket_bind(air, fd, port) < 0 ||
	    nina_socket_listen(air, fd, 1) < 0 ||
	    nina_socket_set_nonblock(air, fd) < 0) {
		nina_socket_close(air, fd);
		return -1;
	}
	*sock = fd;
	return 0;
}

static int nina_socket_poll_raw(struct airlift *air, uint8_t sock,
				uint8_t *flags)
{
	const uint8_t *params[] = { &sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_SOCKET_POLL, params, lens, 1, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1)
		return -1;
	*flags = resp->data[0][0];
	return 0;
}

static int MAYBE_UNUSED nina_socket_poll(struct airlift *air, uint8_t sock,
					 uint8_t *flags)
{
	if (nina_socket_poll_raw(air, sock, flags) < 0)
		return -1;
	return (*flags & NINA_SOCKET_POLL_FAIL) ? -1 : 0;
}

static int MAYBE_UNUSED nina_socket_ioctl_u32(struct airlift *air, uint8_t sock,
					      uint32_t cmd, uint32_t *value)
{
	uint8_t cmd_le[4];
	uint8_t value_le[4];
	const uint8_t *params[] = { &sock, cmd_le, value_le };
	const size_t lens[] = { 1, sizeof(cmd_le), sizeof(value_le) };
	struct response *resp = &response_buf;

	put_le32(cmd_le, cmd);
	put_le32(value_le, *value);
	if (nina_command(air, CMD_SOCKET_IOCTL, params, lens, 3, 1, resp) < 0)
		return -1;
	if (resp->len[0] < sizeof(value_le))
		return -1;
	*value = get_le32(resp->data[0]);
	return 0;
}

static int MAYBE_UNUSED nina_socket_set_nonblock(struct airlift *air, uint8_t sock)
{
	uint32_t enable = 1;

	return nina_socket_ioctl_u32(air, sock, NINA_FIONBIO, &enable);
}

static int MAYBE_UNUSED nina_socket_fionread(struct airlift *air, uint8_t sock,
					     uint16_t *avail)
{
	uint32_t value = 0;

	if (nina_socket_ioctl_u32(air, sock, NINA_FIONREAD, &value) < 0)
		return -1;
	*avail = value > 65535 ? 65535 : (uint16_t)value;
	return 0;
}

static int nina_socket_accept(struct airlift *air, uint8_t listen_sock,
			      uint8_t *client_sock)
{
	const uint8_t *params[] = { &listen_sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	*client_sock = NINA_NO_SOCKET;
	if (nina_command(air, CMD_SOCKET_ACCEPT, params, lens, 1, 3, resp) < 0)
		return -1;
	if (resp->len[0] < 1 || resp->data[0][0] == NINA_NO_SOCKET)
		return 0;
	*client_sock = resp->data[0][0];
	return 0;
}

static int nina_socket_connect(struct airlift *air, uint8_t sock,
			       const uint8_t ip[4], uint16_t port)
{
	uint8_t port_be[2];
	const uint8_t *params[] = { &sock, ip, port_be };
	const size_t lens[] = { 1, 4, sizeof(port_be) };
	struct response *resp = &response_buf;

	put_be16(port_be, port);
	if (nina_command(air, CMD_SOCKET_CONNECT, params, lens, 3, 1, resp) < 0)
		return -1;
	return resp->len[0] >= 1 && resp->data[0][0] == 1 ? 0 : -1;
}

static int nina_socket_send(struct airlift *air, uint8_t sock,
			    const uint8_t *data, uint16_t len, uint16_t *sent)
{
	const uint8_t *params[] = { &sock, data };
	const size_t lens[] = { 1, len };
	struct response *resp = &response_buf;

	if (nina_command16_response8(air, CMD_SOCKET_SEND, params, lens, 2, 1,
				     resp) < 0)
		return -1;
	if (resp->len[0] < 2)
		return -1;
	*sent = get_be16(resp->data[0]);
	return 0;
}

static int nina_socket_send_all(struct airlift *air, uint8_t sock,
				const uint8_t *data, uint16_t len)
{
	uint16_t offset = 0;
	long deadline = now_ms() + 5000;

	while (offset < len && now_ms() < deadline) {
		uint16_t want = len - offset;
		uint16_t sent = 0;

		inbound_heartbeat_poll();
		if (want > AIRLIFT_SEND_CHUNK)
			want = AIRLIFT_SEND_CHUNK;
		if (nina_socket_send(air, sock, data + offset, want, &sent) < 0)
			return -1;
		if (sent == 0) {
			usleep(AIRLIFT_SEND_RETRY_US);
			continue;
		}
		if (sent > want)
			return -1;
		offset += sent;
	}
	return offset == len ? 0 : -1;
}

static int nina_socket_recv(struct airlift *air, uint8_t sock, uint8_t *data,
			    uint16_t *len)
{
	uint8_t len_le[2];
	const uint8_t *params[] = { &sock, len_le };
	const size_t lens[] = { 1, sizeof(len_le) };

	put_le16(len_le, *len);
	return nina_command8_response16(air, CMD_SOCKET_RECV, params, lens, 2,
					data, len);
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

static int tcp_server_state(struct airlift *air, uint8_t sock, uint8_t *state)
{
	const uint8_t *params[] = { &sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_GET_STATE_TCP, params, lens, 1, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1)
		return -1;
	*state = resp->data[0][0];
	return 0;
}

static int tcp_accept_server(struct airlift *air, uint8_t server_sock,
			     uint8_t accept, uint8_t *client_sock)
{
	const uint8_t *params[] = { &server_sock, &accept };
	const size_t lens[] = { 1, 1 };
	struct response *resp = &response_buf;
	uint16_t sock;

	if (nina_command(air, CMD_AVAIL_DATA_TCP, params, lens, 2, 1, resp) < 0)
		return -1;
	if (resp->len[0] >= 2)
		sock = get_le16(resp->data[0]);
	else if (resp->len[0] == 1)
		sock = resp->data[0][0];
	else
		return -1;
	*client_sock = sock > 254 ? NINA_NO_SOCKET : (uint8_t)sock;
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

static void tcp_close_client(struct airlift *air, uint8_t sock)
{
	int i;

	if (sock == NINA_NO_SOCKET)
		return;
	tcp_stop_client(air, sock);
	for (i = 0; i < 30; i++) {
		uint8_t state = 0;

		if (tcp_client_state(air, sock, &state) == 0 && state == CLOSED)
			break;
		usleep(50000);
	}
	usleep(750000);
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
	struct response *resp = &response_buf;
	uint16_t offset = 0;
	long deadline = now_ms() + 3000;

	while (offset < len && now_ms() < deadline) {
		uint16_t want = len - offset;
		const uint8_t *params[] = { &sock, data + offset };
		const size_t lens[] = { 1, want };
		uint16_t sent;

		if (nina_command16_response8(air, CMD_SEND_DATA_TCP, params, lens, 2, 1, resp) < 0)
			return -1;
		if (resp->len[0] < 2)
			return -1;
		sent = get_le16(resp->data[0]);
		if (sent == 0) {
			usleep(AIRLIFT_SEND_RETRY_US);
			continue;
		}
		if (sent > want) {
			fprintf(stderr, "airliftctl: TCP sent %u of %u bytes\n", sent, want);
			return -1;
		}
		offset += sent;
	}
	if (offset == len)
		return 0;
	fprintf(stderr, "airliftctl: TCP sent %u of %u bytes\n", offset, len);
	return -1;
}

static int tcp_check_sent(struct airlift *air, uint8_t sock)
{
	const uint8_t *params[] = { &sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;
	int i;

	for (i = 0; i < 50; i++) {
		if (nina_command(air, CMD_DATA_SENT_TCP, params, lens, 1, 1, resp) < 0)
			return -1;
		if (resp->len[0] >= 1 && resp->data[0][0])
			return 0;
		usleep(10000);
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

static int tcp_send_fmt(struct airlift *air, uint8_t sock, const char *fmt, ...);

static int udp_insert_data(struct airlift *air, uint8_t sock, const uint8_t *data,
			   uint16_t len)
{
	const uint8_t *params[] = { &sock, data };
	const size_t lens[] = { 1, len };
	struct response *resp = &response_buf;

	if (nina_command16_response8(air, CMD_INSERT_DATABUF, params, lens, 2, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1 || resp->data[0][0] == 0)
		return -1;
	return 0;
}

static int udp_send_data(struct airlift *air, uint8_t sock)
{
	const uint8_t *params[] = { &sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_SEND_DATA_UDP, params, lens, 1, 1, resp) < 0)
		return -1;
	if (resp->len[0] < 1 || resp->data[0][0] == 0)
		return -1;
	return 0;
}

static int udp_remote_data(struct airlift *air, uint8_t sock, uint8_t ip[4],
			   uint16_t *port)
{
	const uint8_t *params[] = { &sock };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_GET_REMOTE_DATA, params, lens, 1, 2, resp) < 0)
		return -1;
	if (resp->len[0] < 4 || resp->len[1] < 2)
		return -1;
	memcpy(ip, resp->data[0], 4);
	*port = get_be16(resp->data[1]);
	return 0;
}

static int udp_sendto(struct airlift *air, uint8_t sock, const uint8_t ip[4],
		      uint16_t port, const uint8_t *data, uint16_t len)
{
	if (udp_start_client_ip(air, ip, port, sock) < 0)
		return -1;
	if (udp_insert_data(air, sock, data, len) < 0)
		return -1;
	return udp_send_data(air, sock);
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

static int set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);

	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static long env_ms(const char *name, long fallback, long min_value)
{
	const char *value = getenv(name);
	char *end;
	long parsed;

	if (!value || !*value)
		return fallback;
	parsed = strtol(value, &end, 10);
	if (*end || parsed < min_value)
		return fallback;
	return parsed;
}

static int spawn_shell(const char *shell, int *in_fd, int *out_fd, pid_t *pid)
{
	int to_child[2];
	int from_child[2];
	pid_t child;

	if (pipe(to_child) < 0) {
		perror("airliftctl: pipe");
		return -1;
	}
	if (pipe(from_child) < 0) {
		perror("airliftctl: pipe");
		close(to_child[0]);
		close(to_child[1]);
		return -1;
	}

	child = vfork();
	if (child < 0) {
		perror("airliftctl: vfork");
		close(to_child[0]);
		close(to_child[1]);
		close(from_child[0]);
		close(from_child[1]);
		return -1;
	}
	if (child == 0) {
		dup2(to_child[0], STDIN_FILENO);
		dup2(from_child[1], STDOUT_FILENO);
		dup2(from_child[1], STDERR_FILENO);
		close(to_child[0]);
		close(to_child[1]);
		close(from_child[0]);
		close(from_child[1]);
		execl(shell, shell, (char *)NULL);
		_exit(127);
	}

	close(to_child[0]);
	close(from_child[1]);
	set_nonblock(to_child[1]);
	set_nonblock(from_child[0]);
	*in_fd = to_child[1];
	*out_fd = from_child[0];
	*pid = child;
	return 0;
}

static int spawn_shell_pty(const char *shell, int *master_fd, pid_t *pid)
{
    int master;
    int unlock = 0;
    int ptn = 0;
    char slave_name[32];
    pid_t child;

    master = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    if (master < 0) {
        perror("airliftctl: open ptmx");
        return -1;
    }
    if (ioctl(master, TIOCSPTLCK, &unlock) < 0 ||
        ioctl(master, TIOCGPTN, &ptn) < 0) {
        perror("airliftctl: pty setup");
        close(master);
        return -1;
    }
    snprintf(slave_name, sizeof(slave_name), "/dev/pts/%d", ptn);

    child = vfork();
    if (child < 0) {
        perror("airliftctl: vfork");
        close(master);
        return -1;
    }
    if (child == 0) {
        static char *const envp[] = {
            "TERM=vt100",
            "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
            "HOME=/root",
            NULL
        };
        int s;

        setsid();
        s = open(slave_name, O_RDWR);
        if (s < 0)
            _exit(127);
        ioctl(s, TIOCSCTTY, 0);
        dup2(s, STDIN_FILENO);
        dup2(s, STDOUT_FILENO);
        dup2(s, STDERR_FILENO);
        if (s > STDERR_FILENO)
            close(s);
        close(master);
        execle(shell, shell, (char *)NULL, envp);
        _exit(127);
    }

    set_nonblock(master);
    *master_fd = master;
    *pid = child;
    return 0;
}

static void telnet_send_negotiation(struct airlift *air, uint8_t sock)
{
    static const uint8_t nego[] = {
        TELNET_IAC, TELNET_WILL, TELNET_OPT_ECHO,
        TELNET_IAC, TELNET_WILL, TELNET_OPT_SGA,
        TELNET_IAC, TELNET_DO,   TELNET_OPT_NAWS,
    };

    tcp_send(air, sock, nego, sizeof(nego));
    tcp_check_sent(air, sock);
}

static int telnet_reply_option(struct airlift *air, uint8_t sock, uint8_t cmd,
			       uint8_t opt)
{
	uint8_t resp[3];

	if ((cmd == TELNET_DO || cmd == TELNET_DONT) &&
	    (opt == TELNET_OPT_ECHO || opt == TELNET_OPT_SGA))
		return 0;
	if ((cmd == TELNET_WILL || cmd == TELNET_WONT) && opt == TELNET_OPT_NAWS)
		return 0;

	resp[0] = TELNET_IAC;
	resp[1] = (cmd == TELNET_DO || cmd == TELNET_DONT) ? TELNET_WONT : TELNET_DONT;
	resp[2] = opt;
	return tcp_send(air, sock, resp, sizeof(resp)) < 0 ? -1 : tcp_check_sent(air, sock);
}

static int write_child_payload(int fd, const uint8_t *buf, size_t len)
{
	size_t off = 0;

	while (off < len) {
		ssize_t wrote = write(fd, buf + off, len - off);

		if (wrote > 0) {
			off += (size_t)wrote;
			continue;
		}
		if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			usleep(1000);
			continue;
		}
		return -1;
	}
	return 0;
}

/*
 * AirLift Telnet intentionally stays minimal: no remote echo, no suppress-go-ahead,
 * no NAWS/window-size parsing, and no subnegotiation support. Unsupported WILL/DO
 * requests are rejected with DONT/WONT so IAC negotiation bytes never reach the
 * shell input stream.
 */
static int write_telnet_payload(struct airlift *air, uint8_t sock, int fd,
				const uint8_t *buf, uint16_t len)
{
	uint8_t out[256];
	size_t out_len = 0;
	uint16_t i;

	for (i = 0; i < len; i++) {
		uint8_t c = buf[i];

		if (c != TELNET_IAC) {
			if (c == 0)
				continue;  /* drop telnet CR-NUL filler */
			out[out_len++] = c;
			if (out_len == sizeof(out)) {
				if (write_child_payload(fd, out, out_len) < 0)
					return -1;
				out_len = 0;
			}
			continue;
		}

		if (out_len) {
			if (write_child_payload(fd, out, out_len) < 0)
				return -1;
			out_len = 0;
		}
		if (++i >= len)
			break;
		c = buf[i];
		if (c == TELNET_IAC) {
			if (write_child_payload(fd, &c, 1) < 0)
				return -1;
			continue;
		}
		if (c == TELNET_DO || c == TELNET_DONT || c == TELNET_WILL || c == TELNET_WONT) {
			if (++i >= len)
				break;
			telnet_reply_option(air, sock, c, buf[i]);
			continue;
		}
        if (c == TELNET_SB) {
            uint8_t sbopt = (++i < len) ? buf[i] : 0;
            uint8_t sub[8];
            size_t sn = 0;

            while (++i < len) {
                if (buf[i] == TELNET_IAC) {
                    if (i + 1 < len && buf[i + 1] == TELNET_IAC) {
                        if (sn < sizeof(sub))
                            sub[sn++] = TELNET_IAC;
                        i++;
                        continue;
                    }
                    if (i + 1 < len && buf[i + 1] == TELNET_SE)
                        i++;
                    break;
                }
                if (sn < sizeof(sub))
                    sub[sn++] = buf[i];
            }
            if (sbopt == TELNET_OPT_NAWS && sn >= 4) {
                struct winsize ws;

                memset(&ws, 0, sizeof(ws));
                ws.ws_col = (uint16_t)((sub[0] << 8) | sub[1]);
                ws.ws_row = (uint16_t)((sub[2] << 8) | sub[3]);
                if (ws.ws_col && ws.ws_row)
                    ioctl(fd, TIOCSWINSZ, &ws);
            }
        }
	}
	if (out_len && write_child_payload(fd, out, out_len) < 0)
		return -1;
	return 0;
}

static void telnet_session_init(struct telnet_session *session)
{
	memset(session, 0, sizeof(*session));
	session->sock = NINA_NO_SOCKET;
	session->child_in = -1;
	session->child_out = -1;
}

static int telnet_session_active(const struct telnet_session *session)
{
	return session->sock != NINA_NO_SOCKET;
}

static void reap_child_deadline(pid_t pid)
{
	int status;
	int i;

	for (i = 0; i < 20; i++) {
		pid_t got = waitpid(pid, &status, WNOHANG);

		if (got == pid || got < 0)
			return;
		usleep(10000);
	}
	kill(pid, SIGKILL);
	for (i = 0; i < 20; i++) {
		pid_t got = waitpid(pid, &status, WNOHANG);

		if (got == pid || got < 0)
			return;
		usleep(10000);
	}
}

static void telnet_session_close(struct airlift *air, struct telnet_session *session)
{
	if (session->child_in >= 0)
		close(session->child_in);
	if (session->child_out >= 0 && session->child_out != session->child_in)
		close(session->child_out);
	if (session->child_alive) {
		kill(session->pid, SIGTERM);
		reap_child_deadline(session->pid);
	}
	if (session->sock != NINA_NO_SOCKET)
		tcp_close_client(air, session->sock);
	telnet_session_init(session);
}

static int telnet_session_start(struct telnet_session *session, uint8_t sock,
				const char *shell)
{
	int master;

	telnet_session_init(session);
	if (spawn_shell_pty(shell, &master, &session->pid) < 0)
		return -1;
	session->child_in = master;
	session->child_out = master;
	session->sock = sock;
	session->child_alive = 1;
	session->idle_ms = env_ms("AIRLIFT_TELNET_IDLE_MS", AIRLIFT_SHELL_IDLE_MS, 60000L);
	session->idle_deadline = now_ms() + session->idle_ms;
	return 0;
}

static int telnet_session_poll(struct airlift *air, struct telnet_session *session)
{
	uint8_t buf[512];
	uint16_t avail = 0;
	uint8_t state = 0;
	fd_set rfds;
	struct timeval tv;
	int ret;

	if (!telnet_session_active(session))
		return 0;
	if (now_ms() >= session->idle_deadline) {
		tcp_send_fmt(air, session->sock, "\r\nFruit Jam telnet shell idle timeout\r\n");
		telnet_session_close(air, session);
		return 0;
	}

	if (session->child_alive) {
		int status;
		pid_t got = waitpid(session->pid, &status, WNOHANG);

		if (got == session->pid)
			session->child_alive = 0;
	}

	for (int drain = 0; drain < AIRLIFT_TELNET_DRAIN_LIMIT; drain++) {
		FD_ZERO(&rfds);
		FD_SET(session->child_out, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		ret = select(session->child_out + 1, &rfds, NULL, NULL, &tv);
		if (ret <= 0 || !FD_ISSET(session->child_out, &rfds))
			break;
		ssize_t got = read(session->child_out, buf, sizeof(buf));

		if (got > 0) {
			if (tcp_send(air, session->sock, buf, (uint16_t)got) < 0 ||
			    tcp_check_sent(air, session->sock) < 0) {
				telnet_session_close(air, session);
				return -1;
			}
			session->idle_deadline = now_ms() + session->idle_ms;
		} else if (got == 0) {
			session->child_alive = 0;
			break;
		} else if (errno != EAGAIN && errno != EWOULDBLOCK) {
			telnet_session_close(air, session);
			return -1;
		}
	}

	if (tcp_avail(air, session->sock, &avail) < 0) {
		telnet_session_close(air, session);
		return -1;
	}
	for (int drain = 0; avail && drain < AIRLIFT_TELNET_DRAIN_LIMIT; drain++) {
		uint16_t len = avail > sizeof(buf) ? sizeof(buf) : avail;

		if (tcp_read_buf(air, session->sock, buf, &len) < 0) {
			telnet_session_close(air, session);
			return -1;
		}
		if (len) {
			if (write_telnet_payload(air, session->sock, session->child_in, buf, len) < 0) {
				telnet_session_close(air, session);
				return -1;
			}
			session->idle_deadline = now_ms() + session->idle_ms;
		}
		if (tcp_avail(air, session->sock, &avail) < 0) {
			telnet_session_close(air, session);
			return -1;
		}
	}

	if (tcp_client_state(air, session->sock, &state) == 0 && state == CLOSED) {
		telnet_session_close(air, session);
		return 0;
	}
	if (!session->child_alive && !avail) {
		telnet_session_close(air, session);
		return 0;
	}
	return 0;
}

static int bridge_shell_client(struct airlift *air, uint8_t client_sock,
			       const char *shell)
{
	uint8_t buf[256];
	pid_t pid;
	int child_in;
	int child_out;
	int child_alive = 1;
	long idle_deadline = now_ms() + AIRLIFT_SHELL_IDLE_MS;

	if (spawn_shell(shell, &child_in, &child_out, &pid) < 0)
		return -1;

	while (!stop_requested && now_ms() < idle_deadline) {
		uint16_t avail = 0;
		uint8_t state = 0;
		fd_set rfds;
		struct timeval tv;
		int ret;

		if (child_alive) {
			int status;
			pid_t got = waitpid(pid, &status, WNOHANG);

			if (got == pid)
				child_alive = 0;
		}

		FD_ZERO(&rfds);
		FD_SET(child_out, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 1000;
		ret = select(child_out + 1, &rfds, NULL, NULL, &tv);
		if (ret > 0 && FD_ISSET(child_out, &rfds)) {
			ssize_t got = read(child_out, buf, sizeof(buf));

			if (got > 0) {
				if (tcp_send(air, client_sock, buf, (uint16_t)got) < 0 ||
				    tcp_check_sent(air, client_sock) < 0)
					break;
				idle_deadline = now_ms() + AIRLIFT_SHELL_IDLE_MS;
			} else if (got == 0) {
				child_alive = 0;
			}
		}

		if (tcp_avail(air, client_sock, &avail) < 0)
			break;
		if (avail) {
			uint16_t len = avail > sizeof(buf) ? sizeof(buf) : avail;

			if (tcp_read_buf(air, client_sock, buf, &len) < 0)
				break;
			if (len) {
				if (write_telnet_payload(air, client_sock, child_in, buf, len) < 0)
					break;
				idle_deadline = now_ms() + AIRLIFT_SHELL_IDLE_MS;
			}
		}

		if (tcp_client_state(air, client_sock, &state) == 0 && state == CLOSED)
			break;
		if (!child_alive && !avail)
			break;
		usleep(10000);
	}

	close(child_in);
	close(child_out);
	if (child_alive) {
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
	}
	tcp_close_client(air, client_sock);
	return 0;
}

static int serve_shell(struct airlift *air, uint16_t port, const char *shell)
{
	uint8_t server_sock = NINA_NO_SOCKET;

	if (tcp_get_socket(air, &server_sock) < 0)
		return -1;
	if (tcp_start_server(air, port, server_sock) < 0)
		return -1;
	stop_requested = 0;
	signal(SIGINT, request_stop);
	signal(SIGTERM, request_stop);
	printf("airlift shell bridge listening on port %u socket %u\n", port, server_sock);
	fflush(stdout);

	while (!stop_requested) {
		uint8_t client_sock = NINA_NO_SOCKET;
		uint8_t state = 0;

		if (tcp_server_state(air, server_sock, &state) == 0 && state == CLOSED) {
			fprintf(stderr, "airliftctl: server socket closed\n");
			break;
		}
		if (tcp_accept_server(air, server_sock, 1, &client_sock) < 0)
			break;
		if (client_sock != NINA_NO_SOCKET) {
			printf("airlift shell bridge accepted socket %u\n", client_sock);
			fflush(stdout);
			bridge_shell_client(air, client_sock, shell);
			printf("airlift shell bridge closed socket %u\n", client_sock);
			fflush(stdout);
		}
		usleep(20000);
	}

	tcp_stop_server(air, server_sock);
	return -1;
}

static int tcp_send_fmt(struct airlift *air, uint8_t sock, const char *fmt, ...)
{
	char msg[192];
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (len < 0 || (size_t)len >= sizeof(msg))
		return -1;
	return tcp_send(air, sock, (const uint8_t *)msg, (uint16_t)len) < 0 ? -1 :
	       tcp_check_sent(air, sock);
}

static int ftp_send_fmt(struct airlift *air, uint8_t sock, const char *fmt, ...)
{
	char msg[256];
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (len < 0 || (size_t)len >= sizeof(msg))
		return -1;
	return nina_socket_send_all(air, sock, (const uint8_t *)msg,
				    (uint16_t)len);
}

static int ftp_reply(struct airlift *air, uint8_t sock, int code, const char *text)
{
	return ftp_send_fmt(air, sock, "%d %s\r\n", code, text);
}

static int ftp_raw_read_line_poll(struct airlift *air, uint8_t sock,
				  uint8_t listen_sock, uint8_t http_sock,
				  uint8_t shell_sock,
				  char *line,
				  size_t line_len, long idle_ms)
{
	size_t pos = 0;
	long deadline = now_ms() + idle_ms;

	if (line_len == 0)
		return -1;

	while (!stop_requested && now_ms() < deadline) {
		uint8_t flags = 0;

		inbound_heartbeat_poll();
		if (nina_socket_poll(air, sock, &flags) < 0 ||
		    (flags & NINA_SOCKET_POLL_ERR))
			return -1;
		if (!(flags & NINA_SOCKET_POLL_RD)) {
			if (http_sock != NINA_NO_SOCKET) {
				uint8_t http_flags = 0;

				if (nina_socket_poll(air, http_sock, &http_flags) == 0 &&
				    (http_flags & NINA_SOCKET_POLL_RD))
					return FTP_READ_SERVICE_WAITING;
			}
			if (listen_sock != NINA_NO_SOCKET) {
				uint8_t listen_flags = 0;

				if (nina_socket_poll(air, listen_sock, &listen_flags) == 0 &&
				    (listen_flags & NINA_SOCKET_POLL_RD))
					return FTP_READ_NEW_CONTROL;
			}
			if (shell_sock != NINA_NO_SOCKET) {
				uint8_t shell_flags = 0;

				if (nina_socket_poll(air, shell_sock, &shell_flags) == 0 &&
				    (shell_flags & NINA_SOCKET_POLL_RD))
					return FTP_READ_SERVICE_WAITING;
			}
			usleep(10000);
			continue;
		}

		while (flags & NINA_SOCKET_POLL_RD) {
			uint8_t c;
			uint16_t len = 1;

			if (nina_socket_recv(air, sock, &c, &len) < 0)
				return -1;
			if (len == 0) {
				line[pos] = '\0';
				return pos ? 1 : 0;
			}
			deadline = now_ms() + idle_ms;
			if (c == '\n') {
				while (pos && line[pos - 1] == '\r')
					pos--;
				line[pos] = '\0';
				return 1;
			}
			if (pos + 1 < line_len)
				line[pos++] = (char)c;
			if (nina_socket_poll(air, sock, &flags) < 0)
				return -1;
		}
	}

	line[pos] = '\0';
	return pos ? 1 : 0;
}

static int http_read_line_recv(struct airlift *air, uint8_t sock, char *line,
			       size_t line_len, long idle_ms)
{
	size_t pos = 0;
	long deadline = now_ms() + idle_ms;

	if (!line_len)
		return -1;
	while (!stop_requested && now_ms() < deadline) {
		uint8_t c;
		uint16_t len = 1;

		inbound_heartbeat_poll();
		if (nina_socket_recv(air, sock, &c, &len) < 0)
			return -1;
		if (len == 0) {
			usleep(10000);
			continue;
		}
		deadline = now_ms() + idle_ms;
		if (c == '\n') {
			while (pos && line[pos - 1] == '\r')
				pos--;
			line[pos] = '\0';
			return 1;
		}
		if (pos + 1 < line_len)
			line[pos++] = (char)c;
	}

	line[pos] = '\0';
	return pos ? 1 : 0;
}

static char *ftp_arg(char *line)
{
	while (*line && !isspace((unsigned char)*line))
		line++;
	while (*line && isspace((unsigned char)*line))
		*line++ = '\0';
	return line;
}

static const char *http_content_type(const char *path)
{
	const char *ext = strrchr(path, '.');

	if (!ext)
		return "application/octet-stream";
	if (!strcasecmp(ext, ".html") || !strcasecmp(ext, ".htm"))
		return "text/html; charset=utf-8";
	if (!strcasecmp(ext, ".css"))
		return "text/css; charset=utf-8";
	if (!strcasecmp(ext, ".js"))
		return "application/javascript; charset=utf-8";
	if (!strcasecmp(ext, ".json"))
		return "application/json";
	if (!strcasecmp(ext, ".txt"))
		return "text/plain; charset=utf-8";
	return "application/octet-stream";
}

static int http_send_fmt(struct airlift *air, uint8_t sock, const char *fmt, ...)
{
	char msg[256];
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (len < 0 || (size_t)len >= sizeof(msg))
		return -1;
	return nina_socket_send_all(air, sock, (const uint8_t *)msg,
				    (uint16_t)len);
}

static int http_reply_body(struct airlift *air, uint8_t sock, int code,
			   const char *reason, const char *content_type,
			   const char *body)
{
	if (http_send_fmt(air, sock,
			  "HTTP/1.0 %d %s\r\n"
			  "Connection: close\r\n"
			  "Content-Type: %s\r\n"
			  "Access-Control-Allow-Origin: *\r\n"
			  "Cache-Control: no-store\r\n"
			  "Pragma: no-cache\r\n"
			  "Content-Length: %lu\r\n\r\n",
			  code, reason, content_type,
			  (unsigned long)strlen(body)) < 0)
		return -1;
	return nina_socket_send_all(air, sock, (const uint8_t *)body,
				    (uint16_t)strlen(body));
}

static int http_reply_text(struct airlift *air, uint8_t sock, int code,
			   const char *reason, const char *body)
{
	return http_reply_body(air, sock, code, reason,
			       "text/plain; charset=utf-8", body);
}

static int http_reply_json(struct airlift *air, uint8_t sock, const char *body)
{
	return http_reply_body(air, sock, 200, "OK", "application/json", body);
}

struct http_buf {
	char *data;
	size_t len;
	size_t cap;
	int overflow;
};

static void http_buf_init(struct http_buf *b, char *data, size_t cap)
{
	b->data = data;
	b->len = 0;
	b->cap = cap;
	b->overflow = 0;
	if (cap)
		data[0] = '\0';
}

static void http_buf_puts(struct http_buf *b, const char *s)
{
	size_t n = strlen(s);

	if (b->len + n + 1 > b->cap) {
		b->overflow = 1;
		return;
	}
	memcpy(b->data + b->len, s, n + 1);
	b->len += n;
}

static void http_buf_printf(struct http_buf *b, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (b->len >= b->cap) {
		b->overflow = 1;
		return;
	}
	va_start(ap, fmt);
	n = vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= b->cap - b->len) {
		b->overflow = 1;
		return;
	}
	b->len += (size_t)n;
}

static void http_buf_json_string(struct http_buf *b, const char *s)
{
	http_buf_puts(b, "\"");
	for (; s && *s; s++) {
		unsigned char c = (unsigned char)*s;

		switch (c) {
		case '\\':
			http_buf_puts(b, "\\\\");
			break;
		case '"':
			http_buf_puts(b, "\\\"");
			break;
		case '\n':
			http_buf_puts(b, "\\n");
			break;
		case '\r':
			http_buf_puts(b, "\\r");
			break;
		case '\t':
			http_buf_puts(b, "\\t");
			break;
		default:
			if (c < 0x20)
				http_buf_printf(b, "\\u%04x", c);
			else {
				char tmp[2] = { (char)c, '\0' };
				http_buf_puts(b, tmp);
			}
			break;
		}
	}
	http_buf_puts(b, "\"");
}

static void http_json_error_body(char *body, size_t body_len, const char *message)
{
	struct http_buf b;

	http_buf_init(&b, body, body_len);
	http_buf_puts(&b, "{\"ok\":false,\"error\":");
	http_buf_json_string(&b, message);
	http_buf_puts(&b, "}");
	if (b.overflow && body_len)
		snprintf(body, body_len, "{\"ok\":false,\"error\":\"response too large\"}");
}

static int http_hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static void http_url_decode(char *dst, size_t dst_len,
			    const char *src, size_t src_len)
{
	size_t di = 0;
	size_t si;

	if (!dst_len)
		return;
	for (si = 0; si < src_len && di + 1 < dst_len; si++) {
		if (src[si] == '+') {
			dst[di++] = ' ';
		} else if (src[si] == '%' && si + 2 < src_len) {
			int hi = http_hexval(src[si + 1]);
			int lo = http_hexval(src[si + 2]);

			if (hi >= 0 && lo >= 0) {
				dst[di++] = (char)((hi << 4) | lo);
				si += 2;
			} else {
				dst[di++] = src[si];
			}
		} else {
			dst[di++] = src[si];
		}
	}
	dst[di] = '\0';
}

static int http_query_param(const char *query, const char *key,
			    char *out, size_t out_len)
{
	const char *p = query ? query : "";
	size_t key_want = strlen(key);

	if (out_len)
		out[0] = '\0';
	while (*p) {
		const char *amp = strchr(p, '&');
		const char *end = amp ? amp : p + strlen(p);
		const char *eq = memchr(p, '=', (size_t)(end - p));
		size_t key_len = eq ? (size_t)(eq - p) : (size_t)(end - p);

		if (key_len == key_want && !strncmp(p, key, key_len)) {
			if (eq)
				http_url_decode(out, out_len, eq + 1,
						(size_t)(end - eq - 1));
			return 1;
		}
		p = amp ? amp + 1 : end;
	}
	return 0;
}

static int http_read_text_file(const char *path, char *buf, size_t len)
{
	int fd;
	ssize_t ret;
	size_t n;

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
	n = (size_t)ret;
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
		     buf[n - 1] == ' ' || buf[n - 1] == '\t'))
		n--;
	buf[n] = '\0';
	return 0;
}

static int http_write_existing_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY);
	size_t len = strlen(text);
	ssize_t ret;

	if (fd < 0)
		return -1;
	ret = write(fd, text, len);
	close(fd);
	return ret == (ssize_t)len ? 0 : -1;
}

static int http_gpio_export(unsigned int gpio)
{
	char path[64];
	char text[12];
	int i;

	snprintf(text, sizeof(text), "%u", gpio);
	if (http_write_existing_file("/sys/class/gpio/export", text) < 0 &&
	    errno != EBUSY)
		return -1;
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio);
	for (i = 0; i < 50; i++) {
		if (access(path, F_OK) == 0)
			return 0;
		usleep(2000);
	}
	return -1;
}

static int http_gpio_value(unsigned int gpio)
{
	char path[64];
	char value[8];

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio);
	if (http_read_text_file(path, value, sizeof(value)) < 0) {
		http_gpio_export(gpio);
		if (http_read_text_file(path, value, sizeof(value)) < 0)
			return -1;
	}
	return value[0] == '0' ? 0 : 1;
}

static int http_gpio_set_output(unsigned int gpio, int value)
{
	char path[64];

	http_gpio_export(gpio);
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/direction", gpio);
	if (http_write_existing_file(path, "out") < 0)
		return -1;
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio);
	return http_write_existing_file(path, value ? "1" : "0");
}

static int http_gpio_set_input(unsigned int gpio)
{
	char path[64];

	http_gpio_export(gpio);
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/direction", gpio);
	return http_write_existing_file(path, "in");
}

static int http_usbhost_bus_reset(void)
{
	int ret = 0;

	if (access(USBHOST_DEV, W_OK) == 0)
		return http_write_existing_file(USBHOST_DEV, "reset 50");

	if (http_gpio_set_output(11, 1) < 0)
		ret = -1;
	usleep(USBHOST_POST_RESET_US);
	if (http_gpio_set_output(1, 0) < 0)
		ret = -1;
	if (http_gpio_set_output(2, 0) < 0)
		ret = -1;
	usleep(USBHOST_RESET_MS * 1000u);
	if (http_gpio_set_input(1) < 0)
		ret = -1;
	if (http_gpio_set_input(2) < 0)
		ret = -1;
	usleep(USBHOST_POST_RESET_US);
	return ret;
}

static int http_is_mounted(const char *mountpoint)
{
	FILE *fp = fopen("/proc/mounts", "r");
	char line[192];
	int found = 0;

	if (!fp)
		return 0;
	while (fgets(line, sizeof(line), fp)) {
		char dev[64];
		char mnt[64];

		(void)dev;
		if (sscanf(line, "%63s %63s", dev, mnt) == 2 &&
		    !strcmp(mnt, mountpoint)) {
			found = 1;
			break;
		}
	}
	fclose(fp);
	return found;
}

static void http_button_json(struct http_buf *b, const char *name,
			     unsigned int gpio)
{
	int value = http_gpio_value(gpio);

	http_buf_puts(b, "{\"name\":");
	http_buf_json_string(b, name);
	http_buf_printf(b, ",\"gpio\":%u,\"value\":%d,\"pressed\":%s}",
			gpio, value, value == 0 ? "true" : "false");
}

static int http_parse_color(const char *text, unsigned int rgb[3])
{
	const char *p = text;
	size_t i;

	if (!p || !*p)
		p = "000000";
	if (*p == '#')
		p++;
	if (strlen(p) != 6)
		return -1;
	for (i = 0; i < 6; i++) {
		if (http_hexval(p[i]) < 0)
			return -1;
	}
	rgb[0] = (unsigned int)((http_hexval(p[0]) << 4) | http_hexval(p[1]));
	rgb[1] = (unsigned int)((http_hexval(p[2]) << 4) | http_hexval(p[3]));
	rgb[2] = (unsigned int)((http_hexval(p[4]) << 4) | http_hexval(p[5]));
	return 0;
}

static int http_valid_simple_arg(const char *s, size_t max_len,
				 const char *allowed)
{
	size_t i;

	if (!s || strlen(s) > max_len)
		return 0;
	for (i = 0; s[i]; i++) {
		if (!strchr(allowed, s[i]))
			return 0;
	}
	return 1;
}

static int http_parse_uint_limited(const char *s, unsigned int min_value,
				   unsigned int max_value, unsigned int *out)
{
	char *end;
	unsigned long parsed;

	if (!s || !*s || !http_valid_simple_arg(s, 10, "0123456789"))
		return -1;
	errno = 0;
	parsed = strtoul(s, &end, 10);
	if (errno || *end || parsed < min_value || parsed > max_value)
		return -1;
	*out = (unsigned int)parsed;
	return 0;
}

static int http_reboot_bootsel_after_delay(unsigned int delay_ms)
{
	if (delay_ms)
		usleep(delay_ms * 1000u);
	sync();
	return syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		       LINUX_REBOOT_CMD_RESTART2, "bootsel");
}

static int http_run_capture_timeout(char *const argv[], char *out,
				    size_t out_len, unsigned int timeout_ms)
{
	char path[64];
	int fd;
	pid_t pid;
	int status = 127;
	volatile unsigned int waited_ms = 0;
	ssize_t ret;

	if (out_len)
		out[0] = '\0';
	snprintf(path, sizeof(path), "/tmp/fj-http-api-%ld.out", (long)getpid());
	fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (fd < 0)
		return 127;
	pid = vfork();
	if (pid < 0) {
		close(fd);
		unlink(path);
		return 127;
	}
	if (pid == 0) {
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		close(fd);
		execv(argv[0], argv);
		_exit(127);
	}

	for (;;) {
		pid_t done = waitpid(pid, &status, WNOHANG);

		if (done == pid)
			break;
		if (done < 0) {
			status = 127;
			break;
		}
		if (timeout_ms && waited_ms >= timeout_ms) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			status = 124 << 8;
			break;
		}
		usleep(20000);
		waited_ms += 20;
	}

	lseek(fd, 0, SEEK_SET);
	if (out_len) {
		ret = read(fd, out, out_len - 1);
		if (ret < 0)
			out[0] = '\0';
		else
			out[ret] = '\0';
	}
	close(fd);
	unlink(path);

	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return 127;
}

static int http_valid_berry_script(const char *name)
{
	size_t i;
	size_t len;

	if (!name || !*name || name[0] == '.')
		return 0;
	len = strlen(name);
	if (len > BERRY_SCRIPT_MAX || len < 4)
		return 0;
	if (strcmp(name + len - 3, ".be"))
		return 0;
	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)name[i];

		if (!isalnum(c) && c != '-' && c != '_' && c != '.')
			return 0;
	}
	return 1;
}

static int http_known_berry_script(const char *name)
{
	size_t i;

	if (!http_valid_berry_script(name))
		return 0;
	for (i = 0; i < ARRAY_SIZE(berry_scripts); i++) {
		if (!strcmp(name, berry_scripts[i]))
			return 1;
	}
	return 0;
}

static int http_berry_script_path(const char *name, char *path, size_t path_len)
{
	int ret;

	if (!http_known_berry_script(name))
		return -1;
	ret = snprintf(path, path_len, "%s/%s", BERRY_DIR, name);
	return ret > 0 && (size_t)ret < path_len ? 0 : -1;
}

static void http_api_berry_list_body(char *body, size_t body_len)
{
	struct http_buf b;
	size_t i;

	http_buf_init(&b, body, body_len);
	http_buf_puts(&b, "{\"ok\":true,\"source\":\"airlift-direct\",\"dir\":\"");
	http_buf_puts(&b, BERRY_DIR);
	http_buf_puts(&b, "\",\"scripts\":[");
	for (i = 0; i < ARRAY_SIZE(berry_scripts); i++) {
		if (i)
			http_buf_puts(&b, ",");
		http_buf_json_string(&b, berry_scripts[i]);
	}
	http_buf_puts(&b, "]}");
	if (b.overflow)
		http_json_error_body(body, body_len, "berry list response too large");
}

static void http_api_berry_run_body(const char *query, char *body,
				    size_t body_len)
{
	char script[BERRY_SCRIPT_MAX + 1];
	char path[sizeof(BERRY_DIR) + 1 + BERRY_SCRIPT_MAX];
	char output[768];
	int ret;
	struct http_buf b;

	if (!http_query_param(query, "script", script, sizeof(script)) ||
	    http_berry_script_path(script, path, sizeof(path)) < 0) {
		http_json_error_body(body, body_len, "bad berry script");
		return;
	}
	if (access(path, R_OK) < 0) {
		http_json_error_body(body, body_len, "berry script not found");
		return;
	}
	{
		char *const argv[] = { BERRY_BIN, path, NULL };

		ret = http_run_capture_timeout(argv, output, sizeof(output), 22000);
	}
	http_buf_init(&b, body, body_len);
	http_buf_printf(&b, "{\"ok\":%s,\"exit\":%d,\"source\":\"airlift-direct-berry\",\"script\":",
			ret == 0 ? "true" : "false", ret);
	http_buf_json_string(&b, script);
	http_buf_puts(&b, ",\"output\":");
	http_buf_json_string(&b, output);
	http_buf_puts(&b, "}");
	if (b.overflow)
		http_json_error_body(body, body_len, "berry response too large");
}

static int http_wav_name_has_suffix(const char *name)
{
	size_t len;

	if (!name)
		return 0;
	len = strlen(name);
	if (len < 5)
		return 0;
	name += len - 4;
	return tolower((unsigned char)name[0]) == '.' &&
	       tolower((unsigned char)name[1]) == 'w' &&
	       tolower((unsigned char)name[2]) == 'a' &&
	       tolower((unsigned char)name[3]) == 'v';
}

static int http_valid_wav_file(const char *name)
{
	size_t i;
	size_t len;

	if (!name || !*name || name[0] == '.')
		return 0;
	len = strlen(name);
	if (len > WAV_FILE_MAX || !http_wav_name_has_suffix(name))
		return 0;
	if (strstr(name, ".."))
		return 0;
	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)name[i];

		if (c < 0x20 || c >= 0x7f || c == '/' || c == '\\')
			return 0;
	}
	return 1;
}

static int http_wav_file_path(const char *name, char *path, size_t path_len)
{
	int ret;

	if (!http_valid_wav_file(name))
		return -1;
	ret = snprintf(path, path_len, "%s/%s", WAV_DIR, name);
	return ret > 0 && (size_t)ret < path_len ? 0 : -1;
}

static void http_api_wav_list_body(char *body, size_t body_len)
{
	DIR *dir;
	struct dirent *de;
	struct http_buf b;
	unsigned int count = 0;
	int truncated = 0;

	dir = opendir(WAV_DIR);
	http_buf_init(&b, body, body_len);
	http_buf_printf(&b,
			"{\"ok\":%s,\"source\":\"airlift-direct\",\"dir\":\"%s\",\"files\":[",
			dir ? "true" : "false", WAV_DIR);
	if (dir) {
		while ((de = readdir(dir)) != NULL && count < WAV_LIST_MAX) {
			char path[sizeof(WAV_DIR) + 1 + WAV_FILE_MAX];
			struct stat st;

			if (!http_valid_wav_file(de->d_name) ||
			    http_wav_file_path(de->d_name, path, sizeof(path)) < 0 ||
			    stat(path, &st) < 0 || !S_ISREG(st.st_mode))
				continue;
			if (b.len + strlen(de->d_name) * 2 + 80 > b.cap) {
				truncated = 1;
				break;
			}
			if (count)
				http_buf_puts(&b, ",");
			http_buf_puts(&b, "{\"name\":");
			http_buf_json_string(&b, de->d_name);
			http_buf_printf(&b, ",\"bytes\":%lu}",
					(unsigned long)st.st_size);
			count++;
		}
		closedir(dir);
	}
	http_buf_printf(&b, "],\"count\":%u", count);
	if (truncated)
		http_buf_puts(&b, ",\"truncated\":true");
	if (!dir) {
		http_buf_puts(&b, ",\"error\":");
		http_buf_json_string(&b, "cannot open /mnt/sd/wavs");
	}
	http_buf_puts(&b, "}");
	if (b.overflow)
		http_json_error_body(body, body_len, "wav list response too large");
}

static void http_api_wav_play_body(const char *query, char *body,
				   size_t body_len)
{
	char file[WAV_FILE_MAX + 1];
	char backend[16];
	char loud[8];
	char path[sizeof(WAV_DIR) + 1 + WAV_FILE_MAX];
	char output[768];
	int use_beep;
	int ret;
	struct http_buf b;

	if (!http_query_param(query, "file", file, sizeof(file)) ||
	    http_wav_file_path(file, path, sizeof(path)) < 0) {
		http_json_error_body(body, body_len, "bad WAV file");
		return;
	}
	if (access(path, R_OK) < 0) {
		http_json_error_body(body, body_len, "WAV file not found");
		return;
	}
	http_query_param(query, "backend", backend, sizeof(backend));
	http_query_param(query, "loud", loud, sizeof(loud));
	use_beep = !strcmp(backend, "beep");
	if (!loud[0] || strcmp(loud, "0")) {
		char *const argv[] = {
			WAV_BIN, use_beep ? "--beep" : "--i2s", "--loud", path, NULL
		};

		ret = http_run_capture_timeout(argv, output, sizeof(output), 50000);
	} else {
		char *const argv[] = {
			WAV_BIN, use_beep ? "--beep" : "--i2s", path, NULL
		};

		ret = http_run_capture_timeout(argv, output, sizeof(output), 50000);
	}
	http_buf_init(&b, body, body_len);
	http_buf_printf(&b,
			"{\"ok\":%s,\"exit\":%d,\"source\":\"airlift-direct-wav\",\"file\":",
			ret == 0 ? "true" : "false", ret);
	http_buf_json_string(&b, file);
	http_buf_printf(&b, ",\"backend\":\"%s\",\"output\":",
			use_beep ? "beep" : "i2s");
	http_buf_json_string(&b, output);
	http_buf_puts(&b, "}");
	if (b.overflow)
		http_json_error_body(body, body_len, "wav response too large");
}

static void http_api_status_body(char *body, size_t body_len)
{
	struct http_buf b;
	int usbhost_power = http_gpio_value(11);

	http_buf_init(&b, body, body_len);
	http_buf_puts(&b, "{\"ok\":true,\"control\":{\"ok\":true,\"mode\":\"airlift-direct\",\"output\":\"direct C hardware paths in AirLift HTTP bridge\"}");
	http_buf_printf(&b, ",\"devices\":{\"neopixels\":%s,\"audio\":%s,\"i2c0\":%s,\"sd\":%s,\"dvi\":%s,\"usbhost\":%s,\"usbhost_power\":%d}",
			access("/dev/neopixels", W_OK) == 0 ? "true" : "false",
			access("/dev/fruitjam-audio", W_OK) == 0 ? "true" : "false",
			access("/dev/i2c-0", R_OK | W_OK) == 0 ? "true" : "false",
			http_is_mounted("/mnt/sd") ? "true" : "false",
			access(DVI_DEV, W_OK) == 0 ? "true" : "false",
			usbhost_power >= 0 ? "true" : "false",
			usbhost_power);
	http_buf_puts(&b, ",\"buttons\":[");
	http_button_json(&b, "button1", 0);
	http_buf_puts(&b, ",");
	http_button_json(&b, "button2", 4);
	http_buf_puts(&b, ",");
	http_button_json(&b, "button3", 5);
	http_buf_puts(&b, "]}");
	if (b.overflow)
		http_json_error_body(body, body_len, "status response too large");
}

#ifndef I2C_RDWR
#define I2C_RDWR 0x0707
#endif

struct fj_http_i2c_msg {
	unsigned short addr;
	unsigned short flags;
	unsigned short len;
	unsigned char *buf;
};

struct fj_http_i2c_rdwr {
	struct fj_http_i2c_msg *msgs;
	unsigned int nmsgs;
};

static int http_i2c_ping(int fd, int addr)
{
	unsigned char dummy = 0;
	struct fj_http_i2c_msg msg = { (unsigned short)addr, 0, 0, &dummy };
	struct fj_http_i2c_rdwr data = { &msg, 1 };

	return ioctl(fd, I2C_RDWR, &data) == 1 ? 0 : -1;
}

static void http_api_i2c_body(char *body, size_t body_len)
{
	struct http_buf b;
	int fd = open("/dev/i2c-0", O_RDWR);
	int first = 1;
	int addr;

	http_buf_init(&b, body, body_len);
	http_buf_printf(&b, "{\"ok\":%s,\"bus\":\"/dev/i2c-0\",\"source\":\"airlift-direct\",\"devices\":[",
			fd >= 0 ? "true" : "false");
	for (addr = 0x03; fd >= 0 && addr <= 0x77; addr++) {
		if (http_i2c_ping(fd, addr) != 0)
			continue;
		http_buf_printf(&b, "%s\"0x%02x%s\"", first ? "" : ",", addr,
				addr == 0x18 ? " TLV320DAC3100" : "");
		first = 0;
	}
	http_buf_printf(&b, "],\"exit\":%d", fd >= 0 ? 0 : 1);
	if (fd >= 0) {
		close(fd);
		http_buf_puts(&b, ",\"message\":\"live scan of /dev/i2c-0 (0x18 = onboard audio codec)\"");
	} else {
		http_buf_puts(&b, ",\"error\":\"cannot access /dev/i2c-0\"");
	}
	http_buf_puts(&b, "}");
	if (b.overflow)
		http_json_error_body(body, body_len, "i2c response too large");
}

static void http_api_dvi_body(const char *query, char *body, size_t body_len)
{
	static const char *const allowed[] = {
		"start", "show", "on", "stop", "off", "test",
		"pattern", "bars", "clear", "black", "white",
	};
	char cmd[24];
	char text[192];
	char output[256];
	size_t i;
	int ok = 0;
	int ret;
	struct http_buf b;

	if (!http_query_param(query, "cmd", cmd, sizeof(cmd)) || !cmd[0])
		strcpy(cmd, "test");
	if (!strcmp(cmd, "dashboard")) {
		char *const argv[] = { DVI_BIN, "dashboard", NULL };

		ret = http_run_capture_timeout(argv, output, sizeof(output), 7000);
		http_buf_init(&b, body, body_len);
		http_buf_printf(&b,
				"{\"ok\":%s,\"source\":\"fruitjam-dvi\",\"cmd\":\"dashboard\",\"exit\":%d,\"output\":",
				ret == 0 ? "true" : "false", ret);
		http_buf_json_string(&b, output);
		http_buf_puts(&b, "}");
		if (b.overflow)
			http_json_error_body(body, body_len, "dvi response too large");
		return;
	}
	if (!strcmp(cmd, "text")) {
		size_t j;

		if (!http_query_param(query, "text", text, sizeof(text)) ||
		    !text[0]) {
			http_json_error_body(body, body_len, "bad DVI text");
			return;
		}
		for (j = 0; text[j]; j++) {
			unsigned char c = (unsigned char)text[j];

			if (c < 0x20 || c > 0x7e) {
				http_json_error_body(body, body_len, "bad DVI text");
				return;
			}
		}
		{
			char *const argv[] = { DVI_BIN, "text", text, NULL };

			ret = http_run_capture_timeout(argv, output, sizeof(output), 7000);
		}
		http_buf_init(&b, body, body_len);
		http_buf_printf(&b,
				"{\"ok\":%s,\"source\":\"fruitjam-dvi\",\"cmd\":\"text\",\"exit\":%d,\"output\":",
				ret == 0 ? "true" : "false", ret);
		http_buf_json_string(&b, output);
		http_buf_puts(&b, "}");
		if (b.overflow)
			http_json_error_body(body, body_len, "dvi response too large");
		return;
	}
	for (i = 0; i < ARRAY_SIZE(allowed); i++) {
		if (!strcmp(cmd, allowed[i])) {
			ok = 1;
			break;
		}
	}
	if (!ok) {
		http_json_error_body(body, body_len, "bad DVI command");
		return;
	}
	if (http_write_existing_file(DVI_DEV, cmd) < 0) {
		http_json_error_body(body, body_len, "cannot write /dev/fruitjam-dvi");
		return;
	}
	http_buf_init(&b, body, body_len);
	http_buf_puts(&b, "{\"ok\":true,\"source\":\"airlift-direct-dvi\",\"cmd\":");
	http_buf_json_string(&b, cmd);
	http_buf_puts(&b, "}");
	if (b.overflow)
		http_json_error_body(body, body_len, "dvi response too large");
}

static const char *http_usb_device_state(int power, int dp, int dm)
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

static int http_status_text_int(const char *text, const char *key, int fallback)
{
	size_t key_len = strlen(key);
	const char *p = text;
	char *end;
	long value;

	while (p && *p) {
		const char *line_end = strchr(p, '\n');
		size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

		if (line_len > key_len && !strncmp(p, key, key_len) &&
		    p[key_len] == ' ') {
			errno = 0;
			value = strtol(p + key_len + 1, &end, 0);
			if (!errno && end != p + key_len + 1)
				return (int)value;
		}
		if (!line_end)
			break;
		p = line_end + 1;
	}

	return fallback;
}

static void http_status_text_string(const char *text, const char *key,
				    char *out, size_t out_len)
{
	size_t key_len = strlen(key);
	const char *p = text;

	if (!out_len)
		return;
	out[0] = '\0';
	while (p && *p) {
		const char *line_end = strchr(p, '\n');
		size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

		if (line_len > key_len && !strncmp(p, key, key_len) &&
		    p[key_len] == ' ') {
			size_t n = line_len - key_len - 1;

			if (n >= out_len)
				n = out_len - 1;
			memcpy(out, p + key_len + 1, n);
			out[n] = '\0';
			return;
		}
		if (!line_end)
			break;
		p = line_end + 1;
	}
}

static void http_api_usbhost_body(const char *query, char *body, size_t body_len)
{
	char cmd[48];
	char bridge_status[1024] = "";
	char last_rx_hex[65] = "";
	char helper_output[512] = "";
	int helper_ran = 0;
	int helper_ret = 0;
	unsigned int helper_seconds = 0;
	int bridge_ok = 0;
	int pio_ready = 0;
	int pio_configured = 0;
	int packets = 0;
	int tx_errors = 0;
	int last_tx_result = 0;
	int last_tx_len = 0;
	int rx_attempts = 0;
	int rx_errors = 0;
	int last_rx_result = 0;
	int last_rx_len = 0;
	int last_rx_pid = 0;
	int power;
	int dp;
	int dm;
	struct http_buf b;

	if (!http_query_param(query, "cmd", cmd, sizeof(cmd)) || !cmd[0])
		strcpy(cmd, "status");
	if (!strcmp(cmd, "on")) {
		if (access(USBHOST_DEV, W_OK) == 0) {
			if (http_write_existing_file(USBHOST_DEV, "on") < 0) {
				http_json_error_body(body, body_len, "cannot enable USB host power");
				return;
			}
		} else if (http_gpio_set_output(11, 1) < 0) {
			http_json_error_body(body, body_len, "cannot enable USB host power");
			return;
		}
	} else if (!strcmp(cmd, "off")) {
		if (access(USBHOST_DEV, W_OK) == 0) {
			if (http_write_existing_file(USBHOST_DEV, "off") < 0) {
				http_json_error_body(body, body_len, "cannot disable USB host power");
				return;
			}
		} else if (http_gpio_set_output(11, 0) < 0) {
			http_json_error_body(body, body_len, "cannot disable USB host power");
			return;
		}
	} else if (!strcmp(cmd, "in-token") ||
		   !strcmp(cmd, "get-device-8") ||
		   !strcmp(cmd, "get-device-8-combo-skipack") ||
		   !strcmp(cmd, "reset-get-device-8-combo-skipack") ||
		   !strcmp(cmd, "kbd-init") ||
		   !strcmp(cmd, "kbd-poll") ||
		   !strcmp(cmd, "kbd-init-poll")) {
		if (access(USBHOST_DEV, W_OK) != 0) {
			http_json_error_body(body, body_len,
					     "USB host PIO probe requires kernel bridge");
			return;
		}
		(void)http_write_existing_file(USBHOST_DEV, cmd);
	} else if (!strcmp(cmd, "reset")) {
		if (http_usbhost_bus_reset() < 0) {
			http_json_error_body(body, body_len, "cannot reset USB host bus");
			return;
		}
	} else if (!strcmp(cmd, "kbd-find")) {
		char *const argv[] = { USBHOST_BIN, "kbd-find", NULL };

		helper_ret = http_run_capture_timeout(argv, helper_output,
						      sizeof(helper_output), 9000);
		helper_ran = 1;
	} else if (!strcmp(cmd, "kbd-auto-text") ||
		   !strcmp(cmd, "kbd-auto-events") ||
		   !strcmp(cmd, "kbd-auto-shell")) {
		char *const argv[] = {
			USBHOST_BIN, cmd, USBHOST_KBD_WEB_SECONDS, NULL
		};

		helper_ret = http_run_capture_timeout(argv, helper_output,
						      sizeof(helper_output), 15000);
		helper_ran = 1;
		helper_seconds = 3;
	} else if (strcmp(cmd, "status")) {
		http_json_error_body(body, body_len, "bad USB host command");
		return;
	}

	if (access(USBHOST_DEV, R_OK) == 0 &&
	    http_read_text_file(USBHOST_DEV, bridge_status,
				sizeof(bridge_status)) == 0) {
		bridge_ok = 1;
		power = http_status_text_int(bridge_status, "power", -1);
		dp = http_status_text_int(bridge_status, "dp", -1);
		dm = http_status_text_int(bridge_status, "dm", -1);
		pio_ready = http_status_text_int(bridge_status, "pio_ready", 0);
		pio_configured = http_status_text_int(bridge_status, "pio_configured", 0);
		packets = http_status_text_int(bridge_status, "packets", 0);
		tx_errors = http_status_text_int(bridge_status, "tx_errors", 0);
		last_tx_result = http_status_text_int(bridge_status, "last_tx_result", 0);
		last_tx_len = http_status_text_int(bridge_status, "last_tx_len", 0);
		rx_attempts = http_status_text_int(bridge_status, "rx_attempts", 0);
		rx_errors = http_status_text_int(bridge_status, "rx_errors", 0);
		last_rx_result = http_status_text_int(bridge_status, "last_rx_result", 0);
		last_rx_len = http_status_text_int(bridge_status, "last_rx_len", 0);
		last_rx_pid = http_status_text_int(bridge_status, "last_rx_pid", 0);
		http_status_text_string(bridge_status, "last_rx_hex", last_rx_hex,
					sizeof(last_rx_hex));
	} else {
		power = http_gpio_value(11);
		dp = http_gpio_value(1);
		dm = http_gpio_value(2);
	}
	http_buf_init(&b, body, body_len);
	http_buf_printf(&b,
			"{\"ok\":%s,\"source\":\"airlift-direct-gpio\",\"cmd\":",
			helper_ran ? (helper_ret == 0 ? "true" : "false") :
			(power >= 0 ? "true" : "false"));
	http_buf_json_string(&b, cmd);
	http_buf_printf(&b, ",\"power\":%d,\"dp\":%d,\"dm\":%d,\"stack\":",
			power, dp, dm);
	http_buf_json_string(&b, access(USBHOST_DEV, R_OK) == 0 ?
			     "kernel bridge line-state; PIO2 host program staged; experimental boot-keyboard init/poll available" :
			     "sysfs line-state only; PIO USB host/HID report polling not implemented yet");
	http_buf_printf(&b, ",\"present\":%s,\"hid\":false,"
			"\"driver\":\"%s\","
			"\"pio_ready\":%s,"
			"\"pio_configured\":%s,"
			"\"packets\":%d,\"tx_errors\":%d,"
			"\"last_tx_result\":%d,\"last_tx_len\":%d,"
			"\"rx_attempts\":%d,\"rx_errors\":%d,"
			"\"last_rx_result\":%d,\"last_rx_len\":%d,"
			"\"last_rx_pid\":%d,\"last_rx_hex\":",
			power > 0 &&
			((dp == 1 && dm == 0) || (dp == 0 && dm == 1)) ?
			"true" : "false",
			bridge_ok ? "kernel-line-state" : "sysfs-line-state",
			pio_ready ? "true" : "false",
			pio_configured ? "true" : "false",
			packets, tx_errors, last_tx_result, last_tx_len,
			rx_attempts, rx_errors, last_rx_result, last_rx_len,
			last_rx_pid);
	http_buf_json_string(&b, last_rx_hex);
	http_buf_printf(&b,
			",\"probe_ok\":%s,\"next\":\"pio-packet-io\","
			"\"first_milestone\":\"boot-protocol-keyboard\"",
			rx_attempts > 0 && last_rx_result == 0 ? "true" : "false");
	if (!strcmp(cmd, "reset"))
		http_buf_printf(&b, ",\"reset_ms\":%u", USBHOST_RESET_MS);
	if (helper_ran) {
		http_buf_printf(&b, ",\"exit\":%d", helper_ret);
		if (helper_seconds)
			http_buf_printf(&b, ",\"seconds\":%u", helper_seconds);
		http_buf_puts(&b, ",\"output\":");
		http_buf_json_string(&b, helper_output);
	}
	http_buf_puts(&b, ",\"device\":");
	http_buf_json_string(&b, http_usb_device_state(power, dp, dm));
	http_buf_puts(&b, "}");
	if (b.overflow)
		http_json_error_body(body, body_len, "usbhost response too large");
}

static void http_api_neopixels_body(const char *query, char *body,
				    size_t body_len)
{
	unsigned int rgb[5][3];
	size_t i;
	int fd;

	for (i = 0; i < ARRAY_SIZE(rgb); i++) {
		char key[4];
		char value[16];

		snprintf(key, sizeof(key), "c%u", (unsigned int)i);
		http_query_param(query, key, value, sizeof(value));
		if (http_parse_color(value, rgb[i]) < 0) {
			http_json_error_body(body, body_len,
					     "bad color; use RRGGBB or #RRGGBB");
			return;
		}
	}

	fd = open("/dev/neopixels", O_WRONLY);
	if (fd < 0) {
		http_json_error_body(body, body_len, "cannot open /dev/neopixels");
		return;
	}
	dprintf(fd, "clear\n");
	for (i = 0; i < ARRAY_SIZE(rgb); i++) {
		dprintf(fd, "set %u %u %u %u\n",
			(unsigned int)i, rgb[i][0], rgb[i][1], rgb[i][2]);
	}
	dprintf(fd, "write\n");
	close(fd);
	snprintf(body, body_len,
		 "{\"ok\":true,\"source\":\"airlift-direct\",\"message\":\"neopixels updated\"}");
}

static void http_api_adc_body(const char *query, char *body, size_t body_len)
{
	char channel[16];
	char raw_attr[96];
	char mv_attr[96];
	char tmp[128];
	unsigned int raw;
	unsigned int mv;
	unsigned int ch;
	char *end;
	unsigned long parsed;
	struct http_buf b;

	if (!http_query_param(query, "channel", channel, sizeof(channel)) ||
	    !channel[0])
		strcpy(channel, "0");
	if (!http_valid_simple_arg(channel, 8, "0123456789temp")) {
		http_json_error_body(body, body_len, "bad adc channel");
		return;
	}
	if (!strcmp(channel, "temp")) {
		ch = ADC_TEMP_CH;
	} else {
		errno = 0;
		parsed = strtoul(channel, &end, 0);
		if (errno || *end || parsed > ADC_TEMP_CH) {
			http_json_error_body(body, body_len, "bad adc channel");
			return;
		}
		ch = (unsigned int)parsed;
	}

	snprintf(raw_attr, sizeof(raw_attr), "%s/raw%u", ADC_SYSFS, ch);
	snprintf(mv_attr, sizeof(mv_attr), "%s/millivolts%u", ADC_SYSFS, ch);
	if (http_read_text_file(raw_attr, tmp, sizeof(tmp)) < 0) {
		http_json_error_body(body, body_len,
				     "ADC raw sysfs attribute not available");
		return;
	}
	errno = 0;
	parsed = strtoul(tmp, &end, 0);
	if (errno || end == tmp) {
		http_json_error_body(body, body_len, "ADC raw sysfs value is invalid");
		return;
	}
	raw = (unsigned int)parsed;
	if (http_read_text_file(mv_attr, tmp, sizeof(tmp)) < 0) {
		http_json_error_body(body, body_len,
				     "ADC millivolts sysfs attribute not available");
		return;
	}
	errno = 0;
	parsed = strtoul(tmp, &end, 0);
	if (errno || end == tmp) {
		http_json_error_body(body, body_len,
				     "ADC millivolts sysfs value is invalid");
		return;
	}
	mv = (unsigned int)parsed;

	if (ch == ADC_TEMP_CH)
		snprintf(tmp, sizeof(tmp), "temp adc%u raw %u millivolts %u\n",
			 ch, raw, mv);
	else
		snprintf(tmp, sizeof(tmp), "gpio%u adc%u raw %u millivolts %u\n",
			 ADC_BASE_GPIO + ch, ch, raw, mv);

	http_buf_init(&b, body, body_len);
	http_buf_puts(&b, "{\"ok\":true,\"exit\":0,\"source\":\"airlift-direct\",\"channel\":");
	http_buf_json_string(&b, ch == ADC_TEMP_CH ? "temp" : channel);
	http_buf_printf(&b, ",\"raw\":%u,\"millivolts\":%u,\"output\":", raw, mv);
	http_buf_json_string(&b, tmp);
	http_buf_puts(&b, "}");
	if (b.overflow)
		http_json_error_body(body, body_len, "adc response too large");
}

static void http_api_button_body(const char *query, char *body, size_t body_len)
{
	char button[24];
	char line[32];
	int fd;

	if (!http_query_param(query, "button", button, sizeof(button)) ||
	    (strcmp(button, "button1") && strcmp(button, "button2") &&
	     strcmp(button, "button3"))) {
		http_json_error_body(body, body_len, "bad button name");
		return;
	}
	snprintf(line, sizeof(line), "test %s\n", button);
	fd = open(BUTTON_FIFO, O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		http_json_error_body(body, body_len, "button fifo not available");
		return;
	}
	if (write(fd, line, strlen(line)) != (ssize_t)strlen(line)) {
		close(fd);
		http_json_error_body(body, body_len, "button fifo write failed");
		return;
	}
	close(fd);
	snprintf(body, body_len, "{\"ok\":true,\"source\":\"airlift-direct-fifo\",\"output\":\"test %s\\n\"}",
		 button);
}

static void http_api_rtttl_body(const char *query, char *body, size_t body_len)
{
	char song[192];
	char tone[16];
	char ms[16];
	char output[512];
	char tone_arg[16];
	char ms_arg[16];
	unsigned int tone_hz;
	unsigned int tone_ms = 1200;
	int ret;
	struct http_buf b;

	if (http_query_param(query, "tone", tone, sizeof(tone)) && tone[0]) {
		if (http_parse_uint_limited(tone, 20, 8000, &tone_hz) < 0 ||
		    (http_query_param(query, "ms", ms, sizeof(ms)) && ms[0] &&
		     http_parse_uint_limited(ms, 20, 10000, &tone_ms) < 0)) {
			http_json_error_body(body, body_len, "bad tone parameters");
			return;
		}
		snprintf(tone_arg, sizeof(tone_arg), "%u", tone_hz);
		snprintf(ms_arg, sizeof(ms_arg), "%u", tone_ms);
		{
			char *const argv[] = {
				"/usr/bin/fruitjam-rtttl", "--tone",
				tone_arg, ms_arg, NULL
			};

			ret = http_run_capture_timeout(argv, output, sizeof(output),
						       tone_ms + 3500);
		}
		http_buf_init(&b, body, body_len);
		http_buf_printf(&b,
				"{\"ok\":%s,\"exit\":%d,\"source\":\"airlift-direct-helper\",\"mode\":\"tone\",\"tone\":%u,\"ms\":%u,\"output\":",
				ret == 0 ? "true" : "false", ret, tone_hz, tone_ms);
		http_buf_json_string(&b, output);
		http_buf_puts(&b, "}");
		if (b.overflow)
			http_json_error_body(body, body_len, "rtttl response too large");
		return;
	}

	if (http_query_param(query, "song", song, sizeof(song)) && song[0]) {
		char *const argv[] = {
			"/usr/bin/fruitjam-rtttl", song, NULL
		};

		if (!http_valid_simple_arg(song, 180,
					   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
					   "abcdefghijklmnopqrstuvwxyz"
					   "0123456789:=,#. -_")) {
			http_json_error_body(body, body_len, "bad RTTTL text");
			return;
		}
		ret = http_run_capture_timeout(argv, output, sizeof(output), 5000);
	} else {
		char *const argv[] = { "/usr/bin/fruitjam-rtttl", NULL };

		ret = http_run_capture_timeout(argv, output, sizeof(output), 5000);
	}

	http_buf_init(&b, body, body_len);
	http_buf_printf(&b, "{\"ok\":%s,\"exit\":%d,\"source\":\"airlift-direct-helper\",\"output\":",
			ret == 0 ? "true" : "false", ret);
	http_buf_json_string(&b, output);
	http_buf_puts(&b, "}");
	if (b.overflow)
		http_json_error_body(body, body_len, "rtttl response too large");
}

static int http_send_fruitjam_api(struct airlift *air, uint8_t sock,
				  const char *query)
{
	char action[32];
	char body[1280];
	int ret;
	int reboot_bootsel = 0;

	if (!http_query_param(query, "action", action, sizeof(action)) ||
	    !action[0] || !strcmp(action, "status"))
		http_api_status_body(body, sizeof(body));
	else if (!strcmp(action, "neopixels"))
		http_api_neopixels_body(query, body, sizeof(body));
	else if (!strcmp(action, "i2c"))
		http_api_i2c_body(body, sizeof(body));
	else if (!strcmp(action, "dvi"))
		http_api_dvi_body(query, body, sizeof(body));
	else if (!strcmp(action, "usbhost"))
		http_api_usbhost_body(query, body, sizeof(body));
	else if (!strcmp(action, "adc"))
		http_api_adc_body(query, body, sizeof(body));
	else if (!strcmp(action, "button-test"))
		http_api_button_body(query, body, sizeof(body));
	else if (!strcmp(action, "rtttl"))
		http_api_rtttl_body(query, body, sizeof(body));
	else if (!strcmp(action, "berry-list"))
		http_api_berry_list_body(body, sizeof(body));
	else if (!strcmp(action, "berry-run"))
		http_api_berry_run_body(query, body, sizeof(body));
	else if (!strcmp(action, "wav-list"))
		http_api_wav_list_body(body, sizeof(body));
	else if (!strcmp(action, "wav-play"))
		http_api_wav_play_body(query, body, sizeof(body));
	else if (!strcmp(action, "bootsel")) {
		snprintf(body, sizeof(body),
			 "{\"ok\":true,\"accepted\":true,\"verified\":false,"
			 "\"source\":\"airlift-direct\","
			 "\"message\":\"BOOTSEL request accepted; verify from host with picotool info -a\"}");
		reboot_bootsel = 1;
	}
	else
		http_json_error_body(body, sizeof(body), "unknown action");

	ret = http_reply_json(air, sock, body);
	if (!ret && reboot_bootsel &&
	    http_reboot_bootsel_after_delay(1500) < 0)
		fprintf(stderr, "airliftctl: reboot bootsel: %s\n", strerror(errno));
	return ret;
}

static int http_send_env_cgi(struct airlift *air, uint8_t sock, const char *query)
{
	char body[384];
	int len;

	len = snprintf(body, sizeof(body),
		       "Fruit Jam CGI OK\n"
		       "REQUEST_METHOD=GET\n"
		       "QUERY_STRING=%s\n"
		       "REMOTE_ADDR=\n"
		       "REMOTE_PORT=\n"
		       "SERVER_PROTOCOL=HTTP/1.0\n"
		       "\n"
		       "PATH=\n",
		       query ? query : "");
	if (len < 0 || (size_t)len >= sizeof(body))
		return http_reply_text(air, sock, 500, "Internal Server Error",
				       "env response too large\n");
	return http_reply_text(air, sock, 200, "OK", body);
}

static int http_send_file(struct airlift *air, uint8_t sock, const char *path)
{
	uint8_t buf[AIRLIFT_SEND_CHUNK];
	struct stat st;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return http_reply_text(air, sock, 404, "Not Found", "not found\n");
	if (fstat(fd, &st) < 0) {
		close(fd);
		return http_reply_text(air, sock, 500, "Internal Server Error",
				       "stat failed\n");
	}
	if (http_send_fmt(air, sock,
			 "HTTP/1.0 200 OK\r\n"
			 "Connection: close\r\n"
			 "Content-Type: %s\r\n"
			 "Access-Control-Allow-Origin: *\r\n"
			 "Cache-Control: no-store\r\n"
			 "Pragma: no-cache\r\n"
			 "Content-Length: %lu\r\n\r\n",
			 http_content_type(path), (unsigned long)st.st_size) < 0) {
		close(fd);
		return -1;
	}
	while (!stop_requested) {
		ssize_t got = read(fd, buf, sizeof(buf));

		inbound_heartbeat_poll();
		if (got < 0) {
			close(fd);
			return -1;
		}
		if (got == 0)
			break;
		if (nina_socket_send_all(air, sock, buf, (uint16_t)got) < 0) {
			close(fd);
			return -1;
		}
	}
	close(fd);
	return 0;
}

static int MAYBE_UNUSED http_send_cgi(struct airlift *air, uint8_t sock,
				      const char *script_name,
				      const char *exec_path,
				      const char *query)
{
	char query_env[300];
	char script_env[80];
	char *const envp[] = {
		"REQUEST_METHOD=GET",
		query_env,
		script_env,
		"GATEWAY_INTERFACE=CGI/1.1",
		"SERVER_PROTOCOL=HTTP/1.0",
		NULL
	};
	char path[64];
	uint8_t buf[256];
	char cgi_headers[512];
	struct stat st;
	ssize_t got;
	int fd;
	pid_t child;
	long deadline;
	int done = 0;
	size_t body_offset = 0;

	snprintf(query_env, sizeof(query_env), "QUERY_STRING=%s", query ? query : "");
	snprintf(script_env, sizeof(script_env), "SCRIPT_NAME=%s", script_name);
	snprintf(path, sizeof(path), "/tmp/fj-http-cgi-%ld.out", (long)getpid());
	fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (fd < 0)
		return http_reply_text(air, sock, 500, "Internal Server Error", "open failed\n");
	child = vfork();
	if (child < 0) {
		close(fd);
		unlink(path);
		return http_reply_text(air, sock, 500, "Internal Server Error", "vfork failed\n");
	}
	if (child == 0) {
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		close(fd);
		execle(exec_path, exec_path, (char *)NULL, envp);
		_exit(127);
	}

	deadline = now_ms() + 15000;
	while (!stop_requested && now_ms() < deadline) {
		pid_t ret = waitpid(child, NULL, WNOHANG);

		inbound_heartbeat_poll();
		if (ret == child) {
			done = 1;
			break;
		}
		if (ret < 0)
			break;
		usleep(10000);
	}
	if (!done) {
		kill(child, SIGTERM);
		waitpid(child, NULL, 0);
		close(fd);
		unlink(path);
		return http_reply_text(air, sock, 504, "Gateway Timeout", "cgi timed out\n");
	}
	if (fstat(fd, &st) < 0) {
		close(fd);
		unlink(path);
		return http_reply_text(air, sock, 500, "Internal Server Error",
				       "cgi stat failed\n");
	}
	lseek(fd, 0, SEEK_SET);
	got = read(fd, cgi_headers, sizeof(cgi_headers));
	if (got > 0) {
		size_t i;

		for (i = 0; i < (size_t)got; i++) {
			if (i + 3 < (size_t)got &&
			    cgi_headers[i] == '\r' && cgi_headers[i + 1] == '\n' &&
			    cgi_headers[i + 2] == '\r' && cgi_headers[i + 3] == '\n') {
				body_offset = i + 4;
				break;
			}
			if (i + 1 < (size_t)got &&
			    cgi_headers[i] == '\n' && cgi_headers[i + 1] == '\n') {
				body_offset = i + 2;
				break;
			}
		}
		if ((off_t)body_offset < st.st_size)
			st.st_size -= (off_t)body_offset;
		else
			body_offset = 0;
	}
	if (http_send_fmt(air, sock,
			  "HTTP/1.0 200 OK\r\n"
			  "Connection: close\r\n"
			  "Access-Control-Allow-Origin: *\r\n"
			  "Cache-Control: no-store\r\n"
			  "Pragma: no-cache\r\n"
			  "Content-Length: %lu\r\n\r\n",
			  (unsigned long)st.st_size) < 0) {
		close(fd);
		unlink(path);
		return -1;
	}
	lseek(fd, (off_t)body_offset, SEEK_SET);
	while ((got = read(fd, buf, sizeof(buf))) > 0) {
		inbound_heartbeat_poll();
		if (nina_socket_send_all(air, sock, buf, (uint16_t)got) < 0) {
			close(fd);
			unlink(path);
			return -1;
		}
	}
	close(fd);
	unlink(path);
	return got < 0 ? -1 : 0;
}

static int serve_http_client(struct airlift *air, uint8_t sock)
{
	char line[192];
	char target[160];
	char path[192];
	const char *root = SD_WEB_ROOT;
	const char *file_target;
	char *query;
	char *sp;

	if (http_read_line_recv(air, sock, line, sizeof(line),
				AIRLIFT_HTTP_IDLE_MS) <= 0)
		goto out;
	if (strncasecmp(line, "GET ", 4)) {
		http_reply_text(air, sock, 405, "Method Not Allowed", "only GET is supported\n");
		goto out;
	}
	sp = line + 4;
	while (*sp == ' ')
		sp++;
	if (strlen(sp) >= sizeof(target)) {
		http_reply_text(air, sock, 414, "URI Too Long", "uri too long\n");
		goto out;
	}
	strcpy(target, sp);
	sp = strchr(target, ' ');
	if (sp)
		*sp = '\0';

	while (http_read_line_recv(air, sock, line, sizeof(line),
				   AIRLIFT_HTTP_HEADER_DRAIN_MS) > 0) {
		if (!line[0])
			break;
	}

	query = strchr(target, '?');
	if (query)
		*query++ = '\0';
	if (!strcmp(target, "/cgi-bin/fruitjam.cgi")) {
		http_send_fruitjam_api(air, sock, query);
		goto out;
	}
	if (!strcmp(target, "/cgi-bin/env.cgi")) {
		http_send_env_cgi(air, sock, query);
		goto out;
	}
	if (!strncmp(target, "/cgi-bin/", 9)) {
		http_reply_text(air, sock, 404, "Not Found", "cgi not found\n");
		goto out;
	}

	file_target = target;
	if (!strcmp(target, PLAYGROUND_PREFIX)) {
		root = PLAYGROUND_ROOT;
		file_target = "/";
	} else if (!strncmp(target, PLAYGROUND_PREFIX "/", strlen(PLAYGROUND_PREFIX) + 1)) {
		root = PLAYGROUND_ROOT;
		file_target = target + strlen(PLAYGROUND_PREFIX);
	}
	if (!strcmp(file_target, "/"))
		file_target = "/index.html";
	if (safe_root_path(root, file_target, path, sizeof(path)) < 0) {
		http_reply_text(air, sock, 403, "Forbidden", "bad path\n");
		goto out;
	}
	http_send_file(air, sock, path);

out:
	nina_socket_close(air, sock);
	return 0;
}

static int get_ipaddr(struct airlift *air, uint8_t ip[4])
{
	static const uint8_t dummy = 0xff;
	const uint8_t *params[] = { &dummy };
	const size_t lens[] = { 1 };
	struct response *resp = &response_buf;

	if (nina_command(air, CMD_GET_IPADDR, params, lens, 1, 3, resp) < 0)
		return -1;
	if (resp->len[0] < 4)
		return -1;
	memcpy(ip, resp->data[0], 4);
	return 0;
}

enum ftp_data_mode {
	FTP_DATA_NONE = 0,
	FTP_DATA_PASSIVE,
	FTP_DATA_ACTIVE,
};

struct ftp_data_state {
	enum ftp_data_mode mode;
	uint8_t passive_listener;
	uint8_t passive_client;
	uint16_t passive_port;
	uint8_t active_ip[4];
	uint16_t active_port;
};

static void ftp_data_init(struct ftp_data_state *data)
{
	memset(data, 0, sizeof(*data));
	data->passive_listener = NINA_NO_SOCKET;
	data->passive_client = NINA_NO_SOCKET;
}

static void ftp_data_clear(struct airlift *air, struct ftp_data_state *data)
{
	if (data->passive_client != NINA_NO_SOCKET) {
		nina_socket_close(air, data->passive_client);
		data->passive_client = NINA_NO_SOCKET;
	}
	if (data->passive_listener != NINA_NO_SOCKET) {
		nina_socket_close(air, data->passive_listener);
		data->passive_listener = NINA_NO_SOCKET;
	}
	data->mode = FTP_DATA_NONE;
	data->passive_port = 0;
	data->active_port = 0;
	memset(data->active_ip, 0, sizeof(data->active_ip));
}

static int ftp_parse_port_arg(const char *arg, uint8_t ip[4], uint16_t *port)
{
	unsigned int h1, h2, h3, h4, p1, p2;
	char tail;

	if (sscanf(arg, "%u,%u,%u,%u,%u,%u%c",
		   &h1, &h2, &h3, &h4, &p1, &p2, &tail) != 6)
		return -1;
	if (h1 > 255 || h2 > 255 || h3 > 255 || h4 > 255 ||
	    p1 > 255 || p2 > 255)
		return -1;
	ip[0] = (uint8_t)h1;
	ip[1] = (uint8_t)h2;
	ip[2] = (uint8_t)h3;
	ip[3] = (uint8_t)h4;
	*port = (uint16_t)((p1 << 8) | p2);
	return *port ? 0 : -1;
}

static int ftp_parse_ipv4(const char *s, uint8_t ip[4])
{
	unsigned int h1, h2, h3, h4;
	char tail;

	if (sscanf(s, "%u.%u.%u.%u%c", &h1, &h2, &h3, &h4, &tail) != 4)
		return -1;
	if (h1 > 255 || h2 > 255 || h3 > 255 || h4 > 255)
		return -1;
	ip[0] = (uint8_t)h1;
	ip[1] = (uint8_t)h2;
	ip[2] = (uint8_t)h3;
	ip[3] = (uint8_t)h4;
	return 0;
}

static int ftp_parse_eprt_arg(const char *arg, uint8_t ip[4], uint16_t *port)
{
	char delim;
	const char *proto;
	const char *addr;
	const char *port_s;
	const char *end;
	unsigned long value;
	char addr_buf[32];
	char port_buf[12];
	size_t len;

	if (!arg || !*arg)
		return -1;
	delim = *arg++;
	proto = arg;
	addr = strchr(proto, delim);
	if (!addr)
		return -1;
	len = (size_t)(addr - proto);
	if (len != 1 || proto[0] != '1')
		return -1;
	addr++;
	port_s = strchr(addr, delim);
	if (!port_s)
		return -1;
	len = (size_t)(port_s - addr);
	if (len == 0 || len >= sizeof(addr_buf))
		return -1;
	memcpy(addr_buf, addr, len);
	addr_buf[len] = '\0';
	port_s++;
	end = strchr(port_s, delim);
	if (!end || end[1])
		return -1;
	len = (size_t)(end - port_s);
	if (len == 0 || len >= sizeof(port_buf))
		return -1;
	memcpy(port_buf, port_s, len);
	port_buf[len] = '\0';
	errno = 0;
	value = strtoul(port_buf, NULL, 10);
	if (errno || value == 0 || value > 65535)
		return -1;
	if (ftp_parse_ipv4(addr_buf, ip) < 0)
		return -1;
	*port = (uint16_t)value;
	return 0;
}

static int ftp_virtual_path(const char *cwd, const char *arg, char *out,
			    size_t out_len)
{
	char combined[160];
	char tmp[160];
	char *saveptr = NULL;
	char *part;
	size_t used = 1;
	int ret;

	if (!arg || !*arg)
		arg = ".";
	if (strchr(arg, '\\'))
		return -1;
	if (arg[0] == '/')
		ret = snprintf(combined, sizeof(combined), "%s", arg);
	else if (!strcmp(cwd, "/"))
		ret = snprintf(combined, sizeof(combined), "/%s", arg);
	else
		ret = snprintf(combined, sizeof(combined), "%s/%s", cwd, arg);
	if (ret < 0 || (size_t)ret >= sizeof(combined))
		return -1;

	out[0] = '/';
	out[1] = '\0';
	strcpy(tmp, combined);
	for (part = strtok_r(tmp, "/", &saveptr); part;
	     part = strtok_r(NULL, "/", &saveptr)) {
		size_t len;

		if (!strcmp(part, ".") || !*part)
			continue;
		if (!strcmp(part, "..")) {
			char *slash;

			if (!strcmp(out, "/"))
				continue;
			slash = strrchr(out, '/');
			if (!slash || slash == out)
				out[1] = '\0';
			else
				*slash = '\0';
			used = strlen(out);
			continue;
		}
		len = strlen(part);
		if (used + (used > 1 ? 1 : 0) + len >= out_len)
			return -1;
		if (used > 1) {
			strcat(out, "/");
			used++;
		}
		strcat(out, part);
		used += len;
	}
	return 0;
}

static int ftp_fs_path(const char *virt, char *out, size_t out_len)
{
	int ret;

	if (!virt || virt[0] != '/' || strstr(virt, "..") || strchr(virt, '\\'))
		return -1;
	ret = snprintf(out, out_len, "%s%s", FTP_ROOT, virt);
	return ret > 0 && (size_t)ret < out_len ? 0 : -1;
}

static int ftp_path_from_cwd(const char *cwd, const char *arg, char *virt,
			     size_t virt_len, char *path, size_t path_len)
{
	if (ftp_virtual_path(cwd, arg, virt, virt_len) < 0)
		return -1;
	return ftp_fs_path(virt, path, path_len);
}

static const char *ftp_basename(const char *virt)
{
	const char *base = strrchr(virt, '/');

	if (!base || !base[1])
		return "/";
	return base + 1;
}

static void ftp_format_mdtm(time_t mtime, char *out, size_t out_len)
{
	struct tm *tm = gmtime(&mtime);

	if (!tm || !strftime(out, out_len, "%Y%m%d%H%M%S", tm))
		snprintf(out, out_len, "19700101000000");
}

static void ftp_format_list_date(time_t mtime, char *out, size_t out_len)
{
	struct tm *tm = gmtime(&mtime);

	if (!tm || !strftime(out, out_len, "%b %d %Y", tm))
		snprintf(out, out_len, "Jan 01 1970");
}

static int MAYBE_UNUSED ftp_accept_data_client(struct airlift *air,
					       uint8_t data_sock,
					       uint8_t control_sock,
					       uint8_t *client_sock)
{
	long deadline = now_ms() + 7000;

	if (*client_sock != NINA_NO_SOCKET)
		return 0;
	while (!stop_requested && now_ms() < deadline) {
		if (tcp_accept_server(air, data_sock, 1, client_sock) < 0)
			return -1;
		if (*client_sock == control_sock) {
			*client_sock = NINA_NO_SOCKET;
			usleep(10000);
			continue;
		}
		if (*client_sock != NINA_NO_SOCKET) {
			printf("airlift inbound ftp data accepted socket %u\n",
			       *client_sock);
			fflush(stdout);
			return 0;
		}
		usleep(10000);
	}
	return -1;
}

static int ftp_start_passive_listener(struct airlift *air,
				      struct ftp_data_state *data,
				      uint16_t *port)
{
	uint16_t candidate;

	ftp_data_clear(air, data);
	for (candidate = DEFAULT_FTP_DATA_PORT;
	     candidate < DEFAULT_FTP_DATA_PORT + 10; candidate++) {
		uint8_t sock = NINA_NO_SOCKET;

		if (nina_socket_listen_tcp(air, candidate, &sock) == 0) {
			data->mode = FTP_DATA_PASSIVE;
			data->passive_listener = sock;
			data->passive_port = candidate;
			*port = candidate;
			printf("airlift inbound ftp passive listening on port %u socket %u\n",
			       candidate, sock);
			fflush(stdout);
			return 0;
		}
	}
	return -1;
}

static int ftp_accept_passive_client(struct airlift *air,
				     struct ftp_data_state *data,
				     uint8_t *data_client)
{
	long deadline = now_ms() + 15000;

	if (data->passive_client != NINA_NO_SOCKET) {
		*data_client = data->passive_client;
		data->passive_client = NINA_NO_SOCKET;
		return 0;
	}
	if (data->passive_listener == NINA_NO_SOCKET)
		return -1;

	while (!stop_requested && now_ms() < deadline) {
		uint8_t client = NINA_NO_SOCKET;

		if (nina_socket_accept(air, data->passive_listener, &client) < 0)
			return -1;
		if (client == NINA_NO_SOCKET) {
			usleep(10000);
			continue;
		}
		printf("airlift inbound ftp passive data accepted socket %u\n",
		       client);
		fflush(stdout);
		nina_socket_set_nonblock(air, client);
		*data_client = client;
		nina_socket_close(air, data->passive_listener);
		data->passive_listener = NINA_NO_SOCKET;
		return 0;
	}
	return -1;
}

static int ftp_open_data_client(struct airlift *air, uint8_t data_sock,
				uint8_t control_sock, struct ftp_data_state *data,
				uint8_t *data_client)
{
	(void)data_sock;
	(void)control_sock;

	*data_client = NINA_NO_SOCKET;
	if (data->mode == FTP_DATA_ACTIVE) {
		uint8_t sock = NINA_NO_SOCKET;

		if (nina_socket_create(air, NINA_SOCK_STREAM, NINA_IPPROTO_TCP,
				       &sock) < 0)
			return -1;
		if (nina_socket_connect(air, sock, data->active_ip,
					data->active_port) < 0) {
			nina_socket_close(air, sock);
			return -1;
		}
		printf("airlift inbound ftp active data connected socket %u to %u.%u.%u.%u:%u\n",
		       sock, data->active_ip[0], data->active_ip[1],
		       data->active_ip[2], data->active_ip[3], data->active_port);
		fflush(stdout);
		nina_socket_set_nonblock(air, sock);
		*data_client = sock;
		return 0;
	}
	if (data->mode == FTP_DATA_PASSIVE) {
		return ftp_accept_passive_client(air, data, data_client);
	}
	return -1;
}

static void ftp_close_data_client(struct airlift *air, uint8_t *data_client)
{
	if (*data_client != NINA_NO_SOCKET) {
		nina_socket_close(air, *data_client);
		*data_client = NINA_NO_SOCKET;
	}
}

static int ftp_send_data_file(struct airlift *air, uint8_t data_sock,
			      uint8_t control_sock, struct ftp_data_state *data,
			      const char *path)
{
	uint8_t buf[256];
	uint8_t data_client = NINA_NO_SOCKET;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	if (ftp_open_data_client(air, data_sock, control_sock, data, &data_client) < 0) {
		close(fd);
		return -1;
	}
	while (!stop_requested) {
		ssize_t got = read(fd, buf, sizeof(buf));

		inbound_heartbeat_poll();
		if (got < 0) {
			close(fd);
			ftp_close_data_client(air, &data_client);
			return -1;
		}
		if (got == 0)
			break;
		if (nina_socket_send_all(air, data_client, buf,
					 (uint16_t)got) < 0) {
			close(fd);
			ftp_close_data_client(air, &data_client);
			return -1;
		}
	}
	close(fd);
	ftp_close_data_client(air, &data_client);
	return 0;
}

static int ftp_send_data_line(struct airlift *air, uint8_t sock, const char *line)
{
	return nina_socket_send_all(air, sock, (const uint8_t *)line,
				    (uint16_t)strlen(line));
}

static int ftp_send_listing_entry(struct airlift *air, uint8_t sock,
				  const char *name, const struct stat *st,
				  int names_only)
{
	char line[256];
	char date[16];
	const char *perms = S_ISDIR(st->st_mode) ? "drwxr-xr-x" : "-rw-r--r--";

	if (names_only)
		snprintf(line, sizeof(line), "%.96s\r\n", name);
	else {
		ftp_format_list_date(st->st_mtime, date, sizeof(date));
		snprintf(line, sizeof(line), "%s 1 root root %lu %s %.96s\r\n",
			 perms, (unsigned long)st->st_size, date, name);
	}
	return ftp_send_data_line(air, sock, line);
}

static int ftp_send_listing(struct airlift *air, uint8_t data_sock,
			    uint8_t control_sock, struct ftp_data_state *data,
			    const char *path, const char *virt, int names_only)
{
	DIR *dir;
	struct dirent *ent;
	uint8_t data_client = NINA_NO_SOCKET;
	struct stat st;

	if (stat(path, &st) < 0)
		return -1;
	if (ftp_open_data_client(air, data_sock, control_sock, data, &data_client) < 0)
		return -1;
	if (!S_ISDIR(st.st_mode)) {
		if (ftp_send_listing_entry(air, data_client, ftp_basename(virt),
					   &st, names_only) < 0) {
			ftp_close_data_client(air, &data_client);
			return -1;
		}
		ftp_close_data_client(air, &data_client);
		return 0;
	}
	dir = opendir(path);
	if (!dir) {
		ftp_close_data_client(air, &data_client);
		return -1;
	}
	while ((ent = readdir(dir)) != NULL) {
		char child[192];
		struct stat st;
		int len;

		inbound_heartbeat_poll();
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;
		len = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
		if (len < 0 || (size_t)len >= sizeof(child))
			continue;
		if (stat(child, &st) < 0)
			memset(&st, 0, sizeof(st));
		if (ftp_send_listing_entry(air, data_client, ent->d_name,
					   &st, names_only) < 0) {
			closedir(dir);
			ftp_close_data_client(air, &data_client);
			return -1;
		}
	}
	closedir(dir);
	ftp_close_data_client(air, &data_client);
	return 0;
}

static int ftp_recv_data_file(struct airlift *air, uint8_t data_sock,
			      uint8_t control_sock, struct ftp_data_state *data,
			      const char *path, int append)
{
	uint8_t buf[512];
	uint8_t data_client = NINA_NO_SOCKET;
	long deadline;
	int fd;
	int saw_data = 0;

	if (ftp_open_data_client(air, data_sock, control_sock, data, &data_client) < 0)
		return -1;
	fd = open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
	if (fd < 0) {
		ftp_close_data_client(air, &data_client);
		return -1;
	}
	deadline = now_ms() + 15000;
	while (!stop_requested && now_ms() < deadline) {
		uint8_t flags = 0;

		inbound_heartbeat_poll();
		if (nina_socket_poll_raw(air, data_client, &flags) < 0)
			break;
		if (flags & NINA_SOCKET_POLL_RD) {
			uint16_t len = sizeof(buf);

			if (nina_socket_recv(air, data_client, buf, &len) < 0)
				break;
			if (len == 0) {
				if (saw_data) {
					close(fd);
					ftp_close_data_client(air, &data_client);
					return 0;
				}
				usleep(10000);
				continue;
			}
			if (len && write(fd, buf, len) != len)
				break;
			if (len) {
				saw_data = 1;
				deadline = now_ms() + 3000;
			}
			continue;
		}
		if (flags & (NINA_SOCKET_POLL_ERR | NINA_SOCKET_POLL_FAIL)) {
			close(fd);
			ftp_close_data_client(air, &data_client);
			return saw_data ? 0 : -1;
		}
		usleep(10000);
	}

	close(fd);
	ftp_close_data_client(air, &data_client);
	return saw_data ? 0 : -1;
}

static int serve_ftp_client(struct airlift *air, uint8_t sock, uint8_t data_sock,
			    uint8_t listen_sock, uint8_t http_sock,
			    uint8_t shell_sock,
			    const uint8_t local_ip[4])
{
	char line[160];
	char cwd[96] = "/";
	char rename_from[160] = "";
	struct ftp_data_state data;

	ftp_data_init(&data);

	if (ftp_reply(air, sock, 220, "Fruit Jam FTP ready") < 0) {
		nina_socket_close(air, sock);
		return -1;
	}

	while (!stop_requested) {
		char *arg;
		int got = ftp_raw_read_line_poll(air, sock, listen_sock,
						 http_sock, shell_sock, line,
						 sizeof(line), AIRLIFT_FTP_IDLE_MS);

		inbound_heartbeat_poll();
		if (got == FTP_READ_NEW_CONTROL) {
			ftp_reply(air, sock, 421, "Closing idle control connection");
			break;
		}
		if (got == FTP_READ_SERVICE_WAITING)
			break;
		if (got <= 0)
			break;
		arg = ftp_arg(line);
		if (!strcasecmp(line, "USER")) {
			ftp_reply(air, sock, 331, "Anonymous login ok");
		} else if (!strcasecmp(line, "PASS")) {
			ftp_reply(air, sock, 230, "Logged in");
		} else if (!strcasecmp(line, "SYST")) {
			ftp_reply(air, sock, 215, "UNIX Type: L8");
		} else if (!strcasecmp(line, "FEAT")) {
			ftp_send_fmt(air, sock,
				     "211-Features\r\n"
				     " EPSV\r\n"
				     " PASV\r\n"
				     " EPRT\r\n"
				     " PORT\r\n"
				     " SIZE\r\n"
				     " MDTM\r\n"
				     " UTF8\r\n"
				     "211 End\r\n");
		} else if (!strcasecmp(line, "PWD") || !strcasecmp(line, "XPWD")) {
			ftp_send_fmt(air, sock, "257 \"%s\"\r\n", cwd);
		} else if (!strcasecmp(line, "CWD") || !strcasecmp(line, "XCWD") ||
			   !strcasecmp(line, "CDUP")) {
			char virt[96];
			char path[160];
			struct stat st;
			const char *dest = !strcasecmp(line, "CDUP") ? ".." : arg;

			if (ftp_path_from_cwd(cwd, dest, virt, sizeof(virt),
					      path, sizeof(path)) < 0) {
				ftp_reply(air, sock, 550, "Bad directory");
			} else if (stat(path, &st) < 0 || !S_ISDIR(st.st_mode)) {
				ftp_reply(air, sock, 550, "Not a directory");
			} else {
				strcpy(cwd, virt);
				ftp_reply(air, sock, 250, "Directory changed");
			}
		} else if (!strcasecmp(line, "TYPE") ||
			   !strcasecmp(line, "MODE") ||
			   !strcasecmp(line, "STRU")) {
			ftp_reply(air, sock, 200, "Type set");
		} else if (!strcasecmp(line, "NOOP")) {
			ftp_reply(air, sock, 200, "OK");
		} else if (!strcasecmp(line, "ALLO")) {
			ftp_reply(air, sock, 202, "No storage allocation needed");
		} else if (!strcasecmp(line, "OPTS")) {
			ftp_reply(air, sock, 200, "OK");
		} else if (!strcasecmp(line, "REST")) {
			if (!*arg || !strcmp(arg, "0"))
				ftp_reply(air, sock, 350, "Restarting at 0");
			else
				ftp_reply(air, sock, 502, "Restart offsets are not supported");
		} else if (!strcasecmp(line, "PORT")) {
			uint8_t ip[4];
			uint16_t port;

			if (ftp_parse_port_arg(arg, ip, &port) < 0) {
				ftp_reply(air, sock, 501, "Bad PORT");
			} else {
				ftp_data_clear(air, &data);
				memcpy(data.active_ip, ip, sizeof(data.active_ip));
				data.active_port = port;
				data.mode = FTP_DATA_ACTIVE;
				ftp_reply(air, sock, 200, "PORT command successful");
			}
		} else if (!strcasecmp(line, "EPRT")) {
			uint8_t ip[4];
			uint16_t port;

			if (ftp_parse_eprt_arg(arg, ip, &port) < 0) {
				ftp_reply(air, sock, 501, "Bad EPRT");
			} else {
				ftp_data_clear(air, &data);
				memcpy(data.active_ip, ip, sizeof(data.active_ip));
				data.active_port = port;
				data.mode = FTP_DATA_ACTIVE;
				ftp_reply(air, sock, 200, "EPRT command successful");
			}
		} else if (!strcasecmp(line, "PASV")) {
			uint16_t port;

			if (ftp_start_passive_listener(air, &data, &port) < 0) {
				ftp_reply(air, sock, 425, "Cannot open passive data port");
			} else {
				ftp_send_fmt(air, sock,
					     "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)\r\n",
					     local_ip[0], local_ip[1],
					     local_ip[2], local_ip[3],
					     port >> 8, port & 0xffu);
			}
		} else if (!strcasecmp(line, "EPSV")) {
			uint16_t port;

			if (ftp_start_passive_listener(air, &data, &port) < 0)
				ftp_reply(air, sock, 425, "Cannot open passive data port");
			else
				ftp_send_fmt(air, sock,
					     "229 Entering Extended Passive Mode (|||%u|)\r\n",
					     port);
		} else if (!strcasecmp(line, "STOR") || !strcasecmp(line, "APPE")) {
			int append = !strcasecmp(line, "APPE");
			char virt[96];
			char path[160];

			if (data.mode == FTP_DATA_NONE) {
				ftp_reply(air, sock, 425, "Use PASV/EPSV or PORT/EPRT first");
			} else if (!*arg || ftp_path_from_cwd(cwd, arg, virt, sizeof(virt),
							      path, sizeof(path)) < 0) {
				ftp_reply(air, sock, 501, "Bad filename");
			} else if (ftp_reply(air, sock, 150, "Opening data connection") < 0 ||
				   ftp_recv_data_file(air, data_sock, sock,
						      &data, path, append) < 0) {
				ftp_data_clear(air, &data);
				ftp_reply(air, sock, 451, "Upload failed");
			} else {
				ftp_data_clear(air, &data);
				ftp_reply(air, sock, 226, "Transfer complete");
			}
		} else if (!strcasecmp(line, "LIST") || !strcasecmp(line, "NLST")) {
			int names_only = !strcasecmp(line, "NLST");
			char virt[96];
			char path[160];
			const char *target = arg;

			while (target[0] == '-') {
				target = strchr(target, ' ');
				if (!target)
					target = "";
				while (*target == ' ')
					target++;
			}
			if (data.mode == FTP_DATA_NONE) {
				ftp_reply(air, sock, 425, "Use PASV/EPSV or PORT/EPRT first");
			} else if (ftp_path_from_cwd(cwd, target, virt, sizeof(virt),
						     path, sizeof(path)) < 0) {
				ftp_reply(air, sock, 501, "Bad path");
			} else if (ftp_reply(air, sock, 150, "Opening data connection") < 0 ||
				   ftp_send_listing(air, data_sock, sock, &data,
						    path, virt, names_only) < 0) {
				ftp_data_clear(air, &data);
				ftp_reply(air, sock, 451, "List failed");
			} else {
				ftp_data_clear(air, &data);
				ftp_reply(air, sock, 226, "Transfer complete");
			}
		} else if (!strcasecmp(line, "RETR")) {
			char virt[96];
			char path[160];

			if (data.mode == FTP_DATA_NONE) {
				ftp_reply(air, sock, 425, "Use PASV/EPSV or PORT/EPRT first");
			} else if (!*arg || ftp_path_from_cwd(cwd, arg, virt, sizeof(virt),
							      path, sizeof(path)) < 0) {
				ftp_reply(air, sock, 501, "Bad filename");
			} else if (ftp_reply(air, sock, 150, "Opening data connection") < 0 ||
				   ftp_send_data_file(air, data_sock, sock,
						      &data, path) < 0) {
				ftp_data_clear(air, &data);
				ftp_reply(air, sock, 550, "Download failed");
			} else {
				ftp_data_clear(air, &data);
				ftp_reply(air, sock, 226, "Transfer complete");
			}
		} else if (!strcasecmp(line, "SIZE")) {
			char virt[96];
			char path[160];
			struct stat st;

			if (!*arg || ftp_path_from_cwd(cwd, arg, virt, sizeof(virt),
						       path, sizeof(path)) < 0) {
				ftp_reply(air, sock, 501, "Bad filename");
			} else if (stat(path, &st) < 0) {
				ftp_reply(air, sock, 550, "Not found");
			} else {
				ftp_send_fmt(air, sock, "213 %lu\r\n",
					     (unsigned long)st.st_size);
			}
		} else if (!strcasecmp(line, "MDTM")) {
			char virt[96];
			char path[160];
			struct stat st;
			char stamp[32];

			if (!*arg || ftp_path_from_cwd(cwd, arg, virt, sizeof(virt),
						       path, sizeof(path)) < 0) {
				ftp_reply(air, sock, 501, "Bad filename");
			} else if (stat(path, &st) < 0) {
				ftp_reply(air, sock, 550, "Not found");
			} else {
				ftp_format_mdtm(st.st_mtime, stamp, sizeof(stamp));
				ftp_send_fmt(air, sock, "213 %s\r\n", stamp);
			}
		} else if (!strcasecmp(line, "DELE")) {
			char virt[96];
			char path[160];

			if (!*arg || ftp_path_from_cwd(cwd, arg, virt, sizeof(virt),
						       path, sizeof(path)) < 0) {
				ftp_reply(air, sock, 501, "Bad filename");
			} else if (unlink(path) < 0) {
				ftp_reply(air, sock, 550, "Delete failed");
			} else {
				ftp_reply(air, sock, 250, "Deleted");
			}
		} else if (!strcasecmp(line, "MKD") || !strcasecmp(line, "XMKD")) {
			char virt[96];
			char path[160];

			if (!*arg || ftp_path_from_cwd(cwd, arg, virt, sizeof(virt),
						       path, sizeof(path)) < 0) {
				ftp_reply(air, sock, 501, "Bad directory");
			} else if (mkdir(path, 0755) < 0) {
				ftp_reply(air, sock, 550, "Create directory failed");
			} else {
				ftp_send_fmt(air, sock, "257 \"%s\" created\r\n",
					     virt);
			}
		} else if (!strcasecmp(line, "RMD") || !strcasecmp(line, "XRMD")) {
			char virt[96];
			char path[160];

			if (!*arg || ftp_path_from_cwd(cwd, arg, virt, sizeof(virt),
						       path, sizeof(path)) < 0) {
				ftp_reply(air, sock, 501, "Bad directory");
			} else if (rmdir(path) < 0) {
				ftp_reply(air, sock, 550, "Remove directory failed");
			} else {
				ftp_reply(air, sock, 250, "Directory removed");
			}
		} else if (!strcasecmp(line, "RNFR")) {
			char virt[96];

			if (!*arg || ftp_path_from_cwd(cwd, arg, virt, sizeof(virt),
						       rename_from, sizeof(rename_from)) < 0) {
				rename_from[0] = '\0';
				ftp_reply(air, sock, 501, "Bad filename");
			} else if (access(rename_from, F_OK) < 0) {
				rename_from[0] = '\0';
				ftp_reply(air, sock, 550, "Not found");
			} else {
				ftp_reply(air, sock, 350, "Ready for RNTO");
			}
		} else if (!strcasecmp(line, "RNTO")) {
			char virt[96];
			char path[160];

			if (!rename_from[0]) {
				ftp_reply(air, sock, 503, "Use RNFR first");
			} else if (!*arg || ftp_path_from_cwd(cwd, arg, virt, sizeof(virt),
							      path, sizeof(path)) < 0) {
				ftp_reply(air, sock, 501, "Bad filename");
			} else if (rename(rename_from, path) < 0) {
				ftp_reply(air, sock, 550, "Rename failed");
			} else {
				rename_from[0] = '\0';
				ftp_reply(air, sock, 250, "Renamed");
			}
		} else if (!strcasecmp(line, "QUIT")) {
			ftp_reply(air, sock, 221, "Bye");
			break;
		} else {
			ftp_reply(air, sock, 502, "Command not implemented");
		}
	}

	ftp_data_clear(air, &data);
	nina_socket_close(air, sock);
	return 0;
}

static int tftp_send_error(struct airlift *air, uint8_t sock, const uint8_t ip[4],
			   uint16_t port, uint16_t code, const char *msg)
{
	uint8_t packet[96];
	size_t msg_len = strlen(msg);
	size_t len;

	if (msg_len > sizeof(packet) - 5)
		msg_len = sizeof(packet) - 5;
	put_be16(&packet[0], 5);
	put_be16(&packet[2], code);
	memcpy(&packet[4], msg, msg_len);
	len = 4 + msg_len;
	packet[len++] = '\0';
	return udp_sendto(air, sock, ip, port, packet, (uint16_t)len);
}

static int tftp_send_ack(struct airlift *air, uint8_t sock, const uint8_t ip[4],
			 uint16_t port, uint16_t block)
{
	uint8_t packet[4];

	put_be16(&packet[0], 4);
	put_be16(&packet[2], block);
	return udp_sendto(air, sock, ip, port, packet, sizeof(packet));
}

static int tftp_same_peer(const uint8_t a[4], uint16_t aport,
			  const uint8_t b[4], uint16_t bport)
{
	return aport == bport && !memcmp(a, b, 4);
}

static int tftp_handle_wrq(struct airlift *air, uint8_t sock,
			   const uint8_t remote_ip[4], uint16_t remote_port,
			   const char *name)
{
	char path[160];
	uint8_t buf[560];
	uint16_t expected = 1;
	long deadline = now_ms() + 30000;
	int fd;

	if (safe_root_path(TFTP_ROOT, name, path, sizeof(path)) < 0)
		return tftp_send_error(air, sock, remote_ip, remote_port, 2, "bad path");
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return tftp_send_error(air, sock, remote_ip, remote_port, 2, "cannot create");
	if (tftp_send_ack(air, sock, remote_ip, remote_port, 0) < 0) {
		close(fd);
		return -1;
	}

	while (!stop_requested && now_ms() < deadline) {
		uint8_t peer_ip[4];
		uint16_t peer_port = 0;
		uint16_t avail = 0;
		uint16_t len = sizeof(buf);
		uint16_t opcode;
		uint16_t block;

		if (tcp_avail(air, sock, &avail) < 0)
			break;
		if (!avail) {
			usleep(10000);
			continue;
		}
		if (udp_remote_data(air, sock, peer_ip, &peer_port) < 0 ||
		    tcp_read_buf(air, sock, buf, &len) < 0)
			break;
		if (!tftp_same_peer(remote_ip, remote_port, peer_ip, peer_port) || len < 4)
			continue;
		deadline = now_ms() + 30000;
		opcode = get_be16(&buf[0]);
		block = get_be16(&buf[2]);
		if (opcode != 3)
			break;
		if (block == (uint16_t)(expected - 1)) {
			tftp_send_ack(air, sock, remote_ip, remote_port, block);
			continue;
		}
		if (block != expected)
			break;
		if (len > 4)
			write(fd, &buf[4], len - 4);
		tftp_send_ack(air, sock, remote_ip, remote_port, block);
		expected++;
		if (len < 516) {
			close(fd);
			return 0;
		}
	}

	close(fd);
	return -1;
}

static int MAYBE_UNUSED serve_tftp_packet(struct airlift *air, uint8_t sock)
{
	uint8_t buf[560];
	uint8_t remote_ip[4];
	char req_name[128];
	uint16_t remote_port = 0;
	uint16_t len = sizeof(buf);
	uint16_t opcode;
	const char *name;

	if (udp_remote_data(air, sock, remote_ip, &remote_port) < 0)
		return -1;
	if (tcp_read_buf(air, sock, buf, &len) < 0)
		return -1;
	if (len < 2)
		return 0;

	opcode = get_be16(buf);
	if (opcode == 4)
		return 0;
	if ((opcode != 1 && opcode != 2) || len < 4)
		return tftp_send_error(air, sock, remote_ip, remote_port, 4, "unsupported");

	name = (const char *)&buf[2];
	if (memchr(name, '\0', len - 2) == NULL)
		return tftp_send_error(air, sock, remote_ip, remote_port, 4, "bad request");
	while (*name == '/')
		name++;
	if (opcode == 2)
		return tftp_handle_wrq(air, sock, remote_ip, remote_port, name);
	len = (uint16_t)strlen(name);
	if (len >= sizeof(req_name))
		return tftp_send_error(air, sock, remote_ip, remote_port, 4, "bad request");
	memcpy(req_name, name, len + 1);
	name = req_name;

	memset(buf, 0, sizeof(buf));
	put_be16(&buf[0], 3);
	put_be16(&buf[2], 1);
	if (!*name || !strcmp(name, "README.txt")) {
		memcpy(&buf[4], TFTP_README_TEXT, strlen(TFTP_README_TEXT));
		len = (uint16_t)strlen(TFTP_README_TEXT);
	} else {
		char path[160];
		int fd;
		ssize_t got;

		if (safe_root_path(TFTP_ROOT, name, path, sizeof(path)) < 0)
			return tftp_send_error(air, sock, remote_ip, remote_port, 2, "bad path");
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return tftp_send_error(air, sock, remote_ip, remote_port, 1, "not found");
		got = read(fd, &buf[4], 512);
		close(fd);
		if (got < 0)
			return tftp_send_error(air, sock, remote_ip, remote_port, 1, "read failed");
		len = (uint16_t)got;
	}
	return udp_sendto(air, sock, remote_ip, remote_port, buf, (uint16_t)(4 + len));
}

static int serve_inbound(struct airlift *air)
{
	uint8_t ftp_sock = NINA_NO_SOCKET;
	uint8_t shell_sock = NINA_NO_SOCKET;
	uint8_t http_sock = NINA_NO_SOCKET;
	uint8_t local_ip[4] = { 0, 0, 0, 0 };
	struct telnet_session telnet;

	stop_all_nina_sockets(air);

	mkdir_p(FTP_ROOT);
	get_ipaddr(air, local_ip);
	telnet_session_init(&telnet);

	if (tcp_get_socket(air, &shell_sock) < 0)
		return -1;
	if (tcp_start_server(air, DEFAULT_SHELL_PORT, shell_sock) < 0)
		return -1;
	if (nina_socket_listen_tcp(air, DEFAULT_HTTP_PORT, &http_sock) < 0) {
		fprintf(stderr, "airliftctl: raw HTTP listener retry after raw socket cleanup\n");
		close_stale_raw_sockets(air);
		stop_all_nina_sockets(air);
		if (tcp_get_socket(air, &shell_sock) < 0)
			return -1;
		if (tcp_start_server(air, DEFAULT_SHELL_PORT, shell_sock) < 0)
			return -1;
	}
	if (http_sock == NINA_NO_SOCKET &&
	    nina_socket_listen_tcp(air, DEFAULT_HTTP_PORT, &http_sock) < 0) {
		fprintf(stderr, "airliftctl: raw HTTP listener failed on port %u\n",
			DEFAULT_HTTP_PORT);
		goto fail;
	}
	if (nina_socket_listen_tcp(air, DEFAULT_FTP_PORT, &ftp_sock) < 0) {
		fprintf(stderr, "airliftctl: raw FTP listener failed on port %u\n",
			DEFAULT_FTP_PORT);
		ftp_sock = NINA_NO_SOCKET;
	}

	stop_requested = 0;
	signal(SIGINT, request_stop);
	signal(SIGTERM, request_stop);
	inbound_heartbeat_active = 1;
	inbound_heartbeat_touch();
	if (ftp_sock != NINA_NO_SOCKET)
		printf("airlift inbound ftp listening on port %u socket %u\n",
		       DEFAULT_FTP_PORT, ftp_sock);
	else
		printf("airlift inbound ftp disabled\n");
	printf("airlift inbound shell listening on port %u socket %u\n",
	       DEFAULT_SHELL_PORT, shell_sock);
	printf("airlift inbound http listening on port %u socket %u\n",
	       DEFAULT_HTTP_PORT, http_sock);
	fflush(stdout);

	while (!stop_requested) {
		uint8_t client_sock = NINA_NO_SOCKET;

		inbound_heartbeat_poll();
		telnet_session_poll(air, &telnet);

		client_sock = NINA_NO_SOCKET;
		if (tcp_accept_server(air, shell_sock, 1, &client_sock) == 0 &&
		    client_sock != NINA_NO_SOCKET) {
			printf("airlift inbound shell accepted socket %u\n", client_sock);
			fflush(stdout);
			if (tcp_wait_connected(air, client_sock) < 0) {
				tcp_close_client(air, client_sock);
			} else if (telnet_session_active(&telnet)) {
				tcp_send_fmt(air, client_sock,
					     "Fruit Jam telnet shell is busy; try again later\r\n");
				tcp_close_client(air, client_sock);
			} else if (telnet_session_start(&telnet, client_sock,
							DEFAULT_SHELL_PATH) < 0) {
				tcp_send_fmt(air, client_sock,
					     "Fruit Jam telnet shell failed to start\r\n");
				tcp_close_client(air, client_sock);
			} else {
				telnet_send_negotiation(air, client_sock);
			}
		}

		telnet_session_poll(air, &telnet);

		client_sock = NINA_NO_SOCKET;
		if (nina_socket_accept(air, http_sock, &client_sock) == 0 &&
		    client_sock != NINA_NO_SOCKET) {
			printf("airlift inbound http accepted socket %u\n", client_sock);
			fflush(stdout);
			nina_socket_set_nonblock(air, client_sock);
			serve_http_client(air, client_sock);
		}

		telnet_session_poll(air, &telnet);

		client_sock = NINA_NO_SOCKET;
		if (ftp_sock != NINA_NO_SOCKET &&
		    nina_socket_accept(air, ftp_sock, &client_sock) == 0 &&
		    client_sock != NINA_NO_SOCKET) {
			printf("airlift inbound ftp accepted socket %u\n", client_sock);
			fflush(stdout);
			nina_socket_set_nonblock(air, client_sock);
			serve_ftp_client(air, client_sock, NINA_NO_SOCKET,
					 ftp_sock, http_sock, shell_sock,
					 local_ip);
		}

		telnet_session_poll(air, &telnet);

		usleep(20000);
	}

	telnet_session_close(air, &telnet);
	nina_socket_close(air, ftp_sock);
	nina_socket_close(air, http_sock);
	tcp_stop_server(air, shell_sock);
	inbound_heartbeat_active = 0;
	return 0;

fail:
	inbound_heartbeat_active = 0;
	if (ftp_sock != NINA_NO_SOCKET)
		nina_socket_close(air, ftp_sock);
	if (shell_sock != NINA_NO_SOCKET)
		tcp_stop_server(air, shell_sock);
	if (http_sock != NINA_NO_SOCKET)
		nina_socket_close(air, http_sock);
	return -1;
}

static int mqtt_read_byte_timeout(struct airlift *air, uint8_t sock,
				  uint8_t *byte, long timeout_ms)
{
	long deadline = now_ms() + timeout_ms;

	while (!stop_requested && now_ms() < deadline) {
		uint16_t avail = 0;
		uint16_t len = 1;

		if (tcp_avail(air, sock, &avail) < 0)
			return -1;
		if (!avail) {
			usleep(50000);
			continue;
		}
		if (tcp_read_buf(air, sock, byte, &len) < 0 || len != 1)
			return -1;
		return 0;
	}
	return 1;
}

static int mqtt_read_packet_timeout(struct airlift *air, uint8_t sock,
				    uint8_t *header, uint8_t *payload,
				    size_t cap, size_t *payload_len,
				    long timeout_ms)
{
	size_t multiplier = 1;
	size_t value = 0;
	int i;
	int ret;

	ret = mqtt_read_byte_timeout(air, sock, header, timeout_ms);
	if (ret)
		return ret;

	for (i = 0; i < 4; i++) {
		uint8_t encoded = 0;

		if (mqtt_read_byte_timeout(air, sock, &encoded, 1000) != 0)
			return -1;
		value += (encoded & 127u) * multiplier;
		if (!(encoded & 128u))
			break;
		multiplier *= 128;
	}
	if (i == 4 || value > cap)
		return -1;
	for (i = 0; i < (int)value; i++) {
		if (mqtt_read_byte_timeout(air, sock, payload + i, 1000) != 0)
			return -1;
	}
	*payload_len = value;
	return 0;
}

static int mqtt_send_connect(struct airlift *air, uint8_t sock,
			     const char *client_id, const char *username,
			     const char *password)
{
	uint8_t pkt[512];
	uint8_t body[512];
	uint8_t resp[64];
	uint8_t header = 0;
	size_t pos = 0;
	size_t body_pos = 0;
	size_t resp_len = 0;
	uint8_t flags = 2;

	if (username && *username)
		flags |= 0x80;
	if (password && *password)
		flags |= 0x40;

	if (mqtt_put_string(body, sizeof(body), &body_pos, "MQTT") < 0)
		return -1;
	body[body_pos++] = 4;
	body[body_pos++] = flags;
	body[body_pos++] = 0;
	body[body_pos++] = 60;
	if (mqtt_put_string(body, sizeof(body), &body_pos, client_id) < 0)
		return -1;
	if (username && *username &&
	    mqtt_put_string(body, sizeof(body), &body_pos, username) < 0)
		return -1;
	if (password && *password &&
	    mqtt_put_string(body, sizeof(body), &body_pos, password) < 0)
		return -1;

	pkt[pos++] = 0x10;
	if (mqtt_put_remaining_length(pkt, sizeof(pkt), &pos, body_pos) < 0 ||
	    pos + body_pos > sizeof(pkt))
		return -1;
	memcpy(pkt + pos, body, body_pos);
	pos += body_pos;

	if (tcp_send(air, sock, pkt, (uint16_t)pos) < 0)
		return -1;
	if (tcp_check_sent(air, sock) < 0)
		return -1;
	if (mqtt_read_packet_timeout(air, sock, &header, resp, sizeof(resp),
				     &resp_len, 8000) != 0) {
		fprintf(stderr, "airliftctl: MQTT CONNACK timeout\n");
		return -1;
	}
	if (header != 0x20 || resp_len != 2 || resp[0] != 0x00 ||
	    resp[1] != 0x00) {
		fprintf(stderr, "airliftctl: MQTT connect refused header=0x%02x len=%u rc=0x%02x\n",
			header, (unsigned int)resp_len,
			resp_len > 1 ? resp[1] : 0xff);
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
			const char *client_id, const char *username,
			const char *password)
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
	if (mqtt_send_connect(air, sock, client_id, username, password) < 0)
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

static int mqtt_send_subscribe(struct airlift *air, uint8_t sock,
			       const char *topic)
{
	uint8_t pkt[512];
	uint8_t resp[64];
	uint8_t header = 0;
	size_t pos = 0;
	size_t body_start;
	size_t body_len;
	size_t resp_len = 0;

	pkt[pos++] = 0x82;
	pos += 4;
	body_start = pos;
	pkt[pos++] = 0;
	pkt[pos++] = 1;
	if (mqtt_put_string(pkt, sizeof(pkt), &pos, topic) < 0)
		return -1;
	pkt[pos++] = 0;
	body_len = pos - body_start;
	{
		uint8_t rem[4];
		size_t rem_pos = 0;

		if (mqtt_put_remaining_length(rem, sizeof(rem), &rem_pos,
					      body_len) < 0)
			return -1;
		memmove(pkt + 1 + rem_pos, pkt + body_start, body_len);
		memcpy(pkt + 1, rem, rem_pos);
		pos = 1 + rem_pos + body_len;
	}

	if (tcp_send(air, sock, pkt, (uint16_t)pos) < 0)
		return -1;
	if (tcp_check_sent(air, sock) < 0)
		return -1;
	if (mqtt_read_packet_timeout(air, sock, &header, resp, sizeof(resp),
				     &resp_len, 8000) != 0 ||
	    header != 0x90 || resp_len < 3 || resp[0] != 0 ||
	    resp[1] != 1 || resp[2] == 0x80) {
		fprintf(stderr, "airliftctl: MQTT SUBACK failed\n");
		return -1;
	}
	return 0;
}

static int mqtt_print_publish(const uint8_t *payload, size_t len, bool verbose)
{
	size_t topic_len;
	size_t pos = 2;
	size_t payload_len;
	const uint8_t *message;

	if (len < 2)
		return -1;
	topic_len = ((size_t)payload[0] << 8) | payload[1];
	if (pos + topic_len > len)
		return -1;
	if (verbose) {
		fwrite(payload + pos, 1, topic_len, stdout);
		putchar(' ');
	}
	pos += topic_len;
	message = payload + pos;
	payload_len = len - pos;
	fwrite(message, 1, payload_len, stdout);
	if (!payload_len || message[payload_len - 1] != '\n')
		putchar('\n');
	fflush(stdout);
	return 0;
}

static int mqtt_subscribe_loop(struct airlift *air, uint8_t sock,
			       unsigned int seconds, unsigned int count,
			       bool verbose)
{
	uint8_t payload[1024];
	long end = seconds ? now_ms() + ((long)seconds * 1000L) : 0;
	long last_ping = now_ms();
	unsigned int got = 0;

	while (!stop_requested && (!count || got < count)) {
		uint8_t header = 0;
		size_t payload_len = 0;
		int ret;

		if (seconds && now_ms() >= end)
			return got ? 0 : -1;

		ret = mqtt_read_packet_timeout(air, sock, &header, payload,
					       sizeof(payload), &payload_len,
					       1000);
		if (ret < 0)
			return -1;
		if (ret > 0) {
			if (now_ms() - last_ping >= 30000L) {
				static const uint8_t ping[] = { 0xc0, 0x00 };

				if (tcp_send(air, sock, ping, sizeof(ping)) < 0 ||
				    tcp_check_sent(air, sock) < 0)
					return -1;
				last_ping = now_ms();
			}
			continue;
		}
		if ((header >> 4) == 3 &&
		    mqtt_print_publish(payload, payload_len, verbose) == 0)
			got++;
	}
	return stop_requested ? -1 : 0;
}

static int mqtt_subscribe(struct airlift *air, const char *host, uint16_t port,
			  const char *topic, const char *client_id,
			  const char *username, const char *password,
			  unsigned int seconds, unsigned int count, bool verbose)
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
	if (mqtt_send_connect(air, sock, client_id, username, password) < 0)
		goto out;
	if (mqtt_send_subscribe(air, sock, topic) < 0)
		goto out;
	printf("mqtt subscribed %s:%u %s\n", host, port, topic);
	ret = mqtt_subscribe_loop(air, sock, seconds, count, verbose);
	tcp_send(air, sock, disconnect, sizeof(disconnect));
	tcp_check_sent(air, sock);

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

	ret = airlift_lock_acquire(argv[arg]);
	if (ret > 0)
		return 0;
	if (ret < 0)
		return 1;

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
	if (!strcmp(argv[arg], "serve-shell")) {
		uint16_t port = DEFAULT_SHELL_PORT;
		const char *shell = DEFAULT_SHELL_PATH;

		if (arg + 1 < argc && parse_port(argv[arg + 1], &port) < 0) {
			fprintf(stderr, "airliftctl: invalid shell bridge port\n");
			return 2;
		}
		if (arg + 2 < argc)
			shell = argv[arg + 2];
		if (arg + 3 < argc) {
			usage(stderr);
			return 2;
		}
		return serve_shell(&air, port, shell) < 0 ? 1 : 0;
	}
	if (!strcmp(argv[arg], "serve-inbound")) {
		if (arg + 1 < argc) {
			usage(stderr);
			return 2;
		}
		return serve_inbound(&air) < 0 ? 1 : 0;
	}
	if (!strcmp(argv[arg], "tcp-get")) {
		if (arg + 1 >= argc) {
			usage(stderr);
			return 2;
		}
		return tcp_get(&air, argv[arg + 1], arg + 2 < argc ? argv[arg + 2] : "/") < 0 ? 1 : 0;
	}
	if (!strcmp(argv[arg], "mqtt-pub")) {
		uint16_t port;
		const char *client_id = "fruitjam-rp2350";
		const char *username = NULL;
		const char *password = NULL;

		if (arg + 4 >= argc) {
			usage(stderr);
			return 2;
		}
		if (parse_port(argv[arg + 2], &port) < 0) {
			fprintf(stderr, "airliftctl: invalid MQTT port\n");
			return 2;
		}
		if (arg + 5 < argc)
			client_id = argv[arg + 5];
		if (arg + 6 < argc)
			username = argv[arg + 6];
		if (arg + 7 < argc)
			password = argv[arg + 7];
		if (arg + 8 < argc) {
			usage(stderr);
			return 2;
		}
		if (password && (!username || !*username)) {
			fprintf(stderr, "airliftctl: MQTT password requires username\n");
			return 2;
		}
		return mqtt_publish(&air, argv[arg + 1], port,
				    argv[arg + 3], argv[arg + 4],
				    client_id, username, password) < 0 ? 1 : 0;
	}
	if (!strcmp(argv[arg], "mqtt-sub")) {
		uint16_t port;
		const char *client_id = "fruitjam-rp2350-sub";
		const char *username = NULL;
		const char *password = NULL;
		unsigned int seconds = 30;
		unsigned int count = 0;
		bool verbose = false;

		if (arg + 3 >= argc) {
			usage(stderr);
			return 2;
		}
		if (parse_port(argv[arg + 2], &port) < 0) {
			fprintf(stderr, "airliftctl: invalid MQTT port\n");
			return 2;
		}
		if (arg + 4 < argc)
			client_id = argv[arg + 4];
		if (arg + 5 < argc && argv[arg + 5][0])
			username = argv[arg + 5];
		if (arg + 6 < argc && argv[arg + 6][0])
			password = argv[arg + 6];
		if (arg + 7 < argc)
			seconds = (unsigned int)strtoul(argv[arg + 7], NULL, 10);
		if (arg + 8 < argc)
			count = (unsigned int)strtoul(argv[arg + 8], NULL, 10);
		if (arg + 9 < argc)
			verbose = atoi(argv[arg + 9]) != 0;
		if (arg + 10 < argc) {
			usage(stderr);
			return 2;
		}
		if (password && (!username || !*username)) {
			fprintf(stderr, "airliftctl: MQTT password requires username\n");
			return 2;
		}
		return mqtt_subscribe(&air, argv[arg + 1], port, argv[arg + 3],
				      client_id, username, password, seconds,
				      count, verbose) < 0 ? 1 : 0;
	}

	usage(stderr);
	fprintf(stderr, "airliftctl: unknown command: %s\n", argv[arg]);
	return 2;
}
