# Android makefile for Data ETH modules

LOCAL_PATH := $(call my-dir)

ifneq ($(call is-board-platform-in-list, gen5), true)
ifeq ($(TARGET_USES_GY), true)

# Build/Package only in case of supported target
ifeq ($(call is-board-platform-in-list, gen4 pineapple), true)

LOCAL_PATH := $(call my-dir)
DATAETH_SELECT := CONFIG_DATAETH=m
ifeq ($(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX)$(TARGET_BOARD_DERIVATIVE_SUFFIX), gen4_gvm_gy)
DATAETH_SELECT += CONFIG_EMAC_SHIM=m
DATAETH_SELECT += CONFIG_EMAC_SHIM_GY=m
endif
ifeq ($(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX)$(TARGET_BOARD_DERIVATIVE_SUFFIX), gen4_gvm)
DATAETH_SELECT += CONFIG_EMAC_SHIM=m
DATAETH_SELECT += CONFIG_EMAC_CTRL_FE=m
endif

# This makefile is only for DLKM
ifneq ($(findstring vendor,$(LOCAL_PATH)),)

ifneq ($(findstring opensource,$(LOCAL_PATH)),)
	DATAETH_BLD_DIR := $(TOP)/vendor/qcom/opensource/data-eth
endif # opensource

DLKM_DIR := $(TOP)/device/qcom/common/dlkm


###########################################################
# This is set once per LOCAL_PATH, not per (kernel) module
KBUILD_OPTIONS := DATAETH_ROOT=$(DATAETH_BLD_DIR)
KBUILD_OPTIONS += $(foreach dataeth_select, \
       $(DATAETH_SELECT), \
       $(dataeth_select))
DATAETH_SRC_FILES := \
	$(wildcard $(LOCAL_PATH)/*) \
	$(wildcard $(LOCAL_PATH)/*/*) \
	$(wildcard $(LOCAL_PATH)/*/*/*)

# Module.symvers needs to be generated as a intermediate module so that
# other modules which depend on DATA-ETH  platform modules can set local
# dependencies to it.

########################### Module.symvers ############################
include $(CLEAR_VARS)
LOCAL_SRC_FILES           := $(DATAETH_SRC_FILES)
LOCAL_MODULE              := data-eth-module-symvers
LOCAL_MODULE_STEM         := Module.symvers
LOCAL_MODULE_KBUILD_NAME  := Module.symvers
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
include $(DLKM_DIR)/Build_external_kernelmodule.mk

# Below are for Android build system to recognize each module name, so
# they can be installed properly. Since Kbuild is used to compile these
# modules, invoking any of them will cause other modules to be compiled
# as well if corresponding flags are added in KBUILD_OPTIONS from upper
# level Makefiles.

########################## emac_ctrl_fe_virtio ############################
include $(CLEAR_VARS)
LOCAL_SRC_FILES           := $(DATAETH_SRC_FILES)
LOCAL_MODULE              := emac_ctrl_fe_virtio.ko
LOCAL_MODULE_KBUILD_NAME  := drivers/emac_ctrl_fe/emac_ctrl_fe_virtio.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)


include $(DLKM_DIR)/Build_external_kernelmodule.mk
################################ emac_shim ################################
include $(CLEAR_VARS)
LOCAL_SRC_FILES           := $(DATAETH_SRC_FILES)
LOCAL_MODULE              := emac_thin.ko
LOCAL_MODULE_KBUILD_NAME  := drivers/emac_shim/emac_thin.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
include $(DLKM_DIR)/Build_external_kernelmodule.mk
###########################################################

ifeq ($(call is-board-platform-in-list, pineapple), true)
################################ emac_QPS615 ################################
include $(CLEAR_VARS)
LOCAL_SRC_FILES           := $(DATAETH_SRC_FILES)
LOCAL_MODULE              := tc956x_pcie_eth.ko
LOCAL_MODULE_KBUILD_NAME  := drivers/qps615/src/tc956x_pcie_eth.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
include $(DLKM_DIR)/Build_external_kernelmodule.mk
###########################################################
endif

endif # DLKM check
endif # supported target check
endif # Product Type Check
endif # Nord check
