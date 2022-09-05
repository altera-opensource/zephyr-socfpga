/*
 * Copyright (c) 2015-2022 Intel Corporation
 * Copyright (c) 2018 Nordic Semiconductor
 * Copyright (c) 2019 Centaur Analytics, Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <drivers/watchdog.h>
#include <sys/printk.h>
#include <stdbool.h>

#if DT_HAS_COMPAT_STATUS_OKAY(snps_designware_watchdog)

#define WDT_COUNT		4
#define WDT_FEED_TRIES	6
#define DELAY_MS		500

#define WDT_0_TIMEOUT_MS	1000
#define WDT_1_TIMEOUT_MS	2000
#define WDT_2_TIMEOUT_MS	5000
#define WDT_3_TIMEOUT_MS	10000

#define WDT_NODE_0 DT_INST(0, snps_designware_watchdog)
#define WDT_NODE_1 DT_INST(1, snps_designware_watchdog)
#define WDT_NODE_2 DT_INST(2, snps_designware_watchdog)
#define WDT_NODE_3 DT_INST(3, snps_designware_watchdog)

#define WDT_DEV_NAME_0 DT_LABEL(WDT_NODE_0)
#define WDT_DEV_NAME_1 DT_LABEL(WDT_NODE_1)
#define WDT_DEV_NAME_2 DT_LABEL(WDT_NODE_2)
#define WDT_DEV_NAME_3 DT_LABEL(WDT_NODE_3)

struct wdt_configuration {
	char *wdt_dev_name;
	uint32_t timeout_ms;
	bool interrupt_enabled;
};

static struct wdt_configuration wdt_config[WDT_COUNT] = {
	{
		.wdt_dev_name = WDT_DEV_NAME_0,
		.timeout_ms = WDT_0_TIMEOUT_MS,
		.interrupt_enabled = (bool)DT_IRQ_HAS_CELL(WDT_NODE_0, irq),
	},
	{
		.wdt_dev_name = WDT_DEV_NAME_1,
		.timeout_ms = WDT_1_TIMEOUT_MS,
		.interrupt_enabled = (bool)DT_IRQ_HAS_CELL(WDT_NODE_1, irq),
	},
	{
		.wdt_dev_name = WDT_DEV_NAME_2,
		.timeout_ms = WDT_2_TIMEOUT_MS,
		.interrupt_enabled = (bool)DT_IRQ_HAS_CELL(WDT_NODE_2, irq),
	},
	{
		.wdt_dev_name = WDT_DEV_NAME_3,
		.timeout_ms = WDT_3_TIMEOUT_MS,
		.interrupt_enabled = (bool)DT_IRQ_HAS_CELL(WDT_NODE_3, irq),
	},
};

static int wdt_inst_timeout(const struct device *wdt, uint32_t timeout_ms, bool interrupt_enabled);

static void wdt_callback(const struct device *wdt_dev, int channel_id)
{
	printk("Interrupt triggered for %s\n", wdt_dev->name);

	printk("Feeding watchdog %s...\n", wdt_dev->name);

	wdt_feed(wdt_dev, channel_id);
}

int main(void)
{
	int i, j;
	int ret = 0;
	const struct device *wdt[WDT_COUNT];

	printk("Watchdog test sample\n");

	/* Enable all 4 watchdogs */
	for (i = 0; i < WDT_COUNT; ++i) {
		wdt[i] = device_get_binding(wdt_config[i].wdt_dev_name);

		ret = wdt_inst_timeout(wdt[i], wdt_config[i].timeout_ms,
					wdt_config[i].interrupt_enabled);

		if (ret != 0) {
			printk("Watchdog %s install timeout error [%d]\n", wdt[i]->name, ret);
			return ret;
		}

		ret = wdt_setup(wdt[i], WDT_OPT_PAUSE_HALTED_BY_DBG);

		if (ret != 0) {
			printk("Watchdog %s setup error [%d]", wdt[i]->name, ret);
			return ret;
		}

		printk("Watchdog %s setup successful.\n", wdt[i]->name);
	}

	/* Test watchdog counter restart */
	printk("Feeding watchdogs configured for Reset Mode %d times\n", WDT_FEED_TRIES);

	for (j = 0; j < WDT_FEED_TRIES; ++j) {
		printk("Feeding watchdogs...\n");

		for (i = 0; i < WDT_COUNT; ++i) {
			if (wdt_config[i].interrupt_enabled == false) {
				wdt_feed(wdt[i], 0);
			}
		}

		k_msleep(DELAY_MS);
	}

	printk("Watchdog test sample end\n");

	/* Waiting for the SoC reset. */
	printk("Waiting for reset...\n");

	while (true) {
		k_yield();
	}

	return 0;
}

static int wdt_inst_timeout(const struct device *wdt, uint32_t timeout_ms, bool interrupt_enabled)
{
	int ret = 0;

	if (!wdt) {
		printk("Cannot get WDT device\n");
		return -ENODEV;
	}

	struct wdt_timeout_cfg wdt_timeout_config = {
		/* Reset SoC when watchdog timer expires. */
		.flags = WDT_FLAG_RESET_SOC,

		/* Expire watchdog after max window */
		.window.min = 0U,
		.window.max = timeout_ms,

		.callback = NULL,
	};

	if (interrupt_enabled == true) {
		wdt_timeout_config.callback = wdt_callback;
	}

	ret = wdt_install_timeout(wdt, &wdt_timeout_config);

	return ret;
}

#endif
