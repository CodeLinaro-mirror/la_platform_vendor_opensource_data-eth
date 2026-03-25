KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M=$(PWD)
DATAETH_ROOT=$(KERNEL_SRC)/$(M)

KBUILD_OPTIONS+=KBUILD_DTC_INCLUDE=$(DATAETH_ROOT)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) INSTALL_MOD_STRIP=1 -C $(KERNEL_SRC) M=$(M) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean

%:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $@ $(KBUILD_OPTIONS)
