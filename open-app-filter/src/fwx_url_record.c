// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright(c) 2026 destan19(TT) <www.fanchmwrt.com>
 *
 * Phase2: user-space URL visit records and per-MAC URL black/white list.
 *
 *  - sqlite table url_visit_record lives in the same client.db used by
 *    app_visit_record (path = get_history_data_root_dir() + "/client.db").
 *  - /proc/net/af_url_log is polled every 2s; a monotonic Seq is used for
 *    idempotent collection (no read-to-clear in the kernel).
 *  - per-MAC rules are stored in /etc/config/urlfilter (config rule sections)
 *    and pushed to the kernel through /dev/fwx.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <uci.h>
#include <json-c/json.h>

#include "fwx.h"          /* LOG_*, API_CODE_*, fwx_gen_api_response_data, json-c, uci.h */
#include "fwx_user.h"     /* get_history_data_root_dir */
#include "fwx_uci.h"
#include "fwx_utils.h"    /* fwx_send_msg_to_kernel */
#include "fwx_url_record.h"

#define URLFILTER_PACKAGE   "urlfilter"
#define URLFILTER_FILE      "/etc/config/urlfilter"
#define URLFILTER_RULE_TYPE "rule"
#define URL_LOG_PROC        "/proc/net/af_url_log"
#define URL_DEDUP_WINDOW    5
#define URL_MAC_MAX         64
#define URL_MAC_LEN         32
#define URL_URL_LEN         64

#define URL_RECORD_INIT_SQL \
    "CREATE TABLE IF NOT EXISTS url_visit_record (" \
    "id INTEGER PRIMARY KEY AUTOINCREMENT," \
    "mac TEXT NOT NULL," \
    "ts INTEGER NOT NULL," \
    "url TEXT NOT NULL," \
    "action INTEGER NOT NULL DEFAULT 0);" \
    "CREATE INDEX IF NOT EXISTS idx_url_visit_mac_ts ON url_visit_record(mac, ts);"

typedef struct {
    int enabled;
    int days;
    int mac_count;
    char macs[URL_MAC_MAX][URL_MAC_LEN];
} url_settings_t;

/* ------------------------------ small helpers ------------------------------ */

