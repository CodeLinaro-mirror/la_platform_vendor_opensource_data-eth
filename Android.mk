# Android makefile for Data ETH modules

LOCAL_PATH := $(call my-dir)

ifneq ($(call is-board-platform-in-list, gen5), true)

# Build/Package only in case of supported target
ifeq ($(call is-board-platform-in-list, gen4), true)

LOCAL_PATH := $(call my-dir)
LOCAL_MODULE_DDK_BUILD := true
DATAETH_SELECT := CONFIG_DATAETH=m
DATAETH_SELECT += CONFIG_EMAC_SHIM=m
DATAETH_SELECT += CONFIG_EMAC_CTRL_FE=m

LOCAL_MODULE_DDK_BUILD := true
LOCAL_MODULE_DDK_ALLOW_UNSAFE_HEADERS := true
LOCAL_MODULE_KO_DIRS := emac_thin.ko
LOCAL_MODULE_KO_DIRS += emac_ctrl_fe_virtio.ko

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

# Below are for Android build system to recognize each module name, so
# they can be installed properly. Since Kbuild is used to compile these
# modules, invoking any of them will cause other modules to be compiled
# as well if corresponding flags are added in KBUILD_OPTIONS from upper
# level Makefiles.

########################## emac_ctrl_fe_virtio ############################
include $(CLEAR_VARS)
LOCAL_SRC_FILES           := $(DATAETH_SRC_FILES)
LOCAL_MODULE              := emac_ctrl_fe_virtio.ko
LOCAL_MODULE_KBUILD_NAME  := emac_ctrl_fe_virtio.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
TARGET_KERNEL_DLKM_OVERRIDE += $(LOCAL_MODULE)
KBUILD_OPTIONS += DATAETH_ROOT=$(DATAETH_BLD_DIR)
KBUILD_OPTIONS += $(DATAETH_SELECT)
KBUILD_OPTIONS += ENABLE_DDK_BUILD=true
include $(DLKM_DIR)/Build_external_kernelmodule.mk

################################ emac_shim ################################
include $(CLEAR_VARS)
LOCAL_SRC_FILES           := $(DATAETH_SRC_FILES)
LOCAL_MODULE              := emac_thin.ko
LOCAL_MODULE_KBUILD_NAME  := emac_thin.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
TARGET_KERNEL_DLKM_OVERRIDE += $(LOCAL_MODULE)
KBUILD_OPTIONS += DATAETH_ROOT=$(DATAETH_BLD_DIR)
KBUILD_OPTIONS += $(DATAETH_SELECT)
KBUILD_OPTIONS += ENABLE_DDK_BUILD=true
include $(DLKM_DIR)/Build_external_kernelmodule.mk

endif # DLKM check
endif # supported target check
endif # Nord check
