ccflags-y := -I$(srctree)/include/ -I$(srctree)/include/linux/
ccflags-y += -I$(srctree)/include/net/
ccflags-y += -I$(DATAETH_ROOT)/drivers/include/linux/
ccflags-y += -I$(src)

# Build common stmmac_thin code as a separate module
obj-$(CONFIG_EMAC_SHIM) += stmmac_thin_core.o
stmmac_thin_core-objs := stmmac_thin.o stmmac_ethtool_thin.o \
	      dwmac4_descs.o dwmac4_thin.o hwif_thin.o \
	      stmmac_platform_thin.o

obj-$(CONFIG_EMAC_SHIM) += emac_thin.o
emac_thin-objs := dwmac-qcom-ethqos-thin.o

obj-$(CONFIG_EMAC_SHIM_GY) += emac_thin_gy.o
emac_thin_gy-objs := dwmac-qcom-ethqos-thin-gy.o
