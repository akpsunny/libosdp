/*
 * Copyright (c) 2019 Siddharth Chandrasekaran <siddharth@embedjournal.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include <utils/utils.h>

#include "osdp_common.h"

#define TAG "PD: "

#define CMD_POLL_DATA_LEN              0
#define CMD_LSTAT_DATA_LEN             0
#define CMD_ISTAT_DATA_LEN             0
#define CMD_OSTAT_DATA_LEN             0
#define CMD_RSTAT_DATA_LEN             0
#define CMD_ID_DATA_LEN                1
#define CMD_CAP_DATA_LEN               1
#define CMD_OUT_DATA_LEN               4
#define CMD_LED_DATA_LEN               14
#define CMD_BUZ_DATA_LEN               5
#define CMD_TEXT_DATA_LEN              6   /* variable length command */
#define CMD_COMSET_DATA_LEN            5
#define CMD_MFG_DATA_LEN               4   /* variable length command */
#define CMD_KEYSET_DATA_LEN            18
#define CMD_CHLNG_DATA_LEN             8
#define CMD_SCRYPT_DATA_LEN            16

#define REPLY_ACK_LEN                  1
#define REPLY_PDID_LEN                 13
#define REPLY_PDCAP_LEN                1   /* variable length command */
#define REPLY_PDCAP_ENTITY_LEN         3
#define REPLY_LSTATR_LEN               3
#define REPLY_RSTATR_LEN               2
#define REPLY_KEYPAD_LEN               2
#define REPLY_RAW_LEN                  4
#define REPLY_FMT_LEN                  3
#define REPLY_COM_LEN                  6
#define REPLY_NAK_LEN                  2
#define REPLY_MFGREP_LEN               4   /* variable length command */
#define REPLY_CCRYPT_LEN               33
#define REPLY_RMAC_I_LEN               17

/* Implicit cababilities */
static struct osdp_pd_cap osdp_pd_cap[] = {
	/* Driver Implicit cababilities */
	{
		OSDP_PD_CAP_CHECK_CHARACTER_SUPPORT,
		1, /* The PD supports the 16-bit CRC-16 mode */
		0, /* N/A */
	},
	{
		OSDP_PD_CAP_COMMUNICATION_SECURITY,
#ifdef CONFIG_OSDP_SC_ENABLED
		1, /* (Bit-0) AES128 support */
		1, /* (Bit-0) default AES128 key */
#else
		0, /* SC not supported */
		0, /* SC not supported */
#endif
	},
	{ -1, 0, 0 } /* Sentinel */
};

struct pd_event_node {
	queue_node_t node;
	struct osdp_event object;
};

static int pd_event_queue_init(struct osdp_pd *pd)
{
	if (slab_init(&pd->event.slab, sizeof(struct pd_event_node),
		      OSDP_CP_CMD_POOL_SIZE)) {
		LOG_ERR("Failed to initialize command slab");
		return -1;
	}
	queue_init(&pd->event.queue);
	return 0;
}

static void pd_event_queue_del(struct osdp_pd *pd)
{
	slab_del(&pd->event.slab);
}

static struct osdp_event *pd_event_alloc(struct osdp_pd *pd)
{
	struct pd_event_node *event = NULL;

	if (slab_alloc(&pd->event.slab, (void **)&event)) {
		LOG_ERR("Memory allocation failed");
		return NULL;
	}
	return &event->object;
}

static void pd_event_free(struct osdp_pd *pd, struct osdp_event *event)
{
	struct pd_event_node *n;

	n = CONTAINER_OF(event, struct pd_event_node, object);
	slab_free(&pd->event.slab, n);
}

static void pd_event_enqueue(struct osdp_pd *pd, struct osdp_event *event)
{
	struct pd_event_node *n;

	n = CONTAINER_OF(event, struct pd_event_node, object);
	queue_enqueue(&pd->event.queue, &n->node);
}

static int pd_event_dequeue(struct osdp_pd *pd, struct osdp_event **event)
{
	struct pd_event_node *n;
	queue_node_t *node;

	if (queue_dequeue(&pd->event.queue, &node)) {
		return -1;
	}
	n = CONTAINER_OF(node, struct pd_event_node, node);
	*event = &n->object;
	return 0;
}

static int pd_translate_event(struct osdp_event *event, uint8_t *data)
{
	int reply_code = 0;

	switch (event->type) {
	case OSDP_EVENT_CARDREAD:
		if (event->cardread.format == OSDP_CARD_FMT_RAW_UNSPECIFIED ||
		    event->cardread.format == OSDP_CARD_FMT_RAW_WIEGAND) {
			reply_code = REPLY_RAW;
		} else if (event->cardread.format == OSDP_CARD_FMT_ASCII) {
			reply_code = REPLY_FMT;
		} else {
			LOG_ERR(TAG "Event: cardread; Error: unknown format");
			break;
		}
		break;
	case OSDP_EVENT_KEYPRESS:
		reply_code = REPLY_KEYPPAD;
		break;
	default:
		LOG_ERR(TAG "unknown event");
		break;
	}
	if (reply_code == 0) {
		/* POLL command cannot fail even when there are errors here */
		return REPLY_ACK;
	}
	memcpy(data, event, sizeof(struct osdp_event));
	return reply_code;
}

