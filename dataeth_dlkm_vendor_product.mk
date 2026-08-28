PRODUCT_PACKAGES += r8125.ko
<<<<<<< HEAD   (4f8e45 Merge f3e91c461cab2ad0c04dce61a37f8e203f6d769b on remote bra)

ifneq ($(filter sun,$(TARGET_BOARD_PLATFORM)),)
  PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/tc956x_pcie_eth.ko
endif
=======
>>>>>>> CHANGE (18b17c data-eth: disable tc956x and qca81xx ethernet for sun platfo)