static void normalize_lower(char *s)
{
    for (; s && *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

/* Create the history directory tree (mkdir -p equivalent, no shell). */
static void url_record_ensure_dir(const char *path)
{
    char tmp[512];
    size_t i, len;

    if (!path || !path[0])
        return;
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    for (i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* The package ships an empty urlfilter.config; make sure the file exists so
 * the first uci_add_section() has something to load. */
static void ensure_urlfilter_file(void)
{
    FILE *fp;

    if (access(URLFILTER_FILE, F_OK) == 0)
        return;
    fp = fopen(URLFILTER_FILE, "a");
    if (fp)
        fclose(fp);
}

static void ensure_url_record_section(struct uci_context *ctx)
{
    char section_type[32] = {0};

    if (fwx_uci_get_value(ctx, "fwx.url_record", section_type,
                          sizeof(section_type)) != 0)
        fwx_uci_set_value(ctx, "fwx.url_record", "url_record");
}

/* Read `fwx.url_record` settings. All fwx_uci_get_* helpers are called first
 * (each does its own lookup + unload); uci_load() only happens afterwards, so
 * no uci_package pointer is held across a helper call. */
static void load_settings(struct uci_context *ctx, url_settings_t *st)
{
    struct uci_package *pkg = NULL;
    struct uci_element *e;

    memset(st, 0, sizeof(*st));
    st->days = 7;
    if (!ctx)
        return;

    st->enabled = (fwx_uci_get_int_value(ctx, "fwx.url_record.enabled") == 1) ? 1 : 0;
    st->days = fwx_uci_get_int_value(ctx, "fwx.url_record.days");
    if (st->days < 1 || st->days > 365)
        st->days = 7;

    if (uci_load(ctx, "fwx", &pkg) != UCI_OK || !pkg) {
        if (pkg)
            uci_unload(ctx, pkg);
        return;
    }
    uci_foreach_element(&pkg->sections, e) {
        struct uci_section *s = uci_to_section(e);
        struct uci_option *o;

        if (strcmp(s->type, "url_record") != 0 &&
            strcmp(s->e.name, "url_record") != 0)
            continue;
        o = uci_lookup_option(ctx, s, "mac");
        if (o && o->type == UCI_TYPE_LIST) {
            struct uci_element *me;
            uci_foreach_element(&o->v.list, me) {
                if (!me->name || !me->name[0])
                    continue;
                if (st->mac_count >= URL_MAC_MAX)
                    break;
                snprintf(st->macs[st->mac_count], URL_MAC_LEN, "%s", me->name);
                st->mac_count++;
            }
        }
        break;
    }
    uci_unload(ctx, pkg);
}

static int mac_in_list(const url_settings_t *st, const char *mac)
{
    int i;

    if (!st || !mac || !mac[0])
        return 0;
    for (i = 0; i < st->mac_count; i++) {
        if (strcasecmp(st->macs[i], mac) == 0)
            return 1;
    }
    return 0;
}

int fwx_url_record_enabled_for_mac(const char *mac)
{
    struct uci_context *ctx;
    url_settings_t st;

    if (!mac || !mac[0])
        return 0;
    ctx = uci_alloc_context();
    if (!ctx)
        return 0;
    load_settings(ctx, &st);
    uci_free_context(ctx);
    return (st.enabled && mac_in_list(&st, mac)) ? 1 : 0;
}

/* ------------------------------ sqlite helpers ---------------------------- */

/* Open client.db, create the table/index idempotently. On any failure the
 * handle is closed and *db is reset to NULL. */
static int url_record_open_db(sqlite3 **db)
{
    char db_path[512];
    int rc;

    if (!db)
        return -1;
    *db = NULL;
    url_record_ensure_dir(get_history_data_root_dir());
    snprintf(db_path, sizeof(db_path), "%s/client.db",
             get_history_data_root_dir());

    rc = sqlite3_open(db_path, db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to open url record db: %s (rc:%d)\n", db_path, rc);
        if (*db) {
            sqlite3_close(*db);
            *db = NULL;
        }
        return -1;
    }
    rc = sqlite3_exec(*db, URL_RECORD_INIT_SQL, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to init url_visit_record table (rc:%d)\n", rc);
        sqlite3_close(*db);
        *db = NULL;
        return -1;
    }
    return 0;
}

/* ------------------------------ kernel messages --------------------------- */

/* Build {"api":"...","data":{"mac","url","type"}} with json-c (safe escaping)
 * and write it to /dev/fwx. */
static void url_record_send_kernel(const char *api, const char *mac,
                                   const char *url, const char *type)
{
    struct json_object *root = json_object_new_object();
    struct json_object *data = json_object_new_object();
    const char *text;

    if (!root || !data) {
        if (root)
            json_object_put(root);
        if (data)
            json_object_put(data);
        return;
    }
    json_object_object_add(root, "api", json_object_new_string(api));
    if (mac && url && type) {
        json_object_object_add(data, "mac", json_object_new_string(mac));
        json_object_object_add(data, "url", json_object_new_string(url));
        json_object_object_add(data, "type", json_object_new_string(type));
    }
    json_object_object_add(root, "data", data);
    text = json_object_to_json_string(root);
    if (text)
        fwx_send_msg_to_kernel((char *)text);
    json_object_put(root);
}

/* ------------------------------ urlfilter rules --------------------------- */

typedef void (*url_rule_visitor)(int index, const char *mac, const char *url,
                                 const char *type, void *arg);

/* Iterate every `config rule` section of /etc/config/urlfilter with libuci.
 * index counts only rule sections, matching the @rule[N] UCI syntax. */
static void urlfilter_foreach(struct uci_context *ctx,
                              url_rule_visitor visit, void *arg)
{
    struct uci_package *pkg = NULL;
    struct uci_element *e;
    int index = 0;

    if (!ctx || !visit)
        return;
    if (uci_load(ctx, URLFILTER_PACKAGE, &pkg) != UCI_OK || !pkg) {
        if (pkg)
            uci_unload(ctx, pkg);
        return;
    }
    uci_foreach_element(&pkg->sections, e) {
        struct uci_section *s = uci_to_section(e);
        struct uci_option *o;
        const char *mac = NULL, *url = NULL, *type = NULL;

        if (strcmp(s->type, URLFILTER_RULE_TYPE) != 0)
            continue;
        o = uci_lookup_option(ctx, s, "mac");
        if (o && o->type == UCI_TYPE_STRING)
            mac = o->v.string;
        o = uci_lookup_option(ctx, s, "url");
        if (o && o->type == UCI_TYPE_STRING)
            url = o->v.string;
        o = uci_lookup_option(ctx, s, "type");
        if (o && o->type == UCI_TYPE_STRING)
            type = o->v.string;
        visit(index, mac ? mac : "", url ? url : "", type ? type : "", arg);
        index++;
    }
    uci_unload(ctx, pkg);
}

typedef struct {
    const char *mac;
    const char *url;
    const char *type;
    int found;
    int index;
} find_ctx_t;

static void find_visitor(int index, const char *mac, const char *url,
                         const char *type, void *arg)
{
    find_ctx_t *f = (find_ctx_t *)arg;

    if (!f->found && strcasecmp(mac, f->mac) == 0 &&
        strcasecmp(url, f->url) == 0 && strcasecmp(type, f->type) == 0) {
        f->found = 1;
        f->index = index;
    }
}

#define URL_CONFLICT_MAX 8

typedef struct {
    const char *mac;
    const char *url;
    const char *want_type;
    int same_index;
    int other_index[URL_CONFLICT_MAX];
    char other_type[URL_CONFLICT_MAX][16];
    int other_count;
} conflict_ctx_t;

/* Collect every entry of the same device + domain. The caller keeps the entry
 * matching want_type and drops the rest, so a domain can never be present in
 * the blacklist and the whitelist at the same time. */
static void conflict_visitor(int index, const char *mac, const char *url,
                             const char *type, void *arg)
{
    conflict_ctx_t *c = (conflict_ctx_t *)arg;

    if (strcasecmp(mac, c->mac) != 0 || strcasecmp(url, c->url) != 0)
        return;
    if (strcasecmp(type, c->want_type) == 0) {
        if (c->same_index < 0)
            c->same_index = index;
        return;
    }
    if (c->other_count >= URL_CONFLICT_MAX)
        return;
    c->other_index[c->other_count] = index;
    snprintf(c->other_type[c->other_count], sizeof(c->other_type[0]), "%s", type);
    c->other_count++;
}

typedef struct {
    const char *mac;
    struct json_object *list;
} getlist_ctx_t;

static void getlist_visitor(int index, const char *mac, const char *url,
                            const char *type, void *arg)
{
    getlist_ctx_t *g = (getlist_ctx_t *)arg;
    struct json_object *item;

    (void)index;
    if (strcasecmp(mac, g->mac) != 0 || !url[0] || !type[0])
        return;
    item = json_object_new_object();
    json_object_object_add(item, "url", json_object_new_string(url));
    json_object_object_add(item, "type", json_object_new_string(type));
    json_object_array_add(g->list, item);
}

static void resync_visitor(int index, const char *mac, const char *url,
                           const char *type, void *arg)
{
    (void)index;
    (void)arg;
    if (mac[0] && url[0] && type[0])
        url_record_send_kernel("add_url_list", mac, url, type);
}

/* After oafd (re)starts the kernel rule table is empty: flush then replay the
 * full /etc/config/urlfilter list. Also used after settings changes. */
static void resync_all_rules_to_kernel(void)
{
    struct uci_context *ctx;

    url_record_send_kernel("flush_url_list", NULL, NULL, NULL);
    ctx = uci_alloc_context();
    if (!ctx)
        return;
    urlfilter_foreach(ctx, resync_visitor, NULL);
    uci_free_context(ctx);
}

static int parse_rule_fields(struct json_object *req_obj,
                             char *mac, size_t mac_len,
                             char *url, size_t url_len,
                             char *type, size_t type_len)
{
    struct json_object *o;
    const char *s;

    if (!req_obj)
        return -1;
    o = json_object_object_get(req_obj, "mac");
    s = o ? json_object_get_string(o) : NULL;
    if (!s || !s[0] || strlen(s) >= mac_len)
        return -1;
    snprintf(mac, mac_len, "%s", s);
    normalize_lower(mac);

    o = json_object_object_get(req_obj, "url");
    s = o ? json_object_get_string(o) : NULL;
    if (!s || !s[0] || strlen(s) >= url_len || strlen(s) > 63 ||
        strchr(s, ' ') != NULL)
        return -1;
    snprintf(url, url_len, "%s", s);
    normalize_lower(url);

    o = json_object_object_get(req_obj, "type");
    s = o ? json_object_get_string(o) : NULL;
    if (!s || !s[0] || strlen(s) >= type_len)
        return -1;
    snprintf(type, type_len, "%s", s);
    normalize_lower(type);
    if (strcmp(type, "black") != 0 && strcmp(type, "white") != 0)
        return -1;
    return 0;
}

/* ------------------------------ lifecycle --------------------------------- */

int fwx_url_record_init(void)
{
    sqlite3 *db = NULL;

    ensure_urlfilter_file();
    if (url_record_open_db(&db) != 0)
        return -1;
    sqlite3_close(db);
    resync_all_rules_to_kernel();
    LOG_INFO("url record init done\n");
    return 0;
}

void fwx_url_record_collect(void)
{
    static unsigned long long last_seq = 0;
    FILE *fp;
    char line[256];
    char mac[URL_MAC_LEN];
    char url[URL_URL_LEN];
    unsigned long long seq, max_seq;
    unsigned long ts;
    int action;
    int header_skipped = 0;
    url_settings_t st;
    struct uci_context *ctx;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;

    fp = fopen(URL_LOG_PROC, "r");
    if (!fp)
        return;

    memset(&st, 0, sizeof(st));
    st.days = 7;
    ctx = uci_alloc_context();
    if (ctx) {
        load_settings(ctx, &st);
        uci_free_context(ctx);
    }

    max_seq = last_seq;
    while (fgets(line, sizeof(line), fp)) {
        if (!header_skipped) {          /* first line is the header */
            header_skipped = 1;
            continue;
        }
        mac[0] = '\0';
        url[0] = '\0';
        if (sscanf(line, "%llu %17s %lu %d %63s",
                   &seq, mac, &ts, &action, url) != 5)
            continue;
        /* Always advance the watermark, even when recording is disabled. */
        if (seq > max_seq)
            max_seq = seq;
        if (seq <= last_seq)            /* idempotent de-dup on Seq */
            continue;
        if (!st.enabled || !mac_in_list(&st, mac))
            continue;
        if (!url[0])
            continue;

        if (!db) {                      /* open lazily, only when inserting */
            if (url_record_open_db(&db) != 0)
                break;
        }

        /* 5s de-dup: same (mac,url,action) within the window is skipped. */
        rc = sqlite3_prepare_v2(db,
            "SELECT 1 FROM url_visit_record "
            "WHERE mac = ? AND url = ? AND action = ? AND ts >= ? LIMIT 1;",
            -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            if (stmt) {
                sqlite3_finalize(stmt);
                stmt = NULL;
            }
            continue;
        }
        sqlite3_bind_text(stmt, 1, mac, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, url, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, action);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)ts - URL_DEDUP_WINDOW);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
        if (rc == SQLITE_ROW)
            continue;

        rc = sqlite3_prepare_v2(db,
            "INSERT INTO url_visit_record (mac, ts, url, action) "
            "VALUES (?, ?, ?, ?);",
            -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            if (stmt) {
                sqlite3_finalize(stmt);
                stmt = NULL;
            }
            continue;
        }
        sqlite3_bind_text(stmt, 1, mac, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)ts);
        sqlite3_bind_text(stmt, 3, url, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, action);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    fclose(fp);
    if (db)
        sqlite3_close(db);
    last_seq = max_seq;
}

void fwx_url_record_cleanup(void)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    url_settings_t st;
    struct uci_context *ctx;
    sqlite3_int64 cutoff;
    int rc;

    memset(&st, 0, sizeof(st));
    st.days = 7;
    ctx = uci_alloc_context();
    if (ctx) {
        load_settings(ctx, &st);
        uci_free_context(ctx);
    }
    if (url_record_open_db(&db) != 0)
        return;
    cutoff = (sqlite3_int64)time(NULL) - (sqlite3_int64)st.days * 86400;
    rc = sqlite3_prepare_v2(db,
        "DELETE FROM url_visit_record WHERE ts < ?;", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, cutoff);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
    } else if (stmt) {
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    sqlite3_close(db);
}

/* ------------------------------ ubus api handlers ------------------------- */

struct json_object *fwx_api_get_url_record_settings(struct json_object *req_obj)
{
    struct json_object *data_obj = json_object_new_object();
    struct json_object *macs_obj = json_object_new_array();
    struct uci_context *ctx;
    url_settings_t st;
    int i;

    (void)req_obj;
    memset(&st, 0, sizeof(st));
    st.days = 7;
    ctx = uci_alloc_context();
    if (ctx) {
        load_settings(ctx, &st);
        uci_free_context(ctx);
    }
    for (i = 0; i < st.mac_count; i++)
        json_object_array_add(macs_obj, json_object_new_string(st.macs[i]));

    json_object_object_add(data_obj, "enabled", json_object_new_int(st.enabled));
    json_object_object_add(data_obj, "days", json_object_new_int(st.days));
    json_object_object_add(data_obj, "macs", macs_obj);
    return fwx_gen_api_response_data(API_CODE_SUCCESS, data_obj);
}

struct json_object *fwx_api_set_url_record_settings(struct json_object *req_obj)
{
    struct json_object *enabled_obj, *days_obj, *macs_obj;
    struct uci_context *ctx;
    int enabled = 0, days = 7, i, n;
    char key[128];

    if (!req_obj)
        return fwx_gen_api_response_data(API_CODE_ERROR, NULL);

    enabled_obj = json_object_object_get(req_obj, "enabled");
    days_obj = json_object_object_get(req_obj, "days");
    macs_obj = json_object_object_get(req_obj, "macs");

    if (enabled_obj)
        enabled = (json_object_get_int(enabled_obj) == 1) ? 1 : 0;
    if (days_obj) {
        days = json_object_get_int(days_obj);
        if (days < 1 || days > 365)
            days = 7;
    }

    ctx = uci_alloc_context();
    if (!ctx)
        return fwx_gen_api_response_data(API_CODE_ERROR, NULL);

    ensure_url_record_section(ctx);
    fwx_uci_set_int_value(ctx, "fwx.url_record.enabled", enabled);
    fwx_uci_set_int_value(ctx, "fwx.url_record.days", days);

    /* Replace the mac list: delete first, then add one by one. */
    snprintf(key, sizeof(key), "fwx.url_record.mac");
    fwx_uci_delete(ctx, key);
    if (macs_obj && json_object_is_type(macs_obj, json_type_array)) {
        n = json_object_array_length(macs_obj);
        for (i = 0; i < n && i < URL_MAC_MAX; i++) {
            struct json_object *item = json_object_array_get_idx(macs_obj, i);
            const char *mac = item ? json_object_get_string(item) : NULL;
            char norm[URL_MAC_LEN];

            if (!mac || !mac[0] || strlen(mac) >= sizeof(norm))
                continue;
            snprintf(norm, sizeof(norm), "%s", mac);
            normalize_lower(norm);
            fwx_uci_add_list(ctx, key, norm);
        }
    }
    fwx_uci_commit(ctx, "fwx");
    uci_free_context(ctx);

    /* Settings change -> re-flush and replay all rules into the kernel. */
    resync_all_rules_to_kernel();

    return fwx_gen_api_response_data(API_CODE_SUCCESS, NULL);
}

struct json_object *fwx_api_get_url_records(struct json_object *req_obj)
{
    int page = 1, page_size = 15;
    int total = 0, total_page = 1, bind_idx;
    long long start_time = 0, end_time = 0;
    const char *mac = NULL;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    char db_path[512];
    char where_sql[512] = " WHERE 1=1";
    char count_sql[1024];
    char query_sql[1200];
    struct json_object *data_obj = json_object_new_object();
    struct json_object *list_obj = json_object_new_array();
    int rc = SQLITE_OK;

    if (req_obj) {
        struct json_object *o;

        o = json_object_object_get(req_obj, "mac");
        if (o) {
            mac = json_object_get_string(o);
            if (mac && !mac[0])
                mac = NULL;
        }
        o = json_object_object_get(req_obj, "start_time");
        if (o)
            start_time = json_object_get_int64(o);
        o = json_object_object_get(req_obj, "end_time");
        if (o)
            end_time = json_object_get_int64(o);
        o = json_object_object_get(req_obj, "page");
        if (o) {
            page = json_object_get_int(o);
            if (page < 1)
                page = 1;
        }
        o = json_object_object_get(req_obj, "page_size");
        if (o) {
            page_size = json_object_get_int(o);
            if (page_size < 1)
                page_size = 15;
            if (page_size > 100)
                page_size = 100;
        }
    }
    if (start_time > 0 && end_time > 0 && start_time > end_time) {
        long long tmp = start_time;
        start_time = end_time;
        end_time = tmp;
    }

    snprintf(db_path, sizeof(db_path), "%s/client.db",
             get_history_data_root_dir());
    if (access(db_path, F_OK) != 0) {
        json_object_object_add(data_obj, "list", list_obj);
        json_object_object_add(data_obj, "total", json_object_new_int(0));
        json_object_object_add(data_obj, "page", json_object_new_int(page));
        json_object_object_add(data_obj, "total_page", json_object_new_int(1));
        return fwx_gen_api_response_data(API_CODE_SUCCESS, data_obj);
    }
    if (url_record_open_db(&db) != 0) {
        json_object_put(data_obj);
        json_object_put(list_obj);
        return fwx_gen_api_response_data(API_CODE_ERROR, NULL);
    }

    if (mac)
        strncat(where_sql, " AND mac = ?",
                sizeof(where_sql) - strlen(where_sql) - 1);
    if (start_time > 0)
        strncat(where_sql, " AND ts >= ?",
                sizeof(where_sql) - strlen(where_sql) - 1);
    if (end_time > 0)
        strncat(where_sql, " AND ts <= ?",
                sizeof(where_sql) - strlen(where_sql) - 1);

    snprintf(count_sql, sizeof(count_sql),
             "SELECT COUNT(*) FROM url_visit_record%s;", where_sql);
    rc = sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
        goto CLEANUP;
    bind_idx = 1;
    if (mac)
        sqlite3_bind_text(stmt, bind_idx++, mac, -1, SQLITE_TRANSIENT);
    if (start_time > 0)
        sqlite3_bind_int64(stmt, bind_idx++, start_time);
    if (end_time > 0)
        sqlite3_bind_int64(stmt, bind_idx++, end_time);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        total = sqlite3_column_int(stmt, 0);
    } else {
        rc = SQLITE_ERROR;
        goto CLEANUP;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    total_page = (total + page_size - 1) / page_size;
    if (total_page < 1)
        total_page = 1;
    if (page > total_page)
        page = total_page;

    if (total > 0) {
        int offset = (page - 1) * page_size;

        snprintf(query_sql, sizeof(query_sql),
                 "SELECT url, ts, action FROM url_visit_record%s "
                 "ORDER BY ts DESC LIMIT ? OFFSET ?;", where_sql);
        rc = sqlite3_prepare_v2(db, query_sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK)
            goto CLEANUP;
        bind_idx = 1;
        if (mac)
            sqlite3_bind_text(stmt, bind_idx++, mac, -1, SQLITE_TRANSIENT);
        if (start_time > 0)
            sqlite3_bind_int64(stmt, bind_idx++, start_time);
        if (end_time > 0)
            sqlite3_bind_int64(stmt, bind_idx++, end_time);
        sqlite3_bind_int(stmt, bind_idx++, page_size);
        sqlite3_bind_int(stmt, bind_idx++, offset);

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            const unsigned char *row_url = sqlite3_column_text(stmt, 0);
            long long row_ts = (long long)sqlite3_column_int64(stmt, 1);
            int row_action = sqlite3_column_int(stmt, 2);
            struct json_object *item = json_object_new_object();

            json_object_object_add(item, "url",
                json_object_new_string(row_url ? (const char *)row_url : ""));
            json_object_object_add(item, "timestamp",
                json_object_new_int64(row_ts));
            json_object_object_add(item, "action",
                json_object_new_int(row_action));
            json_object_array_add(list_obj, item);
        }
        if (rc != SQLITE_DONE)
            goto CLEANUP;
    }
    rc = SQLITE_OK;

CLEANUP:
    if (stmt)
        sqlite3_finalize(stmt);
    if (db)
        sqlite3_close(db);
    if (rc != SQLITE_OK) {
        json_object_put(data_obj);
        json_object_put(list_obj);
        return fwx_gen_api_response_data(API_CODE_ERROR, NULL);
    }
    json_object_object_add(data_obj, "list", list_obj);
    json_object_object_add(data_obj, "total", json_object_new_int(total));
    json_object_object_add(data_obj, "page", json_object_new_int(page));
    json_object_object_add(data_obj, "total_page",
                           json_object_new_int(total_page));
    return fwx_gen_api_response_data(API_CODE_SUCCESS, data_obj);
}

