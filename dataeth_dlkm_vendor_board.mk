ifneq ($(TARGET_DISABLE_DATAETH_DLKM),true)
ifneq ($(call is-board-platform-in-list, gen5), true)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_ctrl_fe_virtio.ko
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_thin.ko
endif
endif # TARGET_DISABLE_DATAETH_DLKM
