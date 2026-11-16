
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright(c) 2026 destan19(TT) <www.fanchmwrt.com>
 *
 * OAF 7.1.0 - per-MAC URL rules and URL visit event ring buffer.
 */
#ifndef __FWX_URL_RECORD_H__
#define __FWX_URL_RECORD_H__

#include <linux/types.h>
#include "fwx.h"   /* MAC_ADDR_LEN */

#define AF_URL_LOG_SIZE       512
#define AF_URL_RULE_MAX       256
#define AF_URL_RULE_URL_LEN   64
#define AF_URL_LOG_LINE_LEN   160
#define AF_URL_LOG_URL_LEN    64

/* per-MAC black/white URL rule (contract section 3.1) */
typedef struct {
	unsigned char mac[MAC_ADDR_LEN];
	char url[AF_URL_RULE_URL_LEN];
	unsigned char type;            /* 1 = black, 2 = white */
	unsigned char used;
} af_url_rule_t;

/* one URL visit event, used by the seq_file snapshot (contract section 2) */
typedef struct {
	u64 seq;
	unsigned char mac[MAC_ADDR_LEN];
	unsigned long ts;
	int action;                    /* 0 = normal, 1 = black, 2 = white */
	char url[AF_URL_LOG_URL_LEN];
} af_url_log_line_t;

void af_url_record_init(void);
void af_url_record_exit(void);

int  af_url_match_rule(const unsigned char *mac, const char *url);   /* 0/1/2 */
void af_url_log_event(const unsigned char *mac, const char *url, int action);

int  af_url_rule_add(const char *mac_str, const char *url, const char *type);
int  af_url_rule_del(const char *mac_str, const char *url, const char *type);
void af_url_rule_flush(void);
void af_url_rule_dump(void);
int  af_url_parse_mac(const char *s, unsigned char *out);

/* contract 3.3: format valid ring entries into caller-supplied lines */
int  af_url_log_dump(char (*lines)[AF_URL_LOG_LINE_LEN], int max_lines);
/* memory-safe snapshot used by the af_url_log seq_file */
int  af_url_log_snapshot(af_url_log_line_t *out, int max);

#endif