static void pd_decode_command(struct osdp_pd *pd, uint8_t *buf, int len)
{
	int i, ret = -1, pos = 0, tmp;
	struct osdp_cmd cmd;
	struct osdp_event *event;

	pd->reply_id = 0;
	pd->cmd_id = buf[pos++];
	len--;

	switch (pd->cmd_id) {
	case CMD_POLL:
		if (len != CMD_POLL_DATA_LEN) {
			break;
		}
		/* Check if we have external events in the queue */
		if (pd_event_dequeue(pd, &event) == 0) {
			ret = pd_translate_event(event, pd->ephemeral_data);
			pd->reply_id = ret;
			pd_event_free(pd, event);
		} else {
			pd->reply_id = REPLY_ACK;
		}
		ret = 0;
		break;
	case CMD_LSTAT:
		if (len != CMD_LSTAT_DATA_LEN) {
			break;
		}
		pd->reply_id = REPLY_LSTATR;
		ret = 0;
		break;
	case CMD_ISTAT:
		if (len != CMD_ISTAT_DATA_LEN) {
			break;
		}
		pd->reply_id = REPLY_ISTATR;
		ret = 0;
		break;
	case CMD_OSTAT:
		if (len != CMD_OSTAT_DATA_LEN) {
			break;
		}
		pd->reply_id = REPLY_OSTATR;
		ret = 0;
		break;
	case CMD_RSTAT:
		if (len != CMD_RSTAT_DATA_LEN) {
			break;
		}
		pd->reply_id = REPLY_RSTATR;
		ret = 0;
		break;
	case CMD_ID:
		if (len != CMD_ID_DATA_LEN) {
			break;
		}
		pos++;		/* Skip reply type info. */
		pd->reply_id = REPLY_PDID;
		ret = 0;
		break;
	case CMD_CAP:
		if (len != CMD_CAP_DATA_LEN) {
			break;
		}
		pos++;		/* Skip reply type info. */
		pd->reply_id = REPLY_PDCAP;
		ret = 0;
		break;
	case CMD_OUT:
		if (len != CMD_OUT_DATA_LEN || !pd->command_callback) {
			break;
		}
		cmd.id = OSDP_CMD_OUTPUT;
		cmd.output.output_no    = buf[pos++];
		cmd.output.control_code = buf[pos++];
		cmd.output.timer_count  = buf[pos++];
		cmd.output.timer_count |= buf[pos++] << 8;
		ret = pd->command_callback(pd->command_callback_arg,
					   pd->address, &cmd);
		if (ret != 0) {
			pd->reply_id = REPLY_NAK;
			pd->ephemeral_data[0] = OSDP_PD_NAK_RECORD;
			ret = 0;
			break;
		}
		pd->reply_id = REPLY_ACK;
		ret = 0;
		break;
	case CMD_LED:
		if (len != CMD_LED_DATA_LEN || !pd->command_callback) {
			break;
		}
		cmd.id = OSDP_CMD_LED;
		cmd.led.reader = buf[pos++];
		cmd.led.led_number = buf[pos++];

		cmd.led.temporary.control_code = buf[pos++];
		cmd.led.temporary.on_count     = buf[pos++];
		cmd.led.temporary.off_count    = buf[pos++];
		cmd.led.temporary.on_color     = buf[pos++];
		cmd.led.temporary.off_color    = buf[pos++];
		cmd.led.temporary.timer_count  = buf[pos++];
		cmd.led.temporary.timer_count |= buf[pos++] << 8;

		cmd.led.permanent.control_code = buf[pos++];
		cmd.led.permanent.on_count     = buf[pos++];
		cmd.led.permanent.off_count    = buf[pos++];
		cmd.led.permanent.on_color     = buf[pos++];
		cmd.led.permanent.off_color    = buf[pos++];
		ret = pd->command_callback(pd->command_callback_arg,
					   pd->address, &cmd);
		if (ret != 0) {
			pd->reply_id = REPLY_NAK;
			pd->ephemeral_data[0] = OSDP_PD_NAK_RECORD;
			ret = 0;
			break;
		}
		pd->reply_id = REPLY_ACK;
		ret = 0;
		break;
	case CMD_BUZ:
		if (len != CMD_BUZ_DATA_LEN || !pd->command_callback) {
			break;
		}
		cmd.id = OSDP_CMD_BUZZER;
		cmd.buzzer.reader       = buf[pos++];
		cmd.buzzer.control_code = buf[pos++];
		cmd.buzzer.on_count     = buf[pos++];
		cmd.buzzer.off_count    = buf[pos++];
		cmd.buzzer.rep_count    = buf[pos++];
		ret = pd->command_callback(pd->command_callback_arg,
					   pd->address, &cmd);
		if (ret != 0) {
			pd->reply_id = REPLY_NAK;
			pd->ephemeral_data[0] = OSDP_PD_NAK_RECORD;
			ret = 0;
			break;
		}
		pd->reply_id = REPLY_ACK;
		ret = 0;
		break;
	case CMD_TEXT:
		if (len < CMD_TEXT_DATA_LEN || !pd->command_callback) {
			break;
		}
		cmd.id = OSDP_CMD_TEXT;
		cmd.text.reader       = buf[pos++];
		cmd.text.control_code = buf[pos++];
		cmd.text.temp_time    = buf[pos++];
		cmd.text.offset_row   = buf[pos++];
		cmd.text.offset_col   = buf[pos++];
		cmd.text.length       = buf[pos++];
		if (cmd.text.length > OSDP_CMD_TEXT_MAX_LEN ||
		    ((len - CMD_TEXT_DATA_LEN) < cmd.text.length) ||
		    cmd.text.length > OSDP_CMD_TEXT_MAX_LEN) {
			break;
		}
		for (i = 0; i < cmd.text.length; i++) {
			cmd.text.data[i] = buf[pos++];
		}
		ret = pd->command_callback(pd->command_callback_arg,
					   pd->address, &cmd);
		if (ret != 0) {
			pd->reply_id = REPLY_NAK;
			pd->ephemeral_data[0] = OSDP_PD_NAK_RECORD;
			ret = 0;
			break;
		}
		pd->reply_id = REPLY_ACK;
		ret = 0;
		break;
	case CMD_COMSET:
		if (len != CMD_COMSET_DATA_LEN || !pd->command_callback) {
			break;
		}
		cmd.id = OSDP_CMD_COMSET;
		cmd.comset.address    = buf[pos++];
		cmd.comset.baud_rate  = buf[pos++];
		cmd.comset.baud_rate |= buf[pos++] << 8;
		cmd.comset.baud_rate |= buf[pos++] << 16;
		cmd.comset.baud_rate |= buf[pos++] << 24;
		if (cmd.comset.address >= 0x7F ||
		    (cmd.comset.baud_rate != 9600 &&
		     cmd.comset.baud_rate != 38400 &&
		     cmd.comset.baud_rate != 115200)) {
			LOG_ERR(TAG "COMSET Failed! command discarded");
			cmd.comset.address = pd->address;
			cmd.comset.baud_rate = pd->baud_rate;
		}
		ret = pd->command_callback(pd->command_callback_arg,
						pd->address, &cmd);
		if (ret != 0) {
			pd->reply_id = REPLY_NAK;
			pd->ephemeral_data[0] = OSDP_PD_NAK_RECORD;
			ret = 0;
			break;
		}
		memcpy(pd->ephemeral_data, &cmd, sizeof(struct osdp_cmd));
		pd->reply_id = REPLY_COM;
		ret = 0;
		break;
	case CMD_MFG:
		if (len < CMD_MFG_DATA_LEN || !pd->command_callback) {
			break;
		}
		cmd.id = OSDP_CMD_MFG;
		cmd.mfg.vendor_code  = buf[pos++]; /* vendor_code */
		cmd.mfg.vendor_code |= buf[pos++] << 8;
		cmd.mfg.vendor_code |= buf[pos++] << 16;
		cmd.mfg.command = buf[pos++];
		cmd.mfg.length = len - CMD_MFG_DATA_LEN;
		if (cmd.mfg.length > OSDP_CMD_MFG_MAX_DATALEN) {
			LOG_ERR(TAG "cmd length error");
			break;
		}
		for (i = 0; i < cmd.mfg.length; i++) {
			cmd.mfg.data[i] = buf[pos++];
		}
		ret = pd->command_callback(pd->command_callback_arg,
					   pd->address, &cmd);
		if (ret > 0) { /* App wants to send a REPLY_MFGREP to the CP */
			memcpy(pd->ephemeral_data, &cmd, sizeof(struct osdp_cmd));
			pd->reply_id = REPLY_MFGREP;
		} else if (ret < 0) { /* Errors */
			pd->reply_id = REPLY_NAK;
			pd->ephemeral_data[0] = OSDP_PD_NAK_RECORD;
		} else {
			pd->reply_id = REPLY_ACK;
		}
		ret = 0;
		break;
#ifdef CONFIG_OSDP_SC_ENABLED
	case CMD_KEYSET:
		if (len != CMD_KEYSET_DATA_LEN) {
			break;
		}
		/**
		 * For CMD_KEYSET to be accepted, PD must be
		 * ONLINE and SC_ACTIVE.
		 */
		if (ISSET_FLAG(pd, PD_FLAG_SC_ACTIVE) == 0) {
			pd->reply_id = REPLY_NAK;
			pd->ephemeral_data[0] = OSDP_PD_NAK_SC_COND;
			LOG_ERR(TAG "Keyset with SC inactive");
			break;
		}
		/* only key_type == 1 (SCBK) and key_len == 16 is supported */
		if (buf[pos] != 1 || buf[pos + 1] != 16) {
			LOG_ERR(TAG "Keyset invalid len/type: %d/%d",
			      buf[pos], buf[pos + 1]);
			break;
		}
		cmd.id = OSDP_CMD_KEYSET;
		cmd.keyset.type   = buf[pos++];
		cmd.keyset.length = buf[pos++];
		memcpy(cmd.keyset.data, buf + pos, 16);
		memcpy(pd->sc.scbk, buf + pos, 16);
		ret = 0;
		if (pd->command_callback) {
			ret = pd->command_callback(pd->command_callback_arg,
						   pd->address, &cmd);
		} else {
			LOG_WRN(TAG "Keyset without command callback trigger");
		}
		if (ret != 0) {
			pd->reply_id = REPLY_NAK;
			pd->ephemeral_data[0] = OSDP_PD_NAK_RECORD;
			ret = 0;
			break;
		}
		CLEAR_FLAG(pd, PD_FLAG_SC_USE_SCBKD);
		CLEAR_FLAG(pd, PD_FLAG_INSTALL_MODE);
		pd->reply_id = REPLY_ACK;
		ret = 0;
		break;
	case CMD_CHLNG:
		tmp = OSDP_PD_CAP_COMMUNICATION_SECURITY;
		if (pd->cap[tmp].compliance_level == 0) {
			pd->reply_id = REPLY_NAK;
			pd->ephemeral_data[0] = OSDP_PD_NAK_SC_UNSUP;
			break;
		}
		if (len != CMD_CHLNG_DATA_LEN) {
			LOG_ERR(TAG "CMD_CHLNG length mismatch! %d/8", len);
			break;
		}
		osdp_sc_init(pd);
		CLEAR_FLAG(pd, PD_FLAG_SC_ACTIVE);
		for (i = 0; i < 8; i++) {
			pd->sc.cp_random[i] = buf[pos++];
		}
		pd->reply_id = REPLY_CCRYPT;
		ret = 0;
		break;
	case CMD_SCRYPT:
		if (len != CMD_SCRYPT_DATA_LEN) {
			LOG_ERR(TAG "CMD_SCRYPT length mismatch! %d/16", len);
			break;
		}
		for (i = 0; i < 16; i++) {
			pd->sc.cp_cryptogram[i] = buf[pos++];
		}
		pd->reply_id = REPLY_RMAC_I;
		ret = 0;
		break;
#endif /* CONFIG_OSDP_SC_ENABLED */
	default:
		pd->reply_id = REPLY_NAK;
		pd->ephemeral_data[0] = OSDP_PD_NAK_CMD_UNKNOWN;
		ret = 0;
		break;
	}

	if (ret != 0) {
		LOG_ERR(TAG "Invalid command structure. CMD: %02x, Len: %d",
			pd->cmd_id, len);
		pd->reply_id = REPLY_NAK;
		pd->ephemeral_data[0] = OSDP_PD_NAK_CMD_LEN;
	}

	if (pd->cmd_id != CMD_POLL) {
		LOG_DBG(TAG "CMD: %02x REPLY: %02x", pd->cmd_id, pd->reply_id);
	}
}

/**
 * Returns:
 * +ve: length of command
 * -ve: error
 */
static int pd_build_reply(struct osdp_pd *pd, uint8_t *buf, int max_len)
{
	int i, data_off, len = 0, ret = -1;
	uint8_t t1;
	struct osdp_event *event;
	struct osdp_cmd *cmd;

	data_off = osdp_phy_packet_get_data_offset(pd, buf);
#ifdef CONFIG_OSDP_SC_ENABLED
	uint8_t *smb = osdp_phy_packet_get_smb(pd, buf);
#endif
	buf += data_off;
	max_len -= data_off;
	if (max_len <= 0) {
		LOG_ERR(TAG "Out of buffer space!");
		return -1;
	}

	switch (pd->reply_id) {
	case REPLY_ACK:
		if (max_len < REPLY_ACK_LEN) {
			LOG_ERR(TAG "Out of buffer space!");
			break;
		}
		buf[len++] = pd->reply_id;
		ret = 0;
		break;
	case REPLY_PDID:
		if (max_len < REPLY_PDID_LEN) {
			LOG_ERR(TAG "Out of buffer space!");
			break;
		}
		buf[len++] = pd->reply_id;

		buf[len++] = BYTE_0(pd->id.vendor_code);
		buf[len++] = BYTE_1(pd->id.vendor_code);
		buf[len++] = BYTE_2(pd->id.vendor_code);

		buf[len++] = pd->id.model;
		buf[len++] = pd->id.version;

		buf[len++] = BYTE_0(pd->id.serial_number);
		buf[len++] = BYTE_1(pd->id.serial_number);
		buf[len++] = BYTE_2(pd->id.serial_number);
		buf[len++] = BYTE_3(pd->id.serial_number);

		buf[len++] = BYTE_3(pd->id.firmware_version);
		buf[len++] = BYTE_2(pd->id.firmware_version);
		buf[len++] = BYTE_1(pd->id.firmware_version);
		ret = 0;
		break;
	case REPLY_PDCAP:
		if (max_len < REPLY_PDCAP_LEN) {
			LOG_ERR(TAG "Out of buffer space!");
			break;
		}
		buf[len++] = pd->reply_id;
		for (i = 0; i < OSDP_PD_CAP_SENTINEL; i++) {
			if (pd->cap[i].function_code != i) {
				continue;
			}
			if (max_len < REPLY_PDCAP_ENTITY_LEN) {
				LOG_ERR(TAG "Out of buffer space!");
				break;
			}
			buf[len++] = i;
			buf[len++] = pd->cap[i].compliance_level;
			buf[len++] = pd->cap[i].num_items;
			max_len -= REPLY_PDCAP_ENTITY_LEN;
		}
		ret = 0;
		break;
	case REPLY_LSTATR:
		if (max_len < REPLY_LSTATR_LEN) {
			LOG_ERR(TAG "Out of buffer space!");
			break;
		}
		buf[len++] = pd->reply_id;
		buf[len++] = ISSET_FLAG(pd, PD_FLAG_TAMPER);
		buf[len++] = ISSET_FLAG(pd, PD_FLAG_POWER);
		ret = 0;
		break;
	case REPLY_RSTATR:
		if (max_len < REPLY_RSTATR_LEN) {
			LOG_ERR(TAG "Out of buffer space!");
			break;
		}
		buf[len++] = pd->reply_id;
		buf[len++] = ISSET_FLAG(pd, PD_FLAG_R_TAMPER);
		ret = 0;
		break;
	case REPLY_KEYPPAD:
		event = (struct osdp_event *)pd->ephemeral_data;
		if (max_len < (REPLY_KEYPAD_LEN + event->keypress.length)) {
			LOG_ERR(TAG "Out of buffer space!");
			break;
		}
		buf[len++] = pd->reply_id;
		buf[len++] = (uint8_t)event->keypress.reader_no;
		buf[len++] = (uint8_t)event->keypress.length;
		for (i = 0; i < event->keypress.length; i++) {
			buf[len++] = event->keypress.data[i];
		}
		ret = 0;
		break;
	case REPLY_RAW:
		event = (struct osdp_event *)pd->ephemeral_data;
		t1 = (event->cardread.length + 7) / 8;
		if (max_len < REPLY_RAW_LEN + t1) {
			LOG_ERR(TAG "Out of buffer space!");
			break;
		}
		buf[len++] = pd->reply_id;
		buf[len++] = (uint8_t)event->cardread.reader_no;
		buf[len++] = (uint8_t)event->cardread.format;
		buf[len++] = BYTE_0(event->cardread.length);
		buf[len++] = BYTE_1(event->cardread.reader_no);
		for (i = 0; i < t1; i++) {
			buf[len++] = event->cardread.data[i];
		}
		ret = 0;
		break;
	case REPLY_FMT:
		event = (struct osdp_event *)pd->ephemeral_data;
		if (max_len < REPLY_FMT_LEN + event->cardread.length) {
			LOG_ERR(TAG "Out of buffer space!");
			break;
		}
		buf[len++] = pd->reply_id;
		buf[len++] = (uint8_t)event->cardread.reader_no;
		buf[len++] = (uint8_t)event->cardread.direction;
		buf[len++] = (uint8_t)event->cardread.length;
		for (i = 0; i < event->cardread.length; i++) {
			buf[len++] = event->cardread.data[i];
		}
		ret = 0;
		break;
	case REPLY_COM:
		if (max_len < REPLY_COM_LEN) {
			LOG_ERR(TAG "Out of buffer space!");
			break;
		}
		/**
		 * If COMSET succeeds, the PD must reply with the old params and
		 * then switch to the new params from then then on. We have the
		 * new params in the commands struct that we just enqueued so
		 * we can peek at tail of command queue and set that to
		 * pd->addr/pd->baud_rate.
		 */
		cmd = (struct osdp_cmd *)pd->ephemeral_data;
		buf[len++] = pd->reply_id;
		buf[len++] = cmd->comset.address;
		buf[len++] = BYTE_0(cmd->comset.baud_rate);
		buf[len++] = BYTE_1(cmd->comset.baud_rate);
		buf[len++] = BYTE_2(cmd->comset.baud_rate);
		buf[len++] = BYTE_3(cmd->comset.baud_rate);

		pd->address = (int)cmd->comset.address;
		pd->baud_rate = (int)cmd->comset.baud_rate;
		LOG_INF("COMSET Succeeded! New PD-Addr: %d; Baud: %d",
			pd->address, pd->baud_rate);
		ret = 0;
		break;
	case REPLY_NAK:
		if (max_len < REPLY_NAK_LEN) {
			LOG_ERR(TAG "Fatal: insufficent space for sending NAK");
			return -1;
		}
		buf[len++] = pd->reply_id;
		buf[len++] = pd->ephemeral_data[0];
		ret = 0;
		break;
	case REPLY_MFGREP:
		cmd = (struct osdp_cmd *)pd->ephemeral_data;
		if (max_len < (REPLY_MFGREP_LEN + cmd->mfg.length)) {
			LOG_ERR(TAG "Fatal: insufficent space for sending NAK");
			return -1;
		}
		buf[len++] = pd->reply_id;
		buf[len++] = BYTE_0(cmd->mfg.vendor_code);
		buf[len++] = BYTE_1(cmd->mfg.vendor_code);
		buf[len++] = BYTE_2(cmd->mfg.vendor_code);
		buf[len++] = cmd->mfg.command;
		for (i = 0; i < cmd->mfg.length; i++) {
			buf[len++] = cmd->mfg.data[i];
		}
		ret = 0;
		break;
#ifdef CONFIG_OSDP_SC_ENABLED
	case REPLY_CCRYPT:
		if (smb == NULL)
			break;
		if (max_len < REPLY_CCRYPT_LEN) {
			LOG_ERR(TAG "Out of buffer space!");
			return -1;
		}
		osdp_fill_random(pd->sc.pd_random, 8);
		osdp_compute_session_keys(TO_CTX(pd));
		osdp_compute_pd_cryptogram(pd);
		buf[len++] = pd->reply_id;
		for (i = 0; i < 8; i++) {
			buf[len++] = pd->sc.pd_client_uid[i];
		}
		for (i = 0; i < 8; i++) {
			buf[len++] = pd->sc.pd_random[i];
		}
		for (i = 0; i < 16; i++) {
			buf[len++] = pd->sc.pd_cryptogram[i];
		}
		smb[0] = 3;      /* length */
		smb[1] = SCS_12; /* type */
		smb[2] = ISSET_FLAG(pd, PD_FLAG_SC_USE_SCBKD) ? 0 : 1;
		ret = 0;
		break;
	case REPLY_RMAC_I:
		if (smb == NULL)
			break;
		if (max_len < REPLY_RMAC_I_LEN) {
			LOG_ERR(TAG "Out of buffer space!");
			return -1;
		}
		osdp_compute_rmac_i(pd);
		buf[len++] = pd->reply_id;
		for (i = 0; i < 16; i++) {
			buf[len++] = pd->sc.r_mac[i];
		}
		smb[0] = 3;       /* length */
		smb[1] = SCS_14;  /* type */
		if (osdp_verify_cp_cryptogram(pd) == 0) {
			smb[2] = 1;  /* CP auth succeeded */
			SET_FLAG(pd, PD_FLAG_SC_ACTIVE);
			if (ISSET_FLAG(pd, PD_FLAG_SC_USE_SCBKD)) {
				LOG_WRN(TAG "SC Active with SCBK-D");
			} else {
				LOG_INF(TAG "SC Active");
			}
		} else {
			smb[2] = 0;  /* CP auth failed */
			LOG_WRN(TAG "failed to verify CP_crypt");
		}
		ret = 0;
		break;
#endif /* CONFIG_OSDP_SC_ENABLED */
	}

#ifdef CONFIG_OSDP_SC_ENABLED
	if (smb && (smb[1] > SCS_14) && ISSET_FLAG(pd, PD_FLAG_SC_ACTIVE)) {
		smb[0] = 2; /* length */
		smb[1] = (len > 1) ? SCS_18 : SCS_16;
	}
#endif /* CONFIG_OSDP_SC_ENABLED */

	if (ret != 0) {
		/* catch all errors and report it as a RECORD error to CP */
		LOG_ERR(TAG "ReplyID: $02x unknown or insufficent space or "
			"some other error. Sending NAK");
		if (max_len < REPLY_NAK_LEN) {
			LOG_ERR(TAG "Fatal: insufficent space for sending NAK");
			return -1;
		}
		buf[0] = REPLY_NAK;
		buf[1] = OSDP_PD_NAK_RECORD;
		len = 2;
	}

	return len;
}

/**
 * pd_send_reply - blocking send; doesn't handle partials
 * Returns:
 *   0 - success
 *  -1 - failure
 */
static int pd_send_reply(struct osdp_pd *pd)
{
	int ret, len;

	/* init packet buf with header */
	len = osdp_phy_packet_init(pd, pd->rx_buf, sizeof(pd->rx_buf));
	if (len < 0) {
		return -1;
	}

	/* fill reply data */
	ret = pd_build_reply(pd, pd->rx_buf, sizeof(pd->rx_buf));
	if (ret <= 0) {
		return -1;
	}
	len += ret;

	/* finalize packet */
	len = osdp_phy_packet_finalize(pd, pd->rx_buf, len, sizeof(pd->rx_buf));
	if (len < 0) {
		return -1;
	}

	ret = pd->channel.send(pd->channel.data, pd->rx_buf, len);

	if (IS_ENABLED(CONFIG_OSDP_PACKET_TRACE)) {
		if (pd->cmd_id != CMD_POLL) {
			osdp_dump("PD sent", pd->rx_buf, len);
		}
	}

	return (ret == len) ? 0 : -1;
}

/**
 * pd_receve_packet - received buffer from serial stream handling partials
 * Returns:
 *  0: success
 *  1: no data yet
 * -1: fatal errors
 * -2: phy layer errors that need to reply with NAK
 */
static int pd_receve_packet(struct osdp_pd *pd)
{
	uint8_t *buf;
	int rec_bytes, ret, was_empty, max_len;

	was_empty = pd->rx_buf_len == 0;
	buf = pd->rx_buf + pd->rx_buf_len;
	max_len = sizeof(pd->rx_buf) - pd->rx_buf_len;

	rec_bytes = pd->channel.recv(pd->channel.data, buf, max_len);
	if (rec_bytes <= 0) {
		return 1;
	}
	if (was_empty && rec_bytes > 0) {
		/* Start of message */
		pd->tstamp = osdp_millis_now();
	}
	pd->rx_buf_len += rec_bytes;

	if (IS_ENABLED(CONFIG_OSDP_PACKET_TRACE)) {
		/**
		 * A crude way of identifying and not printing poll messages
		 * when CONFIG_OSDP_PACKET_TRACE is enabled. This is an early
		 * print to catch errors so keeping it simple.
		 */
		if (pd->rx_buf_len > 8 &&
		    pd->rx_buf[6] != CMD_POLL && pd->rx_buf[8] != CMD_POLL) {
			osdp_dump("PD received", pd->rx_buf, pd->rx_buf_len);
		}
	}

	pd->reply_id = 0;    /* reset past reply ID so phy can send NAK */
	pd->ephemeral_data[0] = 0; /* reset past NAK reason */
	ret = osdp_phy_decode_packet(pd, pd->rx_buf, pd->rx_buf_len);
	if (ret == OSDP_ERR_PKT_FMT) {
		if (pd->reply_id != 0) {
			return -2; /* Send a NAK */
		}
		return -1; /* fatal errors */
	} else if (ret == OSDP_ERR_PKT_WAIT) {
		/* rx_buf_len != pkt->len; wait for more data */
		return 1;
	} else if (ret == OSDP_ERR_PKT_SKIP) {
		/* soft fail - discard this message */
		pd->rx_buf_len = 0;
		if (pd->channel.flush) {
			pd->channel.flush(pd->channel.data);
		}
		return 1;
	}
	pd->rx_buf_len = ret;
	return 0;
}

