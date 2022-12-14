/*
 * Copyright (c) 2017 Nordic Semiconductor ASA
 * Copyright (c) 2015 Runtime Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fcb_test.h"

ZTEST(fcb_test_with_2sectors_set, test_fcb_empty_walk)
{
	int rc;
	struct fcb *fcb;

	fcb = &test_fcb;

	rc = fcb_walk(fcb, 0, fcb_test_empty_walk_cb, NULL);
	zassert_true(rc == 0, "fcb_walk call failure");
}
