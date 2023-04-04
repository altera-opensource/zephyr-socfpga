/*
 * Copyright (c) 2022-2023 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT snps_designware_watchdog

#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>

#define LOG_LEVEL CONFIG_WDT_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wdt_snps_designware);

#define DEV_CFG(_dev) ((const struct wdt_dw_config *)(_dev)->config)

#define DEV_DATA(_dev) ((struct wdt_dw_data *const)(_dev)->data)

/* Watchdog register offsets, Bit positions and Masks */
#define WDT_CR_OFFSET			0x0
#define WDT_CRR_OFFSET			0xC

#define WDT_TORR_OFFSET			0x4

#define WDT_CR_EN_BITPOS		0x0
#define WDT_CR_EN_MASK			0x1

#define WDT_CR_RMOD_BITPOS		0x1
#define WDT_CR_RMOD_MASK		0x2

/* Value to be programmed in CRR register for feeding watchdog */
#define WDT_SW_RST			0x76

/* Maximum TOP value and cycles supported */
#define WDT_DW_MAX_TOP			15
#define WDT_DW_MAX_TOP_CYCLES		BIT(31)

/* Watchdog timeout response modes */
enum wdt_dw_rmod {
	/* Generate a system reset */
	WDT_DW_RMOD_RESET = 0,
	/* First generate an interrupt and if it is not cleared
	 * by the time a second timeout occurs then generate
	 * a system reset.
	 */
	WDT_DW_RMOD_IRQ = 1
};

/* Device configurations */
struct wdt_dw_config {
	/* MMIO mapping for watchdog register base */
	DEVICE_MMIO_ROM;
	/* Watchdog timeout response mode configuration */
	uint32_t wdt_rmod;
	/* Watchdog clock frequency configuration */
	uint32_t clk_rate;
	/* Clock controller device instance */
	const struct device *clock_dev;
	/* Clock identifier for watchdog to be used by clock driver */
	clock_control_subsys_t clkid;
	/* Reset controller device configurartions */
	struct reset_dt_spec reset_spec;
	/* Watchdog interrupt configuration function pointer */
	void (*irq_config_func)(const struct device *dev);
};

/* Driver data */
struct wdt_dw_data {
	/* MMIO mapping information for watchdog configurations */
	DEVICE_MMIO_RAM;
	/* Watchdog clock frequency read from clock driver during init */
	uint32_t clk_rate;
	/* Application callback function pointer */
	wdt_callback_t cb;
};

static int wdt_dw_setup(const struct device *dev, uint8_t options)
{
	ARG_UNUSED(options);

	uintptr_t reg_base = DEVICE_MMIO_GET(dev);

	/* Check and Enable WDT */
	if (sys_test_bit(reg_base + WDT_CR_OFFSET, WDT_CR_EN_BITPOS) != BIT(WDT_CR_EN_BITPOS)) {
		sys_set_bit(reg_base + WDT_CR_OFFSET, WDT_CR_EN_BITPOS);
	}

	return 0;
}

static int wdt_dw_feed(const struct device *dev, int channel_id)
{
	ARG_UNUSED(channel_id);

	uintptr_t reg_base = DEVICE_MMIO_GET(dev);

	sys_write32(WDT_SW_RST, reg_base + WDT_CRR_OFFSET);

	return 0;
}

