// SPDX-License-Identifier: MIT
/*
 * Tiny mosquitto_sub-compatible MQTT v3.1.1 QoS 0 subscriber.
 *
 * This is intentionally small for Fruit Jam's no-MMU image. It supports the
 * broker smoke-test shape: CONNECT, SUBSCRIBE QoS 0, text PUBLISH output,
 * optional username/password, finite -C/-W tests, and --airlift handoff.
 */

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MQTT_MAX_PACKET 1536

static void usage(FILE *out)
{
	fprintf(out,
		"usage: mosquitto_sub -h host [-p port] -t topic "
		"[-i clientid] [-u username] [-P password] [-C count] "
		"[-W seconds] [-v] [--airlift] [-q 0]\n");
}

static int put_remaining_length(unsigned char *buf, size_t *pos, size_t len)
{
	do {
		unsigned char byte = len % 128;

		len /= 128;
		buf[(*pos)++] = len ? (byte | 0x80) : byte;
	} while (len);
	return 0;
}

static int put_string(unsigned char *buf, size_t cap, size_t *pos,
		      const char *s)
{
	size_t len = strlen(s);

	if (len > 65535 || *pos + 2 + len > cap)
		return -1;
	buf[(*pos)++] = (unsigned char)(len >> 8);
	buf[(*pos)++] = (unsigned char)len;
	memcpy(buf + *pos, s, len);
	*pos += len;
	return 0;
}

static int write_all(int fd, const unsigned char *buf, size_t len)
{
	while (len) {
		ssize_t ret = write(fd, buf, len);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		buf += ret;
		len -= (size_t)ret;
	}
	return 0;
}

static int read_all(int fd, unsigned char *buf, size_t len)
{
	while (len) {
		ssize_t ret = read(fd, buf, len);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (ret == 0)
			return -1;
		buf += ret;
		len -= (size_t)ret;
	}
	return 0;
}

static int read_remaining_length(int fd, size_t *len)
{
	size_t multiplier = 1;
	size_t value = 0;
	int i;

	for (i = 0; i < 4; i++) {
		unsigned char encoded;

		if (read_all(fd, &encoded, 1) < 0)
			return -1;
		value += (encoded & 127u) * multiplier;
		if (!(encoded & 128u)) {
			*len = value;
			return 0;
		}
		multiplier *= 128;
	}
	return -1;
}

static int read_packet(int fd, unsigned char *header, unsigned char *buf,
		       size_t cap, size_t *len)
{
	if (read_all(fd, header, 1) < 0)
		return -1;
	if (read_remaining_length(fd, len) < 0 || *len > cap)
		return -1;
	return read_all(fd, buf, *len);
}

static int tcp_connect(const char *host, const char *port)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct addrinfo *ai;
	int fd = -1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port, &hints, &res) != 0)
		return -1;

	for (ai = res; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}

static int mqtt_connect(int fd, const char *client_id, const char *username,
			const char *password)
{
	unsigned char pkt[512];
	unsigned char body[512];
	unsigned char resp[MQTT_MAX_PACKET];
	unsigned char header;
	size_t pos = 0;
	size_t body_pos = 0;
	size_t len = 0;
	unsigned char flags = 2; /* clean session */

	if (put_string(body, sizeof(body), &body_pos, "MQTT") < 0)
		return -1;
	body[body_pos++] = 4;
	if (username)
		flags |= 0x80;
	if (password)
		flags |= 0x40;
	body[body_pos++] = flags;
	body[body_pos++] = 0;
	body[body_pos++] = 60;
	if (put_string(body, sizeof(body), &body_pos, client_id) < 0)
		return -1;
	if (username && put_string(body, sizeof(body), &body_pos, username) < 0)
		return -1;
	if (password && put_string(body, sizeof(body), &body_pos, password) < 0)
		return -1;

	pkt[pos++] = 0x10;
	put_remaining_length(pkt, &pos, body_pos);
	if (pos + body_pos > sizeof(pkt))
		return -1;
	memcpy(pkt + pos, body, body_pos);
	pos += body_pos;

	if (write_all(fd, pkt, pos) < 0)
		return -1;
	if (read_packet(fd, &header, resp, sizeof(resp), &len) < 0 ||
	    header != 0x20 || len != 2 || resp[0] != 0 || resp[1] != 0)
		return -1;
	return 0;
}

static int mqtt_subscribe(int fd, const char *topic)
{
	unsigned char pkt[512];
	unsigned char resp[MQTT_MAX_PACKET];
	unsigned char header;
	size_t pos = 0;
	size_t body_start;
	size_t body_len;
	size_t len = 0;

	pkt[pos++] = 0x82;
	pos += 4;
	body_start = pos;
	pkt[pos++] = 0;
	pkt[pos++] = 1;
	if (put_string(pkt, sizeof(pkt), &pos, topic) < 0)
		return -1;
	pkt[pos++] = 0; /* requested QoS 0 */
	body_len = pos - body_start;
	{
		unsigned char rem[4];
		size_t rem_pos = 0;

		put_remaining_length(rem, &rem_pos, body_len);
		memmove(pkt + 1 + rem_pos, pkt + body_start, body_len);
		memcpy(pkt + 1, rem, rem_pos);
		pos = 1 + rem_pos + body_len;
	}

	if (write_all(fd, pkt, pos) < 0)
		return -1;
	if (read_packet(fd, &header, resp, sizeof(resp), &len) < 0 ||
	    header != 0x90 || len < 3 || resp[0] != 0 || resp[1] != 1 ||
	    resp[2] == 0x80)
		return -1;
	return 0;
}

static int print_publish(const unsigned char *pkt, size_t len, int verbose)
{
	size_t topic_len;
	size_t pos = 2;
	size_t payload_len;
	const unsigned char *payload;

	if (len < 2)
		return -1;
	topic_len = ((size_t)pkt[0] << 8) | pkt[1];
	if (pos + topic_len > len)
		return -1;
	if (verbose) {
		fwrite(pkt + pos, 1, topic_len, stdout);
		putchar(' ');
	}
	pos += topic_len;
	payload = pkt + pos;
	payload_len = len - pos;
	fwrite(payload, 1, payload_len, stdout);
	if (!payload_len || payload[payload_len - 1] != '\n')
		putchar('\n');
	fflush(stdout);
	return 0;
}

static long now_sec(void)
{
	return (long)time(NULL);
}

static int mqtt_loop(int fd, int count, int wait_seconds, int verbose)
{
	unsigned char pkt[MQTT_MAX_PACKET];
	long end = wait_seconds > 0 ? now_sec() + wait_seconds : 0;
	long last_ping = now_sec();
	int got = 0;

	while (count <= 0 || got < count) {
		fd_set rfds;
		struct timeval tv;
		int ret;

		if (wait_seconds > 0 && now_sec() >= end)
			return got > 0 ? 0 : 1;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		ret = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return 1;
		}
		if (ret == 0) {
			if (now_sec() - last_ping >= 30) {
				static const unsigned char ping[] = { 0xc0, 0x00 };

				if (write_all(fd, ping, sizeof(ping)) < 0)
					return 1;
				last_ping = now_sec();
			}
			continue;
		}

		{
			unsigned char header;
			size_t len = 0;

			if (read_packet(fd, &header, pkt, sizeof(pkt), &len) < 0)
				return 1;
			if ((header >> 4) == 3) {
				if (print_publish(pkt, len, verbose) == 0)
					got++;
			}
		}
	}
	return 0;
}

static int exec_airlift_sub(const char *host, const char *port,
			    const char *topic, const char *client_id,
			    const char *username, const char *password,
			    int wait_seconds, int count, int verbose)
{
	char wait_buf[16];
	char count_buf[16];
	char verbose_buf[4];

	snprintf(wait_buf, sizeof(wait_buf), "%d", wait_seconds);
	snprintf(count_buf, sizeof(count_buf), "%d", count);
	snprintf(verbose_buf, sizeof(verbose_buf), "%d", verbose ? 1 : 0);
	execl("/usr/bin/airliftctl", "airliftctl", "mqtt-sub",
	      host, port, topic, client_id, username ? username : "",
	      password ? password : "", wait_buf, count_buf, verbose_buf,
	      (char *)NULL);
	fprintf(stderr, "mosquitto_sub: exec /usr/bin/airliftctl: %s\n",
		strerror(errno));
	return 1;
}

int main(int argc, char **argv)
{
	const char *host = NULL;
	const char *port = "1883";
	const char *topic = NULL;
	const char *client_id = "fruitjam-sub";
	const char *username = NULL;
	const char *password = NULL;
	int airlift = 0;
	int verbose = 0;
	int count = 0;
	int wait_seconds = 0;
	int fd;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") && i + 1 < argc) {
			host = argv[++i];
		} else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
			port = argv[++i];
		} else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
			topic = argv[++i];
		} else if (!strcmp(argv[i], "-i") && i + 1 < argc) {
			client_id = argv[++i];
		} else if (!strcmp(argv[i], "-u") && i + 1 < argc) {
			username = argv[++i];
		} else if (!strcmp(argv[i], "-P") && i + 1 < argc) {
			password = argv[++i];
		} else if (!strcmp(argv[i], "-C") && i + 1 < argc) {
			count = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "-W") && i + 1 < argc) {
			wait_seconds = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "-q") && i + 1 < argc) {
			if (strcmp(argv[++i], "0")) {
				fprintf(stderr, "mosquitto_sub: only QoS 0 is supported\n");
				return 2;
			}
		} else if (!strcmp(argv[i], "-v")) {
			verbose = 1;
		} else if (!strcmp(argv[i], "--airlift")) {
			airlift = 1;
		} else {
			usage(stderr);
			return 2;
		}
	}

	if (!host || !topic || count < 0 || wait_seconds < 0) {
		usage(stderr);
		return 2;
	}
	if (password && !username) {
		fprintf(stderr, "mosquitto_sub: -P requires -u\n");
		return 2;
	}
	if (airlift)
		return exec_airlift_sub(host, port, topic, client_id, username,
					password, wait_seconds, count, verbose);

	fd = tcp_connect(host, port);
	if (fd < 0) {
		fprintf(stderr, "mosquitto_sub: connect %s:%s failed\n", host, port);
		return 1;
	}
	if (mqtt_connect(fd, client_id, username, password) < 0) {
		fprintf(stderr, "mosquitto_sub: MQTT connect failed\n");
		close(fd);
		return 1;
	}
	if (mqtt_subscribe(fd, topic) < 0) {
		fprintf(stderr, "mosquitto_sub: subscribe failed\n");
		close(fd);
		return 1;
	}

	i = mqtt_loop(fd, count, wait_seconds, verbose);
	close(fd);
	return i;
}
