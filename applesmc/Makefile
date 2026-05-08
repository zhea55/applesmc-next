# SPDX-License-Identifier: GPL-2.0-only

obj-m       := applesmc.o
applesmc-objs := applesmc-core.o applesmc-io.o applesmc-sysfs.o \
                 applesmc-battery.o applesmc-led.o applesmc-accel.o

KVER       ?= $(shell uname -r)
KBASE      ?= /lib/modules/$(KVER)
KBUILD_DIR ?= $(KBASE)/build

# Kernel is built with clang/LLVM on this system; use LLVM=1 for consistent
# toolchain (silences gcc-vs-clang flag errors)
LLVM      ?= 1

all:
	make -C $(KBUILD_DIR) M=$(CURDIR) LLVM=$(LLVM)

modules_install:
	make -C $(KBUILD_DIR) M=$(CURDIR) LLVM=$(LLVM) modules_install

clean:
	make -C $(KBUILD_DIR) M=$(CURDIR) LLVM=$(LLVM) clean

srcinstall:
	install -Dm644 Makefile $(TARGET)/Makefile
	install -Dm644 *.c $(TARGET)/
	install -Dm644 *.h $(TARGET)/
