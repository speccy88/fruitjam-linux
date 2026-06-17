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
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef FTP_ROOT
#define FTP_ROOT "/mnt/sd"
#endif
#ifndef FTP_PORT
#define FTP_PORT 21
#endif
#define FTP_CONTROL_IDLE_SECONDS 10

struct ftp_data_state {
	int pasv_fd;
	int have_active;
	struct sockaddr_in active_addr;
};

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

static int path_from_cwd(const char *cwd, const char *arg, char *virt,
			 size_t virt_len, char *path, size_t path_len)
{
	if (ftp_virtual_path(cwd, arg, virt, virt_len) < 0)
		return -1;
	return ftp_fs_path(virt, path, path_len);
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

static void data_clear(struct ftp_data_state *data)
{
	if (data->pasv_fd >= 0) {
		close(data->pasv_fd);
		data->pasv_fd = -1;
	}
	data->have_active = 0;
	memset(&data->active_addr, 0, sizeof(data->active_addr));
}

static void data_init(struct ftp_data_state *data)
{
	data->pasv_fd = -1;
	data->have_active = 0;
	memset(&data->active_addr, 0, sizeof(data->active_addr));
}

static int parse_u8(const char *s, char **end, unsigned int *value)
{
	unsigned long parsed;

	if (!isdigit((unsigned char)*s))
		return -1;
	errno = 0;
	parsed = strtoul(s, end, 10);
	if (errno || parsed > 255)
		return -1;
	*value = (unsigned int)parsed;
	return 0;
}

static int parse_port_arg(const char *arg, struct sockaddr_in *addr)
{
	unsigned int v[6];
	char *end;
	int i;

	memset(addr, 0, sizeof(*addr));
	for (i = 0; i < 6; i++) {
		if (parse_u8(arg, &end, &v[i]) < 0)
			return -1;
		if (i < 5) {
			if (*end != ',')
				return -1;
			arg = end + 1;
		} else if (*end) {
			return -1;
		}
	}

	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = htonl((v[0] << 24) | (v[1] << 16) |
				      (v[2] << 8) | v[3]);
	addr->sin_port = htons((uint16_t)((v[4] << 8) | v[5]));
	return ntohs(addr->sin_port) ? 0 : -1;
}

static int parse_ipv4(const char *s, uint32_t *ip)
{
	unsigned int v[4];
	char *end;
	int i;

	for (i = 0; i < 4; i++) {
		if (parse_u8(s, &end, &v[i]) < 0)
			return -1;
		if (i < 3) {
			if (*end != '.')
				return -1;
			s = end + 1;
		} else if (*end) {
			return -1;
		}
	}
	*ip = (v[0] << 24) | (v[1] << 16) | (v[2] << 8) | v[3];
	return 0;
}

static int parse_eprt_arg(const char *arg, struct sockaddr_in *addr)
{
	char delim;
	char *proto_end;
	char *addr_end;
	char *port_end;
	unsigned long port;
	uint32_t ip;

	memset(addr, 0, sizeof(*addr));
	if (!arg || !*arg)
		return -1;
	delim = *arg++;
	if (!delim)
		return -1;
	if (strncmp(arg, "1", 1) || arg[1] != delim)
		return -1;
	arg += 2;
	proto_end = strchr(arg, delim);
	if (!proto_end)
		return -1;
	*proto_end = '\0';
	if (parse_ipv4(arg, &ip) < 0)
		return -1;
	arg = proto_end + 1;
	addr_end = strchr(arg, delim);
	if (!addr_end)
		return -1;
	*addr_end = '\0';
	errno = 0;
	port = strtoul(arg, &port_end, 10);
	if (errno || *port_end || port == 0 || port > 65535)
		return -1;
	if (addr_end[1] != '\0')
		return -1;

	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = htonl(ip);
	addr->sin_port = htons((uint16_t)port);
	return 0;
}

static int start_active(FILE *ctrl, struct ftp_data_state *data,
			const struct sockaddr_in *addr, const char *name)
{
	data_clear(data);
	data->active_addr = *addr;
	data->have_active = 1;
	return reply(ctrl, 200, "%s command successful", name);
}

static int start_passive(FILE *ctrl, int ctrl_fd, struct ftp_data_state *data,
			 int extended)
{
	struct sockaddr_in local;
	struct sockaddr_in data_addr;
	socklen_t len = sizeof(local);
	uint32_t ip;
	uint16_t port;
	int fd;

	data_clear(data);

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
	data->pasv_fd = fd;
	if (extended)
		return reply(ctrl, 229, "Entering Extended Passive Mode (|||%u|)",
			     port);
	return reply(ctrl, 227, "Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
		     (ip >> 24) & 0xff, (ip >> 16) & 0xff,
		     (ip >> 8) & 0xff, ip & 0xff, port >> 8, port & 0xff);
}

static int accept_data(FILE *ctrl, struct ftp_data_state *data)
{
	int fd;

	if (data->have_active) {
		fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			reply(ctrl, 425, "Cannot open active socket");
			data_clear(data);
			return -1;
		}
		if (connect(fd, (struct sockaddr *)&data->active_addr,
			    sizeof(data->active_addr)) < 0) {
			close(fd);
			reply(ctrl, 425, "Active data connection failed");
			data_clear(data);
			return -1;
		}
		data_clear(data);
		return fd;
	}

	if (data->pasv_fd < 0) {
		reply(ctrl, 425, "Use PASV/EPSV or PORT/EPRT first");
		return -1;
	}
	fd = accept(data->pasv_fd, NULL, NULL);
	close(data->pasv_fd);
	data->pasv_fd = -1;
	if (fd < 0)
		reply(ctrl, 425, "Data connection failed");
	return fd;
}

