// SPDX-License-Identifier: MIT
/*
 * Tiny loopback HTTP/CGI server for Fruit Jam no-MMU smoke tests.
 *
 * BusyBox httpd is useful, but the flat binary can require a larger
 * contiguous allocation than is available while AirLift services are resident.
 * This helper only serves /www and the two Fruit Jam CGI endpoints needed by
 * local wget tests.
 */

#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_PORT 80
#define WWW_ROOT "/www"

static int write_all(int fd, const char *buf, size_t len)
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

static int write_str(int fd, const char *s)
{
	return write_all(fd, s, strlen(s));
}

static void send_error(int fd, int code, const char *text)
{
	char buf[160];
	int n = snprintf(buf, sizeof(buf),
			 "HTTP/1.0 %d %s\r\n"
			 "Content-Type: text/plain\r\n"
			 "Connection: close\r\n"
			 "\r\n"
			 "%d %s\n",
			 code, text, code, text);

	if (n > 0)
		(void)write_all(fd, buf, (size_t)n);
}

static const char *content_type(const char *path)
{
	const char *dot = strrchr(path, '.');

	if (!dot)
		return "application/octet-stream";
	if (!strcmp(dot, ".html") || !strcmp(dot, ".htm"))
		return "text/html";
	if (!strcmp(dot, ".css"))
		return "text/css";
	if (!strcmp(dot, ".js"))
		return "application/javascript";
	if (!strcmp(dot, ".txt"))
		return "text/plain";
	return "application/octet-stream";
}

static int serve_file(int fd, const char *path)
{
	char full[220];
	char header[192];
	char buf[384];
	struct stat st;
	int in;
	int n;

	if (strstr(path, "..")) {
		send_error(fd, 403, "Forbidden");
		return 1;
	}
	if (!strcmp(path, "/"))
		path = "/index.html";
	if (snprintf(full, sizeof(full), "%s%s", WWW_ROOT, path) >= (int)sizeof(full)) {
		send_error(fd, 414, "URI Too Long");
		return 1;
	}
	in = open(full, O_RDONLY);
	if (in < 0) {
		send_error(fd, 404, "Not Found");
		return 1;
	}
	if (fstat(in, &st) < 0)
		st.st_size = -1;
	n = snprintf(header, sizeof(header),
		     "HTTP/1.0 200 OK\r\n"
		     "Content-Type: %s\r\n"
		     "Cache-Control: no-store\r\n"
		     "Connection: close\r\n",
		     content_type(full));
	if (n > 0)
		(void)write_all(fd, header, (size_t)n);
	if (st.st_size >= 0) {
		n = snprintf(header, sizeof(header), "Content-Length: %ld\r\n",
			     (long)st.st_size);
		if (n > 0)
			(void)write_all(fd, header, (size_t)n);
	}
	(void)write_str(fd, "\r\n");

	for (;;) {
		ssize_t got = read(in, buf, sizeof(buf));

		if (got < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (got == 0)
			break;
		if (write_all(fd, buf, (size_t)got) < 0)
			break;
	}
	close(in);
	return 0;
}

static int serve_cgi(int fd, const char *script, const char *method,
		     const char *query, const char *proto,
		     const char *remote_addr, unsigned int remote_port)
{
	char remote_port_text[16];
	pid_t pid;
	int status;

	snprintf(remote_port_text, sizeof(remote_port_text), "%u", remote_port);
	setenv("REQUEST_METHOD", method, 1);
	setenv("QUERY_STRING", query ? query : "", 1);
	setenv("REMOTE_ADDR", remote_addr ? remote_addr : "127.0.0.1", 1);
	setenv("REMOTE_PORT", remote_port_text, 1);
	setenv("SERVER_PROTOCOL", proto && proto[0] ? proto : "HTTP/1.0", 1);
	setenv("PATH", "/sbin:/usr/sbin:/bin:/usr/bin", 1);

	if (write_str(fd, "HTTP/1.0 200 OK\r\n") < 0)
		return 1;

	pid = vfork();
	if (pid < 0) {
		send_error(fd, 500, "CGI vfork failed");
		return 1;
	}
	if (pid == 0) {
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		execl(script, script, (char *)NULL);
		_exit(127);
	}
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return 1;
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int read_request(int fd, char *buf, size_t len)
{
	size_t used = 0;

	while (used + 1 < len) {
		ssize_t got = read(fd, buf + used, len - 1 - used);

		if (got < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (got == 0)
			break;
		used += (size_t)got;
		buf[used] = '\0';
		if (strstr(buf, "\r\n\r\n") || strstr(buf, "\n\n"))
			return 0;
	}
	buf[used] = '\0';
	return used ? 0 : -1;
}

static void serve_client(int client)
{
	struct sockaddr_in peer;
	socklen_t peer_len = sizeof(peer);
	char remote_addr[32] = "127.0.0.1";
	unsigned int remote_port = 0;
	char req[512];
	char method[8];
	char target[192];
	char proto[16];
	char *query;

	if (getpeername(client, (struct sockaddr *)&peer, &peer_len) == 0) {
		const char *addr = inet_ntoa(peer.sin_addr);

		if (addr)
			snprintf(remote_addr, sizeof(remote_addr), "%s", addr);
		remote_port = ntohs(peer.sin_port);
	}
	if (read_request(client, req, sizeof(req)) < 0)
		return;
	method[0] = target[0] = proto[0] = '\0';
	if (sscanf(req, "%7s %191s %15s", method, target, proto) < 2) {
		send_error(client, 400, "Bad Request");
		return;
	}
	if (strcmp(method, "GET")) {
		send_error(client, 405, "Method Not Allowed");
		return;
	}

	query = strchr(target, '?');
	if (query)
		*query++ = '\0';
	else
		query = "";

	if (!strcmp(target, "/cgi-bin/env.cgi")) {
		(void)serve_cgi(client, "/www/cgi-bin/env.cgi", method, query, proto,
				remote_addr, remote_port);
	} else if (!strcmp(target, "/cgi-bin/fruitjam.cgi")) {
		(void)serve_cgi(client, "/www/cgi-bin/fruitjam.cgi", method, query, proto,
				remote_addr, remote_port);
	} else {
		(void)serve_file(client, target);
	}
}

int main(int argc, char **argv)
{
	struct sockaddr_in addr;
	int port = DEFAULT_PORT;
	int one = 1;
	int srv;

	if (argc > 1) {
		char *end;
		long value = strtol(argv[1], &end, 10);

		if (!argv[1][0] || *end || value <= 0 || value > 65535) {
			fprintf(stderr, "usage: fruitjam-httpd [port]\n");
			return 1;
		}
		port = (int)value;
	}

	signal(SIGPIPE, SIG_IGN);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((uint16_t)port);

	srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0) {
		perror("fruitjam-httpd: socket");
		return 1;
	}
	setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		if (errno == EADDRINUSE) {
			close(srv);
			return 0;
		}
		perror("fruitjam-httpd: bind");
		close(srv);
		return 1;
	}
	if (listen(srv, 2) < 0) {
		perror("fruitjam-httpd: listen");
		close(srv);
		return 1;
	}

	for (;;) {
		int client = accept(srv, NULL, NULL);

		if (client < 0) {
			if (errno == EINTR)
				continue;
			perror("fruitjam-httpd: accept");
			break;
		}
		serve_client(client);
		close(client);
	}
	close(srv);
	return 1;
}
