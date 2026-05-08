/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * applesmc.h - shared definitions for Apple SMC driver
 *
 * Copyright (C) 2007 Nicolas Boichat <nicolas@boichat.ch>
 * Copyright (C) 2010 Henrik Rydberg <rydberg@euromail.se>
 * Copyright (C) 2023 Chris Osgood <chris_github@functionalfuture.com>
 */

#ifndef _APPLESMC_H_
#define _APPLESMC_H_

#include <linux/version.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/platform_device.h>

/* ---------------------------------------------------------------------------
 * SMC hardware constants
 * ------------------------------------------------------------------------- */

#define APPLESMC_DATA_PORT		0x300
#define APPLESMC_CMD_PORT		0x304
#define APPLESMC_NR_PORTS		32 /* 0x300-0x31f */
#define APPLESMC_MAX_DATA_LENGTH	32

/* SMC status bits */
#define SMC_STATUS_AWAITING_DATA	BIT(0)
#define SMC_STATUS_IB_CLOSED		BIT(1)
#define SMC_STATUS_BUSY			BIT(2)

#define APPLESMC_MIN_WAIT		0x0008

/* SMC commands */
#define APPLESMC_READ_CMD		0x10
#define APPLESMC_WRITE_CMD		0x11
#define APPLESMC_GET_KEY_BY_INDEX_CMD	0x12
#define APPLESMC_GET_KEY_TYPE_CMD	0x13

/* Known keys */
#define KEY_COUNT_KEY			"#KEY"
#define LIGHT_SENSOR_LEFT_KEY		"ALV0"
#define LIGHT_SENSOR_RIGHT_KEY		"ALV1"
#define BACKLIGHT_KEY			"LKSB"
#define CLAMSHELL_KEY			"MSLD"
#define MOTION_SENSOR_X_KEY		"MO_X"
#define MOTION_SENSOR_Y_KEY		"MO_Y"
#define MOTION_SENSOR_Z_KEY		"MO_Z"
#define MOTION_SENSOR_KEY		"MOCN"
#define FANS_COUNT			"FNum"
#define FANS_MANUAL			"FS! "
#define FAN_ID_FMT			"F%dID"
#define TEMP_SENSOR_TYPE		"sp78"
#define CHARGE_END_KEY			"BCLM"
#define CHARGE_FULL_KEY			"BFCL"

/* Fan speed register formats */
extern const char *const fan_speed_fmt[];

/* Maximum number of fans and tight-spin iterations */
#define APPLESMC_MAX_FANS		10
#define APPLESMC_SPIN_TIGHT		200

/* Fan speed is encoded in 14 bits: max value = 0x3FFF (16383) */
#define APPLESMC_FAN_SPEED_14BIT_MAX	0x4000

/* Timeouts */
#define INIT_TIMEOUT_MSECS		5000
#define INIT_WAIT_MSECS			50
#define APPLESMC_POLL_INTERVAL		50
#define APPLESMC_INPUT_FUZZ		4
#define APPLESMC_INPUT_FLAT		4

/* ---------------------------------------------------------------------------
 * Helper macros
 * ------------------------------------------------------------------------- */

#define to_index(attr)	(to_sensor_dev_attr(attr)->index & 0xffff)
#define to_option(attr)	(to_sensor_dev_attr(attr)->index >> 16)

/* ---------------------------------------------------------------------------
 * Data structures
 * ------------------------------------------------------------------------- */

/* Cached SMC register entry */
struct applesmc_entry {
	char key[5];
	u8  valid;
	u8  len;		/* bounded by APPLESMC_MAX_DATA_LENGTH */
	char type[5];
	u8  flags;		/* 0x10: func; 0x40: write; 0x80: read */
};

/* Dynamic sysfs attribute with name storage */
struct applesmc_dev_attr {
	struct sensor_device_attribute sda;
	char name[32];
};

/* Group of related dynamic sysfs nodes (e.g. all fan speeds) */
struct applesmc_node_group {
	const char *format;		/* printf-style name format */
	void *show;			/* show function */
	void *store;			/* store function (NULL = read-only) */
	int option;			/* extra argument encoded in index */
	struct applesmc_dev_attr *nodes;
};

