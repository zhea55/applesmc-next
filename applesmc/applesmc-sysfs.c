// SPDX-License-Identifier: GPL-2.0-only
/*
 * applesmc-sysfs.c - Sysfs interface for Apple SMC sensors
 *
 * Copyright (C) 2007 Nicolas Boichat <nicolas@boichat.ch>
 * Copyright (C) 2010 Henrik Rydberg <rydberg@euromail.se>
 *
 * Handles temperature sensors, fan control, light sensor, and
 * informational SMC registers via /sys/devices/platform/applesmc.768/.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include "applesmc.h"

/* Current key_at_index for raw SMC inspection */
unsigned int key_at_index;

/* ---------------------------------------------------------------------------
 * Names
 * ------------------------------------------------------------------------- */

static ssize_t applesmc_name_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "applesmc\n");
}

/* ---------------------------------------------------------------------------
 * Key count
 * ------------------------------------------------------------------------- */

static ssize_t applesmc_key_count_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	u8 buffer[4];
	u32 count;
	int ret;

	ret = applesmc_read_key(KEY_COUNT_KEY, buffer, 4);
	if (ret)
		return ret;

	count = ((u32)buffer[0] << 24) | ((u32)buffer[1] << 16) |
		((u32)buffer[2] << 8) | buffer[3];
	return sysfs_emit(buf, "%d\n", count);
}

/* ---------------------------------------------------------------------------
 * key_at_index interface
 * ------------------------------------------------------------------------- */

static ssize_t applesmc_key_at_index_read_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	const struct applesmc_entry *entry;
	int ret;

	entry = applesmc_get_entry_by_index(key_at_index);
	if (IS_ERR(entry))
		return PTR_ERR(entry);
	ret = applesmc_read_entry(entry, buf, entry->len);
	if (ret)
		return ret;

	return entry->len;
}

static ssize_t applesmc_key_at_index_data_length_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_index(key_at_index);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	return sysfs_emit(buf, "%d\n", entry->len);
}

static ssize_t applesmc_key_at_index_type_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_index(key_at_index);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	return sysfs_emit(buf, "%s\n", entry->type);
}

static ssize_t applesmc_key_at_index_name_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_index(key_at_index);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	return sysfs_emit(buf, "%s\n", entry->key);
}

static ssize_t applesmc_key_at_index_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sysfs_emit(buf, "%d\n", key_at_index);
}

static ssize_t applesmc_key_at_index_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned long newkey;

	if (kstrtoul(buf, 10, &newkey) < 0 || newkey >= smcreg.key_count)
		return -EINVAL;

	key_at_index = newkey;
	return count;
}

static ssize_t applesmc_key_at_index_write_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
#ifndef APPLESMC_ALLOW_STORE_ANY
	return -EPERM;
#else
	const struct applesmc_entry *entry;
	int ret;

	entry = applesmc_get_entry_by_index(key_at_index);
	if (IS_ERR(entry))
		return PTR_ERR(entry);
	if (entry->len != count)
		return -EINVAL;
	ret = applesmc_write_entry(entry, buf, count);
	if (ret)
		return ret;

	return count;
#endif
}

/* ---------------------------------------------------------------------------
 * Temperature sensors
 * ------------------------------------------------------------------------- */

static ssize_t applesmc_show_sensor_label(struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	const char *key = smcreg.index[to_index(devattr)];

	return sysfs_emit(buf, "%s\n", key);
}

static ssize_t applesmc_show_temperature(struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	const char *key = smcreg.index[to_index(devattr)];
	int ret;
	s16 value;
	int temp;

	ret = applesmc_read_s16(key, &value);
	if (ret)
		return ret;

	temp = 250 * (value >> 6);

	return sysfs_emit(buf, "%d\n", temp);
}

/* ---------------------------------------------------------------------------
 * Fan speed
 * ------------------------------------------------------------------------- */

static const char *const fan_speed_fmt[] = {
	"F%dAc",	/* actual speed */
	"F%dMn",	/* minimum speed (rw) */
	"F%dMx",	/* maximum speed */
	"F%dSf",	/* safe speed */
	"F%dTg",	/* target speed (manual: rw) */
};

