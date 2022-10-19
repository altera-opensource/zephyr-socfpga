/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT snps_designware_watchdog

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>

#define DEV_CFG(_dev) ((const struct wdt_dw_config *)(_dev)->config)

#define DEV_DATA(_dev) ((struct wdt_dw_data *const)(_dev)->data)

#define WDT_CR_OFFSET			0x0
#define WDT_CRR_OFFSET			0xC

#define WDT_TORR_OFFSET			0x4

#define WDT_CR_EN_BITPOS		0x0
#define WDT_CR_EN_MASK			0x1

#define WDT_CR_RMOD_BITPOS		0x1
#define WDT_CR_RMOD_MASK		0x2

#define WDT_SW_RST			0x76

#define WDT_DW_MAX_TOP			15

enum wdt_dw_rmod {
	WDT_DW_RMOD_RESET = 0,
	WDT_DW_RMOD_IRQ = 1
};

struct wdt_dw_config {
	DEVICE_MMIO_ROM;
	uint32_t clk_rate;
	const struct device *clock_dev;
	clock_control_subsys_t clkid;
	struct reset_dt_spec reset_spec;
	uint32_t wdt_rmod;
	void (*irq_config_func)(const struct device *dev);
};

struct wdt_dw_data {
	DEVICE_MMIO_RAM;
	uint32_t clk_rate;
	wdt_callback_t cb;
};

static int wdt_dw_set_config(const struct device *dev, uint8_t options)
{
	if (!dev) {
		return -ENODEV;
	}

	uintptr_t reg_base = DEVICE_MMIO_GET(dev);

	ARG_UNUSED(options);

	/* Enable WDT */
	sys_set_bit(reg_base + WDT_CR_OFFSET, WDT_CR_EN_BITPOS);

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

	const struct wdt_dw_config *dev_cfg = DEV_CFG(dev);
	struct wdt_dw_data *const dev_data = DEV_DATA(dev);

	uintptr_t reg_base = DEVICE_MMIO_GET(dev);
	uint64_t watchdog_clk = dev_data->clk_rate;
	uint64_t period_ms = cfg->window.max;
	uint64_t wdt_cycles = 0;
	uint64_t top_init_cycles = ((period_ms * watchdog_clk) / 1000);
	uint8_t top_val = WDT_DW_MAX_TOP;
	uint8_t i = 0;

	if (cfg->flags != WDT_FLAG_RESET_SOC) {
		return -ENOTSUP;
	}

	if (cfg->window.min != 0U || cfg->window.max == 0U) {
		return -EINVAL;
	}

	/* There are 16 possible timeout values in 0..15 where the number of
	 * cycles is 2 ^ (16 + i) and the watchdog counts down.
	 */
	for (i = 0; i <= WDT_DW_MAX_TOP; i++) {
		wdt_cycles = (1U << (16 + i));

		/* Iterate until we find the closest match */
		if (wdt_cycles >= top_init_cycles) {
			top_val = i;
			break;
		}
	}

	sys_write32(((top_val << 4) | top_val), reg_base + WDT_TORR_OFFSET);

	if ((cfg->callback) && (dev_cfg->wdt_rmod == WDT_DW_RMOD_IRQ)) {
		sys_set_bit(reg_base + WDT_CR_OFFSET, WDT_CR_RMOD_BITPOS);

		dev_data->cb = cfg->callback;
	} else {
		sys_clear_bit(reg_base + WDT_CR_OFFSET, WDT_CR_RMOD_BITPOS);

		dev_data->cb = NULL;
	}

	return 0;
}

static int wdt_dw_feed(const struct device *dev, int channel_id)
{
	if (!dev) {
		return -ENODEV;
	}

	uintptr_t reg_base = DEVICE_MMIO_GET(dev);

	ARG_UNUSED(channel_id);

	sys_set_bits(reg_base + WDT_CRR_OFFSET, WDT_SW_RST);

	return 0;
}

static int wdt_dw_disable(const struct device *dev)
{
	if (!dev) {
		return -ENODEV;
	}

	const struct wdt_dw_config *dev_cfg = DEV_CFG(dev);

	if (!device_is_ready(dev_cfg->reset_spec.dev)) {
		return -ENODEV;
	}

	reset_line_assert(dev_cfg->reset_spec.dev, dev_cfg->reset_spec.id);
	reset_line_deassert(dev_cfg->reset_spec.dev, dev_cfg->reset_spec.id);

	return 0;
}

