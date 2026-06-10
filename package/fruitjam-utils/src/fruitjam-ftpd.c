// SPDX-License-Identifier: MIT
/*
 * Tiny single-process FTP daemon for the Fruit Jam no-MMU image.
 *
 * BusyBox ftpd behind tcpsvd is useful, but each incoming connection needs a
 * fresh exec. On this target that can fail after memory has fragmented, so this
 * daemon keeps the basic anonymous FTP path inside one small process.
 */

#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FTP_ROOT "/tmp/ftp"
#define FTP_PORT 21

static int reply(FILE *ctrl, int code, const char *fmt, ...)
{
	va_list ap;

	fprintf(ctrl, "%d ", code);
	va_start(ap, fmt);
	vfprintf(ctrl, fmt, ap);
	va_end(ap);
	fprintf(ctrl, "\r\n");
	fflush(ctrl);
	return 0;
}

static void trim_crlf(char *s)
{
	size_t len = strlen(s);

	while (len && (s[len - 1] == '\n' || s[len - 1] == '\r'))
		s[--len] = '\0';
}

static char *command_arg(char *line)
{
	while (*line && !isspace((unsigned char)*line))
		line++;
	while (*line && isspace((unsigned char)*line))
		*line++ = '\0';
	return line;
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

static int safe_path(const char *arg, char *out, size_t out_len)
{
	const char *p = arg;
	int ret;

	if (!p || !*p || !strcmp(p, "/"))
		p = ".";
	while (*p == '/')
		p++;
	if (strstr(p, ".."))
		return -1;
	ret = snprintf(out, out_len, "%s/%s", FTP_ROOT, p);
	return ret > 0 && (size_t)ret < out_len ? 0 : -1;
}

static int open_listener(uint16_t port)
{
	int fd;
	int one = 1;
	struct sockaddr_in addr;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    listen(fd, 1) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int start_passive(FILE *ctrl, int ctrl_fd, int *pasv_fd)
{
	struct sockaddr_in local;
	struct sockaddr_in data_addr;
	socklen_t len = sizeof(local);
	uint32_t ip;
	uint16_t port;
	int fd;

	if (*pasv_fd >= 0) {
		close(*pasv_fd);
		*pasv_fd = -1;
	}

	fd = open_listener(0);
	if (fd < 0)
		return reply(ctrl, 425, "Cannot open passive socket");

	memset(&data_addr, 0, sizeof(data_addr));
	len = sizeof(data_addr);
	if (getsockname(fd, (struct sockaddr *)&data_addr, &len) < 0) {
		close(fd);
		return reply(ctrl, 425, "Cannot read passive socket");
	}
	len = sizeof(local);
	if (getsockname(ctrl_fd, (struct sockaddr *)&local, &len) < 0)
		local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (local.sin_addr.s_addr == htonl(INADDR_ANY))
		local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	ip = ntohl(local.sin_addr.s_addr);
	port = ntohs(data_addr.sin_port);
	*pasv_fd = fd;
	return reply(ctrl, 227, "Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
		     (ip >> 24) & 0xff, (ip >> 16) & 0xff,
		     (ip >> 8) & 0xff, ip & 0xff, port >> 8, port & 0xff);
}

static int accept_data(FILE *ctrl, int *pasv_fd)
{
	int fd;

	if (*pasv_fd < 0) {
		reply(ctrl, 425, "Use PASV first");
		return -1;
	}
	fd = accept(*pasv_fd, NULL, NULL);
	close(*pasv_fd);
	*pasv_fd = -1;
	if (fd < 0)
		reply(ctrl, 425, "Data connection failed");
	return fd;
}

static void do_list(FILE *ctrl, int *pasv_fd, const char *arg)
{
	char path[160];
	DIR *dir;
	struct dirent *de;
	int data;

	if (safe_path(arg, path, sizeof(path)) < 0) {
		reply(ctrl, 550, "Bad path");
		return;
	}
	dir = opendir(path);
	if (!dir) {
		reply(ctrl, 550, "Cannot open directory");
		return;
	}
	reply(ctrl, 150, "Opening data connection");
	data = accept_data(ctrl, pasv_fd);
	if (data >= 0) {
		while ((de = readdir(dir)) != NULL) {
			char child[192];
			struct stat st;
			const char *type = "-";
			int n;

			if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
				continue;
			n = snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
			if (n <= 0 || (size_t)n >= sizeof(child))
				continue;
			if (stat(child, &st) == 0 && S_ISDIR(st.st_mode))
				type = "d";
			dprintf(data, "%srw-r--r-- 1 root root %ld Jan 01 00:00 %s\r\n",
				type, stat(child, &st) == 0 ? (long)st.st_size : 0,
				de->d_name);
		}
		close(data);
		reply(ctrl, 226, "Transfer complete");
	}
	closedir(dir);
}

static void do_retr(FILE *ctrl, int *pasv_fd, const char *arg)
{
	char path[160];
	char buf[256];
	int file;
	int data;
	ssize_t len;

	if (safe_path(arg, path, sizeof(path)) < 0) {
		reply(ctrl, 550, "Bad path");
		return;
	}
	file = open(path, O_RDONLY);
	if (file < 0) {
		reply(ctrl, 550, "Cannot open file");
		return;
	}
	reply(ctrl, 150, "Opening data connection");
	data = accept_data(ctrl, pasv_fd);
	if (data >= 0) {
		while ((len = read(file, buf, sizeof(buf))) > 0)
			write(data, buf, (size_t)len);
		close(data);
		reply(ctrl, 226, "Transfer complete");
	}
	close(file);
}

static void do_stor(FILE *ctrl, int *pasv_fd, const char *arg)
{
	char path[160];
	char buf[256];
	int file;
	int data;
	ssize_t len;

	if (safe_path(arg, path, sizeof(path)) < 0) {
		reply(ctrl, 550, "Bad path");
		return;
	}
	file = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (file < 0) {
		reply(ctrl, 550, "Cannot create file");
		return;
	}
	reply(ctrl, 150, "Opening data connection");
	data = accept_data(ctrl, pasv_fd);
	if (data >= 0) {
		while ((len = read(data, buf, sizeof(buf))) > 0)
			write(file, buf, (size_t)len);
		close(data);
		reply(ctrl, 226, "Transfer complete");
	}
	close(file);
}

static void serve_client(int fd)
{
	FILE *ctrl = fdopen(fd, "r+");
	char line[160];
	int pasv_fd = -1;

	if (!ctrl) {
		close(fd);
		return;
	}
	setvbuf(ctrl, NULL, _IONBF, 0);
	reply(ctrl, 220, "Fruit Jam FTP ready");

	while (fgets(line, sizeof(line), ctrl)) {
		char *arg;

		trim_crlf(line);
		arg = command_arg(line);
		if (!strcasecmp(line, "USER")) {
			reply(ctrl, 331, "Anonymous login ok");
		} else if (!strcasecmp(line, "PASS")) {
			reply(ctrl, 230, "Logged in");
		} else if (!strcasecmp(line, "SYST")) {
			reply(ctrl, 215, "UNIX Type: L8");
		} else if (!strcasecmp(line, "FEAT")) {
			fprintf(ctrl, "211-Features\r\n PASV\r\n211 End\r\n");
		} else if (!strcasecmp(line, "PWD") || !strcasecmp(line, "XPWD")) {
			reply(ctrl, 257, "\"/\"");
		} else if (!strcasecmp(line, "CWD") || !strcasecmp(line, "CDUP")) {
			reply(ctrl, 250, "Directory changed");
		} else if (!strcasecmp(line, "TYPE")) {
			reply(ctrl, 200, "Type set");
		} else if (!strcasecmp(line, "NOOP")) {
			reply(ctrl, 200, "OK");
		} else if (!strcasecmp(line, "PASV")) {
			start_passive(ctrl, fd, &pasv_fd);
		} else if (!strcasecmp(line, "LIST") || !strcasecmp(line, "NLST")) {
			do_list(ctrl, &pasv_fd, arg);
		} else if (!strcasecmp(line, "RETR")) {
			do_retr(ctrl, &pasv_fd, arg);
		} else if (!strcasecmp(line, "STOR")) {
			do_stor(ctrl, &pasv_fd, arg);
		} else if (!strcasecmp(line, "QUIT")) {
			reply(ctrl, 221, "Bye");
			break;
		} else {
			reply(ctrl, 502, "Command not implemented");
		}
	}

	if (pasv_fd >= 0)
		close(pasv_fd);
	fclose(ctrl);
}

int main(int argc, char **argv)
{
	int listen_fd;
	uint16_t port = FTP_PORT;

	if (argc == 2)
		port = (uint16_t)strtoul(argv[1], NULL, 10);

	mkdir_p(FTP_ROOT);
	listen_fd = open_listener(port);
	if (listen_fd < 0) {
		fprintf(stderr, "fruitjam-ftpd: listen port %u: %s\n",
			port, strerror(errno));
		return 1;
	}

	for (;;) {
		int fd = accept(listen_fd, NULL, NULL);

		if (fd < 0) {
			if (errno == EINTR)
				continue;
			return 1;
		}
		serve_client(fd);
	}
}
