// SPDX-License-Identifier: MIT
/*
 * Tiny HTTP-only wget for Fruit Jam no-MMU smoke tests.
 *
 * Supports:
 *   wget URL
 *   wget -O FILE URL
 *   wget -O - URL
 *
 * URLs must use http:// and an IPv4 literal host.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct url_parts {
	char host[32];
	char path[160];
	int port;
};

static void usage(void)
{
	fprintf(stderr, "usage: wget [-O file|-] http://ipv4[:port]/path\n");
}

static int parse_url(const char *url, struct url_parts *out)
{
	const char *p;
	const char *slash;
	const char *colon;
	size_t host_len;

	if (strncmp(url, "http://", 7)) {
		fprintf(stderr, "wget: only http:// URLs are supported\n");
		return -1;
	}
	p = url + 7;
	slash = strchr(p, '/');
	if (!slash)
		slash = p + strlen(p);
	colon = memchr(p, ':', slash - p);

	host_len = (colon ? colon : slash) - p;
	if (host_len == 0 || host_len >= sizeof(out->host)) {
		fprintf(stderr, "wget: bad host\n");
		return -1;
	}
	memcpy(out->host, p, host_len);
	out->host[host_len] = '\0';

	out->port = 80;
	if (colon) {
		char *end;
		long port = strtol(colon + 1, &end, 10);
		if (end != slash || port <= 0 || port > 65535) {
			fprintf(stderr, "wget: bad port\n");
			return -1;
		}
		out->port = (int)port;
	}

	if (*slash)
		snprintf(out->path, sizeof(out->path), "%s", slash);
	else
		strcpy(out->path, "/");
	return 0;
}

static int connect_http(const struct url_parts *url)
{
	struct sockaddr_in addr;
	int sock;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)url->port);
	if (!inet_aton(url->host, &addr.sin_addr)) {
		fprintf(stderr, "wget: bad IPv4 address: %s\n", url->host);
		return -1;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("wget: socket");
		return -1;
	}
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("wget: connect");
		close(sock);
		return -1;
	}
	return sock;
}

static int write_all(int fd, const char *buf, size_t len)
{
	while (len) {
		ssize_t done = write(fd, buf, len);
		if (done < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		buf += done;
		len -= done;
	}
	return 0;
}

static int fetch(const struct url_parts *url, int outfd)
{
	char req[256];
	char buf[256];
	char header[4] = { 0, 0, 0, 0 };
	int in_header = 1;
	int sock;
	int len;

	sock = connect_http(url);
	if (sock < 0)
		return 1;

	len = snprintf(req, sizeof(req),
		       "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
		       url->path, url->host);
	if (write_all(sock, req, (size_t)len) < 0) {
		perror("wget: write request");
		close(sock);
		return 1;
	}

	for (;;) {
		ssize_t got = read(sock, buf, sizeof(buf));
		ssize_t i;

		if (got < 0) {
			if (errno == EINTR)
				continue;
			perror("wget: read");
			close(sock);
			return 1;
		}
		if (got == 0)
			break;

		for (i = 0; i < got; i++) {
			if (in_header) {
				header[0] = header[1];
				header[1] = header[2];
				header[2] = header[3];
				header[3] = buf[i];
				if (header[0] == '\r' && header[1] == '\n' &&
				    header[2] == '\r' && header[3] == '\n')
					in_header = 0;
				else if (header[2] == '\n' && header[3] == '\n')
					in_header = 0;
			} else if (write(outfd, &buf[i], 1) != 1) {
				perror("wget: write output");
				close(sock);
				return 1;
			}
		}
	}

	close(sock);
	return 0;
}

int main(int argc, char **argv)
{
	const char *out = NULL;
	const char *url_arg;
	struct url_parts url;
	int outfd = STDOUT_FILENO;
	int ret;

	if (argc == 2) {
		url_arg = argv[1];
	} else if (argc == 4 && !strcmp(argv[1], "-O")) {
		out = argv[2];
		url_arg = argv[3];
	} else {
		usage();
		return 1;
	}

	if (parse_url(url_arg, &url) < 0)
		return 1;

	if (out && strcmp(out, "-")) {
		outfd = open(out, O_CREAT | O_TRUNC | O_WRONLY, 0644);
		if (outfd < 0) {
			perror(out);
			return 1;
		}
	}

	ret = fetch(&url, outfd);
	if (outfd != STDOUT_FILENO)
		close(outfd);
	return ret;
}
