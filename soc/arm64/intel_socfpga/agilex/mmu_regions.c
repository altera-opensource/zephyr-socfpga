/*
 * Copyright (c) 2021, Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm64/arm_mmu.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

static const struct arm_mmu_region mmu_regions[] = {

	MMU_REGION_FLAT_ENTRY("CLOCK",
			      DT_REG_ADDR(DT_NODELABEL(clock)),
			      DT_REG_SIZE(DT_NODELABEL(clock)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_DEFAULT_SECURE_STATE),
	MMU_REGION_FLAT_ENTRY("UART0",
			      DT_REG_ADDR(DT_NODELABEL(uart0)),
			      DT_REG_SIZE(DT_NODELABEL(uart0)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_DEFAULT_SECURE_STATE),

	/* Peripheral to cover UART, I2C, I2C EMAC and Timer SP */
	MMU_REGION_FLAT_ENTRY("PERIPHERAL",
			      DT_REG_ADDR_BY_IDX(DT_NODELABEL(periph), 0),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(periph), 0),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_DEFAULT_SECURE_STATE),

	MMU_REGION_FLAT_ENTRY("RESET_MANAGER",
			      DT_REG_ADDR_BY_IDX(DT_NODELABEL(periph), 1),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(periph), 1),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_DEFAULT_SECURE_STATE),

	/* System manager register that required by clock driver */
	MMU_REGION_FLAT_ENTRY("SYSTEM_MANAGER",
			      DT_REG_ADDR_BY_IDX(DT_NODELABEL(periph), 2),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(periph), 2),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_DEFAULT_SECURE_STATE),

	MMU_REGION_FLAT_ENTRY("PINMUX",
			      DT_REG_ADDR_BY_IDX(DT_NODELABEL(periph), 3),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(periph), 3),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_DEFAULT_SECURE_STATE),

	MMU_REGION_FLAT_ENTRY("SDMMC",
			      DT_REG_ADDR(DT_NODELABEL(sdmmc)),
			      DT_REG_SIZE(DT_NODELABEL(sdmmc)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_RW | MT_DEFAULT_SECURE_STATE),

	MMU_REGION_FLAT_ENTRY("GIC",
			      DT_REG_ADDR_BY_IDX(DT_NODELABEL(gic), 0),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(gic), 0),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_DEFAULT_SECURE_STATE),

	MMU_REGION_FLAT_ENTRY("GIC",
			      DT_REG_ADDR_BY_IDX(DT_NODELABEL(gic), 1),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(gic), 1),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_DEFAULT_SECURE_STATE),
};

const struct arm_mmu_config mmu_config = {
	.num_regions = ARRAY_SIZE(mmu_regions),
	.mmu_regions = mmu_regions,
};
