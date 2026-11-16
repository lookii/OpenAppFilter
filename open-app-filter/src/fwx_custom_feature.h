// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __FWX_CUSTOM_FEATURE_H__
#define __FWX_CUSTOM_FEATURE_H__

#include <stddef.h>
#include <json-c/json.h>

#define FWX_CUSTOM_FEATURE_PATH "/etc/fwxd/custom_feature.cfg"

int fwx_custom_feature_reload(void);
int fwx_custom_feature_send_to_kernel(int (*send_feature)(char *));
int fwx_custom_feature_add_app_names(void);
void fwx_custom_feature_append_class_apps(struct json_object **class_map,
                                          size_t class_map_len);
struct json_object *fwx_api_get_custom_feature(struct json_object *req_obj);
struct json_object *fwx_api_get_custom_feature_class_list(struct json_object *req_obj);
struct json_object *fwx_api_set_custom_feature(struct json_object *req_obj);
/* P1-1: dry-run matcher. Input sample fields (domain/uri/dest_port/protocol/
 * payload) are matched against the saved custom feature set, or against an
 * optional "app_list" submitted in the same shape as set_custom_feature. */
struct json_object *fwx_api_test_custom_feature(struct json_object *req_obj);

#endif
