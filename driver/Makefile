obj-m += led_drv.o

KDIR := /home/cgh/Linux-4.9.88
PWD  := $(shell pwd)
ARCH := arm
CROSS_COMPILE := arm-buildroot-linux-gnueabihf-

all:
	make -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) modules

clean:
	make -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean
