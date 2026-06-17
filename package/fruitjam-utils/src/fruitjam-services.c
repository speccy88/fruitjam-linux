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
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const char *ftp_root = "/mnt/sd";
static const char *sd_web_root = "/mnt/sd/www";
static const char *sd_index_path = "/mnt/sd/www/index.html";
static const char *tftp_root = "/tmp/tftp";
static const char *airlift_monitor_pid = "/run/fruitjam-airlift-monitor.pid";
static const char *const wifi_conf_paths[] = {
	"/mnt/sd/fruitjam/wifi.conf",
	"/etc/fruitjam-wifi.conf",
};
static volatile sig_atomic_t service_stop_requested;
static int runtime_initialized;

static void usage(FILE *out)
{
	fprintf(out,
		"usage: fruitjam-services {init|httpd|telnetd|airlift-shell|airlift-inbound|airlift-monitor|ftpd|tftpd|dropbear|buttonlog|buttons|core|all|sd-refresh|stop|restart|status}\n");
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

static int write_small_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY);
	ssize_t ret;

	if (fd < 0)
		return -1;
	ret = write(fd, text, strlen(text));
	close(fd);
	return ret == (ssize_t)strlen(text) ? 0 : -1;
}

static int gpio_path(unsigned int gpio, const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "/sys/class/gpio/gpio%u/%s", gpio, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int gpio_export(unsigned int gpio)
{
	char path[64];
	char text[12];
	int fd;
	ssize_t ret;
	int i;

	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd < 0)
		return -1;
	snprintf(text, sizeof(text), "%u", gpio);
	ret = write(fd, text, strlen(text));
	close(fd);
	if (ret < 0 && errno != EBUSY)
		return -1;
	if (gpio_path(gpio, "value", path, sizeof(path)) < 0)
		return -1;
	for (i = 0; i < 50; i++) {
		if (access(path, F_OK) == 0)
			return 0;
		usleep(2000);
	}
	return 0;
}