static void do_list(FILE *ctrl, struct ftp_data_state *data_state,
		    const char *cwd, const char *arg, int names_only)
{
	char path[160];
	char virt[128];
	DIR *dir;
	struct dirent *de;
	int data;

	while (arg[0] == '-') {
		arg = strchr(arg, ' ');
		if (!arg)
			arg = "";
		while (*arg == ' ')
			arg++;
	}

	if (path_from_cwd(cwd, *arg ? arg : ".", virt, sizeof(virt),
			  path, sizeof(path)) < 0) {
		reply(ctrl, 550, "Bad path");
		return;
	}
	dir = opendir(path);
	if (!dir) {
		reply(ctrl, 550, "Cannot open directory");
		return;
	}
	reply(ctrl, 150, "Opening data connection");
	data = accept_data(ctrl, data_state);
	if (data >= 0) {
		while ((de = readdir(dir)) != NULL) {
			char child[192];
			struct stat st;
			const char *perms = "-rw-r--r--";
			char date[16];
			int have_st;
			int n;

			if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
				continue;
			n = snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
			if (n <= 0 || (size_t)n >= sizeof(child))
				continue;
			have_st = stat(child, &st) == 0;
			if (names_only) {
				dprintf(data, "%s\r\n", de->d_name);
				continue;
			}
			if (have_st && S_ISDIR(st.st_mode))
				perms = "drwxr-xr-x";
			ftp_format_list_date(have_st ? st.st_mtime : 0, date, sizeof(date));
			dprintf(data, "%s 1 root root %lu %s %s\r\n",
				perms, have_st ? (unsigned long)st.st_size : 0,
				date,
				de->d_name);
		}
		close(data);
		reply(ctrl, 226, "Transfer complete");
	}
	closedir(dir);
}

static void do_retr(FILE *ctrl, struct ftp_data_state *data_state,
		    const char *cwd, const char *arg)
{
	char path[160];
	char virt[128];
	char buf[256];
	int file;
	int data;
	ssize_t len;

	if (!*arg || path_from_cwd(cwd, arg, virt, sizeof(virt),
				   path, sizeof(path)) < 0) {
		reply(ctrl, 550, "Bad path");
		return;
	}
	file = open(path, O_RDONLY);
	if (file < 0) {
		reply(ctrl, 550, "Cannot open file");
		return;
	}
	reply(ctrl, 150, "Opening data connection");
	data = accept_data(ctrl, data_state);
	if (data >= 0) {
		while ((len = read(file, buf, sizeof(buf))) > 0)
			write(data, buf, (size_t)len);
		close(data);
		reply(ctrl, 226, "Transfer complete");
	}
	close(file);
}

