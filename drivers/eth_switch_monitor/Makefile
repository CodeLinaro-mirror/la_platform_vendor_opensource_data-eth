# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for eth_switch_monitor: monitors Ethernet switch reset GPIO on PVM
# and relays the interrupt to GVM via a Gunyah doorbell.

obj-$(CONFIG_ETH_SWITCH_MONITOR) += eth_switch_monitor.o
# For a multi-file module: eth_switch_monitor-objs := eth_switch_monitor.o <other_file>.o

ccflags-y += -DCONFIG_GH_DBL=1 \
	     -DMODULE_NAME='"eth_switch_monitor"' \
	     -I$(GUNYAH_HEADERS)
