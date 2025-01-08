AM_CFLAGS = -Wall \
	   -Wundef \
	   -Wstrict-prototypes \
	   -Wno-trigraphs \
	   -Werror \
	   -g -O0 \
	   -fno-inline \
	   -fno-short-enums \
	   -fpic \
	   -Wextra \
	   -Wno-strict-aliasing \
	   -Wno-unused-function \
	   -Wno-unused-variable \
	   -Wno-stringop-truncation\
	   -Wno-unused-parameter \
	   -Wno-format-truncation \
	   -Wno-missing-field-initializers\
	   -Wno-switch \
	   $(LIBNL_CFLAGS)

AM_CPPFLAGS = -D_GNU_SOURCE \
	      -DENABLE_THERMAL_NETLINK \
	      -DCONFIG_ARCH_SA525 \
	      -I../inc

requiredlibs = -lc \
	       -lrt \
	       -lpthread \
	       -ldl

bin_PROGRAMS = thermal-eth-netlink
thermal_eth_netlink_SOURCES = ./thermal.c ./thermal_util_netlink.c
thermal_eth_netlink_LDADD = $(requiredlibs) $(LIBNL_LIBS)
thermal_eth_netlink_CC = @CC@
