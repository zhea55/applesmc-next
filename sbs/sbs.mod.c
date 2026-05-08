#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

KSYMTAB_FUNC(sbs_hook_unregister, "_gpl", "");
KSYMTAB_FUNC(sbs_hook_register, "_gpl", "");

MODULE_INFO(depends, "sbshc");

MODULE_ALIAS("acpi*:ACPI0002:*");

MODULE_INFO(srcversion, "8A3B454B3979873AF609A7E");