static void osdp_pd_update(struct osdp_pd *pd)
{
	int ret;

	switch (pd->state) {
	case OSDP_PD_STATE_IDLE:
		ret = pd_receve_packet(pd);
		if (ret == 1) {
			break;
		}
		if (ret == -1 || (pd->rx_buf_len > 0 &&
		    osdp_millis_since(pd->tstamp) > OSDP_RESP_TOUT_MS)) {
			/**
			 * When we receive a command from PD after a timeout,
			 * any established secure channel must be discarded.
			 */
			LOG_ERR(TAG "receive errors/timeout");
			pd->state = OSDP_PD_STATE_ERR;
			break;
		}
		if (ret == 0) {
			pd_decode_command(pd, pd->rx_buf, pd->rx_buf_len);
		}
		pd->state = OSDP_PD_STATE_SEND_REPLY;
		/* FALLTHRU */
	case OSDP_PD_STATE_SEND_REPLY:
		if (pd_send_reply(pd) == -1) {
			pd->state = OSDP_PD_STATE_ERR;
			break;
		}
		pd->rx_buf_len = 0;
		pd->state = OSDP_PD_STATE_IDLE;
		break;
	case OSDP_PD_STATE_ERR:
		/**
		 * PD error state is momentary as it doesn't maintain any state
		 * between commands. We just clean up secure channel status and
		 * go back to idle state.
		 */
		CLEAR_FLAG(pd, PD_FLAG_SC_ACTIVE);
		pd->rx_buf_len = 0;
		if (pd->channel.flush) {
			pd->channel.flush(pd->channel.data);
		}
		pd->state = OSDP_PD_STATE_IDLE;
		break;
	}
}

