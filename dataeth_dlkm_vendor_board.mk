ifeq ($(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX)$(TARGET_BOARD_DERIVATIVE_SUFFIX), gen4_gvm)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_ctrl_fe_virtio.ko
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/stmmac.ko
endif

ifneq (,$(call is-board-platform-in-list2,pineapple))
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/tc956x_pcie_eth.ko
endif