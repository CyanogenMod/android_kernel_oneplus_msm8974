# Makefile for frandom 1.1

MOD_NAME = frandom

ifneq ($(KERNELRELEASE), )

obj-$(CONFIG_FRANDOM)		+= $(MOD_NAME).o

else

all:
	@$(MAKE) -C $(KDIR) \
		SUBDIRS=$(CURDIR) modules

clean:
	rm -f *.o *.ko *.mod.* *~ .*.cmd

install:
	@$(MAKE) --no-print-directory -C $(KDIR) \
		SUBDIRS=$(CURDIR) modules_install

endif
