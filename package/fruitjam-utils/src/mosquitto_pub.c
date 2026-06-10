// SPDX-License-Identifier: MIT
/*
 * Tiny mosquitto_pub-compatible MQTT v3.1.1 QoS 0 publisher.
 *
 * This intentionally implements only the subset needed by Fruit Jam button
 * actions: plain TCP, CONNECT, one QoS 0 PUBLISH, DISCONNECT.
 */

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static void usage(FILE *out)
{
	fprintf(out,
		"usage: mosquitto_pub -h host [-p port] -t topic -m message "
		"[-i clientid] [--airlift] [-r] [-q 0]\n");
}

static int put_remaining_length(unsigned char *buf, size_t *pos, size_t len)
{
	do {
		unsigned char byte = len % 128;

		len /= 128;
		if (len)
			byte |= 0x80;
		buf[(*pos)++] = byte;
	} while (len);
	return 0;
}

static int put_string(unsigned char *buf, size_t cap, size_t *pos, const char *s)
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

static int exec_airlift_mqtt(const char *host, const char *port,
			     const char *topic, const char *message,
			     const char *client_id, int retain)
{
	if (retain) {
		fprintf(stderr, "mosquitto_pub: --airlift does not support retain\n");
		return 2;
	}
	execl("/usr/bin/airliftctl", "airliftctl", "mqtt-pub",
	      host, port, topic, message, client_id, (char *)NULL);
	fprintf(stderr, "mosquitto_pub: exec /usr/bin/airliftctl: %s\n",
		strerror(errno));
	return 1;
}

static int mqtt_connect(int fd, const char *client_id)
{
	unsigned char pkt[256];
	unsigned char resp[4];
	size_t pos = 0;
	size_t rem_pos;
	size_t payload_start;
	size_t rem_len;

	pkt[pos++] = 0x10;
	rem_pos = pos++;
	if (put_string(pkt, sizeof(pkt), &pos, "MQTT") < 0)
		return -1;
	pkt[pos++] = 4;    /* MQTT 3.1.1 */
	pkt[pos++] = 2;    /* clean session */
	pkt[pos++] = 0;
	pkt[pos++] = 60;   /* keepalive */
	payload_start = pos;
	if (put_string(pkt, sizeof(pkt), &pos, client_id) < 0)
		return -1;
	rem_len = pos - payload_start + 10;
	if (rem_len > 127)
		return -1;
	pkt[rem_pos] = (unsigned char)rem_len;

	if (write_all(fd, pkt, pos) < 0)
		return -1;
	if (read(fd, resp, sizeof(resp)) != (ssize_t)sizeof(resp))
		return -1;
	if (resp[0] != 0x20 || resp[1] != 0x02 || resp[2] != 0x00 || resp[3] != 0x00)
		return -1;
	return 0;
}

static int mqtt_publish(int fd, const char *topic, const char *message, int retain)
{
	size_t topic_len = strlen(topic);
	size_t msg_len = strlen(message);
	size_t rem_len = 2 + topic_len + msg_len;
	size_t cap = 1 + 4 + rem_len;
	unsigned char *pkt;
	size_t pos = 0;
	int ret;

	if (topic_len > 65535)
		return -1;
	pkt = malloc(cap);
	if (!pkt)
		return -1;

	pkt[pos++] = retain ? 0x31 : 0x30;
	put_remaining_length(pkt, &pos, rem_len);
	if (put_string(pkt, cap, &pos, topic) < 0) {
		free(pkt);
		return -1;
	}
	memcpy(pkt + pos, message, msg_len);
	pos += msg_len;

	ret = write_all(fd, pkt, pos);
	free(pkt);
	return ret;
}

int main(int argc, char **argv)
{
	const char *host = NULL;
	const char *port = "1883";
	const char *topic = NULL;
	const char *message = NULL;
	const char *client_id = "fruitjam";
	int retain = 0;
	int airlift = 0;
	int fd;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") && i + 1 < argc) {
			host = argv[++i];
		} else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
			port = argv[++i];
		} else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
			topic = argv[++i];
		} else if (!strcmp(argv[i], "-m") && i + 1 < argc) {
			message = argv[++i];
		} else if (!strcmp(argv[i], "-i") && i + 1 < argc) {
			client_id = argv[++i];
		} else if (!strcmp(argv[i], "-q") && i + 1 < argc) {
			if (strcmp(argv[++i], "0")) {
				fprintf(stderr, "mosquitto_pub: only QoS 0 is supported\n");
				return 2;
			}
		} else if (!strcmp(argv[i], "-r")) {
			retain = 1;
		} else if (!strcmp(argv[i], "--airlift")) {
			airlift = 1;
		} else {
			usage(stderr);
			return 2;
		}
	}

	if (!host || !topic || !message) {
		usage(stderr);
		return 2;
	}

	if (airlift)
		return exec_airlift_mqtt(host, port, topic, message, client_id, retain);

	fd = tcp_connect(host, port);
	if (fd < 0) {
		fprintf(stderr, "mosquitto_pub: connect %s:%s failed\n", host, port);
		return 1;
	}
	if (mqtt_connect(fd, client_id) < 0) {
		fprintf(stderr, "mosquitto_pub: MQTT connect failed\n");
		close(fd);
		return 1;
	}
	if (mqtt_publish(fd, topic, message, retain) < 0) {
		fprintf(stderr, "mosquitto_pub: publish failed\n");
		close(fd);
		return 1;
	}

	{
		unsigned char disconnect[] = { 0xe0, 0x00 };
		write_all(fd, disconnect, sizeof(disconnect));
	}
	close(fd);
	return 0;
}
