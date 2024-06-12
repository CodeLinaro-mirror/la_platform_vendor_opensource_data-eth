ccflags-y := -I$(srctree)/include/ -I$(srctree)/include/linux/
ccflags-y += -I$(DATAETH_ROOT)/drivers/include/linux

ifeq ($(CONFIG_QTI_QUIN_GVM),y)
obj-m  += emac_ctrl_fe_virtio.o
else 
obj-n += emac_ctrl_fe_virtio.o
endif