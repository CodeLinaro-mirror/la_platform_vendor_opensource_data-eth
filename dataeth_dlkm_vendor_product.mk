# Build DATA-ETH kernel drivers
ifneq ($(call is-board-platform-in-list, gen5), true)
ifeq ($(TARGET_USES_GY), true)
ifneq ($(filter $(PLATFORM_VERSION), 16 Baklava),$(PLATFORM_VERSION))
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/emac_thin.ko\
	$(KERNEL_MODULES_OUT)/emac_ctrl_fe_virtio.ko
endif
endif
endif

ifneq (,$(call is-board-platform-in-list2,pineapple))
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/tc956x_pcie_eth.ko
endif
