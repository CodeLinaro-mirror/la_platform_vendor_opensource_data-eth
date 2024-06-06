ccflags-y := -I$(srctree)/include/ -I$(srctree)/include/linux/
ccflags-y += -I$(DATAETH_ROOT)/drivers/include/linux
emac_thin-objs:= stmmac_thin.o stmmac_ethtool_thin.o \
	      dwmac4_descs.o dwmac4_thin.o hwif_thin.o \
	      stmmac_platform_thin.o dwmac-qcom-ethqos-thin.o
ifeq ($(CONFIG_QTI_QUIN_GVM),y)
obj-m  += emac_thin.o
else 
obj-n += emac_thin.o
endif
