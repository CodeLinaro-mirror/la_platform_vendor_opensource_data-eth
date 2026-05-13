ifneq ($(call is-board-platform-in-list, gen5 auto_gen), true)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_ctrl_fe_virtio.ko
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/stmmac_thin_core.ko
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_thin.ko
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_thin_gy.ko
endif
