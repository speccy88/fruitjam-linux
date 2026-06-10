// SPDX-License-Identifier: MIT
/*
 * Tiny telnet client for loopback and bring-up tests.
 *
 * It strips TELNET negotiation bytes from the display stream and refuses every
 * option. That is enough for a plain shell on BusyBox telnetd.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define IAC 255
#define DONT 254
#define DO 253
#define WONT 252
#define WILL 251
#define SB 250
#define SE 240

static int resolve_host(const char *host, struct in_addr *addr)
{
	if (inet_aton(host, addr))
		return 0;

	fprintf(stderr, "telnet: bad IPv4 address: %s\n", host);
	return -1;
}

static int parse_port(const char *s)
{
	char *end;
	long port = strtol(s, &end, 10);

	if (!*s || *end || port <= 0 || port > 65535) {
		fprintf(stderr, "telnet: bad port: %s\n", s);
		return -1;
	}
	return (int)port;
}

static void reply_option(int sock, unsigned char cmd, unsigned char opt)
{
	unsigned char resp[3];

	resp[0] = IAC;
	resp[1] = (cmd == DO || cmd == DONT) ? WONT : DONT;
	resp[2] = opt;
	write(sock, resp, sizeof(resp));
}

static int write_data(const unsigned char *buf, int len)
{
	return len == 0 || write(STDOUT_FILENO, buf, len) == len;
}

static int handle_socket(int sock)
{
	unsigned char in[256];
	unsigned char out[256];
	int out_len = 0;
	ssize_t got;
	int i;

	got = read(sock, in, sizeof(in));
	if (got < 0) {
		perror("telnet: read socket");
		return -1;
	}
	if (got == 0)
		return 0;

	for (i = 0; i < got; i++) {
		unsigned char c = in[i];

		if (c != IAC) {
			out[out_len++] = c;
			if (out_len == (int)sizeof(out)) {
				if (!write_data(out, out_len))
					return -1;
				out_len = 0;
			}
			continue;
		}

		if (++i >= got)
			break;
		c = in[i];
		if (c == IAC) {
			out[out_len++] = IAC;
			continue;
		}
		if (c == DO || c == DONT || c == WILL || c == WONT) {
			if (++i >= got)
				break;
			reply_option(sock, c, in[i]);
			continue;
		}
		if (c == SB) {
			while (++i < got) {
				if (in[i] == IAC && i + 1 < got && in[i + 1] == SE) {
					i++;
					break;
				}
			}
		}
	}

	if (!write_data(out, out_len))
		return -1;
	return 1;
}

static int pump(int sock)
{
	int stdin_open = 1;
	char buf[256];

	for (;;) {
		fd_set rfds;
		int maxfd = sock;
		int ret;

		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);
		if (stdin_open) {
			FD_SET(STDIN_FILENO, &rfds);
			if (STDIN_FILENO > maxfd)
				maxfd = STDIN_FILENO;
		}

		ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("telnet: select");
			return 1;
		}

		if (stdin_open && FD_ISSET(STDIN_FILENO, &rfds)) {
			ssize_t got = read(STDIN_FILENO, buf, sizeof(buf));
			if (got < 0) {
				perror("telnet: read stdin");
				return 1;
			}
			if (got == 0) {
				stdin_open = 0;
				shutdown(sock, SHUT_WR);
			} else {
				char out[512];
				ssize_t i;
				size_t out_len = 0;

				for (i = 0; i < got; i++) {
					if (buf[i] == '\n')
						out[out_len++] = '\r';
					out[out_len++] = buf[i];
				}
				if (write(sock, out, out_len) != (ssize_t)out_len) {
					perror("telnet: write socket");
					return 1;
				}
			}
		}

		if (FD_ISSET(sock, &rfds)) {
			ret = handle_socket(sock);
			if (ret <= 0)
				return ret < 0;
		}
	}
}

int main(int argc, char **argv)
{
	struct sockaddr_in addr;
	int port = 23;
	int sock;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: telnet host [port]\n");
		return 1;
	}
	if (argc == 3) {
		port = parse_port(argv[2]);
		if (port < 0)
			return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	if (resolve_host(argv[1], &addr.sin_addr) < 0)
		return 1;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("telnet: socket");
		return 1;
	}
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("telnet: connect");
		close(sock);
		return 1;
	}

	return pump(sock);
}