static int wdt_dw_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	if (!cfg) {
		LOG_ERR("WDT timeout configuration missing");
		return -ENODATA;
	}

	const struct wdt_dw_config *dev_cfg = DEV_CFG(dev);
	struct wdt_dw_data *const dev_data = DEV_DATA(dev);

	uintptr_t reg_base = DEVICE_MMIO_GET(dev);
	uint8_t top_val = WDT_DW_MAX_TOP;
	uint8_t i = 0;
	uint32_t wdt_cycles = 0;
	uint64_t watchdog_clk = dev_data->clk_rate;
	uint64_t top_init_cycles = 0;

	if (cfg->flags != WDT_FLAG_RESET_SOC) {
		LOG_ERR("Only SoC reset supported");
		return -ENOTSUP;
	}

	if (cfg->window.min != 0U) {
		LOG_ERR("Minimum timeout out of range");
		return -EINVAL;
	}

	if (cfg->window.max == 0U) {
		LOG_ERR("Maximum timeout out of range");
		return -EINVAL;
	}

	top_init_cycles = ((cfg->window.max * watchdog_clk) / 1000);

	if (top_init_cycles > WDT_DW_MAX_TOP_CYCLES) {
		LOG_ERR("Maximum timeout out of range");
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
		/* Configure wdt to interrupt mode.
		 * Interrupt will be triggered on first timeout and
		 * if wdt is not reloaded using wdt_feed() before next
		 * timeout a watchdog system reset will be triggered.
		 */
		sys_set_bit(reg_base + WDT_CR_OFFSET, WDT_CR_RMOD_BITPOS);

		dev_data->cb = cfg->callback;
	} else {
		/* Configure wdt to reset mode.
		 * Watchdog system reset will be triggered after timeout.
		 */
		sys_clear_bit(reg_base + WDT_CR_OFFSET, WDT_CR_RMOD_BITPOS);

		dev_data->cb = NULL;
	}

	/* Feed new TOP value into the watchdog counter if already activated. */
	if (sys_test_bit(reg_base + WDT_CR_OFFSET, WDT_CR_EN_BITPOS) == BIT(WDT_CR_EN_BITPOS)) {
		wdt_dw_feed(dev, 0);
	}

	return 0;
}

static int wdt_dw_disable(const struct device *dev)
{
	const struct wdt_dw_config *dev_cfg = DEV_CFG(dev);
	int ret = 0;

	/* Check if the Reset controller driver is supported */
	if (dev_cfg->reset_spec.dev == NULL) {
		LOG_ERR("Watchdog Disable not supported");
		return -ENOTSUP;
	}

	if (!device_is_ready(dev_cfg->reset_spec.dev)) {
		LOG_ERR("Reset controller device not ready");
		return -ENODEV;
	}

	ret = reset_line_toggle(dev_cfg->reset_spec.dev, dev_cfg->reset_spec.id);

	if (ret != 0) {
		LOG_ERR("Watchdog Disable/Reset failed");
	}

	return ret;
}

static int wdt_dw_init(const struct device *dev)
{
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
			LOG_ERR("Clock controller device not ready");
			return -ENODEV;
		}

		ret = clock_control_get_rate(dev_cfg->clock_dev, dev_cfg->clkid,
					&dev_data->clk_rate);

		if (ret != 0) {
			LOG_ERR("Failed to get watchdog clock rate");
			return ret;
		}
	} else {
		dev_data->clk_rate = dev_cfg->clk_rate;
	}

	/* Reset Watchdog */
	if (dev_cfg->reset_spec.dev != NULL) {
		ret = wdt_dw_disable(dev);

		if (ret != 0) {
			return ret;
		}
	}

	/* Register and enable WDT IRQ for this instance */
	if (dev_cfg->irq_config_func) {
		dev_cfg->irq_config_func(dev);
	}

	/* Enable watchdog if it needs to be enabled at boot.
	 * Watchdog timer will be started with maximum timeout
	 * that is the default value.
	 */
	if (!IS_ENABLED(CONFIG_WDT_DISABLE_AT_BOOT)) {
		ret = wdt_dw_setup(dev, 0);

		if (ret != 0) {
			LOG_ERR("Failed to enable watchdog");
		}
	}

	return ret;
}

static const struct wdt_driver_api wdt_api = {
	.setup = wdt_dw_setup,
	.disable = wdt_dw_disable,
	.install_timeout = wdt_dw_install_timeout,
	.feed = wdt_dw_feed,
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
		/* Interrupt is supposed to be cleared by the following feed operation. */ \
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

#define WDT_SNPS_DESIGNWARE_RESET_SPEC_INIT(inst) \
	.reset_spec = RESET_DT_SPEC_INST_GET(inst),

#define CREATE_WDT_DEVICE(_inst) \
	\
	IF_ENABLED(DT_INST_IRQ_HAS_CELL(_inst, irq), \
		(WDT_SNPS_DESIGNWARE_IRQ_FUNC_DECLARE(_inst))) \
	\
	static struct wdt_dw_data wdt_dw_data_##_inst = { \
		.cb = NULL, \
	}; \
	\
	static const struct wdt_dw_config wdt_dw_config_##_inst = { \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(_inst)), \
		WDT_SNPS_DESIGNWARE_CLOCK_RATE_INIT(_inst) \
	\
		IF_ENABLED(DT_INST_NODE_HAS_PROP(_inst, resets), \
			(WDT_SNPS_DESIGNWARE_RESET_SPEC_INIT(_inst))) \
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