static void gpio_output_high(unsigned int gpio)
{
	char path[64];

	if (gpio_export(gpio) < 0)
		return;
	if (gpio_path(gpio, "direction", path, sizeof(path)) == 0) {
		if (write_small_file(path, "high") < 0)
			write_small_file(path, "out");
	}
	if (gpio_path(gpio, "value", path, sizeof(path)) == 0)
		write_small_file(path, "1");
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

static int dir_read_ok(const char *path)
{
	DIR *dir = opendir(path);
	int saved;

	if (!dir)
		return errno == ENOENT ? 0 : -1;
	errno = 0;
	while (readdir(dir) != NULL)
		;
	saved = errno;
	closedir(dir);
	return saved == 0 ? 0 : -1;
}

static int sd_is_mounted(void)
{
	FILE *fp = fopen("/proc/mounts", "r");
	char line[192];
	char device[64];
	char mountpoint[64];
	char type[24];

	if (!fp)
		return 0;
	while (fgets(line, sizeof(line), fp)) {
		if (sscanf(line, "%63s %63s %23s", device, mountpoint, type) != 3)
			continue;
		if (!strcmp(mountpoint, "/mnt/sd")) {
			fclose(fp);
			return !strcmp(device, "/dev/mmcblk0p1") && !strcmp(type, "vfat");
		}
	}
	fclose(fp);
	return 0;
}

static int wait_for_sd_device(void)
{
	int i;

	for (i = 0; i < 30; i++) {
		if (access("/dev/mmcblk0p1", F_OK) == 0 && access("/mnt/sd", F_OK) == 0)
			return 0;
		usleep(100000);
	}
	return -1;
}

static int mount_sd(void)
{
	if (mount("/dev/mmcblk0p1", "/mnt/sd", "vfat", MS_SYNCHRONOUS, NULL) < 0) {
		fprintf(stderr, "fruitjam-services: mount /mnt/sd: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int refresh_sd_mount(void)
{
	if (wait_for_sd_device() < 0)
		return 0;

	if (sd_is_mounted()) {
		fprintf(stderr, "fruitjam-services: refreshing /mnt/sd\n");
		if (umount("/mnt/sd") < 0 && errno != EINVAL) {
			fprintf(stderr, "fruitjam-services: umount /mnt/sd: %s\n", strerror(errno));
			return -1;
		}
	} else {
		fprintf(stderr, "fruitjam-services: mounting /mnt/sd\n");
	}

	return mount_sd();
}

static int repair_sd_mount_if_needed(void)
{
	if (wait_for_sd_device() < 0)
		return 0;
	if (!sd_is_mounted())
		return refresh_sd_mount();
	if (dir_read_ok("/mnt/sd") == 0 && dir_read_ok("/mnt/sd/wavs") == 0)
		return 0;

	fprintf(stderr, "fruitjam-services: remounting /mnt/sd after directory read error\n");
	return refresh_sd_mount();
}

static int ensure_sd_web_placeholder(void)
{
	static const char placeholder[] =
		"<!doctype html>\n"
		"<html lang=\"en\">\n"
		"<head>\n"
		"  <meta charset=\"utf-8\">\n"
		"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
		"  <title>Fruit Jam stuff</title>\n"
		"</head>\n"
		"<body>\n"
		"  <h1>Fruit Jam stuff</h1>\n"
		"  <p>This page lives on the microSD card at /mnt/sd/www/index.html.</p>\n"
		"  <p>Replace it with your own page. The hardware playground is at <a href=\"/playground\">/playground</a>.</p>\n"
		"</body>\n"
		"</html>\n";

	if (!sd_is_mounted())
		return 0;
	if (mkdir_p(sd_web_root) < 0) {
		fprintf(stderr, "fruitjam-services: mkdir %s: %s\n",
			sd_web_root, strerror(errno));
		return -1;
	}
	if (access(sd_index_path, F_OK) == 0)
		return 0;
	return write_text_file(sd_index_path, placeholder);
}

static int configure_loopback(void)
{
	struct ifreq ifr;
	struct sockaddr_in *addr;
	int fd;
	int ret = 0;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		fprintf(stderr, "fruitjam-services: loopback socket: %s\n", strerror(errno));
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, "lo", IFNAMSIZ - 1);
	addr = (struct sockaddr_in *)&ifr.ifr_addr;
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (ioctl(fd, SIOCSIFADDR, &ifr) < 0 && errno != EEXIST) {
		fprintf(stderr, "fruitjam-services: loopback address: %s\n", strerror(errno));
		ret = -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, "lo", IFNAMSIZ - 1);
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
		fprintf(stderr, "fruitjam-services: loopback flags read: %s\n", strerror(errno));
		ret = -1;
	} else {
		ifr.ifr_flags |= IFF_UP | IFF_RUNNING | IFF_LOOPBACK;
		if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
			fprintf(stderr, "fruitjam-services: loopback flags write: %s\n",
				strerror(errno));
			ret = -1;
		}
	}

	close(fd);
	return ret;
}

static int sd_refresh_worker(void)
{
	int i;

	for (i = 0; i < 12; i++) {
		usleep(i == 0 ? 5000000 : 2500000);
		if (refresh_sd_mount() == 0 && sd_is_mounted() &&
		    dir_read_ok("/mnt/sd") == 0 && dir_read_ok("/mnt/sd/wavs") == 0)
			return 0;
	}
	return 1;
}

static int spawn_wait(char *const argv[])
{
	pid_t pid = vfork();
	int status;
	pid_t ret;

	if (pid < 0) {
		fprintf(stderr, "fruitjam-services: vfork %s: %s\n", argv[0], strerror(errno));
		return -1;
	}
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}
	for (;;) {
		ret = waitpid(pid, &status, 0);
		if (ret >= 0)
			break;
		if (errno == EINTR && !service_stop_requested)
			continue;
		if (errno == EINTR && service_stop_requested) {
			kill(pid, SIGTERM);
			while ((ret = waitpid(pid, &status, 0)) < 0 &&
			       errno == EINTR)
				;
			if (ret >= 0)
				break;
		}
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

static int spawn_bg_log(char *const argv[], const char *log_path)
{
	pid_t pid = vfork();

	if (pid < 0) {
		fprintf(stderr, "fruitjam-services: vfork %s: %s\n", argv[0], strerror(errno));
		return -1;
	}
	if (pid == 0) {
		int null_fd = open("/dev/null", O_RDONLY);
		int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if (null_fd >= 0) {
			dup2(null_fd, STDIN_FILENO);
			if (null_fd > STDERR_FILENO)
				close(null_fd);
		}
		if (log_fd >= 0) {
			dup2(log_fd, STDOUT_FILENO);
			dup2(log_fd, STDERR_FILENO);
			if (log_fd > STDERR_FILENO)
				close(log_fd);
		}
		execv(argv[0], argv);
		_exit(127);
	}
	return 0;
}

static void trim_line(char *s)
{
	size_t len = strlen(s);

	while (len && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
		       s[len - 1] == ' ' || s[len - 1] == '\t'))
		s[--len] = '\0';
	while (*s == ' ' || *s == '\t')
		memmove(s, s + 1, strlen(s));
}

static int read_wifi_config_file(const char *path, char *ssid, size_t ssid_len,
				 char *password, size_t password_len)
{
	FILE *fp = fopen(path, "r");
	char line[160];

	ssid[0] = '\0';
	password[0] = '\0';
	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		char *eq;
		char *key = line;
		char *value;

		trim_line(line);
		if (!line[0] || line[0] == '#')
			continue;
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		value = eq + 1;
		trim_line(key);
		trim_line(value);
		if (!strcmp(key, "SSID")) {
			strncpy(ssid, value, ssid_len - 1);
			ssid[ssid_len - 1] = '\0';
		} else if (!strcmp(key, "PASSWORD")) {
			strncpy(password, value, password_len - 1);
			password[password_len - 1] = '\0';
		}
	}
	fclose(fp);
	return ssid[0] && password[0] ? 0 : -1;
}

static int read_wifi_config(char *ssid, size_t ssid_len,
			    char *password, size_t password_len)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(wifi_conf_paths); i++) {
		if (read_wifi_config_file(wifi_conf_paths[i], ssid, ssid_len,
					  password, password_len) == 0)
			return 0;
	}
	ssid[0] = '\0';
	password[0] = '\0';
	return -1;
}

static int init_runtime(void)
{
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
	gpio_output_high(11); /* USB host 5 V power on */
	gpio_output_high(22); /* shared TLV320/ESP32-C6 reset deasserted */

	if (repair_sd_mount_if_needed() != 0)
		ret = -1;
	if (ensure_sd_web_placeholder() != 0)
		ret = -1;

	write_text_file("/tmp/tftp/README.txt", "Fruit Jam TFTP area\n");
	write_text_file("/mnt/sd/README.txt", "Fruit Jam SD FTP area\n");

	if (configure_loopback() != 0)
		ret = -1;
	if (ret == 0)
		runtime_initialized = 1;
	return ret;
}

static int start_http(void)
{
	char *const argv[] = {
		"/usr/sbin/fruitjam-httpd", "80", NULL
	};

	init_runtime();
	return spawn_bg_log(argv, "/tmp/fruitjam-httpd.log");
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

static int start_airlift_shell(void)
{
	char ssid[40];
	char password[80];
	char *const argv[] = {
		"/usr/bin/airliftctl", "serve-inbound", NULL
	};

	if (access("/usr/bin/airliftctl", X_OK) < 0)
		return 0;

	init_runtime();
	{
		char *const probe[] = {
			"/usr/bin/airliftctl", "probe", NULL
		};

		if (spawn_wait(probe) != 0) {
			fprintf(stderr, "fruitjam-services: AirLift probe failed\n");
			return -1;
		}
	}
	if (read_wifi_config(ssid, sizeof(ssid), password, sizeof(password)) == 0) {
		char *const join[] = {
			"/usr/bin/airliftctl", "join", ssid, password, NULL
		};

		if (spawn_wait(join) != 0) {
			fprintf(stderr, "fruitjam-services: AirLift WiFi join failed\n");
			return -1;
		}
	}
	return spawn_bg_log(argv, "/tmp/airlift-inbound.log");
}

static int write_pid_file(const char *path)
{
	char text[32];

	snprintf(text, sizeof(text), "%ld\n", (long)getpid());
	return write_text_file(path, text);
}

static int read_pid_file(const char *path, pid_t *pid)
{
	char line[32];
	char *end;
	FILE *fp;
	long value;

	fp = fopen(path, "r");
	if (!fp)
		return -1;
	if (!fgets(line, sizeof(line), fp)) {
		fclose(fp);
		return -1;
	}
	fclose(fp);

	errno = 0;
	value = strtol(line, &end, 10);
	if (errno || value <= 1)
		return -1;
	*pid = (pid_t)value;
	return 0;
}

static int pid_is_alive(pid_t pid)
{
	if (pid <= 1)
		return 0;
	if (kill(pid, 0) == 0 || errno == EPERM)
		return 1;
	return 0;
}

static void request_service_stop(int sig)
{
	(void)sig;
	service_stop_requested = 1;
}

static int prepare_airlift_network(void)
{
	char ssid[40];
	char password[80];
	char *const probe[] = {
		"/usr/bin/airliftctl", "probe", NULL
	};

	init_runtime();
	if (spawn_wait(probe) != 0) {
		fprintf(stderr, "fruitjam-services: AirLift probe failed\n");
		return -1;
	}
	if (read_wifi_config(ssid, sizeof(ssid), password, sizeof(password)) == 0) {
		char *const join[] = {
			"/usr/bin/airliftctl", "join", ssid, password, NULL
		};

		if (spawn_wait(join) != 0) {
			fprintf(stderr, "fruitjam-services: AirLift WiFi join failed\n");
			return -1;
		}
	}
	return 0;
}

static int airlift_monitor(void)
{
	int failures = 0;

	if (access("/usr/bin/airliftctl", X_OK) < 0)
		return 0;
	service_stop_requested = 0;
	signal(SIGINT, request_service_stop);
	signal(SIGTERM, request_service_stop);
	if (write_pid_file(airlift_monitor_pid) < 0)
		fprintf(stderr, "fruitjam-services: cannot write %s\n",
			airlift_monitor_pid);

	while (!service_stop_requested) {
		char *const serve[] = {
			"/usr/bin/airliftctl", "serve-inbound", NULL
		};
		int ret;

		if (prepare_airlift_network() != 0) {
			if (service_stop_requested)
				break;
			failures++;
			sleep(failures > 3 ? 10 : 2);
			continue;
		}

		failures = 0;
		fprintf(stderr, "fruitjam-services: starting AirLift inbound server\n");
		ret = spawn_wait(serve);
		if (service_stop_requested)
			break;
		fprintf(stderr, "fruitjam-services: AirLift inbound server exited %d\n",
			ret);
		failures++;
		sleep(failures > 3 ? 10 : 2);
	}
	unlink(airlift_monitor_pid);
	return 0;
}

static int start_airlift_background(void)
{
	char *const argv[] = {
		"/usr/bin/fruitjam-services", "airlift-monitor", NULL
	};
	pid_t pid;

	if (access("/usr/bin/airliftctl", X_OK) < 0)
		return 0;
	if (read_pid_file(airlift_monitor_pid, &pid) == 0) {
		if (pid_is_alive(pid))
			return 0;
		unlink(airlift_monitor_pid);
	}
	return spawn_bg_log(argv, "/tmp/airlift-start.log");
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
		"httpd", "fruitjam-httpd", "telnetd", "fruitjam-telnet", "dropbear", "tcpsvd",
		"udpsvd", "ftpd", "tftpd", "fruitjam-ftpd", "fruitjam-buttons",
		"fruitjam-button", "airliftctl"
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
	pid_t pid;

	if (read_pid_file(airlift_monitor_pid, &pid) == 0 && pid_is_alive(pid))
		kill(pid, SIGTERM);
	signal_services(SIGTERM);
	usleep(700000);
	if (read_pid_file(airlift_monitor_pid, &pid) == 0 && pid_is_alive(pid))
		kill(pid, SIGKILL);
	signal_services(SIGKILL);
	unlink(airlift_monitor_pid);
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

	/*
	 * Keep the default boot service set below the fragile no-MMU allocation
	 * classes. AirLift's inbound server provides the external HTTP, telnet,
	 * and FTP surface directly, so the heavier loopback daemons are left for
	 * an explicit "fruitjam-services core" when they are needed. AirLift
	 * startup also probes/joins WiFi, so launch it through a logged worker
	 * and let the serial/USB shells come up even if the ESP32-C6 misbehaves.
	 */
	ret |= init_runtime();
	ret |= start_airlift_background();
	ret |= start_buttons();
	return ret;
}

static void print_matching_processes(void)
{
	static const char *const names[] = {
		"httpd", "fruitjam-httpd", "telnetd", "fruitjam-telnet", "dropbear", "tcpsvd",
		"udpsvd", "ftpd", "tftpd", "fruitjam-ftpd", "fruitjam-buttons",
		"fruitjam-button", "airliftctl"
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
	{
		pid_t pid;

			if (read_pid_file(airlift_monitor_pid, &pid) == 0 &&
			    pid_is_alive(pid))
				printf("  %ld fruitjam-services airlift-monitor\n",
				       (long)pid);
	}
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
	if (!strcmp(argv[1], "airlift-shell") || !strcmp(argv[1], "airlift-inbound"))
		return start_airlift_shell();
	if (!strcmp(argv[1], "airlift-monitor"))
		return airlift_monitor();
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
	if (!strcmp(argv[1], "sd-refresh"))
		return sd_refresh_worker();
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
