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

#define WDT_COUNT	4
#define WDT_FEED_TRIES	20000
#define WDT_FEED_PRINT	1000
#define DELAY_COUNT	10000

#define WDT_NODE_0 DT_INST(0, snps_designware_watchdog)
#define WDT_NODE_1 DT_INST(1, snps_designware_watchdog)
#define WDT_NODE_2 DT_INST(2, snps_designware_watchdog)
#define WDT_NODE_3 DT_INST(3, snps_designware_watchdog)

#define WDT_DEV_NAME_0 DT_LABEL(WDT_NODE_0)
#define WDT_DEV_NAME_1 DT_LABEL(WDT_NODE_1)
#define WDT_DEV_NAME_2 DT_LABEL(WDT_NODE_2)
#define WDT_DEV_NAME_3 DT_LABEL(WDT_NODE_3)

static char *wdt_dev_name[WDT_COUNT] = {
	WDT_DEV_NAME_0,
	WDT_DEV_NAME_1,
	WDT_DEV_NAME_2,
	WDT_DEV_NAME_3
};

static int wdt_inst_setup(const struct device *wdt, int timeout_ms);

int main(void)
{
	int i, j, count;
	int ret = 0;
	const struct device *wdt[WDT_COUNT];

	printk("Watchdog test sample\n");

	/* Enable all 4 watchdogs */
	for (i = 0; i < WDT_COUNT; ++i) {
		wdt[i] = device_get_binding(wdt_dev_name[i]);
		ret = wdt_inst_setup(wdt[i], WDT_FEED_TRIES);
		if (ret != 0) {
			printk("Watchdog %d setup failed.\n", i);
			return ret;
		}

		printk("Watchdog %d setup successful.\n", i);
	}

	/* Test watchdog counter restart */
	printk("Feeding watchdog %d times\n", WDT_FEED_TRIES);
	for (j = 0; j < WDT_FEED_TRIES; ++j) {
		if ((j % WDT_FEED_PRINT) == 0) {
			printk("Feeding watchdog...\n");
		}

		for (i = 0; i < WDT_COUNT; ++i) {
			wdt_feed(wdt[i], 0);
		}

		/* Use while loop to mimic a delay */
		count = DELAY_COUNT;
		while (count >= 0) {
			count--;
		}
	}

	/* Test Done */
	printk("Watchdog test sample end\n");

	return 0;
}

int wdt_inst_setup(const struct device *wdt, int timeout_ms)
{
	int ret = 0;

	if (!wdt) {
		printk("Cannot get WDT device\n");
		return -ENODEV;
	}

	struct wdt_timeout_cfg wdt_config = {
		/* Reset SoC when watchdog timer expires. */
		.flags = WDT_FLAG_RESET_SOC,

		/* Expire watchdog after max window */
		.window.min = 0U,
		.window.max = timeout_ms,
	};

	ret = wdt_install_timeout(wdt, &wdt_config);
	if (ret != 0) {
		printk("Watchdog install timeout error");
		return ret;
	}

	ret = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (ret != 0) {
		printk("Watchdog setup error");
		return ret;
	}

	return 0;
}

#endif
