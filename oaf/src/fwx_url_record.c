
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright(c) 2026 destan19(TT) <www.fanchmwrt.com>
 *
 * OAF 7.1.0 - per-MAC URL rules and URL visit event ring buffer.
 *
 * af_url_log_event()/af_url_match_rule() run from netfilter hooks (softirq),
 * so every shared-state access is guarded with spin_lock_bh() and the write
 * path performs no sleeping allocation and no heavy formatting.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include "fwx.h"
#include "fwx_utils.h"
#include "fwx_url_record.h"

/* ---- URL visit event ring buffer (contract section 2) ---- */
static DEFINE_SPINLOCK(g_url_log_lock);
static af_url_log_line_t g_url_log[AF_URL_LOG_SIZE];
static unsigned int g_url_log_head;   /* index of the oldest valid entry */
static unsigned int g_url_log_count;  /* number of valid entries */
static u64 g_url_log_seq;             /* global monotonic seq, starts at 1 */

/* ---- per-MAC URL rules (contract section 3.1) ---- */
static DEFINE_SPINLOCK(g_url_rule_lock);
static af_url_rule_t g_url_rules[AF_URL_RULE_MAX];

/* ---- static snapshot backing af_url_log_dump() ---- */
static DEFINE_MUTEX(g_url_log_dump_mutex);
static af_url_log_line_t g_url_log_dump_snap[AF_URL_LOG_SIZE];

