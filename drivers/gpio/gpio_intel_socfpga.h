/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GPIO_INTEL_SOCFPGA_H
#define GPIO_INTEL_SOCFPGA_H

/* GPIO pinmux register base address */
#if defined(CONFIG_BOARD_INTEL_SOCFPGA_AGILEX_SOCDK)
	#define SOCFPGA_PINMUX_PIN0SEL_REG_BASE 0xffd13000
#elif defined(CONFIG_BOARD_INTEL_SOCFPGA_AGILEX5_SOCDK)
	#define SOCFPGA_PINMUX_PIN0SEL_REG_BASE 0x10d13000
#endif

#define GPIO_SWPORTA_DDR_OFFSET	0x04
#define GPIO_EXT_PORTA_OFFSET	0x50
#define GPIO_ID_CODE_OFFSET	0x64
#define GPIO_VER_ID_CODE_OFFSET	0x6c

#define PMUX_SEL_OFFSET		4
#define PMUX_40_SEL		40
#define PMUX_40_SEL_GAP		96
#define PMUX_GPIO_VAL		8

#endif /* GPIO_INTEL_SOCFPGA_H */
