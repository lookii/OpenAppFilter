// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright(c) 2026 destan19(TT) <www.fanchmwrt.com>
 *
 * Phase2: user-space URL visit records and per-MAC URL black/white list.
 */
#ifndef __FWX_URL_RECORD_H__
#define __FWX_URL_RECORD_H__

#include <json-c/json.h>

/* storage / collection lifecycle (called from main.c) */
int  fwx_url_record_init(void);
void fwx_url_record_collect(void);
void fwx_url_record_cleanup(void);

/* helper used by fwx_ubus.c when building get_user_basic_info */
int  fwx_url_record_enabled_for_mac(const char *mac);

/* ubus "fwx common" api handlers (registered in fwx_ubus.c) */
struct json_object *fwx_api_get_url_record_settings(struct json_object *req_obj);
struct json_object *fwx_api_set_url_record_settings(struct json_object *req_obj);
struct json_object *fwx_api_get_url_records(struct json_object *req_obj);
struct json_object *fwx_api_get_url_list(struct json_object *req_obj);
struct json_object *fwx_api_add_url_list(struct json_object *req_obj);
struct json_object *fwx_api_del_url_list(struct json_object *req_obj);

#endif
