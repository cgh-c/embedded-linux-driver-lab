#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEVICE_PATH "/dev/led_drv"

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s on|off\n", prog);
}

int main(int argc, char *argv[])
{
	int fd;
	char val;

	if (argc != 2) {
		usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "on") == 0)
		val = '1';
	else if (strcmp(argv[1], "off") == 0)
		val = '0';
	else {
		usage(argv[0]);
		return 1;
	}

	fd = open(DEVICE_PATH, O_WRONLY);
	if (fd < 0) {
		perror("open " DEVICE_PATH);
		return 1;
	}

	if (write(fd, &val, 1) != 1) {
		perror("write");
		close(fd);
		return 1;
	}

	printf("LED %s\n", val == '1' ? "ON" : "OFF");
	close(fd);
	return 0;
}
