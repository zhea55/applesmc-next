# SPDX-License-Identifier: GPL-2.0-only

obj-y      := applesmc/
KVER       ?= $(shell uname -r)
KBASE      ?= /lib/modules/$(KVER)
KBUILD_DIR ?= $(KBASE)/build

# Kernel is built with clang/LLVM on this system; use LLVM=1 for consistent
# toolchain (silences gcc-vs-clang flag errors when !LLVM=1)
LLVM      ?= 1

all:
	make -C $(KBUILD_DIR) M=$(CURDIR) LLVM=$(LLVM)

modules_install:
	make -C $(KBUILD_DIR) M=$(CURDIR) LLVM=$(LLVM) modules_install

clean:
	cd applesmc && $(MAKE) clean
	rm -f modules.order Module.symvers .modules.order.cmd .Module.symvers.cmd

test:
	bash tests/smoke-test.sh

.PHONY: all modules_install clean test
