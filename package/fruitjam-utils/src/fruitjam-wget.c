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
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#define WGET_IO_TIMEOUT_MS 5000u

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

static int wait_fd(int fd, int write_ready, unsigned int timeout_ms)
{
	fd_set readfds;
	fd_set writefds;
	struct timeval tv;
	int ret;

	for (;;) {
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		if (write_ready)
			FD_SET(fd, &writefds);
		else
			FD_SET(fd, &readfds);
		tv.tv_sec = timeout_ms / 1000u;
		tv.tv_usec = (long)(timeout_ms % 1000u) * 1000L;
		ret = select(fd + 1, write_ready ? NULL : &readfds,
			     write_ready ? &writefds : NULL, NULL, &tv);
		if (ret < 0 && errno == EINTR)
			continue;
		if (ret == 0)
			errno = ETIMEDOUT;
		return ret > 0 ? 0 : -1;
	}
}

static int set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);

	if (flags < 0)
		return -1;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		return -1;
	return 0;
}

static int connect_http(const struct url_parts *url)
{
	struct sockaddr_in addr;
	struct timeval timeout;
	int sock;
	int err = 0;
	socklen_t err_len = sizeof(err);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)url->port);
	if (!strcmp(url->host, "localhost")) {
		addr.sin_addr.s_addr = htonl(0x7f000001u);
	} else if (!inet_aton(url->host, &addr.sin_addr)) {
		fprintf(stderr, "wget: bad IPv4 address: %s\n", url->host);
		return -1;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("wget: socket");
		return -1;
	}
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

	if (set_nonblock(sock) < 0) {
		perror("wget: nonblock");
		close(sock);
		return -1;
	}
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0 &&
	    errno != EINPROGRESS && errno != EWOULDBLOCK) {
		perror("wget: connect");
		close(sock);
		return -1;
	}
	if (wait_fd(sock, 1, WGET_IO_TIMEOUT_MS) < 0) {
		perror("wget: connect");
		close(sock);
		return -1;
	}
	if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &err_len) < 0) {
		perror("wget: connect status");
		close(sock);
		return -1;
	}
	if (err) {
		errno = err;
		perror("wget: connect");
		close(sock);
		return -1;
	}
	return sock;
}

static int socket_write_all(int fd, const char *buf, size_t len)
{
	while (len) {
		ssize_t done = write(fd, buf, len);
		if (done < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				if (wait_fd(fd, 1, WGET_IO_TIMEOUT_MS) == 0)
					continue;
			}
			return -1;
		}
		if (done == 0) {
			errno = EIO;
			return -1;
		}
		buf += done;
		len -= (size_t)done;
	}
	return 0;
}

static int write_all_fd(int fd, const char *buf, size_t len)
{
	while (len) {
		ssize_t done = write(fd, buf, len);
		if (done < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (done == 0) {
			errno = EIO;
			return -1;
		}
		buf += done;
		len -= (size_t)done;
	}
	return 0;
}

static ssize_t socket_read_some(int fd, char *buf, size_t len)
{
	for (;;) {
		ssize_t got = read(fd, buf, len);

		if (got >= 0)
			return got;
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			if (wait_fd(fd, 0, WGET_IO_TIMEOUT_MS) == 0)
				continue;
		}
		return -1;
	}
}

static int ci_starts_with(const char *s, const char *prefix)
{
	while (*prefix) {
		char a = *s++;
		char b = *prefix++;

		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = (char)(b - 'A' + 'a');
		if (a != b)
			return 0;
	}
	return 1;
}

static long parse_content_length(const char *headers)
{
	const char *p = headers;

	while (*p) {
		const char *line = p;
		const char *end = strchr(p, '\n');
		char *num_end;
		long value;

		if (!end)
			end = p + strlen(p);
		if (ci_starts_with(line, "Content-Length:")) {
			line += strlen("Content-Length:");
			while (*line == ' ' || *line == '\t')
				line++;
			value = strtol(line, &num_end, 10);
			if (num_end != line && value >= 0)
				return value;
		}
		p = *end ? end + 1 : end;
	}
	return -1;
}

static int parse_status_code(const char *headers)
{
	const char *p = strchr(headers, ' ');

	if (!p)
		return 0;
	while (*p == ' ')
		p++;
	return atoi(p);
}

static int fetch(const struct url_parts *url, int outfd)
{
	char req[256];
	char buf[256];
	char headers[1024];
	size_t header_len = 0;
	long content_length = -1;
	long body_written = 0;
	int in_header = 1;
	int sock;
	int len;
	int status = 0;
	int ret = 0;

	sock = connect_http(url);
	if (sock < 0)
		return 1;

	len = snprintf(req, sizeof(req),
		       "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
		       url->path, url->host);
	if (socket_write_all(sock, req, (size_t)len) < 0) {
		perror("wget: write request");
		close(sock);
		return 1;
	}

	for (;;) {
		ssize_t got = socket_read_some(sock, buf, sizeof(buf));
		ssize_t i;

		if (got < 0) {
			perror("wget: read");
			close(sock);
			return 1;
		}
		if (got == 0)
			break;

		for (i = 0; i < got;) {
			if (in_header) {
				if (header_len + 1 >= sizeof(headers)) {
					fprintf(stderr, "wget: response headers too large\n");
					close(sock);
					return 1;
				}
				headers[header_len++] = buf[i++];
				headers[header_len] = '\0';
				if ((header_len >= 4 &&
				     headers[header_len - 4] == '\r' &&
				     headers[header_len - 3] == '\n' &&
				     headers[header_len - 2] == '\r' &&
				     headers[header_len - 1] == '\n') ||
				    (header_len >= 2 &&
				     headers[header_len - 2] == '\n' &&
				     headers[header_len - 1] == '\n')) {
					in_header = 0;
					status = parse_status_code(headers);
					content_length = parse_content_length(headers);
				}
			} else {
				size_t body_len = (size_t)(got - i);

				if (content_length >= 0 && body_written >= content_length)
					break;
				if (content_length >= 0 &&
				    body_written + (long)body_len > content_length)
					body_len = (size_t)(content_length - body_written);
				if (body_len == 0)
					break;
				if (write_all_fd(outfd, &buf[i], body_len) < 0) {
					perror("wget: write output");
					close(sock);
					return 1;
				}
				body_written += (long)body_len;
				i += (ssize_t)body_len;
			}
		}
		if (content_length >= 0 && body_written >= content_length)
			break;
	}

	if (in_header) {
		fprintf(stderr, "wget: incomplete HTTP response\n");
		ret = 1;
	} else if (status < 200 || status >= 400) {
		fprintf(stderr, "wget: server returned HTTP %d\n", status);
		ret = 1;
	} else if (content_length >= 0 && body_written < content_length) {
		fprintf(stderr, "wget: short response body\n");
		ret = 1;
	}
	close(sock);
	return ret;
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
