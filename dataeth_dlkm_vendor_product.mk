# Build DATA-ETH kernel drivers
ifeq ($(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX)$(TARGET_BOARD_DERIVATIVE_SUFFIX), gen4_gvm)
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/emac_thin.ko\
	$(KERNEL_MODULES_OUT)/emac_ctrl_fe_virtio.ko
endif

ifneq (,$(call is-board-platform-in-list2,pineapple))
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/tc956x_pcie_eth.ko
endif
