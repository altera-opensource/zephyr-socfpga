/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT snps_designware_watchdog

#include <string.h>
#include <drivers/watchdog.h>
#include <device.h>
#include <soc.h>
#include <socfpga_reset_manager.h>
#include "wdt_snps_designware.h"

#define DEV_CFG(_dev) ((struct wdt_dw_config *const)(_dev)->config)

struct wdt_dw_config {
	DEVICE_MMIO_ROM;
	uint32_t clk_rate;
	uint32_t wdt_inst;
};

struct wdt_dw_data {
	DEVICE_MMIO_RAM;
};

static int wdt_dw_set_config(const struct device *dev, uint8_t options)
{
	if (!dev) {
		return -ENODEV;
	}

	uint32_t reg_base = DEVICE_MMIO_GET(dev);

	ARG_UNUSED(options);

	sys_set_bits(reg_base + WDT_CR_OFFSET, WDT_CR_RMOD | WDT_CR_EN);

	return 0;
}

static int wdt_dw_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	if (!dev) {
		return -ENODEV;
	}

	if (!cfg) {
		return -ENODATA;
	}

	struct wdt_dw_config *const dev_cfg = DEV_CFG(dev);

	uint32_t reg_base = DEVICE_MMIO_GET(dev);
	uint32_t watchdog_clk = dev_cfg->clk_rate;
	uint32_t period_ms = cfg->window.max;
	uint8_t cycles_i = 0;
	uint32_t wdt_cycles = WDT_MIN_CYCLES;
	uint32_t top_init_cycles = (period_ms * watchdog_clk) / 1000;

	if (cfg->flags != WDT_FLAG_RESET_SOC) {
		return -ENOTSUP;
	}

	if (cfg->window.min != 0U || cfg->window.max == 0U) {
		return -EINVAL;
	}

	while ((cycles_i < 15) && (wdt_cycles < top_init_cycles)) {
		wdt_cycles = (wdt_cycles << 1);
		cycles_i++;
	}

	sys_set_bits(reg_base + WDT_TORR_OFFSET, (cycles_i << 4) | cycles_i);

	return 0;
}

static int wdt_dw_feed(const struct device *dev, int channel_id)
{
	if (!dev) {
		return -ENODEV;
	}

	uint32_t reg_base = DEVICE_MMIO_GET(dev);

	ARG_UNUSED(channel_id);

	sys_set_bits(reg_base + WDT_CRR_OFFSET, WDT_SW_RST);

	return 0;
}

static int wdt_dw_init(const struct device *dev)
{
	if (!dev) {
		return -ENODEV;
	}

	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	struct wdt_dw_config *const cfg = DEV_CFG(dev);
	int inst = cfg->wdt_inst;
	int bitmask = 0;

	switch (inst) {
	case WDT_0:
		bitmask = RSTMGR_PER1MODRST_WATCHDOG0;
		break;
	case WDT_1:
		bitmask = RSTMGR_PER1MODRST_WATCHDOG1;
		break;
	case WDT_2:
		bitmask = RSTMGR_PER1MODRST_WATCHDOG2;
		break;
	case WDT_3:
		bitmask = RSTMGR_PER1MODRST_WATCHDOG3;
		break;
	}

	sys_set_bits(SOCFPGA_RSTMGR_REG_BASE + SOCFPGA_RSTMGR_PER1MODRST, bitmask);
	sys_clear_bits(SOCFPGA_RSTMGR_REG_BASE + SOCFPGA_RSTMGR_PER1MODRST, bitmask);

	return 0;
}

static const struct wdt_driver_api wdt_api = {
	.setup = wdt_dw_set_config,
	.disable = NULL,
	.install_timeout = wdt_dw_install_timeout,
	.feed = wdt_dw_feed
};

#define CREATE_WDT_DEVICE(_inst)					\
									\
	static struct wdt_dw_data wdt_dw_data_##_inst;			\
									\
	static struct wdt_dw_config wdt_dw_config_##_inst = {		\
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(_inst)),		\
		.clk_rate = DT_INST_PROP(_inst, clock_frequency),	\
		.wdt_inst = _inst					\
	};								\
	DEVICE_DT_INST_DEFINE(_inst,					\
			wdt_dw_init,					\
			NULL,						\
			&wdt_dw_data_##_inst,				\
			&wdt_dw_config_##_inst,				\
			PRE_KERNEL_1,					\
			CONFIG_KERNEL_INIT_PRIORITY_DEVICE,		\
			&wdt_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_WDT_DEVICE)
