# SPDX-License-Identifier: GPL-2.0
ifneq ($(KERNELRELEASE),)
obj-m += runtime/
obj-m += vdec/
obj-m += venc/
else
KDIR ?= /lib/modules/$(shell uname -r)/build
ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-
CC := $(CROSS_COMPILE)gcc-11

.PHONY: modules
modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) CC=$(CC) LOCALVERSION=-meson modules
endif

# Volatile overlay for the live device tree of a specific target.
# Regenerate per-machine: scripts/make-overlay.py reads phandles from the
# running tree, so the output is NOT portable between boards.
DTC ?= dtc
.PHONY: dtbo
dtbo: meson8b-runtime.dts
	$(DTC) -@ -I dts -O dtb -o firmware/meson/vdec/meson8b-runtime.dtbo $<
