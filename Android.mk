# Android makefile for Data ETH modules
# This is the FIRST LINE in the file

LOCAL_PATH := $(call my-dir)

# This makefile is only for DLKM
ifneq ($(findstring vendor,$(LOCAL_PATH)),)

ifneq ($(findstring opensource,$(LOCAL_PATH)),)
	DATAETH_BLD_DIR := $(TOP)/vendor/qcom/opensource/data-eth
endif # opensource

DLKM_DIR := $(TOP)/device/qcom/common/dlkm

###########################################################
# This is set once per LOCAL_PATH, not per (kernel) module
KBUILD_OPTIONS := DATAETH_ROOT=$(DATAETH_BLD_DIR)
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
LOCAL_MODULE_DDK_BUILD := true
$(info DEBUG: LOCAL_MODULE_DDK_BUILD set to $(LOCAL_MODULE_DDK_BUILD) for module $(LOCAL_MODULE))
include $(DLKM_DIR)/Build_external_kernelmodule.mk

# Below are for Android build system to recognize each module name, so
# they can be installed properly. Since Kbuild is used to compile these
# modules, invoking any of them will cause other modules to be compiled
# as well if corresponding flags are added in KBUILD_OPTIONS from upper
# level Makefiles.

################################ r8125 ################################
include $(CLEAR_VARS)
LOCAL_SRC_FILES           := $(DATAETH_SRC_FILES)
LOCAL_MODULE              := r8125.ko
LOCAL_MODULE_KBUILD_NAME  := r8125.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
#Enabling BAZEL
LOCAL_MODULE_DDK_BUILD := true
LOCAL_MODULE_KO_DIRS := drivers/r8125/src
include $(DLKM_DIR)/Build_external_kernelmodule.mk
#######################################################################

################################ qps615 ################################
include $(CLEAR_VARS)
LOCAL_SRC_FILES           := $(DATAETH_SRC_FILES)
LOCAL_MODULE              := tc956x_pcie_eth.ko
LOCAL_MODULE_KBUILD_NAME  := tc956x_pcie_eth.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
#Enabling BAZEL
LOCAL_MODULE_DDK_BUILD := true
$(info DEBUG: LOCAL_MODULE_DDK_BUILD set to $(LOCAL_MODULE_DDK_BUILD) for module $(LOCAL_MODULE))
LOCAL_MODULE_KO_DIRS := drivers/qps615/src
include $(DLKM_DIR)/Build_external_kernelmodule.mk
###########################################################

endif # DLKM check
