// SPDX-License-Identifier: GPL-2.0-only
/*
 * applesmc-core.c - Apple SMC core module (init, exit, register cache)
 *
 * Copyright (C) 2007 Nicolas Boichat <nicolas@boichat.ch>
 * Copyright (C) 2010 Henrik Rydberg <rydberg@euromail.se>
 *
 * Add charge threshold support:
 * Copyright (C) 2023 Chris Osgood <chris_github@functionalfuture.com>
 *
 * Based on hdaps.c driver:
 * Copyright (C) 2005 Robert Love <rml@novell.com>
 * Copyright (C) 2005 Jesper Juhl <jj@chaosbits.net>
 *
 * Fan control based on smcFanControl:
 * Copyright (C) 2006 Hendrik Holtmann <holtmann@mac.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/moduleparam.h>
#include "applesmc.h"

/* ---------------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------------- */

struct applesmc_registers smcreg = {
	.mutex = __MUTEX_INITIALIZER(smcreg.mutex),
};

struct platform_device *pdev;
struct device *hwmon_dev;
struct input_dev *applesmc_idev;

bool applesmc_debug;

module_param_named(debug, applesmc_debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable debug messages");

/* ---------------------------------------------------------------------------
 * Register cache initialization
 * ------------------------------------------------------------------------- */

static int applesmc_init_index(struct applesmc_registers *s)
{
	const struct applesmc_entry *entry;
	unsigned int i;

	if (s->index)
		return 0;

	s->index = kcalloc(s->temp_count, sizeof(s->index[0]), GFP_KERNEL);
	if (!s->index)
		return -ENOMEM;

	for (i = s->temp_begin; i < s->temp_end; i++) {
		entry = applesmc_get_entry_by_index(i);
		if (IS_ERR(entry))
			continue;
		if (strcmp(entry->type, TEMP_SENSOR_TYPE))
			continue;
		s->index[s->index_count++] = entry->key;
	}

	return 0;
}

/*
 * applesmc_init_smcreg_try - Try to initialize register cache. Idempotent.
 */
static int applesmc_init_smcreg_try(void)
{
	struct applesmc_registers *s = &smcreg;
	bool left_light_sensor = false, right_light_sensor = false;
	unsigned int count;
	u8 tmp[1];
	int ret;

	if (s->init_complete)
		return 0;

	ret = read_register_count(&count);
	if (ret)
		return ret;

	if (s->cache && s->key_count != count) {
		pr_warn("key count changed from %d to %d\n",
			s->key_count, count);
		kfree(s->cache);
		s->cache = NULL;
	}
	s->key_count = count;

	if (!s->cache) {
		s->cache = kcalloc(s->key_count, sizeof(*s->cache),
				   GFP_KERNEL);
	}
	if (!s->cache)
		return -ENOMEM;

	ret = applesmc_read_key(FANS_COUNT, tmp, 1);
	if (ret)
		return ret;
	s->fan_count = tmp[0];
	if (s->fan_count > APPLESMC_MAX_FANS)
		s->fan_count = APPLESMC_MAX_FANS;

	ret = applesmc_get_lower_bound(&s->temp_begin, "T");
	if (ret)
		return ret;
	ret = applesmc_get_lower_bound(&s->temp_end, "U");
	if (ret)
		return ret;
	s->temp_count = s->temp_end - s->temp_begin;

	ret = applesmc_init_index(s);
	if (ret)
		return ret;

	ret = applesmc_has_key(LIGHT_SENSOR_LEFT_KEY, &left_light_sensor);
	if (ret)
		return ret;
	ret = applesmc_has_key(LIGHT_SENSOR_RIGHT_KEY, &right_light_sensor);
	if (ret)
		return ret;
	ret = applesmc_has_key(MOTION_SENSOR_KEY, &s->has_accelerometer);
	if (ret)
		return ret;
	ret = applesmc_has_key(BACKLIGHT_KEY, &s->has_key_backlight);
	if (ret)
		return ret;

	s->num_light_sensors = left_light_sensor + right_light_sensor;
	s->init_complete = true;

	pr_info("key=%d fan=%d temp=%d index=%d acc=%d lux=%d kbd=%d\n",
		s->key_count, s->fan_count, s->temp_count, s->index_count,
		s->has_accelerometer,
		s->num_light_sensors,
		s->has_key_backlight);

	return 0;
}

void applesmc_destroy_smcreg(void)
{
	kfree(smcreg.index);
	smcreg.index = NULL;
	kfree(smcreg.cache);
	smcreg.cache = NULL;
	smcreg.init_complete = false;
}

int applesmc_init_smcreg(void)
{
	int ms, ret;

	for (ms = 0; ms < INIT_TIMEOUT_MSECS; ms += INIT_WAIT_MSECS) {
		ret = applesmc_init_smcreg_try();
		if (!ret) {
			if (ms)
				pr_info("init_smcreg() took %d ms\n", ms);
			return 0;
		}
		msleep(INIT_WAIT_MSECS);
	}

	applesmc_destroy_smcreg();

	return ret;
}

/* ---------------------------------------------------------------------------
 * Accelerometer device init
 * ------------------------------------------------------------------------- */

static void applesmc_device_init(void)
{
	int total;
	u8 buffer[2];

	if (!smcreg.has_accelerometer)
		return;

	for (total = INIT_TIMEOUT_MSECS; total > 0; total -= INIT_WAIT_MSECS) {
		if (!applesmc_read_key(MOTION_SENSOR_KEY, buffer, 2) &&
		    (buffer[0] != 0x00 || buffer[1] != 0x00))
			return;
		buffer[0] = 0xe0;
		buffer[1] = 0x00;
		applesmc_write_key(MOTION_SENSOR_KEY, buffer, 2);
		msleep(INIT_WAIT_MSECS);
	}

	pr_warn("failed to init the device\n");
}

/* ---------------------------------------------------------------------------
 * Platform driver
 * ------------------------------------------------------------------------- */

static int applesmc_probe(struct platform_device *dev)
{
	int ret;

	ret = applesmc_init_smcreg();
	if (ret)
		return ret;

	applesmc_device_init();

	return 0;
}

/* Synchronize device with memorized backlight state */
static int applesmc_pm_resume(struct device *dev)
{
	if (smcreg.has_key_backlight)
		applesmc_write_key(BACKLIGHT_KEY, backlight_state, 2);
	return 0;
}

/* Reinitialize device on resume from hibernation */
static int applesmc_pm_restore(struct device *dev)
{
	applesmc_device_init();
	return applesmc_pm_resume(dev);
}

static const struct dev_pm_ops applesmc_pm_ops = {
	.resume	 = applesmc_pm_resume,
	.restore = applesmc_pm_restore,
};

static struct platform_driver applesmc_driver = {
	.probe	= applesmc_probe,
	.driver	= {
		.name = "applesmc",
		.pm   = &applesmc_pm_ops,
	},
};

/* ---------------------------------------------------------------------------
 * DMI whitelist
 * ------------------------------------------------------------------------- */

static int applesmc_dmi_match(const struct dmi_system_id *id)
{
	return 1;
}

static const struct dmi_system_id applesmc_whitelist[] __initconst = {
	{ applesmc_dmi_match, "Apple MacBook Air", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "MacBookAir") },
	},
	{ applesmc_dmi_match, "Apple MacBook Pro", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro") },
	},
	{ applesmc_dmi_match, "Apple MacBook", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "MacBook") },
	},
	{ applesmc_dmi_match, "Apple Macmini", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "Macmini") },
	},
	{ applesmc_dmi_match, "Apple MacPro", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "MacPro") },
	},
	{ applesmc_dmi_match, "Apple iMac", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "iMac") },
	},
	{ applesmc_dmi_match, "Apple Xserve", {
	  DMI_MATCH(DMI_BOARD_VENDOR, "Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME, "Xserve") },
	},
	{ .ident = NULL }
};

/* ---------------------------------------------------------------------------
 * Module init / exit
 * ------------------------------------------------------------------------- */

static int __init applesmc_init(void)
{
	int ret;

	if (!dmi_check_system(applesmc_whitelist)) {
		pr_warn("supported laptop not found!\n");
		return -ENODEV;
	}

	if (!request_region(APPLESMC_DATA_PORT, APPLESMC_NR_PORTS,
			    "applesmc")) {
		return -ENXIO;
	}

	ret = platform_driver_register(&applesmc_driver);
	if (unlikely(ret))
		goto out_region;

	pdev = platform_device_register_simple("applesmc",
					       APPLESMC_DATA_PORT, NULL, 0);
	if (unlikely(IS_ERR(pdev))) {
		ret = PTR_ERR(pdev);
		goto out_driver;
	}

	/*
	 * Register cache is already populated by applesmc_probe() which was
	 * triggered by platform_device_register_simple() above.
	 * No need to call applesmc_init_smcreg() again.
	 */

	ret = applesmc_create_nodes(info_group, 1);
	if (unlikely(ret))
		goto out_smcreg;

	ret = applesmc_create_nodes(fan_group, smcreg.fan_count);
	if (unlikely(ret))
		goto out_info;

	ret = applesmc_create_nodes(temp_group, smcreg.index_count);
	if (unlikely(ret))
		goto out_fans;

	ret = applesmc_create_accelerometer();
	if (unlikely(ret))
		goto out_temperature;

	ret = applesmc_create_light_sensor();
	if (unlikely(ret))
		goto out_accelerometer;

	ret = applesmc_create_key_backlight();
	if (unlikely(ret))
		goto out_light_sysfs;

	/*
	 * Register a minimal hwmon device.  We use *_with_groups() with
	 * no custom groups because this driver manages its own ad-hoc sysfs
	 * files directly on the platform device.  The *with_info() variant
	 * in kernel 7.x requires at least one channel attribute.
	 */
	hwmon_dev = hwmon_device_register_with_groups(&pdev->dev,
						      "applesmc",
						      NULL, NULL);
	if (unlikely(IS_ERR(hwmon_dev))) {
		ret = PTR_ERR(hwmon_dev);
		goto out_light_ledclass;
	}

	applesmc_battery_init();

	return 0;

out_light_ledclass:
	applesmc_release_key_backlight();
out_light_sysfs:
	applesmc_release_light_sensor();
out_accelerometer:
	applesmc_release_accelerometer();
out_temperature:
	applesmc_destroy_nodes(temp_group);
out_fans:
	applesmc_destroy_nodes(fan_group);
out_info:
	applesmc_destroy_nodes(info_group);
out_smcreg:
	applesmc_destroy_smcreg();
	platform_device_unregister(pdev);
out_driver:
	platform_driver_unregister(&applesmc_driver);
out_region:
	release_region(APPLESMC_DATA_PORT, APPLESMC_NR_PORTS);
	pr_warn("driver init failed (ret=%d)!\n", ret);
	return ret;
}

static void __exit applesmc_exit(void)
{
	applesmc_battery_exit();
	hwmon_device_unregister(hwmon_dev);
	applesmc_release_key_backlight();
	applesmc_release_light_sensor();
	applesmc_release_accelerometer();
	applesmc_destroy_nodes(temp_group);
	applesmc_destroy_nodes(fan_group);
	applesmc_destroy_nodes(info_group);
	applesmc_destroy_smcreg();
	platform_device_unregister(pdev);
	platform_driver_unregister(&applesmc_driver);
	release_region(APPLESMC_DATA_PORT, APPLESMC_NR_PORTS);
}

module_init(applesmc_init);
module_exit(applesmc_exit);

MODULE_AUTHOR("Nicolas Boichat <nicolas@boichat.ch>");
MODULE_AUTHOR("Chris Osgood <chris_github@functionalfuture.com>");
MODULE_DESCRIPTION("Apple SMC");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.2.0-next");
MODULE_DEVICE_TABLE(dmi, applesmc_whitelist);
