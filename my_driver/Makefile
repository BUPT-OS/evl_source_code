obj-m := user_dma.o
KDIR := /home/han/Desktop/evl_riscv/linux
PWD  := $(shell pwd)

CROSS_COMPILE := riscv64-linux-gnu-
ARCH := riscv

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
