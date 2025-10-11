ifeq ($(RELEASE_PACKAGE),1)
EXTRA_CFLAGS+=-DRELEASE_PACKAGE
endif
LBITS := $(shell getconf LONG_BIT)
ifeq ($(LBITS),64)
CCFLAGS += -m64
EXTRA_CFLAGS+=-DSYSTEM_IS_64
else
CCFLAGS += -m32
endif

qps615_dir	        = $(prefix)/qps615
qps615_CFLAGS	= -Werror

KERNEL_FLAGS ?= ARCH=arm

module = tc956x_pcie_eth.ko
kmake  = $(MAKE) $(KERNEL_FLAGS) -C $(KERNEL_DIR) M=$(CURDIR)

$(module):
	$(kmake) modules

all-local: $(module)

install-exec-local: $(module)
	$(kmake) INSTALL_MOD_PATH=$(DESTDIR)$(prefix)/modules modules_install

# "make distclean" will always run clean-local in this directory,
# regardless of the KERNELMODULES conditional. Therefore, ensure
# KERNEL_DIR exists before running clean. Further, don't fail even
# if there is a problem.
clean-local:
	-test ! -d "$(KERNEL_DIR)" || $(kmake) clean


