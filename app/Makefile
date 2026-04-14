CROSS_COMPILE := arm-buildroot-linux-gnueabihf-
CC := $(CROSS_COMPILE)gcc

all: led_test

led_test: led_test.c
	$(CC) -o $@ $<

clean:
	rm -f led_test
