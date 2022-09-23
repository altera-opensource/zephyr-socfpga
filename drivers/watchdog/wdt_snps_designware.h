/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WDT_SNPS_DESIGNWARE_H
#define WDT_SNPS_DESIGNWARE_H

#define WDT_CR_OFFSET			0x0
#define WDT_TORR_OFFSET			0x4
#define WDT_CRR_OFFSET			0xC

#define WDT_CR_EN_BITPOS		0x0
#define WDT_CR_EN_MASK			0x1

#define WDT_CR_RMOD_BITPOS		0x1
#define WDT_CR_RMOD_MASK		0x2

#define WDT_SW_RST			0x76

#define WDT_DW_MAX_TOP			15

#endif /* WDT_SNPS_DESIGNWARE_H */
