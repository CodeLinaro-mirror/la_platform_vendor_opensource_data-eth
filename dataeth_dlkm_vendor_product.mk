# Build DATA-ETH kernel drivers
ifeq (true,$(call is-board-platform-in-list,gen4))
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/stmmac_thin_core.ko\
	$(KERNEL_MODULES_OUT)/emac_thin.ko\
	$(KERNEL_MODULES_OUT)/emac_thin_gy.ko\
	$(KERNEL_MODULES_OUT)/emac_ctrl_fe_virtio.ko
endif

ifeq (true,$(call is-board-platform-in-list,lahaina))
PRODUCT_PACKAGES += tc956x_pcie_eth.ko
endif
