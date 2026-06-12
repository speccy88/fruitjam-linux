// SPDX-License-Identifier: MIT
/*
 * Tiny raw telnet-style shell server for Fruit Jam.
 *
 * It intentionally does not allocate a pty or implement telnet option
 * negotiation. Each TCP client gets a tiny fruitjam-shell child with the
 * socket connected to stdin/stdout/stderr, while the listener immediately
 * goes back to accepting the next connection.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_PORT 23
#define DEFAULT_SHELL "/usr/bin/fruitjam-shell"
#define CLIENT_IDLE_SEC 120

static int parse_port(const char *s)
{
	char *end;
	long port = strtol(s, &end, 10);

	if (!*s || *end || port <= 0 || port > 65535)
		return -1;
	return (int)port;
}

static int serve_client(int client, const char *shell)
{
	pid_t pid = vfork();
	int one = 1;
	struct timeval idle = { CLIENT_IDLE_SEC, 0 };

	if (pid < 0) {
		perror("fruitjam-telnetd: vfork");
		close(client);
		return 1;
	}
	setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &idle, sizeof(idle));
	setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	if (pid == 0) {
		dup2(client, STDIN_FILENO);
		dup2(client, STDOUT_FILENO);
		dup2(client, STDERR_FILENO);
		if (client > STDERR_FILENO)
			close(client);
		execl(shell, shell, (char *)NULL);
		_exit(127);
	}

	close(client);
	return 0;
}

int main(int argc, char **argv)
{
	const char *shell = DEFAULT_SHELL;
	struct sockaddr_in addr;
	int port = DEFAULT_PORT;
	int one = 1;
	int srv;

	if (argc > 1) {
		port = parse_port(argv[1]);
		if (port < 0) {
			fprintf(stderr, "usage: fruitjam-telnetd [port] [shell]\n");
			return 1;
		}
	}
	if (argc > 2)
		shell = argv[2];

	signal(SIGCHLD, SIG_IGN);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)port);

	srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0) {
		perror("fruitjam-telnetd: socket");
		return 1;
	}
	setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("fruitjam-telnetd: bind");
		return 1;
	}
	if (listen(srv, 2) < 0) {
		perror("fruitjam-telnetd: listen");
		return 1;
	}

	for (;;) {
		int client = accept(srv, NULL, NULL);

		if (client < 0) {
			if (errno == EINTR)
				continue;
			perror("fruitjam-telnetd: accept");
			return 1;
		}
		serve_client(client, shell);
	}
}
