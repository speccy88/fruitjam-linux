// SPDX-License-Identifier: MIT
/*
 * Small netcat-compatible tool for the Fruit Jam no-MMU image.
 *
 * BusyBox nc works, but its flat binary is just large enough to need a 128 KiB
 * contiguous allocation. This implements the connect/listen forms used by the
 * bring-up scripts while keeping the executable in a much smaller class.
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

static void usage(void)
{
	fprintf(stderr, "usage: nc [-l] [-p port] [host] [port]\n");
}

static int parse_port(const char *s)
{
	char *end;
	long port = strtol(s, &end, 10);

	if (!*s || *end || port <= 0 || port > 65535) {
		fprintf(stderr, "nc: bad port: %s\n", s);
		return -1;
	}
	return (int)port;
}

static int resolve_host(const char *host, struct in_addr *addr)
{
	if (inet_aton(host, addr))
		return 0;

	fprintf(stderr, "nc: bad IPv4 address: %s\n", host);
	return -1;
}

static int pump(int sock)
{
	int stdin_open = 1;
	int sock_open = 1;
	char buf[256];

	for (;;) {
		fd_set rfds;
		int maxfd = sock;
		int ret;

		FD_ZERO(&rfds);
		if (sock_open)
			FD_SET(sock, &rfds);
		if (stdin_open) {
			FD_SET(STDIN_FILENO, &rfds);
			if (STDIN_FILENO > maxfd)
				maxfd = STDIN_FILENO;
		}
		if (!stdin_open && !sock_open)
			return 0;

		ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("nc: select");
			return 1;
		}

		if (stdin_open && FD_ISSET(STDIN_FILENO, &rfds)) {
			ssize_t got = read(STDIN_FILENO, buf, sizeof(buf));
			if (got < 0) {
				perror("nc: read stdin");
				return 1;
			}
			if (got == 0) {
				stdin_open = 0;
				shutdown(sock, SHUT_WR);
				if (!sock_open)
					return 0;
			} else if (write(sock, buf, got) != got) {
				perror("nc: write socket");
				return 1;
			}
		}

		if (sock_open && FD_ISSET(sock, &rfds)) {
			ssize_t got = read(sock, buf, sizeof(buf));
			if (got < 0) {
				perror("nc: read socket");
				return 1;
			}
			if (got == 0) {
				sock_open = 0;
				if (!stdin_open)
					return 0;
				continue;
			}
			if (write(STDOUT_FILENO, buf, got) != got) {
				perror("nc: write stdout");
				return 1;
			}
		}
	}
}

static int connect_mode(const char *host, int port)
{
	struct sockaddr_in addr;
	int sock;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	if (resolve_host(host, &addr.sin_addr) < 0)
		return 1;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("nc: socket");
		return 1;
	}
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("nc: connect");
		close(sock);
		return 1;
	}

	return pump(sock);
}

static int listen_mode(int port)
{
	struct sockaddr_in addr;
	int one = 1;
	int srv;
	int sock;
	int ret;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)port);

	srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0) {
		perror("nc: socket");
		return 1;
	}
	setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("nc: bind");
		close(srv);
		return 1;
	}
	if (listen(srv, 1) < 0) {
		perror("nc: listen");
		close(srv);
		return 1;
	}

	sock = accept(srv, NULL, NULL);
	close(srv);
	if (sock < 0) {
		perror("nc: accept");
		return 1;
	}

	ret = pump(sock);
	close(sock);
	return ret;
}

int main(int argc, char **argv)
{
	int listen = 0;
	int port = -1;
	const char *host = NULL;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage();
			return 0;
		}
		if (!strcmp(argv[i], "-l")) {
			listen = 1;
			continue;
		}
		if (!strcmp(argv[i], "-p")) {
			if (++i >= argc) {
				usage();
				return 1;
			}
			port = parse_port(argv[i]);
			if (port < 0)
				return 1;
			continue;
		}
		if (!host)
			host = argv[i];
		else if (port < 0) {
			port = parse_port(argv[i]);
			if (port < 0)
				return 1;
		} else {
			usage();
			return 1;
		}
	}

	if (listen) {
		if (port < 0 && host) {
			port = parse_port(host);
			if (port < 0)
				return 1;
		}
		if (port < 0) {
			usage();
			return 1;
		}
		return listen_mode(port);
	}

	if (!host || port < 0) {
		usage();
		return 1;
	}
	return connect_mode(host, port);
}