/* Global SMC registers and state */
struct applesmc_registers {
	struct mutex mutex;
	unsigned int key_count;
	unsigned int fan_count;
	unsigned int temp_count;
	unsigned int temp_begin;
	unsigned int temp_end;
	unsigned int index_count;
	int num_light_sensors;
	bool has_accelerometer;
	bool has_key_backlight;
	bool init_complete;
	struct applesmc_entry *cache;
	const char **index;
};

/* ---------------------------------------------------------------------------
 * Global state (shared across compilation units)
 * ------------------------------------------------------------------------- */

extern struct applesmc_registers smcreg;
extern struct platform_device *pdev;
extern struct device *hwmon_dev;
extern struct input_dev *applesmc_idev;
extern u8 backlight_state[2];
extern unsigned int key_at_index;
extern struct workqueue_struct *applesmc_led_wq;
extern struct led_classdev applesmc_backlight;
extern bool applesmc_debug;

/* ---------------------------------------------------------------------------
 * SMC I/O layer (applesmc-io.c)
 * ------------------------------------------------------------------------- */

int  wait_status(u8 val, u8 mask);
int  send_byte(u8 cmd, u16 port);
int  send_command(u8 cmd);
int  smc_sane(void);
int  send_argument(const char *key);
int  read_smc(u8 cmd, const char *key, u8 *buffer, u8 len);
int  write_smc(u8 cmd, const char *key, const u8 *buffer, u8 len);
int  read_register_count(unsigned int *count);

int  applesmc_read_entry(const struct applesmc_entry *entry, u8 *buf, u8 len);
int  applesmc_write_entry(const struct applesmc_entry *entry,
			  const u8 *buf, u8 len);
const struct applesmc_entry *applesmc_get_entry_by_index(int index);
int  applesmc_get_lower_bound(unsigned int *lo, const char *key);
int  applesmc_get_upper_bound(unsigned int *hi, const char *key);
const struct applesmc_entry *applesmc_get_entry_by_key(const char *key);
int  applesmc_read_key(const char *key, u8 *buffer, u8 len);
int  applesmc_write_key(const char *key, const u8 *buffer, u8 len);
int  applesmc_has_key(const char *key, bool *value);
int  applesmc_read_s16(const char *key, s16 *value);

/* ---------------------------------------------------------------------------
 * Device initialization (applesmc-core.c)
 * ------------------------------------------------------------------------- */

int  applesmc_init_smcreg(void);
void applesmc_destroy_smcreg(void);
void applesmc_device_init(void);

/* ---------------------------------------------------------------------------
 * Sysfs helpers (applesmc-sysfs.c)
 * ------------------------------------------------------------------------- */

int  applesmc_create_nodes(struct applesmc_node_group *groups, int num);
void applesmc_destroy_nodes(struct applesmc_node_group *groups);

/* ---------------------------------------------------------------------------
 * Accelerometer helpers (applesmc-accel.c)
 * ------------------------------------------------------------------------- */

int  applesmc_create_accelerometer(void);
void applesmc_release_accelerometer(void);
ssize_t applesmc_position_show(struct device *dev,
			       struct device_attribute *attr, char *buf);
ssize_t applesmc_calibrate_show(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t applesmc_calibrate_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count);

/* ---------------------------------------------------------------------------
 * Subsystem lifetime helpers
 * ------------------------------------------------------------------------- */

int  applesmc_create_light_sensor(void);
void applesmc_release_light_sensor(void);
int  applesmc_create_key_backlight(void);
void applesmc_release_key_backlight(void);
void applesmc_battery_init(void);
void applesmc_battery_exit(void);

/* ---------------------------------------------------------------------------
 * Sysfs node groups (defined in applesmc-sysfs.c)
 * ------------------------------------------------------------------------- */

extern struct applesmc_node_group info_group[];
extern struct applesmc_node_group accelerometer_group[];
extern struct applesmc_node_group light_sensor_group[];
extern struct applesmc_node_group fan_group[];
extern struct applesmc_node_group temp_group[];

#endif /* _APPLESMC_H_ */