static int wdt_dw_init(const struct device *dev)
{
	if (!dev) {
		return -ENODEV;
	}

	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	const struct wdt_dw_config *dev_cfg = DEV_CFG(dev);
	struct wdt_dw_data *const dev_data = DEV_DATA(dev);
	int ret = 0;

	/*
	 * Set clock rate from clock_frequency property if valid,
	 * otherwise, get clock rate from clock manager
	 */
	if (dev_cfg->clk_rate == 0U) {
		if (!device_is_ready(dev_cfg->clock_dev)) {
			return -EINVAL;
		}

		ret = clock_control_get_rate(dev_cfg->clock_dev, dev_cfg->clkid,
					&dev_data->clk_rate);

		if (ret != 0) {
			return ret;
		}
	}

	/* Reset Watchdog */
	ret = wdt_dw_disable(dev);

	if (ret != 0) {
		return ret;
	}

	/* Register and enable WDT IRQ for this instance */
	if (dev_cfg->irq_config_func) {
		dev_cfg->irq_config_func(dev);
	}

	return 0;
}

static const struct wdt_driver_api wdt_api = {
	.setup = wdt_dw_set_config,
	.disable = wdt_dw_disable,
	.install_timeout = wdt_dw_install_timeout,
	.feed = wdt_dw_feed
};

#define WDT_SNPS_DESIGNWARE_IRQ_FUNC_DECLARE(inst) \
	static void wdt_dw_irq_config_func##inst(const struct device *dev);

#define WDT_SNPS_DESIGNWARE_IRQ_FUNC_INIT(inst) \
	.wdt_rmod = DT_INST_IRQ_HAS_CELL(inst, irq), \
	.irq_config_func = wdt_dw_irq_config_func##inst,

#define WDT_SNPS_DESIGNWARE_IRQ_FUNC_DEFINE(inst) \
	static void wdt_dw_isr##inst(const struct device *dev) \
	{ \
		struct wdt_dw_data *const dev_data = DEV_DATA(dev); \
		\
		if (dev_data->cb) { \
			dev_data->cb(dev, 0); \
		} \
	} \
	\
	static void wdt_dw_irq_config_func##inst(const struct device *dev) \
	{ \
		ARG_UNUSED(dev); \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority), \
				wdt_dw_isr##inst, DEVICE_DT_INST_GET(inst), 0); \
		irq_enable(DT_INST_IRQN(inst)); \
	}

#define WDT_SNPS_DESIGNWARE_CLOCK_RATE_INIT(inst) \
		COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, clock_frequency), \
			( \
				.clk_rate = DT_INST_PROP(inst, clock_frequency), \
				.clock_dev = NULL, \
				.clkid = (clock_control_subsys_t)0, \
			), \
			( \
				.clk_rate = 0, \
				.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)), \
				.clkid = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid), \
			) \
		)

#define CREATE_WDT_DEVICE(_inst) \
	\
	IF_ENABLED(DT_INST_IRQ_HAS_CELL(_inst, irq), \
		(WDT_SNPS_DESIGNWARE_IRQ_FUNC_DECLARE(_inst))) \
	\
	static struct wdt_dw_data wdt_dw_data_##_inst = { \
		.cb = NULL, \
	}; \
	\
	static struct wdt_dw_config wdt_dw_config_##_inst = { \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(_inst)), \
		WDT_SNPS_DESIGNWARE_CLOCK_RATE_INIT(_inst) \
	\
		.reset_spec = RESET_DT_SPEC_INST_GET(_inst), \
	\
		IF_ENABLED(DT_INST_IRQ_HAS_CELL(_inst, irq), \
			(WDT_SNPS_DESIGNWARE_IRQ_FUNC_INIT(_inst))) \
	}; \
	\
	IF_ENABLED(DT_INST_IRQ_HAS_CELL(_inst, irq), \
		(WDT_SNPS_DESIGNWARE_IRQ_FUNC_DEFINE(_inst))) \
	\
	DEVICE_DT_INST_DEFINE(_inst, \
			wdt_dw_init, \
			NULL, \
			&wdt_dw_data_##_inst, \
			&wdt_dw_config_##_inst, \
			PRE_KERNEL_1, \
			CONFIG_KERNEL_INIT_PRIORITY_DEVICE, \
			&wdt_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_WDT_DEVICE)
