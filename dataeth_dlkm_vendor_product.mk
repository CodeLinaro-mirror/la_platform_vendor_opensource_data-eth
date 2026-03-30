# Build DATA-ETH kernel drivers
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/stmmac_thin_core.ko\
	$(KERNEL_MODULES_OUT)/emac_thin.ko\
	$(KERNEL_MODULES_OUT)/emac_thin_gy.ko\
	$(KERNEL_MODULES_OUT)/emac_ctrl_fe_virtio.ko
