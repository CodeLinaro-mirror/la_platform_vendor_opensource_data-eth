# Build DATA-ETH kernel drivers
ifneq (,$(call is-board-platform-in-list2,gen4))
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/stmmac.ko\
	$(KERNEL_MODULES_OUT)/emac_ctrl_fe_virtio.ko
endif

ifneq (,$(call is-board-platform-in-list2,pineapple))
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/tc956x_pcie_eth.ko
endif