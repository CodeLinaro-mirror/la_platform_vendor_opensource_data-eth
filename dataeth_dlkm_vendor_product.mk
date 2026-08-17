PRODUCT_PACKAGES += r8125.ko

ifneq ($(filter sun,$(TARGET_BOARD_PLATFORM)),)
  PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/tc956x_pcie_eth.ko
endif
