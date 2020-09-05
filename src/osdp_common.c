/*
 * Copyright (c) 2019 Siddharth Chandrasekaran <siddharth@embedjournal.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _GNU_SOURCE		/* See feature_test_macros(7) */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>

#include "osdp_common.h"

#define LOG_CTX_GLOBAL -153

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "0.0.0"
#endif

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"

const char *log_level_colors[LOG_MAX_LEVEL] = {
	RED,   RED,   RED,   RED,
	YEL,   MAG,   GRN,   GRN
};

const char *log_level_names[LOG_MAX_LEVEL] = {
	"EMERG", "ALERT", "CRIT ", "ERROR",
	"WARN ", "NOTIC", "INFO ", "DEBUG"
};

int g_log_level = LOG_WARNING;	/* Note: log level is not contextual */
int g_log_ctx = LOG_CTX_GLOBAL;
int g_old_log_ctx = LOG_CTX_GLOBAL;
int (*log_printf)(const char *fmt, ...) = printf;

void osdp_log_set_color(const char *color)
{
	int ret, len;

	len = strnlen(color, 8);
	if (isatty(fileno(stdout))) {
		ret = write(fileno(stdout), color, len);
		assert(ret == len);
		ARG_UNUSED(ret); /* squash warning in Release builds */
	}
}

OSDP_EXPORT
void osdp_logger_init(int log_level, int (*log_fn)(const char *fmt, ...))
{
	g_log_level = log_level;
	if (log_fn != NULL) {
		log_printf = log_fn;
	}
}

void osdp_log_ctx_set(int log_ctx)
{
	g_old_log_ctx = g_log_ctx;
	g_log_ctx = log_ctx;
}

void osdp_log_ctx_reset()
{
	g_old_log_ctx = g_log_ctx;
	g_log_ctx = LOG_CTX_GLOBAL;
}

void osdp_log_ctx_restore()
{
	g_log_ctx = g_old_log_ctx;
}

void osdp_log(int log_level, const char *fmt, ...)
{
	va_list args;
	char *buf;

	if (log_level < LOG_EMERG || log_level >= LOG_MAX_LEVEL) {
		return;
	}
	if (log_level > g_log_level) {
		return;
	}
	va_start(args, fmt);
	if (vasprintf(&buf, fmt, args) == -1) {
		log_printf("vasprintf error\n");
		return;
	}
	va_end(args);
	osdp_log_set_color(log_level_colors[log_level]);
	if (g_log_ctx == LOG_CTX_GLOBAL) {
		log_printf("OSDP: %s: %s\n", log_level_names[log_level], buf);
	} else {
		log_printf("OSDP: %s: PD[%d] %s\n", log_level_names[log_level],
			   g_log_ctx, buf);
	}
	osdp_log_set_color(RESET);
	free(buf);
}

void osdp_dump(const char *head, uint8_t *buf, int len)
{
	hexdump(head, buf, len);
}

uint16_t crc16_itu_t(uint16_t seed, const uint8_t * src, size_t len)
{
	for (; len > 0; len--) {
		seed = (seed >> 8U) | (seed << 8U);
		seed ^= *src++;
		seed ^= (seed & 0xffU) >> 4U;
		seed ^= seed << 12U;
		seed ^= (seed & 0xffU) << 5U;
	}
	return seed;
}

uint16_t osdp_compute_crc16(const uint8_t *buf, size_t len)
{
	return crc16_itu_t(0x1D0F, buf, len);
}

int64_t osdp_millis_now()
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (int64_t) ((tv.tv_sec) * 1000L + (tv.tv_usec) / 1000L);
}

int64_t osdp_millis_since(int64_t last)
{
	return osdp_millis_now() - last;
}

#ifdef CONFIG_OSDP_SC_ENABLED
#include "osdp_aes.h"

void osdp_encrypt(uint8_t *key, uint8_t *iv, uint8_t *data, int len)
{
	struct AES_ctx aes_ctx;

	if (iv != NULL) {
		/* encrypt multiple block with AES in CBC mode */
		AES_init_ctx_iv(&aes_ctx, key, iv);
		AES_CBC_encrypt_buffer(&aes_ctx, data, len);
	} else {
		/* encrypt one block with AES in ECB mode */
		assert(len <= 16);
		AES_init_ctx(&aes_ctx, key);
		AES_ECB_encrypt(&aes_ctx, data);
	}
}

void osdp_decrypt(uint8_t *key, uint8_t *iv, uint8_t *data, int len)
{
	struct AES_ctx aes_ctx;

	if (iv != NULL) {
		/* decrypt multiple block with AES in CBC mode */
		AES_init_ctx_iv(&aes_ctx, key, iv);
		AES_CBC_decrypt_buffer(&aes_ctx, data, len);
	} else {
		/* decrypt one block with AES in ECB mode */
		assert(len <= 16);
		AES_init_ctx(&aes_ctx, key);
		AES_ECB_decrypt(&aes_ctx, data);
	}
}

void osdp_fill_random(uint8_t *buf, int len)
{
	int i, rnd;

	for (i = 0; i < len; i++) {
		rnd = rand();
		buf[i] = (uint8_t)(((float)rnd) / RAND_MAX * 256);
	}
}
#endif /* CONFIG_OSDP_SC_ENABLED */

/* --- Exported Methods --- */

OSDP_EXPORT
const char *osdp_get_version()
{
	return PROJECT_NAME "-" PROJECT_VERSION;
}

OSDP_EXPORT
const char *osdp_get_source_info()
{
	if (strnlen(GIT_TAG, 8) > 0) {
		return GIT_BRANCH " (" GIT_TAG ")";
	} else if (strnlen(GIT_REV, 8) > 0) {
		return GIT_BRANCH " (" GIT_REV GIT_DIFF ")";
	} else {
		return GIT_BRANCH;
	}
}

OSDP_EXPORT
uint32_t osdp_get_sc_status_mask(osdp_t *ctx)
{
	int i;
	uint32_t mask = 0;
	struct osdp_pd *pd;

	assert(ctx);

	if (ISSET_FLAG(TO_OSDP(ctx), FLAG_CP_MODE)) {
		for (i = 0; i < NUM_PD(ctx); i++) {
			pd = TO_PD(ctx, i);
			if (pd->state == OSDP_CP_STATE_ONLINE &&
			    ISSET_FLAG(pd, PD_FLAG_SC_ACTIVE)) {
				mask |= 1 << i;
			}
		}
	} else {
		pd = TO_PD(ctx, 0);
		if (ISSET_FLAG(pd, PD_FLAG_SC_ACTIVE)) {
			mask = 1;
		}
	}

	return mask;
}

OSDP_EXPORT
uint32_t osdp_get_status_mask(osdp_t *ctx)
{
	int i;
	uint32_t mask = 0;
	struct osdp_pd *pd;

	assert(ctx);

	if (ISSET_FLAG(TO_OSDP(ctx), FLAG_CP_MODE)) {
		for (i = 0; i < TO_OSDP(ctx)->cp->num_pd; i++) {
			pd = TO_PD(ctx, i);
			if (pd->state == OSDP_CP_STATE_ONLINE) {
				mask |= 1 << i;
			}
		}
	} else {
		/* PD is stateless */
		mask = 1;
	}

	return mask;
}