struct json_object *fwx_api_get_url_list(struct json_object *req_obj)
{
    struct json_object *mac_obj;
    const char *mac;
    struct json_object *data_obj = json_object_new_object();
    struct json_object *list_obj = json_object_new_array();
    struct uci_context *ctx;
    getlist_ctx_t g;

    if (!req_obj ||
        !(mac_obj = json_object_object_get(req_obj, "mac")) ||
        !(mac = json_object_get_string(mac_obj)) || !mac[0]) {
        json_object_put(data_obj);
        json_object_put(list_obj);
        return fwx_gen_api_response_data(API_CODE_ERROR, NULL);
    }
    g.mac = mac;
    g.list = list_obj;
    ctx = uci_alloc_context();
    if (ctx) {
        urlfilter_foreach(ctx, getlist_visitor, &g);
        uci_free_context(ctx);
    }
    json_object_object_add(data_obj, "list", list_obj);
    return fwx_gen_api_response_data(API_CODE_SUCCESS, data_obj);
}

struct json_object *fwx_api_add_url_list(struct json_object *req_obj)
{
    char mac[URL_MAC_LEN];
    char url[URL_URL_LEN];
    char type[16];
    char key[128];
    struct uci_context *ctx;
    conflict_ctx_t c;
    struct json_object *data_obj;
    int switched = 0;
    int removed = 0;
    int i;

