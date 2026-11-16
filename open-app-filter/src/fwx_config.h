
// SPDX-License-Identifier: GPL-2.0-or-later
/* 
 * Copyright(c) 2026 destan19(TT) <www.fanchmwrt.com>  
*/
#ifndef __FWX_CONFIG_H__
#define __FWX_CONFIG_H__
#include <uci.h>

#define MAX_SUPPORT_APP_NUM 1024
#define MAX_CLASS_NAME_LEN 32

#include "fwx_user.h"
extern int g_cur_class_num;
extern int g_app_count;
extern char CLASS_NAME_TABLE[MAX_APP_TYPE][MAX_CLASS_NAME_LEN];

typedef struct app_name_info
{
    int id;
    char name[64];
} app_name_info_t;
void init_app_name_table(void);
void init_app_class_name_table(void);
char *get_app_name_by_id(int id);
int app_icon_exists_by_id(int id);
int get_base_app_count(void);
/* Returns 1 when id is occupied by a built-in feature-library app.
 * Only the base part of app_name_table is considered; custom apps added by
 * fwx_custom_feature_add_app_names() are deliberately excluded so that a
 * reload does not report a conflict with the user's own previous config. */
int is_builtin_appid(int id);

int appfilter_config_alloc(void);

int appfilter_config_free(void);
int config_get_appfilter_enable(void);
int config_get_lan_ip(char *lan_ip, int len);
int config_get_lan_mask(char *lan_mask, int len);
#endif