static void osdp_pd_set_attributes(struct osdp_pd *pd, struct osdp_pd_cap *cap,
				   struct osdp_pd_id *id)
{
	int fc;

	while (cap && ((fc = cap->function_code) > 0)) {
		if (fc >= OSDP_PD_CAP_SENTINEL) {
			break;
		}
		pd->cap[fc].function_code = cap->function_code;
		pd->cap[fc].compliance_level = cap->compliance_level;
		pd->cap[fc].num_items = cap->num_items;
		cap++;
	}
	if (id != NULL) {
		memcpy(&pd->id, id, sizeof(struct osdp_pd_id));
	}
}

/* --- Exported Methods --- */

OSDP_EXPORT
osdp_t *osdp_pd_setup(osdp_pd_info_t *info, uint8_t *scbk)
{
	struct osdp_pd *pd;
	struct osdp_cp *cp;
	struct osdp *ctx;

	assert(info);

	ctx = calloc(1, sizeof(struct osdp));
	if (ctx == NULL) {
		LOG_ERR(TAG "failed to alloc struct osdp");
		return NULL;
	}
	ctx->magic = 0xDEADBEAF;

	ctx->cp = calloc(1, sizeof(struct osdp_cp));
	if (ctx->cp == NULL) {
		LOG_ERR(TAG "failed to alloc struct osdp_cp");
		goto error;
	}
	cp = TO_CP(ctx);
	cp->__parent = ctx;
	cp->num_pd = 1;

	ctx->pd = calloc(1, sizeof(struct osdp_pd));
	if (ctx->pd == NULL) {
		LOG_ERR(TAG "failed to alloc struct osdp_pd");
		goto error;
	}
	SET_CURRENT_PD(ctx, 0);
	pd = TO_PD(ctx, 0);

	pd->__parent = ctx;
	pd->offset = 0;
	pd->baud_rate = info->baud_rate;
	pd->address = info->address;
	pd->flags = info->flags;
	pd->seq_number = -1;
	memcpy(&pd->channel, &info->channel, sizeof(struct osdp_channel));

	if (pd_event_queue_init(pd)) {
		goto error;
	}

#ifdef CONFIG_OSDP_SC_ENABLED
	if (scbk == NULL) {
		LOG_WRN(TAG "SCBK not provided. PD is in INSTALL_MODE");
		SET_FLAG(pd, PD_FLAG_INSTALL_MODE);
	}
	else {
		memcpy(pd->sc.scbk, scbk, 16);
	}
	SET_FLAG(pd, PD_FLAG_SC_CAPABLE);
#else
	ARG_UNUSED(scbk);
#endif
	osdp_pd_set_attributes(pd, info->cap, &info->id);
	osdp_pd_set_attributes(pd, osdp_pd_cap, NULL);

	SET_FLAG(pd, PD_FLAG_PD_MODE); /* used in checks in phy */

	LOG_INF(TAG "setup complete");
	return (osdp_t *) ctx;

error:
	osdp_pd_teardown((osdp_t *) ctx);
	return NULL;
}