    if (parse_rule_fields(req_obj, mac, sizeof(mac), url, sizeof(url),
                          type, sizeof(type)) != 0)
        return fwx_gen_api_response_data(API_CODE_ERROR, NULL);

    ctx = uci_alloc_context();
    if (!ctx)
        return fwx_gen_api_response_data(API_CODE_ERROR, NULL);

    memset(&c, 0, sizeof(c));
    c.mac = mac;
    c.url = url;
    c.want_type = type;
    c.same_index = -1;
    urlfilter_foreach(ctx, conflict_visitor, &c);

    /* Drop stale duplicates from the opposite list first. Iterating backwards
     * keeps the remaining section indexes valid while deleting. */
    for (i = c.other_count - 1; i >= 0; i--) {
        snprintf(key, sizeof(key), "%s.@rule[%d]", URLFILTER_PACKAGE,
                 c.other_index[i]);
        fwx_uci_delete(ctx, key);
        fwx_uci_commit(ctx, URLFILTER_PACKAGE);
        url_record_send_kernel("del_url_list", mac, url, c.other_type[i]);
        removed++;
    }

    if (c.same_index >= 0) {          /* already listed -> nothing to add */
        uci_free_context(ctx);
        if (removed == 0)
            return fwx_gen_api_response_data(API_CODE_SUCCESS, NULL);
        switched = 1;
    } else {
        fwx_uci_add_section(ctx, URLFILTER_PACKAGE, URLFILTER_RULE_TYPE);
        snprintf(key, sizeof(key), "%s.@rule[-1].mac", URLFILTER_PACKAGE);
        fwx_uci_set_value(ctx, key, mac);
        snprintf(key, sizeof(key), "%s.@rule[-1].url", URLFILTER_PACKAGE);
        fwx_uci_set_value(ctx, key, url);
        snprintf(key, sizeof(key), "%s.@rule[-1].type", URLFILTER_PACKAGE);
        fwx_uci_set_value(ctx, key, type);
        fwx_uci_commit(ctx, URLFILTER_PACKAGE);
        uci_free_context(ctx);

        url_record_send_kernel("add_url_list", mac, url, type);
    }