static ssize_t applesmc_show_fan_speed(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	unsigned int speed = 0;
	char newkey[5];
	u8 buffer[2];
	int ret;

	scnprintf(newkey, sizeof(newkey), fan_speed_fmt[to_option(attr)],
		  to_index(attr));

	ret = applesmc_read_key(newkey, buffer, 2);
	if (ret)
		return ret;

	speed = ((buffer[0] << 8 | buffer[1]) >> 2);
	return sysfs_emit(buf, "%u\n", speed);
}

static ssize_t applesmc_store_fan_speed(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned long speed;
	char newkey[5];
	u8 buffer[2];
	int ret;

	if (kstrtoul(buf, 10, &speed) < 0 || speed >= 0x4000)
		return -EINVAL;

	scnprintf(newkey, sizeof(newkey), fan_speed_fmt[to_option(attr)],
		  to_index(attr));

	buffer[0] = (speed >> 6) & 0xff;
	buffer[1] = (speed << 2) & 0xff;
	ret = applesmc_write_key(newkey, buffer, 2);

	return ret ?: count;
}

/* ---------------------------------------------------------------------------
 * Fan manual
 * ------------------------------------------------------------------------- */

static ssize_t applesmc_show_fan_manual(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 manual = 0;
	u8 buffer[2];
	int ret;

	ret = applesmc_read_key(FANS_MANUAL, buffer, 2);
	if (ret)
		return ret;

	manual = ((buffer[0] << 8 | buffer[1]) >> to_index(attr)) & 0x01;
	return sysfs_emit(buf, "%d\n", manual);
}

static ssize_t applesmc_store_fan_manual(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	u8 buffer[2];
	unsigned long input;
	u16 val;
	int ret;

	if (kstrtoul(buf, 10, &input) < 0)
		return -EINVAL;

	ret = applesmc_read_key(FANS_MANUAL, buffer, 2);
	if (ret)
		return ret;

	val = (buffer[0] << 8 | buffer[1]);

	if (input)
		val |= (0x01 << to_index(attr));
	else
		val &= ~(0x01 << to_index(attr));

	buffer[0] = (val >> 8) & 0xFF;
	buffer[1] = val & 0xFF;

	ret = applesmc_write_key(FANS_MANUAL, buffer, 2);

	return ret ?: count;
}

/* ---------------------------------------------------------------------------
 * Fan position (label)
 * ------------------------------------------------------------------------- */

static ssize_t applesmc_show_fan_position(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	char newkey[5];
	u8 buffer[17];
	int ret;

	scnprintf(newkey, sizeof(newkey), FAN_ID_FMT, to_index(attr));

	ret = applesmc_read_key(newkey, buffer, 16);
	buffer[16] = 0;

	if (ret)
		return ret;

	return sysfs_emit(buf, "%s\n", buffer + 4);
}

/* ---------------------------------------------------------------------------
 * Light sensor
 * ------------------------------------------------------------------------- */

static ssize_t applesmc_light_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	const struct applesmc_entry *entry;
	static int data_length;
	int ret;
	u8 left = 0, right = 0;
	u8 buffer[10];

	if (!data_length) {
		entry = applesmc_get_entry_by_key(LIGHT_SENSOR_LEFT_KEY);
		if (IS_ERR(entry))
			return PTR_ERR(entry);
		if (entry->len > 10)
			return -ENXIO;
		data_length = entry->len;
		pr_info("light sensor data length set to %d\n", data_length);
	}

	ret = applesmc_read_key(LIGHT_SENSOR_LEFT_KEY, buffer, data_length);
	if (ret)
		return ret;
	/* Newer MacBooks report a single 10-bit big-endian value */
	if (data_length == 10) {
		left = be16_to_cpu(*(__be16 *)(buffer + 6)) >> 2;
		goto out;
	}
	left = buffer[2];

	ret = applesmc_read_key(LIGHT_SENSOR_RIGHT_KEY, buffer, data_length);
	if (ret)
		return ret;
	right = buffer[2];

out:
	return sysfs_emit(buf, "(%d,%d)\n", left, right);
}

