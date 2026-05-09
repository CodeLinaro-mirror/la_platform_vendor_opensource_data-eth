ifeq ($(call is-board-platform-in-list,gen4),true)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_ctrl_fe_virtio.ko
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/stmmac_thin_core.ko
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_thin.ko
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_thin_gy.ko
endif

ifeq (true,$(call is-board-platform-in-list,lahaina))
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/tc956x_pcie_eth.ko
endif