static void do_stor(FILE *ctrl, struct ftp_data_state *data_state,
		    const char *cwd, const char *arg, int append)
{
	char path[160];
	char virt[128];
	char buf[256];
	int file;
	int data;
	ssize_t len;

	if (!*arg || path_from_cwd(cwd, arg, virt, sizeof(virt),
				   path, sizeof(path)) < 0) {
		reply(ctrl, 550, "Bad path");
		return;
	}
	file = open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC),
		    0644);
	if (file < 0) {
		reply(ctrl, 550, "Cannot create file");
		return;
	}
	reply(ctrl, 150, "Opening data connection");
	data = accept_data(ctrl, data_state);
	if (data >= 0) {
		while ((len = read(data, buf, sizeof(buf))) > 0)
			write(file, buf, (size_t)len);
		close(data);
		reply(ctrl, 226, "Transfer complete");
	}
	close(file);
}

static void do_size(FILE *ctrl, const char *cwd, const char *arg)
{
	char path[160];
	char virt[128];
	struct stat st;

	if (!*arg || path_from_cwd(cwd, arg, virt, sizeof(virt),
				   path, sizeof(path)) < 0) {
		reply(ctrl, 501, "Bad filename");
		return;
	}
	if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) {
		reply(ctrl, 550, "Not found");
		return;
	}
	reply(ctrl, 213, "%lu", (unsigned long)st.st_size);
}

static void do_mdtm(FILE *ctrl, const char *cwd, const char *arg)
{
	char path[160];
	char virt[128];
	char stamp[24];
	struct stat st;

	if (!*arg || path_from_cwd(cwd, arg, virt, sizeof(virt),
				   path, sizeof(path)) < 0) {
		reply(ctrl, 501, "Bad filename");
		return;
	}
	if (stat(path, &st) < 0) {
		reply(ctrl, 550, "Not found");
		return;
	}
	ftp_format_mdtm(st.st_mtime, stamp, sizeof(stamp));
	reply(ctrl, 213, "%s", stamp);
}

static void do_dele(FILE *ctrl, const char *cwd, const char *arg)
{
	char path[160];
	char virt[128];

	if (!*arg || path_from_cwd(cwd, arg, virt, sizeof(virt),
				   path, sizeof(path)) < 0) {
		reply(ctrl, 501, "Bad filename");
		return;
	}
	if (unlink(path) < 0)
		reply(ctrl, 550, "Delete failed");
	else
		reply(ctrl, 250, "Deleted");
}

static void do_mkd(FILE *ctrl, const char *cwd, const char *arg)
{
	char path[160];
	char virt[128];

	if (!*arg || path_from_cwd(cwd, arg, virt, sizeof(virt),
				   path, sizeof(path)) < 0) {
		reply(ctrl, 501, "Bad directory");
		return;
	}
	if (mkdir(path, 0755) < 0 && errno != EEXIST)
		reply(ctrl, 550, "Create directory failed");
	else
		reply(ctrl, 257, "\"%s\" created", virt);
}

static void do_rmd(FILE *ctrl, const char *cwd, const char *arg)
{
	char path[160];
	char virt[128];

	if (!*arg || path_from_cwd(cwd, arg, virt, sizeof(virt),
				   path, sizeof(path)) < 0) {
		reply(ctrl, 501, "Bad directory");
		return;
	}
	if (rmdir(path) < 0)
		reply(ctrl, 550, "Remove directory failed");
	else
		reply(ctrl, 250, "Directory removed");
}

static void serve_client(int fd)
{
	struct timeval timeout = { FTP_CONTROL_IDLE_SECONDS, 0 };
	FILE *ctrl = fdopen(fd, "r+");
	char line[160];
	char cwd[128] = "/";
	char rename_from[160] = "";
	struct ftp_data_state data;

	data_init(&data);
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
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
			fprintf(ctrl,
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
			reply(ctrl, 257, "\"%s\"", cwd);
		} else if (!strcasecmp(line, "CWD") || !strcasecmp(line, "XCWD") ||
			   !strcasecmp(line, "CDUP")) {
			const char *dest = !strcasecmp(line, "CDUP") ? ".." :
					   (*arg ? arg : "/");
			char path[160];
			char virt[128];
			struct stat st;

			if (path_from_cwd(cwd, dest, virt, sizeof(virt),
					  path, sizeof(path)) < 0) {
				reply(ctrl, 550, "Bad directory");
			} else if (stat(path, &st) < 0 || !S_ISDIR(st.st_mode)) {
				reply(ctrl, 550, "Not a directory");
			} else {
				strcpy(cwd, virt);
				reply(ctrl, 250, "Directory changed");
			}
		} else if (!strcasecmp(line, "TYPE")) {
			reply(ctrl, 200, "Type set");
		} else if (!strcasecmp(line, "MODE") ||
			   !strcasecmp(line, "STRU")) {
			reply(ctrl, 200, "OK");
		} else if (!strcasecmp(line, "NOOP")) {
			reply(ctrl, 200, "OK");
		} else if (!strcasecmp(line, "ALLO")) {
			reply(ctrl, 202, "No storage allocation needed");
		} else if (!strcasecmp(line, "REST")) {
			if (!*arg || !strcmp(arg, "0"))
				reply(ctrl, 350, "Restarting at 0");
			else
				reply(ctrl, 502, "Restart offsets are not supported");
		} else if (!strcasecmp(line, "OPTS")) {
			if (!strncasecmp(arg, "UTF8", 4))
				reply(ctrl, 200, "UTF8 mode enabled");
			else
				reply(ctrl, 502, "Option not implemented");
		} else if (!strcasecmp(line, "PORT")) {
			struct sockaddr_in active;

			if (parse_port_arg(arg, &active) < 0)
				reply(ctrl, 501, "Bad PORT");
			else
				start_active(ctrl, &data, &active, "PORT");
		} else if (!strcasecmp(line, "EPRT")) {
			struct sockaddr_in active;

			if (parse_eprt_arg(arg, &active) < 0)
				reply(ctrl, 501, "Bad EPRT");
			else
				start_active(ctrl, &data, &active, "EPRT");
		} else if (!strcasecmp(line, "PASV")) {
			start_passive(ctrl, fd, &data, 0);
		} else if (!strcasecmp(line, "EPSV")) {
			start_passive(ctrl, fd, &data, 1);
		} else if (!strcasecmp(line, "LIST") || !strcasecmp(line, "NLST")) {
			do_list(ctrl, &data, cwd, arg, !strcasecmp(line, "NLST"));
		} else if (!strcasecmp(line, "RETR")) {
			do_retr(ctrl, &data, cwd, arg);
		} else if (!strcasecmp(line, "STOR") || !strcasecmp(line, "APPE")) {
			do_stor(ctrl, &data, cwd, arg, !strcasecmp(line, "APPE"));
		} else if (!strcasecmp(line, "SIZE")) {
			do_size(ctrl, cwd, arg);
		} else if (!strcasecmp(line, "MDTM")) {
			do_mdtm(ctrl, cwd, arg);
		} else if (!strcasecmp(line, "DELE")) {
			do_dele(ctrl, cwd, arg);
		} else if (!strcasecmp(line, "MKD") || !strcasecmp(line, "XMKD")) {
			do_mkd(ctrl, cwd, arg);
		} else if (!strcasecmp(line, "RMD") || !strcasecmp(line, "XRMD")) {
			do_rmd(ctrl, cwd, arg);
		} else if (!strcasecmp(line, "RNFR")) {
			char virt[128];

			if (!*arg || path_from_cwd(cwd, arg, virt, sizeof(virt),
						   rename_from,
						   sizeof(rename_from)) < 0) {
				rename_from[0] = '\0';
				reply(ctrl, 501, "Bad filename");
			} else if (access(rename_from, F_OK) < 0) {
				rename_from[0] = '\0';
				reply(ctrl, 550, "Not found");
			} else {
				reply(ctrl, 350, "Ready for RNTO");
			}
		} else if (!strcasecmp(line, "RNTO")) {
			char virt[128];
			char path[160];

			if (!rename_from[0]) {
				reply(ctrl, 503, "Use RNFR first");
			} else if (!*arg ||
				   path_from_cwd(cwd, arg, virt, sizeof(virt),
						 path, sizeof(path)) < 0) {
				reply(ctrl, 501, "Bad filename");
			} else if (rename(rename_from, path) < 0) {
				reply(ctrl, 550, "Rename failed");
			} else {
				rename_from[0] = '\0';
				reply(ctrl, 250, "Renamed");
			}
		} else if (!strcasecmp(line, "QUIT")) {
			reply(ctrl, 221, "Bye");
			break;
		} else {
			reply(ctrl, 502, "Command not implemented");
		}
	}

	data_clear(&data);
	if (ferror(ctrl))
		reply(ctrl, 421, "Control connection idle timeout");
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