/* ---------------------------------------------------------------------------
 * Node group definitions
 * ------------------------------------------------------------------------- */

struct applesmc_node_group info_group[] = {
	{ "name", applesmc_name_show },
	{ "key_count", applesmc_key_count_show },
	{ "key_at_index", applesmc_key_at_index_show,
	  applesmc_key_at_index_store },
	{ "key_at_index_name", applesmc_key_at_index_name_show },
	{ "key_at_index_type", applesmc_key_at_index_type_show },
	{ "key_at_index_data_length", applesmc_key_at_index_data_length_show },
	{ "key_at_index_data", applesmc_key_at_index_read_show,
	  applesmc_key_at_index_write_store },
	{ }
};

struct applesmc_node_group accelerometer_group[] = {
	{ "position", applesmc_position_show },
	{ "calibrate", applesmc_calibrate_show, applesmc_calibrate_store },
	{ }
};

struct applesmc_node_group light_sensor_group[] = {
	{ "light", applesmc_light_show },
	{ }
};

struct applesmc_node_group fan_group[] = {
	{ "fan%d_label", applesmc_show_fan_position },
	{ "fan%d_input", applesmc_show_fan_speed, NULL, 0 },
	{ "fan%d_min", applesmc_show_fan_speed, applesmc_store_fan_speed, 1 },
	{ "fan%d_max", applesmc_show_fan_speed, NULL, 2 },
	{ "fan%d_safe", applesmc_show_fan_speed, NULL, 3 },
	{ "fan%d_output", applesmc_show_fan_speed, applesmc_store_fan_speed, 4 },
	{ "fan%d_manual", applesmc_show_fan_manual, applesmc_store_fan_manual },
	{ }
};

struct applesmc_node_group temp_group[] = {
	{ "temp%d_label", applesmc_show_sensor_label },
	{ "temp%d_input", applesmc_show_temperature },
	{ }
};

/* ---------------------------------------------------------------------------
 * Node creation / destruction helpers
 * ------------------------------------------------------------------------- */

void applesmc_destroy_nodes(struct applesmc_node_group *groups)
{
	struct applesmc_node_group *grp;
	struct applesmc_dev_attr *node;

	for (grp = groups; grp->nodes; grp++) {
		for (node = grp->nodes;
		     node->sda.dev_attr.attr.name; node++)
			sysfs_remove_file(&pdev->dev.kobj,
					  &node->sda.dev_attr.attr);
		kfree(grp->nodes);
		grp->nodes = NULL;
	}
}

int applesmc_create_nodes(struct applesmc_node_group *groups, int num)
{
	struct applesmc_node_group *grp;
	struct applesmc_dev_attr *node;
	struct attribute *attr;
	int ret, i;

	for (grp = groups; grp->format; grp++) {
		grp->nodes = kcalloc(num + 1, sizeof(*node), GFP_KERNEL);
		if (!grp->nodes) {
			ret = -ENOMEM;
			goto out;
		}
		for (i = 0; i < num; i++) {
			node = &grp->nodes[i];
			scnprintf(node->name, sizeof(node->name), grp->format,
				  i + 1);
			node->sda.index = (grp->option << 16) | (i & 0xffff);
			node->sda.dev_attr.show = grp->show;
			node->sda.dev_attr.store = grp->store;
			attr = &node->sda.dev_attr.attr;
			sysfs_attr_init(attr);
			attr->name = node->name;
			attr->mode = 0444 | (grp->store ? 0200 : 0);
			ret = sysfs_create_file(&pdev->dev.kobj, attr);
			if (ret) {
				attr->name = NULL;
				goto out;
			}
		}
	}

	return 0;
out:
	applesmc_destroy_nodes(groups);
	return ret;
}

/* ---------------------------------------------------------------------------
 * Light sensor create / release
 * ------------------------------------------------------------------------- */

int applesmc_create_light_sensor(void)
{
	if (!smcreg.num_light_sensors)
		return 0;
	return applesmc_create_nodes(light_sensor_group, 1);
}

void applesmc_release_light_sensor(void)
{
	if (!smcreg.num_light_sensors)
		return;
	applesmc_destroy_nodes(light_sensor_group);
}
