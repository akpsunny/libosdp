/*
 * Copyright (c) 2019 Siddharth Chandrasekaran <siddharth@embedjournal.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unistd.h>

#include <osdp.h>
#include "test.h"

extern int (*test_state_update)(struct osdp_pd *);

int test_fsm_resp = 0;

int test_cp_fsm_send(void *data, uint8_t *buf, int len)
{
	ARG_UNUSED(data);

	switch (buf[6]) {
	case 0x60:
		test_fsm_resp = 1;
		break;
	case 0x61:
		test_fsm_resp = 2;
		break;
	case 0x62:
		test_fsm_resp = 3;
		break;
	}
	return len;
}

int test_cp_fsm_receive(void *data, uint8_t *buf, int len)
{
	ARG_UNUSED(data);

	uint8_t resp_id[] = {
		0xff, 0x53, 0xe5, 0x14, 0x00, 0x04, 0x45, 0xa1, 0xa2,
		0xa3, 0xb1, 0xc1, 0xd1, 0xd2, 0xd3, 0xd4, 0xe1, 0xe2,
		0xe3, 0xf8, 0xd9
	};
	uint8_t resp_cap[] = {
		0xff, 0x53, 0xe5, 0x0b, 0x00, 0x05, 0x46, 0x04, 0x04,
		0x01, 0xb3, 0xec
	};
	uint8_t resp_ack[] = {
		0xff, 0x53, 0xe5, 0x08, 0x00, 0x06, 0x40, 0xb0, 0xf0
	};

	ARG_UNUSED(len);

	switch (test_fsm_resp) {
	case 1:
		memcpy(buf, resp_ack, sizeof(resp_ack));
		return sizeof(resp_ack);
	case 2:
		memcpy(buf, resp_id, sizeof(resp_id));
		return sizeof(resp_id);
	case 3:
		memcpy(buf, resp_cap, sizeof(resp_cap));
		return sizeof(resp_cap);
	}
	return -1;
}

int test_cp_fsm_setup(struct test *t)
{
	/* mock application data */
	osdp_pd_info_t info = {
		.address = 101,
		.baud_rate = 9600,
		.flags = 0,
		.channel.data = NULL,
		.channel.send = test_cp_fsm_send,
		.channel.recv = test_cp_fsm_receive,
		.channel.flush = NULL
	};
	struct osdp *ctx = (struct osdp *) osdp_cp_setup(1, &info, NULL);
	if (ctx == NULL) {
		printf("   init failed!\n");
		return -1;
	}
	osdp_set_log_level(LOG_INFO);
	SET_CURRENT_PD(ctx, 0);
	SET_FLAG(GET_CURRENT_PD(ctx), PD_FLAG_SKIP_SEQ_CHECK);
	t->mock_data = (void *)ctx;
	return 0;
}

void test_cp_fsm_teardown(struct test *t)
{
	osdp_cp_teardown(t->mock_data);
}

void run_cp_fsm_tests(struct test *t)
{
	int result = true;
	uint32_t count = 0;
	struct osdp *ctx;

	printf("\nStarting CP Phy state tests\n");

	if (test_cp_fsm_setup(t))
		return;

	ctx = t->mock_data;

	printf("    -- executing state_update()\n");
	while (1) {
		test_state_update(GET_CURRENT_PD(ctx));

		if (GET_CURRENT_PD(ctx)->state == OSDP_CP_STATE_OFFLINE) {
			printf("    -- state_update() CP went offline\n");
			result = false;
			break;
		}
		if (count++ > 300)
			break;
		usleep(1000);
	}
	printf("    -- state_update() complete\n");

	TEST_REPORT(t, result);

	test_cp_fsm_teardown(t);
}

// unnecessary
