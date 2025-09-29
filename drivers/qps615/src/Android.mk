LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

BOARD_COMMON_DIR ?= device/qcom/common

#Enabling BAZEL
LOCAL_MODULE_DDK_BUILD := true

LOCAL_CFLAGS := -Wno-macro-redefined -Wno-unused-function -Wall -Werror
LOCAL_CLANG :=true
LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)
LOCAL_MODULE := tc956x_pcie_eth.ko
LOCAL_SRC_FILES   := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
DLKM_DIR := $(TOP)/$(BOARD_COMMON_DIR)/dlkm
$(warning $(DLKM_DIR))
include $(DLKM_DIR)/Build_external_kernelmodule.mk

