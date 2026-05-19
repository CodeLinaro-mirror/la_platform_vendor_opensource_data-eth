ifneq ($(TARGET_DISABLE_DATA_ETH_DLKM),true)
ifneq ($(call is-board-platform-in-list, gen5), true)
ifeq ($(TARGET_USES_GY), true)
ifeq ($(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX)$(TARGET_BOARD_DERIVATIVE_SUFFIX), gen4_gvm)
ifneq ($(filter $(PLATFORM_VERSION), 16 Baklava),$(PLATFORM_VERSION))
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_ctrl_fe_virtio.ko
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_thin.ko
endif
endif
endif

ifeq ($(TARGET_USES_GY), true)
ifeq ($(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX)$(TARGET_BOARD_DERIVATIVE_SUFFIX), gen4_gvm_gy)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_thin.ko
endif
endif

ifeq ($(TARGET_USES_GY), true)
ifeq ($(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX)$(TARGET_BOARD_DERIVATIVE_SUFFIX), gen4_gvm_gy_qmaa)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_thin.ko
endif
endif

ifeq ($(TARGET_USES_GY), true)
ifeq ($(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX)$(TARGET_BOARD_DERIVATIVE_SUFFIX), gen4_gvm_gy_sgt)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/emac_thin.ko
endif
endif
endif

ifneq (,$(call is-board-platform-in-list2,pineapple))
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/tc956x_pcie_eth.ko
endif
endif