    if (!switched)
        return fwx_gen_api_response_data(API_CODE_SUCCESS, NULL);

    data_obj = json_object_new_object();
    json_object_object_add(data_obj, "switched", json_object_new_int(1));
    json_object_object_add(data_obj, "from_type",
                           json_object_new_string(c.other_type[0]));
    json_object_object_add(data_obj, "to_type",
                           json_object_new_string(type));
    return fwx_gen_api_response_data(API_CODE_SUCCESS, data_obj);
}

struct json_object *fwx_api_del_url_list(struct json_object *req_obj)
{
    char mac[URL_MAC_LEN];
    char url[URL_URL_LEN];
    char type[16];
    char key[128];
    struct uci_context *ctx;
    find_ctx_t f;

    if (parse_rule_fields(req_obj, mac, sizeof(mac), url, sizeof(url),
                          type, sizeof(type)) != 0)
        return fwx_gen_api_response_data(API_CODE_ERROR, NULL);

    ctx = uci_alloc_context();
    if (!ctx)
        return fwx_gen_api_response_data(API_CODE_ERROR, NULL);

    f.mac = mac;
    f.url = url;
    f.type = type;
    f.found = 0;
    f.index = -1;
    urlfilter_foreach(ctx, find_visitor, &f);
    if (f.found) {
        snprintf(key, sizeof(key), "%s.@rule[%d]", URLFILTER_PACKAGE, f.index);
        fwx_uci_delete(ctx, key);
        fwx_uci_commit(ctx, URLFILTER_PACKAGE);
    }
    uci_free_context(ctx);

    url_record_send_kernel("del_url_list", mac, url, type);
    return fwx_gen_api_response_data(API_CODE_SUCCESS, NULL);
}
