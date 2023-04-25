# SPDX-License-Identifier: GPL-2.0-only

ccflags-y := -Wno-unused-function
obj-y := data-eth.o

obj-$(CONFIG_QPS615) += drivers/qps615/src/
obj-$(CONFIG_QPS615_IOSS) += drivers/qps615_ioss/
obj-m += drivers/r8125/src/
obj-$(CONFIG_R8125_IOSS) += drivers/r8125_ioss/
obj-m += drivers/ioss/
obj-$(CONFIG_AQFWD_IOSS)  += drivers/aqc_ioss/
obj-$(CONFIG_QTI_QUIN_GVM) += drivers/emac_ctrl_fe/
obj-$(CONFIG_EMAC_SHIM) += drivers/emac_shim/
obj-m += drivers/emac_ioss/

ifeq ($(KP_MODULE_ROOT),)
	KP_MODULE_ROOT=$(KERNEL_SRC)/$(M)
endif

M ?= $(shell pwd)

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

KBUILD_OPTIONS+=KBUILD_DTC_INCLUDE=$(KP_MODULE_ROOT)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) M=$(M) -C $(KERNEL_SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean

%:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $@ $(KBUILD_OPTIONS)
