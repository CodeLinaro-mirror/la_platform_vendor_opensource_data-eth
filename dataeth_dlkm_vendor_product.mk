# Build DATA-ETH kernel drivers
ifeq ($(call is-board-platform-in-list, kalama), true)
PRODUCT_PACKAGES += $(KERNEL_MODULES_OUT)/tc956x_pcie_eth.ko
endif
