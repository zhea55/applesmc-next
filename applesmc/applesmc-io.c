// SPDX-License-Identifier: GPL-2.0-only
/*
 * applesmc-io.c - Apple SMC low-level I/O operations
 *
 * Copyright (C) 2007 Nicolas Boichat <nicolas@boichat.ch>
 * Copyright (C) 2010 Henrik Rydberg <rydberg@euromail.se>
 *
 * All I/O functions in this file assume the caller does NOT hold smcreg.mutex,
 * except for the raw read_smc / write_smc / send_* / wait_status family.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include "applesmc.h"

/* ---------------------------------------------------------------------------
 * Low-level SMC protocol
 * ------------------------------------------------------------------------- */

/*
 * Wait for specific status bits with a mask.
 *
 * Strategy:
 *   1. Tight busy-wait for ~8 µs (SMC often ready immediately)
 *   2. Short spinning with udelay(1) for ~32 µs
 *   3. Exponential backoff with usleep_range (up to ~256 ms total)
 */
int wait_status(u8 val, u8 mask)
{
	u8 status;
	int i;

	/* Phase 1: tight poll — no yielding */
	for (i = 0; i < 200; i++) {
		status = inb(APPLESMC_CMD_PORT);
		if ((status & mask) == val)
			return 0;
		cpu_relax();
	}

	/* Phase 2: short spinning with 1 µs steps */
	for (i = 0; i < 32; i++) {
		status = inb(APPLESMC_CMD_PORT);
		if ((status & mask) == val)
			return 0;
		udelay(1);
	}

	/* Phase 3: exponential backoff sleep (max ~256 ms) */
	for (i = 0; i < 16; i++) {
		status = inb(APPLESMC_CMD_PORT);
		if ((status & mask) == val)
			return 0;
		usleep_range(APPLESMC_MIN_WAIT << i,
			     (APPLESMC_MIN_WAIT << i) * 2);
	}
	return -EIO;
}

int send_byte(u8 cmd, u16 port)
{
	int status;

	status = wait_status(0, SMC_STATUS_IB_CLOSED);
	if (status)
		return status;
	/* Extra read for bit 0x04 after bit 0x02 falls — required on some SMC */
	status = wait_status(SMC_STATUS_BUSY, SMC_STATUS_BUSY);
	if (status)
		return status;

	outb(cmd, port);
	return 0;
}

int send_command(u8 cmd)
{
	int ret;

	ret = wait_status(0, SMC_STATUS_IB_CLOSED);
	if (ret)
		return ret;
	outb(cmd, APPLESMC_CMD_PORT);
	return 0;
}

/*
 * Reset SMC state machine if busy is stuck high.
 */
int smc_sane(void)
{
	int ret;

	ret = wait_status(0, SMC_STATUS_BUSY);
	if (!ret)
		return ret;
	ret = send_command(APPLESMC_READ_CMD);
	if (ret)
		return ret;
	return wait_status(0, SMC_STATUS_BUSY);
}

int send_argument(const char *key)
{
	int i;

	for (i = 0; i < 4; i++)
		if (send_byte(key[i], APPLESMC_DATA_PORT))
			return -EIO;
	return 0;
}

int read_smc(u8 cmd, const char *key, u8 *buffer, u8 len)
{
	u8 status, data = 0;
	int i;
	int ret;

	ret = smc_sane();
	if (ret)
		return ret;

	if (send_command(cmd) || send_argument(key)) {
		pr_warn("%.4s: read arg fail\n", key);
		return -EIO;
	}

	/* Has no effect on newer (2012+) SMCs */
	if (send_byte(len, APPLESMC_DATA_PORT)) {
		pr_warn("%.4s: read len fail\n", key);
		return -EIO;
	}

	/* Wait for first byte (full 3-phase) */
	if (wait_status(SMC_STATUS_AWAITING_DATA | SMC_STATUS_BUSY,
			SMC_STATUS_AWAITING_DATA | SMC_STATUS_BUSY)) {
		pr_warn("%.4s: read data[0] fail\n", key);
		return -EIO;
	}
	buffer[0] = inb(APPLESMC_DATA_PORT);

	/*
	 * Subsequent bytes: SMC is already streaming — brief spin only.
	 * The SMC makes each byte available within a few microseconds.
	 */
	for (i = 1; i < len; i++) {
		u8 s;
		int spins;

		for (spins = 0; spins < 200; spins++) {
			s = inb(APPLESMC_CMD_PORT);
			if ((s & (SMC_STATUS_AWAITING_DATA | SMC_STATUS_BUSY)) ==
			    (SMC_STATUS_AWAITING_DATA | SMC_STATUS_BUSY))
				break;
			cpu_relax();
		}
		if (spins >= 200) {
			pr_warn("%.4s: read data[%d] fail\n", key, i);
			return -EIO;
		}
		buffer[i] = inb(APPLESMC_DATA_PORT);
	}

	/*
	 * Drain remaining data — tight poll, no yielding.
	 * This is an error-recovery path (SMC sent more than expected),
	 * so we want to clear it as fast as possible.
	 */
	for (i = 0; i < 64; i++) {
		status = inb(APPLESMC_CMD_PORT);
		if (!(status & SMC_STATUS_AWAITING_DATA))
			break;
		data = inb(APPLESMC_DATA_PORT);
		cpu_relax();
	}
	if (i)
		pr_debug("flushed %d bytes, last value: %d\n", i, data);

	return wait_status(0, SMC_STATUS_BUSY);
}