OSDP_EXPORT
void osdp_pd_teardown(osdp_t *ctx)
{
	assert(ctx);

	pd_event_queue_del(TO_PD(ctx, 0));
	safe_free(TO_PD(ctx, 0));
	safe_free(TO_CP(ctx));
	safe_free(ctx);
}

OSDP_EXPORT
void osdp_pd_refresh(osdp_t *ctx)
{
	assert(ctx);
	struct osdp_pd *pd = GET_CURRENT_PD(ctx);

	osdp_pd_update(pd);
}

OSDP_EXPORT
void osdp_pd_set_command_callback(osdp_t *ctx, pd_commnand_callback_t cb, void *arg)
{
	assert(ctx);
	struct osdp_pd *pd = GET_CURRENT_PD(ctx);

	pd->command_callback_arg = arg;
	pd->command_callback = cb;
}

OSDP_EXPORT
int osdp_pd_notify_event(osdp_t *ctx, struct osdp_event *event)
{
	assert(ctx);
	struct osdp_event *ev;
	struct osdp_pd *pd = GET_CURRENT_PD(ctx);

	ev = pd_event_alloc(pd);
	if (ev == NULL) {
		return -1;
	}

	memcpy(ev, event, sizeof(struct osdp_event));
	pd_event_enqueue(pd, ev);
	return 0;
}

#ifdef UNIT_TESTING

/**
 * Force export some private methods for testing.
 */
void (*test_osdp_pd_update)(struct osdp_pd *pd) = osdp_pd_update;

#endif /* UNIT_TESTING */
