obj-m += led_drv.o

KDIR := /home/book/100ask_imx6ull-sdk/Linux-4.9.88
PWD  := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean