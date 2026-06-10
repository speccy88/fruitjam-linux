// SPDX-License-Identifier: MIT
/*
 * Tiny service launcher for the no-MMU Fruit Jam image.
 *
 * Keeping this as a C binary avoids starting another full /bin/sh instance
 * just to launch small BusyBox-derived networking applets.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const char *www_root = "/www";
static const char *ftp_root = "/tmp/ftp";
static const char *tftp_root = "/tmp/tftp";
static int runtime_initialized;

static void usage(FILE *out)
{
	fprintf(out,
		"usage: fruitjam-services {init|httpd|telnetd|ftpd|tftpd|dropbear|buttonlog|buttons|core|all|stop|restart|status}\n");
}

static int write_text_file(const char *path, const char *text)
{
	FILE *fp = fopen(path, "w");

	if (!fp) {
		fprintf(stderr, "fruitjam-services: %s: %s\n", path, strerror(errno));
		return -1;
	}
	fputs(text, fp);
	fclose(fp);
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

static int spawn_wait(char *const argv[])
{
	pid_t pid = vfork();
	int status;

	if (pid < 0) {
		fprintf(stderr, "fruitjam-services: vfork %s: %s\n", argv[0], strerror(errno));
		return -1;
	}
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0) {
		fprintf(stderr, "fruitjam-services: wait %s: %s\n", argv[0], strerror(errno));
		return -1;
	}
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 1;
}

static int spawn_bg(char *const argv[])
{
	pid_t pid = vfork();

	if (pid < 0) {
		fprintf(stderr, "fruitjam-services: vfork %s: %s\n", argv[0], strerror(errno));
		return -1;
	}
	if (pid == 0) {
		int fd = open("/dev/null", O_RDWR);

		if (fd >= 0) {
			dup2(fd, STDIN_FILENO);
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			if (fd > STDERR_FILENO)
				close(fd);
		}
		execv(argv[0], argv);
		_exit(127);
	}
	return 0;
}

static int init_runtime(void)
{
	char *const ifconfig[] = {
		"/sbin/ifconfig", "lo", "127.0.0.1", "up", NULL
	};
	int ret = 0;

	if (runtime_initialized)
		return 0;

	if (mkdir_p(ftp_root) < 0) {
		fprintf(stderr, "fruitjam-services: mkdir %s: %s\n", ftp_root, strerror(errno));
		ret = -1;
	}
	if (mkdir_p(tftp_root) < 0) {
		fprintf(stderr, "fruitjam-services: mkdir %s: %s\n", tftp_root, strerror(errno));
		ret = -1;
	}
	if (mkdir_p("/run/dropbear") < 0) {
		fprintf(stderr, "fruitjam-services: mkdir /run/dropbear: %s\n", strerror(errno));
		ret = -1;
	}

	write_text_file("/tmp/tftp/README.txt", "Fruit Jam TFTP area\n");
	write_text_file("/tmp/ftp/README.txt", "Fruit Jam FTP area\n");

	if (spawn_wait(ifconfig) != 0)
		ret = -1;
	if (ret == 0)
		runtime_initialized = 1;
	return ret;
}

static int start_http(void)
{
	char *const argv[] = {
		"/usr/sbin/httpd", "-h", (char *)www_root, "-p", "80", NULL
	};

	init_runtime();
	return spawn_wait(argv);
}

static int start_tftp(void)
{
	char *const argv[] = {
		"/usr/bin/udpsvd", "-E", "0.0.0.0", "69",
		"/usr/sbin/tftpd", (char *)tftp_root, NULL
	};

	init_runtime();
	return spawn_bg(argv);
}

static int start_ftp(void)
{
	char *const argv[] = {
		"/usr/sbin/fruitjam-ftpd", "21", NULL
	};

	init_runtime();
	return spawn_bg(argv);
}

static int start_telnet(void)
{
	char *const argv[] = {
		"/usr/sbin/fruitjam-telnetd", "23", "/usr/bin/fruitjam-shell", NULL
	};

	init_runtime();
	return spawn_bg(argv);
}

static int start_dropbear(void)
{
	char *const argv[] = {
		"/usr/bin/tcpsvd", "-E", "0.0.0.0", "22",
		"/usr/sbin/dropbear", "-i", "-R", NULL
	};

	if (access("/usr/sbin/dropbear", X_OK) < 0)
		return 0;

	init_runtime();
	return spawn_bg(argv);
}

static int start_buttons(void)
{
	char *const argv[] = {
		"/usr/bin/fruitjam-buttons", "daemon", NULL
	};

	if (access("/usr/bin/fruitjam-buttons", X_OK) < 0)
		return 0;

	init_runtime();
	return spawn_bg(argv);
}

static int start_buttonlog(void)
{
	char *const argv[] = {
		"/usr/bin/fruitjam-buttonlog", "daemon", "/mnt/sd/fruitjam/buttons.db", NULL
	};

	if (access("/usr/bin/fruitjam-buttonlog", X_OK) < 0)
		return 0;

	init_runtime();
	return spawn_bg(argv);
}

static void trim_newline(char *s)
{
	size_t len = strlen(s);

	while (len && (s[len - 1] == '\n' || s[len - 1] == '\r'))
		s[--len] = '\0';
}

static int comm_matches(pid_t pid, const char *const *names, size_t count)
{
	char path[64];
	char comm[64];
	FILE *fp;
	size_t i;

	snprintf(path, sizeof(path), "/proc/%ld/comm", (long)pid);
	fp = fopen(path, "r");
	if (!fp)
		return 0;
	if (!fgets(comm, sizeof(comm), fp)) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	trim_newline(comm);

	for (i = 0; i < count; i++) {
		if (!strcmp(comm, names[i]))
			return 1;
	}
	return 0;
}

static int read_comm(pid_t pid, char *comm, size_t comm_len)
{
	char path[64];
	FILE *fp;

	snprintf(path, sizeof(path), "/proc/%ld/comm", (long)pid);
	fp = fopen(path, "r");
	if (!fp)
		return -1;
	if (!fgets(comm, comm_len, fp)) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	trim_newline(comm);
	return 0;
}

static void signal_services(int sig)
{
	static const char *const names[] = {
		"httpd", "telnetd", "fruitjam-telnet", "dropbear", "tcpsvd",
		"udpsvd", "ftpd", "tftpd", "fruitjam-ftpd", "fruitjam-buttons",
		"fruitjam-button"
	};
	DIR *dir = opendir("/proc");
	struct dirent *de;

	if (!dir)
		return;

	while ((de = readdir(dir)) != NULL) {
		char *end;
		long pid = strtol(de->d_name, &end, 10);

		if (*end || pid <= 1)
			continue;
		if (comm_matches((pid_t)pid, names, ARRAY_SIZE(names)))
			kill((pid_t)pid, sig);
	}
	closedir(dir);
}

static int stop_services(void)
{
	signal_services(SIGTERM);
	usleep(150000);
	signal_services(SIGKILL);
	return 0;
}

static int start_core(void)
{
	int ret = 0;

	ret |= start_http();
	ret |= start_telnet();
	ret |= start_ftp();
	ret |= start_tftp();
	return ret;
}

static int start_all(void)
{
	int ret = 0;

	ret |= start_core();
	ret |= start_buttons();
	return ret;
}

static void print_matching_processes(void)
{
	static const char *const names[] = {
		"httpd", "telnetd", "fruitjam-telnet", "dropbear", "tcpsvd",
		"udpsvd", "ftpd", "tftpd", "fruitjam-ftpd", "fruitjam-buttons",
		"fruitjam-button"
	};
	DIR *dir = opendir("/proc");
	struct dirent *de;

	puts("services:");
	if (!dir) {
		printf("  /proc unavailable: %s\n", strerror(errno));
		return;
	}

	while ((de = readdir(dir)) != NULL) {
		char *end;
		long pid = strtol(de->d_name, &end, 10);
		char comm[64];

		if (*end || pid <= 1)
			continue;
		if (!comm_matches((pid_t)pid, names, ARRAY_SIZE(names)))
			continue;
		if (read_comm((pid_t)pid, comm, sizeof(comm)) < 0)
			continue;
		printf("  %ld %s\n", pid, comm);
	}
	closedir(dir);
}

static void print_ipv4(unsigned int addr)
{
	printf("%u.%u.%u.%u",
	       addr & 0xff,
	       (addr >> 8) & 0xff,
	       (addr >> 16) & 0xff,
	       (addr >> 24) & 0xff);
}

static void print_listeners(const char *path, const char *proto)
{
	FILE *fp = fopen(path, "r");
	char line[192];

	if (!fp) {
		printf("%s listeners unavailable: %s\n", proto, strerror(errno));
		return;
	}

	if (!fgets(line, sizeof(line), fp)) {
		fclose(fp);
		return;
	}

	while (fgets(line, sizeof(line), fp)) {
		unsigned int local_addr, local_port, remote_addr, remote_port, state;
		int fields;

		fields = sscanf(line, " %*u: %x:%x %x:%x %x",
				&local_addr, &local_port,
				&remote_addr, &remote_port, &state);
		if (fields != 5)
			continue;
		if (!strcmp(proto, "tcp") && state != 0x0a)
			continue;

		printf("  %s ", proto);
		print_ipv4(local_addr);
		printf(":%u\n", local_port);
	}

	fclose(fp);
}

static int status_services(void)
{
	print_matching_processes();
	puts("listeners:");
	print_listeners("/proc/net/tcp", "tcp");
	print_listeners("/proc/net/udp", "udp");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		usage(stderr);
		return 2;
	}

	if (!strcmp(argv[1], "init"))
		return init_runtime() == 0 ? 0 : 1;
	if (!strcmp(argv[1], "httpd"))
		return start_http();
	if (!strcmp(argv[1], "telnetd"))
		return start_telnet();
	if (!strcmp(argv[1], "ftpd"))
		return start_ftp();
	if (!strcmp(argv[1], "tftpd"))
		return start_tftp();
	if (!strcmp(argv[1], "dropbear"))
		return start_dropbear();
	if (!strcmp(argv[1], "buttonlog"))
		return start_buttonlog();
	if (!strcmp(argv[1], "buttons"))
		return start_buttons();
	if (!strcmp(argv[1], "core"))
		return start_core();
	if (!strcmp(argv[1], "all"))
		return start_all();
	if (!strcmp(argv[1], "stop"))
		return stop_services();
	if (!strcmp(argv[1], "restart")) {
		stop_services();
		return start_all();
	}
	if (!strcmp(argv[1], "status"))
		return status_services();

	usage(stderr);
	return 2;
}
