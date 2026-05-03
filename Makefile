# SPDX-License-Identifier: GPL-2.0-only

ccflags-y := -Wno-unused-function
obj-y := data-eth.o

obj-$(CONFIG_QPS615) += drivers/qps615/src/
obj-$(CONFIG_QTI_QUIN_GVM) += drivers/emac_ctrl_fe/
obj-$(CONFIG_EMAC_SHIM) += drivers/emac_shim/
obj-$(CONFIG_QCA_NSS_PHY) += drivers/qca-nss-phy/linux_std/qca81xx/

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
