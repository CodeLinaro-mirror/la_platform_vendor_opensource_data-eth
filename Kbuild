ifeq ($(CONFIG_EMAC_CTRL_FE),m)
KBUILD_CPPFLAGS += -DCONFIG_EMAC_CTRL_FE
endif

ifeq ($(CONFIG_EMAC_SHIM),m)
KBUILD_CPPFLAGS += -DCONFIG_EMAC_SHIM
endif

obj-$(CONFIG_EMAC_CTRL_FE) += drivers/emac_ctrl_fe/
obj-$(CONFIG_EMAC_SHIM) += drivers/emac_shim/
