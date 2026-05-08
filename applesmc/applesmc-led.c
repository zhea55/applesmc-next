// SPDX-License-Identifier: GPL-2.0-only
/*
 * applesmc-led.c - Keyboard backlight LED class device
 *
 * Copyright (C) 2007 Nicolas Boichat <nicolas@boichat.ch>
 * Copyright (C) 2010 Henrik Rydberg <rydberg@euromail.se>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include "applesmc.h"

/* Global backlight state — indexed 0 = brightness, 1 = unused */
u8 backlight_state[2];

struct workqueue_struct *applesmc_led_wq;

static void applesmc_backlight_set(struct work_struct *work)
{
	applesmc_write_key(BACKLIGHT_KEY, backlight_state, 2);
}
static DECLARE_WORK(backlight_work, applesmc_backlight_set);

static void applesmc_brightness_set(struct led_classdev *led_cdev,
				    enum led_brightness value)
{
	backlight_state[0] = value;
	queue_work(applesmc_led_wq, &backlight_work);

	if (applesmc_debug)
		dev_dbg(led_cdev->dev, "queued backlight work\n");
}

struct led_classdev applesmc_backlight = {
	.name			= "smc::kbd_backlight",
	.default_trigger	= "nand-disk",
	.brightness_set		= applesmc_brightness_set,
};

int applesmc_create_key_backlight(void)
{
	if (!smcreg.has_key_backlight)
		return 0;

	applesmc_led_wq = create_singlethread_workqueue("applesmc-led");
	if (!applesmc_led_wq)
		return -ENOMEM;

	return led_classdev_register(&pdev->dev, &applesmc_backlight);
}

void applesmc_release_key_backlight(void)
{
	if (!smcreg.has_key_backlight)
		return;

	led_classdev_unregister(&applesmc_backlight);
	destroy_workqueue(applesmc_led_wq);
	applesmc_led_wq = NULL;
}
