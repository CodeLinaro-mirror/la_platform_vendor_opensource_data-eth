# Build DATA-ETH kernel drivers
ifneq ($(TARGET_DISABLE_DATAETH_DLKM),true)
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/emac_thin.ko\
	$(KERNEL_MODULES_OUT)/emac_ctrl_fe_virtio.ko
endif # TARGET_DISABLE_DATAETH_DLKM
