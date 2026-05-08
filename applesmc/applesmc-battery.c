// SPDX-License-Identifier: GPL-2.0-only
/*
 * applesmc-battery.c - Battery charge threshold control via ACPI battery hook
 *
 * Copyright (C) 2023 Chris Osgood <chris_github@functionalfuture.com>
 *
 * Adds charge_control_end_threshold / charge_control_full_threshold /
 * charge_control_start_threshold sysfs files to each ACPI battery device.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <acpi/battery.h>
#include "applesmc.h"

#define CHARGE_MIN_PCT		10
#define CHARGE_MAX_PCT		100

/* ---------------------------------------------------------------------------
 * SMC percent helpers (bypass cache for direct read/write)
 * ------------------------------------------------------------------------- */

static int smc_read_pct(const char *key, u8 *val)
{
	return read_smc(APPLESMC_READ_CMD, key, val, sizeof(*val));
}

static int smc_write_pct(const char *key, u8 val)
{
	return write_smc(APPLESMC_WRITE_CMD, key, &val, sizeof(val));
}

/* ---------------------------------------------------------------------------
 * Sysfs helpers
 * ------------------------------------------------------------------------- */

static int charge_read(const char *smc_key, u8 *val)
{
	int ret;

	mutex_lock(&smcreg.mutex);
	ret = smc_read_pct(smc_key, val);
	mutex_unlock(&smcreg.mutex);
	return ret;
}

static int charge_write(const char *smc_key, unsigned long val)
{
	u8 buf = (u8)val;
	int ret;

	mutex_lock(&smcreg.mutex);
	ret = smc_write_pct(smc_key, buf);
	mutex_unlock(&smcreg.mutex);
	return ret;
}

/* ---------------------------------------------------------------------------
 * Raw percent show/store (reusable)
 * ------------------------------------------------------------------------- */

static int percent_show(const char *smc_key, char *buf)
{
	u8 val;
	int ret;

	ret = charge_read(smc_key, &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", val);
}

static int percent_store(const char *smc_key, const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val) < 0 || val < CHARGE_MIN_PCT ||
	    val > CHARGE_MAX_PCT)
		return -EINVAL;

	ret = charge_write(smc_key, val);
	if (ret)
		return ret;

	return count;
}

/* ---------------------------------------------------------------------------
 * Charge threshold attributes
 * ------------------------------------------------------------------------- */

static ssize_t charge_control_start_threshold_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	/* Not implemented — SMC key for lower bound is unavailable */
	return sysfs_emit(buf, "0\n");
}

static ssize_t charge_control_end_threshold_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return percent_show(CHARGE_END_KEY, buf);
}

static ssize_t charge_control_end_threshold_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return percent_store(CHARGE_END_KEY, buf, count);
}

static ssize_t charge_control_full_threshold_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return percent_show(CHARGE_FULL_KEY, buf);
}

static ssize_t charge_control_full_threshold_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return percent_store(CHARGE_FULL_KEY, buf, count);
}

static DEVICE_ATTR_RO(charge_control_start_threshold);
static DEVICE_ATTR_RW(charge_control_end_threshold);
static DEVICE_ATTR_RW(charge_control_full_threshold);

/* ---------------------------------------------------------------------------
 * ACPI battery hook callbacks
 * ------------------------------------------------------------------------- */

static int applesmc_battery_add(struct power_supply *battery,
				struct acpi_battery_hook *hook)
{
	int ret;

	pr_debug("Battery added: %s\n", battery->desc->name);

	ret = device_create_file(&battery->dev,
				 &dev_attr_charge_control_start_threshold);
	if (ret)
		return ret;

	ret = device_create_file(&battery->dev,
				 &dev_attr_charge_control_end_threshold);
	if (ret)
		goto remove_start;

	ret = device_create_file(&battery->dev,
				 &dev_attr_charge_control_full_threshold);
	if (ret)
		goto remove_end;

	return 0;

remove_end:
	device_remove_file(&battery->dev,
			   &dev_attr_charge_control_end_threshold);
remove_start:
	device_remove_file(&battery->dev,
			   &dev_attr_charge_control_start_threshold);
	return ret;
}

static int applesmc_battery_remove(struct power_supply *battery,
				   struct acpi_battery_hook *hook)
{
	pr_debug("Battery removed: %s\n", battery->desc->name);

	device_remove_file(&battery->dev,
			   &dev_attr_charge_control_full_threshold);
	device_remove_file(&battery->dev,
			   &dev_attr_charge_control_end_threshold);
	device_remove_file(&battery->dev,
			   &dev_attr_charge_control_start_threshold);
	return 0;
}

static struct acpi_battery_hook battery_hook = {
	.add_battery	= applesmc_battery_add,
	.remove_battery	= applesmc_battery_remove,
	.name		= "AppleSMC Battery Charge Extension",
};

void applesmc_battery_init(void)
{
	battery_hook_register(&battery_hook);
}

void applesmc_battery_exit(void)
{
	battery_hook_unregister(&battery_hook);
}
