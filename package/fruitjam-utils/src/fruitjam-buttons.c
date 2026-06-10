// SPDX-License-Identifier: MIT
/*
 * GPIO0/GPIO4/GPIO5 button action helper for Fruit Jam.
 *
 * Falling edges are logged to SQLite on the SD card and optionally published
 * through the tiny mosquitto_pub-compatible helper when MQTT_HOST is set.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define CONTROL_FIFO "/run/fruitjam-buttons.fifo"
#define LOG_FIFO "/run/fruitjam-buttonlog.fifo"

struct button {
	const char *name;
	unsigned int gpio;
	int last_value;
};

struct config {
	char db_path[128];
	char mqtt_host[96];
	char mqtt_port[8];
	char mqtt_topic[96];
	char mqtt_client_id[64];
	char mqtt_transport[16];
	unsigned int poll_ms;
};

static volatile sig_atomic_t stop_requested;

static struct button buttons[] = {
	{ "button1", 0, 1 },
	{ "button2", 4, 1 },
	{ "button3", 5, 1 },
};

static const struct button *find_button(const char *name);

static void on_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static void config_defaults(struct config *cfg)
{
	strcpy(cfg->db_path, "/mnt/sd/fruitjam/buttons.db");
	cfg->mqtt_host[0] = '\0';
	strcpy(cfg->mqtt_port, "1883");
	strcpy(cfg->mqtt_topic, "fruitjam/buttons");
	strcpy(cfg->mqtt_client_id, "fruitjam-rp2350");
	strcpy(cfg->mqtt_transport, "socket");
	cfg->poll_ms = 50;
}

static void trim(char *s)
{
	char *end;

	while (*s == ' ' || *s == '\t')
		memmove(s, s + 1, strlen(s));
	end = s + strlen(s);
	while (end > s && (end[-1] == '\n' || end[-1] == '\r' ||
			   end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';
}

static void copy_value(char *dst, size_t dst_len, const char *src)
{
	snprintf(dst, dst_len, "%s", src);
}

static void load_config_file(struct config *cfg, const char *path)
{
	FILE *fp = fopen(path, "r");
	char line[192];

	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		char *eq;
		char *key = line;
		char *value;

		trim(key);
		if (!key[0] || key[0] == '#')
			continue;
		eq = strchr(key, '=');
		if (!eq)
			continue;
		*eq = '\0';
		value = eq + 1;
		trim(key);
		trim(value);

		if (!strcmp(key, "DB_PATH"))
			copy_value(cfg->db_path, sizeof(cfg->db_path), value);
		else if (!strcmp(key, "MQTT_HOST"))
			copy_value(cfg->mqtt_host, sizeof(cfg->mqtt_host), value);
		else if (!strcmp(key, "MQTT_PORT"))
			copy_value(cfg->mqtt_port, sizeof(cfg->mqtt_port), value);
		else if (!strcmp(key, "MQTT_TOPIC"))
			copy_value(cfg->mqtt_topic, sizeof(cfg->mqtt_topic), value);
		else if (!strcmp(key, "MQTT_CLIENT_ID"))
			copy_value(cfg->mqtt_client_id, sizeof(cfg->mqtt_client_id), value);
		else if (!strcmp(key, "MQTT_TRANSPORT"))
			copy_value(cfg->mqtt_transport, sizeof(cfg->mqtt_transport), value);
		else if (!strcmp(key, "POLL_MS"))
			cfg->poll_ms = (unsigned int)strtoul(value, NULL, 10);
	}

	fclose(fp);
}

static void load_config(struct config *cfg)
{
	config_defaults(cfg);
	load_config_file(cfg, "/etc/fruitjam-buttons.conf");
	load_config_file(cfg, "/mnt/sd/fruitjam/buttons.conf");
	if (cfg->poll_ms < 10)
		cfg->poll_ms = 10;
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

static void ensure_parent_dir(const char *path)
{
	char tmp[128];
	char *slash;

	snprintf(tmp, sizeof(tmp), "%s", path);
	slash = strrchr(tmp, '/');
	if (!slash || slash == tmp)
		return;
	*slash = '\0';
	mkdir_p(tmp);
}

static int write_text_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_TRUNC);
	ssize_t ret;

	if (fd < 0)
		return -1;
	ret = write(fd, text, strlen(text));
	close(fd);
	return ret == (ssize_t)strlen(text) ? 0 : -1;
}

static int export_gpio(unsigned int gpio)
{
	char buf[16];
	int fd = open("/sys/class/gpio/export", O_WRONLY);
	ssize_t ret;

	if (fd < 0)
		return -1;
	snprintf(buf, sizeof(buf), "%u", gpio);
	ret = write(fd, buf, strlen(buf));
	close(fd);
	if (ret < 0 && errno != EBUSY)
		return -1;
	return 0;
}

static int gpio_value_path(unsigned int gpio, const char *leaf, char *path, size_t len)
{
	int ret = snprintf(path, len, "/sys/class/gpio/gpio%u/%s", gpio, leaf);

	return ret > 0 && (size_t)ret < len ? 0 : -1;
}

static int setup_button_gpio(unsigned int gpio)
{
	char path[64];

	export_gpio(gpio);
	if (gpio_value_path(gpio, "direction", path, sizeof(path)) < 0)
		return -1;
	if (write_text_file(path, "in") < 0)
		return -1;
	return 0;
}

static int read_gpio(unsigned int gpio)
{
	char path[64];
	char c;
	int fd;

	if (gpio_value_path(gpio, "value", path, sizeof(path)) < 0)
		return -1;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	if (read(fd, &c, 1) != 1) {
		close(fd);
		return -1;
	}
	close(fd);
	return c == '0' ? 0 : 1;
}

static int spawn_wait(char *const argv[])
{
	pid_t pid = vfork();
	int status;

	if (pid < 0)
		return -1;
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 1;
}

static void ensure_schema(const struct config *cfg)
{
	char *const argv[] = {
		"/usr/bin/fruitjam-buttonlog", "init", (char *)cfg->db_path, NULL
	};

	if (access("/usr/bin/fruitjam-buttonlog", X_OK) < 0)
		return;
	ensure_parent_dir(cfg->db_path);
	spawn_wait(argv);
}

static int write_log_fifo(const struct button *button, time_t ts, const char *action)
{
	char line[96];
	int fd;
	int len;
	ssize_t ret;

	fd = open(LOG_FIFO, O_WRONLY | O_NONBLOCK);
	if (fd < 0)
		return -1;
	len = snprintf(line, sizeof(line), "event %s %u %s %ld\n",
		       button->name, button->gpio, action, (long)ts);
	if (len <= 0 || (size_t)len >= sizeof(line)) {
		close(fd);
		return -1;
	}
	ret = write(fd, line, (size_t)len);
	close(fd);
	return ret == len ? 0 : -1;
}

static void log_button_event(const struct config *cfg, const struct button *button,
			     time_t ts, const char *action)
{
	char gpio[16];
	char ts_buf[24];
	char *const argv[] = {
		"/usr/bin/fruitjam-buttonlog", "event", (char *)cfg->db_path,
		(char *)button->name, gpio, (char *)action, ts_buf, NULL
	};

	if (access("/usr/bin/fruitjam-buttonlog", X_OK) < 0)
		return;
	ensure_parent_dir(cfg->db_path);
	if (write_log_fifo(button, ts, action) == 0)
		return;
	snprintf(gpio, sizeof(gpio), "%u", button->gpio);
	snprintf(ts_buf, sizeof(ts_buf), "%ld", (long)ts);
	spawn_wait(argv);
}

static void publish_button_event(const struct config *cfg, const struct button *button,
				 time_t ts, const char *action)
{
	char topic[128];
	char message[128];
	char *const socket_argv[] = {
		"/usr/bin/mosquitto_pub",
		"-h", (char *)cfg->mqtt_host,
		"-p", (char *)cfg->mqtt_port,
		"-i", (char *)cfg->mqtt_client_id,
		"-t", topic,
		"-m", message,
		NULL
	};
	char *const airlift_argv[] = {
		"/usr/bin/mosquitto_pub",
		"--airlift",
		"-h", (char *)cfg->mqtt_host,
		"-p", (char *)cfg->mqtt_port,
		"-i", (char *)cfg->mqtt_client_id,
		"-t", topic,
		"-m", message,
		NULL
	};

	if (!cfg->mqtt_host[0])
		return;
	snprintf(topic, sizeof(topic), "%s/%s", cfg->mqtt_topic, button->name);
	snprintf(message, sizeof(message), "%s gpio%u %s %ld",
		 button->name, button->gpio, action, (long)ts);
	if (!strcmp(cfg->mqtt_transport, "airlift"))
		spawn_wait(airlift_argv);
	else
		spawn_wait(socket_argv);
}

static void handle_button_event(const struct config *cfg, const struct button *button,
				const char *action)
{
	time_t ts = time(NULL);

	printf("%ld %s gpio%u %s\n", (long)ts, button->name, button->gpio, action);
	fflush(stdout);
	log_button_event(cfg, button, ts, action);
	publish_button_event(cfg, button, ts, action);
}

static void handle_control_line(const struct config *cfg, char *line)
{
	const char *name;
	const struct button *button;

	trim(line);
	if (!line[0])
		return;

	if (!strncmp(line, "test ", 5)) {
		name = line + 5;
		button = find_button(name);
		if (button)
			handle_button_event(cfg, button, "test");
		return;
	}
	if (!strcmp(line, "button1") || !strcmp(line, "button2") || !strcmp(line, "button3")) {
		button = find_button(line);
		if (button)
			handle_button_event(cfg, button, "test");
	}
}

static int setup_control_fifo(void)
{
	int fd;

	unlink(CONTROL_FIFO);
	if (mkfifo(CONTROL_FIFO, 0600) < 0)
		return -1;
	fd = open(CONTROL_FIFO, O_RDONLY | O_NONBLOCK);
	return fd;
}

static void poll_control_fifo(const struct config *cfg, int fd)
{
	char buf[128];
	ssize_t len;
	char *line;
	char *next;

	if (fd < 0)
		return;

	for (;;) {
		len = read(fd, buf, sizeof(buf) - 1);
		if (len <= 0)
			return;
		buf[len] = '\0';

		line = buf;
		while (line && *line) {
			next = strchr(line, '\n');
			if (next)
				*next++ = '\0';
			handle_control_line(cfg, line);
			line = next;
		}
	}
}

static int setup_buttons(void)
{
	size_t i;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(buttons); i++) {
		if (setup_button_gpio(buttons[i].gpio) < 0)
			ret = -1;
		buttons[i].last_value = read_gpio(buttons[i].gpio);
		if (buttons[i].last_value < 0)
			buttons[i].last_value = 1;
	}
	return ret;
}

static int status_buttons(void)
{
	size_t i;
	int ret = 0;

	setup_buttons();
	for (i = 0; i < ARRAY_SIZE(buttons); i++) {
		int value = read_gpio(buttons[i].gpio);

		if (value < 0) {
			printf("%s gpio%u error\n", buttons[i].name, buttons[i].gpio);
			ret = 1;
		} else {
			printf("%s gpio%u %s\n", buttons[i].name, buttons[i].gpio,
			       value ? "released" : "pressed");
		}
	}
	return ret;
}

static int dump_events(const struct config *cfg, unsigned int limit)
{
	char limit_buf[16];
	char *const argv[] = {
		"/usr/bin/fruitjam-buttonlog", "dump", (char *)cfg->db_path,
		limit_buf, NULL
	};

	if (access("/usr/bin/fruitjam-buttonlog", X_OK) < 0)
		return -1;
	snprintf(limit_buf, sizeof(limit_buf), "%u", limit ? limit : 10);
	return spawn_wait(argv);
}

static const struct button *find_button(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(buttons); i++) {
		if (!strcmp(buttons[i].name, name))
			return &buttons[i];
	}
	return NULL;
}

static int run_daemon(const struct config *cfg)
{
	size_t i;
	int fifo_fd;

	if (setup_buttons() < 0)
		fprintf(stderr, "fruitjam-buttons: one or more GPIOs failed to initialize\n");
	fifo_fd = setup_control_fifo();
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);

	while (!stop_requested) {
		poll_control_fifo(cfg, fifo_fd);
		for (i = 0; i < ARRAY_SIZE(buttons); i++) {
			int value = read_gpio(buttons[i].gpio);

			if (value < 0)
				continue;
			if (buttons[i].last_value == 1 && value == 0)
				handle_button_event(cfg, &buttons[i], "pressed");
			buttons[i].last_value = value;
		}
		usleep(cfg->poll_ms * 1000);
	}
	if (fifo_fd >= 0)
		close(fifo_fd);
	unlink(CONTROL_FIFO);
	return 0;
}

static void usage(FILE *out)
{
	fprintf(out,
		"usage: fruitjam-buttons {daemon|status|dump [limit]|test button1|button2|button3}\n");
}

int main(int argc, char **argv)
{
	struct config cfg;

	load_config(&cfg);

	if (argc == 2 && !strcmp(argv[1], "daemon"))
		return run_daemon(&cfg);
	if (argc == 2 && !strcmp(argv[1], "status"))
		return status_buttons();
	if ((argc == 2 || argc == 3) && !strcmp(argv[1], "dump"))
		return dump_events(&cfg, argc == 3 ? (unsigned int)strtoul(argv[2], NULL, 10) : 10);
	if (argc == 3 && !strcmp(argv[1], "test")) {
		const struct button *button = find_button(argv[2]);

		if (!button) {
			usage(stderr);
			return 2;
		}
		ensure_schema(&cfg);
		handle_button_event(&cfg, button, "test");
		return 0;
	}

	usage(stderr);
	return 2;
}
