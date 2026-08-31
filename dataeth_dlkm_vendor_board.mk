TARGET_DATAETH_ENABLE := false

ifeq ($(TARGET_KERNEL_DLKM_DISABLE), true)
    TARGET_DATAETH_ENABLE := true
else
    TARGET_DATAETH_ENABLE := true
endif

ifeq ($(TARGET_DATAETH_ENABLE), true)
    DATA_DLKM_BOARD_PLATFORMS_LIST := pineapple
    DATA_DLKM_BOARD_PLATFORMS_LIST += sun
    DATA_DLKM_BOARD_PLATFORMS_LIST += parrot
    DATA_DLKM_BOARD_PLATFORMS_LIST += monaco
    DATA_DLKM_BOARD_PLATFORMS_LIST += tuna

    ifneq ($(TARGET_BOARD_AUTO), true)
        ifneq (,$(call is-board-platform-in-list2,$(DATA_DLKM_BOARD_PLATFORMS_LIST)))
            BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/r8125.ko
<<<<<<< HEAD   (4f8e45 Merge f3e91c461cab2ad0c04dce61a37f8e203f6d769b on remote bra)
            # Add only for sun target
            ifeq ($(TARGET_BOARD_PLATFORM), sun)
                BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/tc956x_pcie_eth.ko
		BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/qca81xx-phy.ko
            endif
=======
>>>>>>> CHANGE (18b17c data-eth: disable tc956x and qca81xx ethernet for sun platfo)
        endif
    endif
endif
