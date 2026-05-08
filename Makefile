# SPDX-License-Identifier: GPL-2.0-or-later

obj-y      := applesmc/
KVER       ?= `uname -r`
KBASE      ?= /lib/modules/$(KVER)
KBUILD_DIR ?= $(KBASE)/build

all:
	make -C $(KBUILD_DIR) M=`pwd`

modules_install:
	make -C $(KBUILD_DIR) M=`pwd` modules_install

clean:
	cd applesmc && make clean
	rm -f modules.order Module.symvers .modules.order.cmd .Module.symvers.cmd
