/*
 * Copyright (c) 2019 Siddharth Chandrasekaran <siddharth@embedjournal.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _OSDP_TEST_H_
#define _OSDP_TEST_H_

#include <stdio.h>
#include <string.h>
#include "osdp_common.h"

#define DO_TEST(t, m) do {          \
        t->tests++;                 \
        if (m(t->mock_data)) {      \
            t->failure++;           \
        } else {                    \
            t->success++;           \
        }                           \
    } while(0)

#define TEST_REPORT(t, s) do {      \
        t->tests++;                 \
        if (s == true)              \
            t->success++;           \
        else                        \
            t->failure++;           \
    } while (0)

#define CHECK_ARRAY(a, l, e)    do {                        \
        if (l < 0)                                          \
            printf ("error! invalid length %d\n", len);     \
        if (l != sizeof(e) || memcmp(a, e, sizeof(e))) {    \
            printf("error! comparison failed!\n");          \
            osdp_dump("  Expected: ", e, sizeof(e));        \
            osdp_dump("  Got", a, l);                       \
            return -1;                                      \
        }                                                   \
    } while(0)

struct test {
	int success;
	int failure;
	int tests;
	void *mock_data;
};

void run_cp_phy_tests(struct test *t);
void run_cp_phy_fsm_tests(struct test *t);
void run_cp_fsm_tests(struct test *t);
void run_mixed_fsm_tests(struct test *t);

#endif
