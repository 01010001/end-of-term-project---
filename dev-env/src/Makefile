KDIR ?= /lib/modules/$(KERNEL_VERSION)/build
obj-m += helloguys.o

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean