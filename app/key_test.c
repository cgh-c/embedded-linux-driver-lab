/*
 * key_test.c - User-space test program for key_drv
 *
 * Usage:
 *   ./key_test read          # blocking read, Ctrl-C to exit
 *   ./key_test poll [ms]     # poll with timeout (default 5000ms)
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>

#define DEVICE_PATH "/dev/key_drv"

static void usage(const char *prog)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s read          blocking read\n", prog);
	fprintf(stderr, "  %s poll [ms]     poll with timeout (default 5000)\n", prog);
}

/* Mode 1: blocking read — process sleeps until key event */
static int do_blocking_read(int fd)
{
	char val;
	ssize_t ret;

	printf("Blocking read mode. Press the key (Ctrl-C to quit)...\n");

	while (1) {
		ret = read(fd, &val, 1);
		if (ret < 0) {
			perror("read");
			return 1;
		}
		printf("Key event: %s\n", val == '1' ? "PRESSED" : "RELEASED");
	}

	return 0;
}

/* Mode 2: poll — monitor fd with timeout, then read */
static int do_poll_read(int fd, int timeout_ms)
{
	struct pollfd pfd;
	char val;
	int ret;

	printf("Poll mode, timeout=%dms. Press the key (Ctrl-C to quit)...\n",
	       timeout_ms);

	while (1) {
		pfd.fd     = fd;
		pfd.events = POLLIN;

		ret = poll(&pfd, 1, timeout_ms);
		if (ret < 0) {
			perror("poll");
			return 1;
		}

		if (ret == 0) {
			printf("poll: timeout, no key event\n");
			continue;
		}

		if (pfd.revents & POLLIN) {
			if (read(fd, &val, 1) < 0) {
				perror("read");
				return 1;
			}
			printf("Key event: %s\n",
			       val == '1' ? "PRESSED" : "RELEASED");
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int fd;
	int ret;

	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	fd = open(DEVICE_PATH, O_RDONLY);
	if (fd < 0) {
		perror("open " DEVICE_PATH);
		return 1;
	}

	if (strcmp(argv[1], "read") == 0) {
		ret = do_blocking_read(fd);
	} else if (strcmp(argv[1], "poll") == 0) {
		int timeout = 5000;
		if (argc >= 3)
			timeout = atoi(argv[2]);
		ret = do_poll_read(fd, timeout);
	} else {
		usage(argv[0]);
		ret = 1;
	}

	close(fd);
	return ret;
}
