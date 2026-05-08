// SPDX-License-Identifier: GPL-2.0-only
/*
 * applesmc-accel.c - Accelerometer input device
 *
 * Copyright (C) 2007 Nicolas Boichat <nicolas@boichat.ch>
 * Copyright (C) 2010 Henrik Rydberg <rydberg@euromail.se>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/device.h>
#include "applesmc.h"

/* Resting position */
static s16 rest_x;
static s16 rest_y;

/* ---------------------------------------------------------------------------
 * Calibration
 * ------------------------------------------------------------------------- */

static void applesmc_calibrate(void)
{
	applesmc_read_s16(MOTION_SENSOR_X_KEY, &rest_x);
	applesmc_read_s16(MOTION_SENSOR_Y_KEY, &rest_y);
	rest_x = -rest_x;
}

/* ---------------------------------------------------------------------------
 * Polled input device
 * ------------------------------------------------------------------------- */

static void applesmc_idev_poll(struct input_dev *idev)
{
	s16 x, y;

	if (applesmc_read_s16(MOTION_SENSOR_X_KEY, &x))
		return;
	if (applesmc_read_s16(MOTION_SENSOR_Y_KEY, &y))
		return;

	x = -x;
	input_report_abs(idev, ABS_X, x - rest_x);
	input_report_abs(idev, ABS_Y, y - rest_y);
	input_sync(idev);
}

/* ---------------------------------------------------------------------------
 * Sysfs: position & calibrate
 * ------------------------------------------------------------------------- */

ssize_t applesmc_position_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	s16 x, y, z;
	int ret;

	ret = applesmc_read_s16(MOTION_SENSOR_X_KEY, &x);
	if (ret)
		return ret;
	ret = applesmc_read_s16(MOTION_SENSOR_Y_KEY, &y);
	if (ret)
		return ret;
	ret = applesmc_read_s16(MOTION_SENSOR_Z_KEY, &z);
	if (ret)
		return ret;

	return sysfs_emit(buf, "(%d,%d,%d)\n", x, y, z);
}

ssize_t applesmc_calibrate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "(%d,%d)\n", rest_x, rest_y);
}

ssize_t applesmc_calibrate_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	applesmc_calibrate();
	return count;
}

/* ---------------------------------------------------------------------------
 * Create / release
 * ------------------------------------------------------------------------- */

int applesmc_create_accelerometer(void)
{
	int ret;

	if (!smcreg.has_accelerometer)
		return 0;

	/* Create position + calibrate sysfs nodes */
	ret = applesmc_create_nodes(accelerometer_group, 1);
	if (ret)
		goto out;

	applesmc_idev = input_allocate_device();
	if (!applesmc_idev) {
		ret = -ENOMEM;
		goto remove_sysfs;
	}

	applesmc_calibrate();

	applesmc_idev->name = "applesmc";
	applesmc_idev->id.bustype = BUS_HOST;
	applesmc_idev->dev.parent = &pdev->dev;

	input_set_abs_params(applesmc_idev, ABS_X,
			     -256, 256, APPLESMC_INPUT_FUZZ,
			     APPLESMC_INPUT_FLAT);
	input_set_abs_params(applesmc_idev, ABS_Y,
			     -256, 256, APPLESMC_INPUT_FUZZ,
			     APPLESMC_INPUT_FLAT);

	ret = input_setup_polling(applesmc_idev, applesmc_idev_poll);
	if (ret)
		goto free_idev;

	input_set_poll_interval(applesmc_idev, APPLESMC_POLL_INTERVAL);

	ret = input_register_device(applesmc_idev);
	if (ret)
		goto free_idev;

	return 0;

free_idev:
	input_free_device(applesmc_idev);
	applesmc_idev = NULL;
remove_sysfs:
	applesmc_destroy_nodes(accelerometer_group);
out:
	pr_warn("accelerometer init failed (ret=%d)!\n", ret);
	return ret;
}

void applesmc_release_accelerometer(void)
{
	if (!smcreg.has_accelerometer)
		return;

	input_unregister_device(applesmc_idev);
	applesmc_idev = NULL;
	applesmc_destroy_nodes(accelerometer_group);
}
