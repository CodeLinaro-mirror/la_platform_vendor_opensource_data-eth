ifeq ($(KP_MODULE_ROOT),)
	KP_MODULE_ROOT=$(KERNEL_SRC)/$(M)
endif

M ?= $(shell pwd)

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

KBUILD_OPTIONS+=KBUILD_DTC_INCLUDE=$(KP_MODULE_ROOT)
# Optional: expose a config symbol if desired
# KBUILD_OPTIONS+=CONFIG_ETH_QOS=m

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) M=$(M) -C $(KERNEL_SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean

%:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $@ $(KBUILD_OPTIONS)