int write_smc(u8 cmd, const char *key, const u8 *buffer, u8 len)
{
	int i;
	int ret;

	ret = smc_sane();
	if (ret)
		return ret;

	if (send_command(cmd) || send_argument(key)) {
		pr_warn("%s: write arg fail\n", key);
		return -EIO;
	}

	if (send_byte(len, APPLESMC_DATA_PORT)) {
		pr_warn("%.4s: write len fail\n", key);
		return -EIO;
	}

	for (i = 0; i < len; i++) {
		if (send_byte(buffer[i], APPLESMC_DATA_PORT)) {
			pr_warn("%s: write data fail\n", key);
			return -EIO;
		}
	}

	return wait_status(0, SMC_STATUS_BUSY);
}

/* ---------------------------------------------------------------------------
 * Register count
 * ------------------------------------------------------------------------- */

int read_register_count(unsigned int *count)
{
	__be32 be;
	int ret;

	ret = read_smc(APPLESMC_READ_CMD, KEY_COUNT_KEY, (u8 *)&be, 4);
	if (ret)
		return ret;

	*count = be32_to_cpu(be);
	return 0;
}

/* ---------------------------------------------------------------------------
 * Serialized I/O — callers should NOT hold the mutex
 * ------------------------------------------------------------------------- */

int applesmc_read_entry(const struct applesmc_entry *entry, u8 *buf, u8 len)
{
	int ret;

	if (entry->len != len)
		return -EINVAL;
	mutex_lock(&smcreg.mutex);
	ret = read_smc(APPLESMC_READ_CMD, entry->key, buf, len);
	mutex_unlock(&smcreg.mutex);
	return ret;
}

int applesmc_write_entry(const struct applesmc_entry *entry,
			 const u8 *buf, u8 len)
{
	int ret;

	if (entry->len != len)
		return -EINVAL;
	mutex_lock(&smcreg.mutex);
	ret = write_smc(APPLESMC_WRITE_CMD, entry->key, buf, len);
	mutex_unlock(&smcreg.mutex);
	return ret;
}

/* ---------------------------------------------------------------------------
 * Entry cache
 * ------------------------------------------------------------------------- */

const struct applesmc_entry *applesmc_get_entry_by_index(int index)
{
	struct applesmc_entry *cache = &smcreg.cache[index];
	u8 key[4], info[6];
	__be32 be;
	int ret = 0;

	if (cache->valid)
		return cache;

	mutex_lock(&smcreg.mutex);

	if (cache->valid)
		goto out;
	be = cpu_to_be32(index);
	ret = read_smc(APPLESMC_GET_KEY_BY_INDEX_CMD, (u8 *)&be, key, 4);
	if (ret)
		goto out;
	ret = read_smc(APPLESMC_GET_KEY_TYPE_CMD, key, info, 6);
	if (ret)
		goto out;

	memcpy(cache->key, key, 4);
	cache->len = info[0];
	memcpy(cache->type, &info[1], 4);
	cache->flags = info[5];
	cache->valid = true;

out:
	mutex_unlock(&smcreg.mutex);
	if (ret)
		return ERR_PTR(ret);
	return cache;
}

int applesmc_get_lower_bound(unsigned int *lo, const char *key)
{
	int begin = 0, end = smcreg.key_count;
	const struct applesmc_entry *entry;

	while (begin != end) {
		int middle = begin + (end - begin) / 2;
		entry = applesmc_get_entry_by_index(middle);
		if (IS_ERR(entry)) {
			*lo = 0;
			return PTR_ERR(entry);
		}
		if (strcmp(entry->key, key) < 0)
			begin = middle + 1;
		else
			end = middle;
	}

	*lo = begin;
	return 0;
}

int applesmc_get_upper_bound(unsigned int *hi, const char *key)
{
	int begin = 0, end = smcreg.key_count;
	const struct applesmc_entry *entry;

	while (begin != end) {
		int middle = begin + (end - begin) / 2;
		entry = applesmc_get_entry_by_index(middle);
		if (IS_ERR(entry)) {
			*hi = smcreg.key_count;
			return PTR_ERR(entry);
		}
		if (strcmp(key, entry->key) < 0)
			end = middle;
		else
			begin = middle + 1;
	}

	*hi = begin;
	return 0;
}

const struct applesmc_entry *applesmc_get_entry_by_key(const char *key)
{
	int begin, end;
	int ret;

	ret = applesmc_get_lower_bound(&begin, key);
	if (ret)
		return ERR_PTR(ret);
	ret = applesmc_get_upper_bound(&end, key);
	if (ret)
		return ERR_PTR(ret);
	if (end - begin != 1)
		return ERR_PTR(-EINVAL);

	return applesmc_get_entry_by_index(begin);
}

int applesmc_read_key(const char *key, u8 *buffer, u8 len)
{
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_key(key);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	return applesmc_read_entry(entry, buffer, len);
}

int applesmc_write_key(const char *key, const u8 *buffer, u8 len)
{
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_key(key);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	return applesmc_write_entry(entry, buffer, len);
}

int applesmc_has_key(const char *key, bool *value)
{
	const struct applesmc_entry *entry;

	entry = applesmc_get_entry_by_key(key);
	if (IS_ERR(entry) && PTR_ERR(entry) != -EINVAL)
		return PTR_ERR(entry);

	*value = !IS_ERR(entry);
	return 0;
}

int applesmc_read_s16(const char *key, s16 *value)
{
	u8 buffer[2];
	int ret;

	ret = applesmc_read_key(key, buffer, 2);
	if (ret)
		return ret;

	*value = ((s16)buffer[0] << 8) | buffer[1];
	return 0;
}