static int af_url_hex_val(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* accept aa:bb:cc:dd:ee:ff, upper/lower case; 0 on success */
int af_url_parse_mac(const char *s, unsigned char *out)
{
	int i, hi, lo;

	if (!s || !out)
		return -1;
	if (strnlen(s, 18) < 17)
		return -1;
	for (i = 0; i < MAC_ADDR_LEN; i++) {
		hi = af_url_hex_val(s[0]);
		lo = af_url_hex_val(s[1]);
		if (hi < 0 || lo < 0)
			return -1;
		out[i] = (unsigned char)((hi << 4) | lo);
		if (i + 1 < MAC_ADDR_LEN) {
			if (s[2] != ':')
				return -1;
			s += 3;
		}
	}
	return 0;
}

/* case-insensitive full-string equality (hand-written, no strcasestr) */
static int af_url_iequal(const char *a, const char *b)
{
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return (*a == '\0' && *b == '\0');
}

/* suffix match: url == rule.url or url ends with "." + rule.url */
static int af_url_suffix_match(const char *url, const char *rule)
{
	int ulen, rlen;

	if (!url || !rule)
		return 0;
	ulen = strnlen(url, AF_URL_RULE_URL_LEN);
	rlen = strnlen(rule, AF_URL_RULE_URL_LEN);
	if (rlen == 0 || ulen < rlen)
		return 0;
	if (ulen == rlen)
		return af_url_iequal(url, rule);
	if (url[ulen - rlen - 1] == '.')
		return af_url_iequal(url + ulen - rlen, rule);
	return 0;
}

static int af_url_rule_type(const char *type)
{
	if (!type)
		return 0;
	if (strcmp(type, "black") == 0)
		return 1;
	if (strcmp(type, "white") == 0)
		return 2;
	return 0;
}

static void af_url_normalize_url(char *dst, int dst_len, const char *src)
{
	int i, len = strnlen(src, dst_len - 1);

	for (i = 0; i < len; i++)
		dst[i] = tolower((unsigned char)src[i]);
	dst[len] = '\0';
}

/* return 0 = no match, 1 = black, 2 = white; black takes precedence */
int af_url_match_rule(const unsigned char *mac, const char *url)
{
	int i, ret = 0;

	if (!mac || !url)
		return 0;

	spin_lock_bh(&g_url_rule_lock);
	for (i = 0; i < AF_URL_RULE_MAX; i++) {
		af_url_rule_t *r = &g_url_rules[i];

		if (!r->used)
			continue;
		if (memcmp(r->mac, mac, MAC_ADDR_LEN) != 0)
			continue;
		if (!af_url_suffix_match(url, r->url))
			continue;
		if (r->type == 1) {
			ret = 1;       /* black wins, stop immediately */
			break;
		}
		if (r->type == 2)
			ret = 2;       /* white, keep scanning for a black hit */
	}
	spin_unlock_bh(&g_url_rule_lock);
	return ret;
}

/* softirq-safe: no sleeping allocation, no heavy formatting under the lock */
void af_url_log_event(const unsigned char *mac, const char *url, int action)
{
	char url_buf[AF_URL_LOG_URL_LEN];
	unsigned long now;
	int i, idx, count, url_len;
	af_url_log_line_t *e;

	if (!mac || !url)
		return;

	url_len = strnlen(url, sizeof(url_buf) - 1);
	if (url_len == 0)
		return;
	memcpy(url_buf, url, url_len);
	url_buf[url_len] = '\0';

	now = (unsigned long)af_get_timestamp_sec();

	spin_lock_bh(&g_url_log_lock);

	/* 5-second de-duplication: linear scan over the 512-entry ring */
	count = g_url_log_count;
	for (i = 0; i < count; i++) {
		idx = (g_url_log_head + i) % AF_URL_LOG_SIZE;
		e = &g_url_log[idx];
		if (now >= e->ts && (now - e->ts) < 5 &&
		    memcmp(e->mac, mac, MAC_ADDR_LEN) == 0 &&
		    af_url_iequal(e->url, url_buf)) {
			spin_unlock_bh(&g_url_log_lock);
			return;
		}
	}

	/* append, or overwrite the oldest entry when full */
	if (g_url_log_count < AF_URL_LOG_SIZE) {
		idx = (g_url_log_head + g_url_log_count) % AF_URL_LOG_SIZE;
		g_url_log_count++;
	} else {
		idx = g_url_log_head;
		g_url_log_head = (g_url_log_head + 1) % AF_URL_LOG_SIZE;
	}

	e = &g_url_log[idx];
	e->seq = ++g_url_log_seq;
	memcpy(e->mac, mac, MAC_ADDR_LEN);
	e->ts = now;
	e->action = action;
	memcpy(e->url, url_buf, url_len);
	e->url[url_len] = '\0';

	spin_unlock_bh(&g_url_log_lock);
}

/* copy valid entries (oldest first) into out; returns the count */
int af_url_log_snapshot(af_url_log_line_t *out, int max)
{
	int i, idx, n = 0, count;

	if (!out || max <= 0)
		return 0;
	if (max > AF_URL_LOG_SIZE)
		max = AF_URL_LOG_SIZE;

	spin_lock_bh(&g_url_log_lock);
	count = g_url_log_count;
	if (count > max)
		count = max;
	for (i = 0; i < count; i++) {
		idx = (g_url_log_head + i) % AF_URL_LOG_SIZE;
		out[n++] = g_url_log[idx];
	}
	spin_unlock_bh(&g_url_log_lock);
	return n;
}

/* contract 3.3: format each valid entry as "Seq Mac Timestamp Action Url" */
int af_url_log_dump(char (*lines)[AF_URL_LOG_LINE_LEN], int max_lines)
{
	int i, n;

	if (!lines || max_lines <= 0)
		return 0;
	if (max_lines > AF_URL_LOG_SIZE)
		max_lines = AF_URL_LOG_SIZE;

	mutex_lock(&g_url_log_dump_mutex);
	n = af_url_log_snapshot(g_url_log_dump_snap, max_lines);
	for (i = 0; i < n; i++) {
		af_url_log_line_t *e = &g_url_log_dump_snap[i];

		snprintf(lines[i], AF_URL_LOG_LINE_LEN,
			 "%llu " MAC_FMT " %lu %d %s",
			 (unsigned long long)e->seq, MAC_ARRAY(e->mac),
			 e->ts, e->action, e->url);
	}
	mutex_unlock(&g_url_log_dump_mutex);
	return n;
}

int af_url_rule_add(const char *mac_str, const char *url, const char *type)
{
	unsigned char mac[MAC_ADDR_LEN];
	char url_buf[AF_URL_RULE_URL_LEN];
	unsigned char t;
	int i, slot = -1, ret = -1;

	if (!mac_str || !url || !type)
		return -1;
	if (af_url_parse_mac(mac_str, mac) != 0)
		return -1;
	if (strnlen(url, AF_URL_RULE_URL_LEN) >= AF_URL_RULE_URL_LEN)
		return -1;                      /* url must be <= 63 bytes */
	t = (unsigned char)af_url_rule_type(type);
	if (t == 0)
		return -1;
	af_url_normalize_url(url_buf, sizeof(url_buf), url);

	spin_lock_bh(&g_url_rule_lock);
	for (i = 0; i < AF_URL_RULE_MAX; i++) {
		af_url_rule_t *r = &g_url_rules[i];

		if (r->used && memcmp(r->mac, mac, MAC_ADDR_LEN) == 0 &&
		    r->type == t &&
		    strncmp(r->url, url_buf, AF_URL_RULE_URL_LEN) == 0) {
			spin_unlock_bh(&g_url_rule_lock);
			return 0;               /* idempotent */
		}
	}
	for (i = 0; i < AF_URL_RULE_MAX; i++) {
		if (!g_url_rules[i].used) {
			slot = i;
			break;
		}
	}
	if (slot >= 0) {
		af_url_rule_t *r = &g_url_rules[slot];

		memcpy(r->mac, mac, MAC_ADDR_LEN);
		memcpy(r->url, url_buf, sizeof(r->url));
		r->type = t;
		r->used = 1;
		ret = 0;
	}
	spin_unlock_bh(&g_url_rule_lock);
	return ret;
}

int af_url_rule_del(const char *mac_str, const char *url, const char *type)
{
	unsigned char mac[MAC_ADDR_LEN];
	char url_buf[AF_URL_RULE_URL_LEN];
	unsigned char t;
	int i, ret = -1;

	if (!mac_str || !url || !type)
		return -1;
	if (af_url_parse_mac(mac_str, mac) != 0)
		return -1;
	if (strnlen(url, AF_URL_RULE_URL_LEN) >= AF_URL_RULE_URL_LEN)
		return -1;
	t = (unsigned char)af_url_rule_type(type);
	if (t == 0)
		return -1;
	af_url_normalize_url(url_buf, sizeof(url_buf), url);

	spin_lock_bh(&g_url_rule_lock);
	for (i = 0; i < AF_URL_RULE_MAX; i++) {
		af_url_rule_t *r = &g_url_rules[i];

		if (r->used && memcmp(r->mac, mac, MAC_ADDR_LEN) == 0 &&
		    r->type == t &&
		    strncmp(r->url, url_buf, AF_URL_RULE_URL_LEN) == 0) {
			r->used = 0;
			ret = 0;
			break;
		}
	}
	spin_unlock_bh(&g_url_rule_lock);
	return ret;
}

void af_url_rule_flush(void)
{
	int i;

	spin_lock_bh(&g_url_rule_lock);
	for (i = 0; i < AF_URL_RULE_MAX; i++)
		g_url_rules[i].used = 0;
	spin_unlock_bh(&g_url_rule_lock);
}

void af_url_rule_dump(void)
{
	int i, n = 0;

	spin_lock_bh(&g_url_rule_lock);
	printk("af_url_rule_list:\n");
	for (i = 0; i < AF_URL_RULE_MAX; i++) {
		af_url_rule_t *r = &g_url_rules[i];

		if (!r->used)
			continue;
		n++;
		printk("  " MAC_FMT " %s %s\n", MAC_ARRAY(r->mac), r->url,
		       r->type == 1 ? "black" : "white");
	}
	printk("af_url_rule_list: total %d\n", n);
	spin_unlock_bh(&g_url_rule_lock);
}

void af_url_record_init(void)
{
	spin_lock_bh(&g_url_log_lock);
	memset(g_url_log, 0, sizeof(g_url_log));
	g_url_log_head = 0;
	g_url_log_count = 0;
	g_url_log_seq = 0;
	spin_unlock_bh(&g_url_log_lock);

	spin_lock_bh(&g_url_rule_lock);
	memset(g_url_rules, 0, sizeof(g_url_rules));
	spin_unlock_bh(&g_url_rule_lock);
}

void af_url_record_exit(void)
{
	af_url_rule_flush();

	spin_lock_bh(&g_url_log_lock);
	memset(g_url_log, 0, sizeof(g_url_log));
	g_url_log_head = 0;
	g_url_log_count = 0;
	g_url_log_seq = 0;
	spin_unlock_bh(&g_url_log_lock);
}
