#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""检查最终源码树的关键标记，防止后续改动破坏已修复的功能。

用途：
  - GitHub Actions 的 validate 工作流（每次 push / PR 秒级反馈）
  - 本地提交前自检：python3 scripts/check-sources.py

退出码 0 = 全部通过；1 = 有缺失或语法错误。
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(rel):
    p = os.path.join(ROOT, rel)
    if not os.path.isfile(p):
        return None
    with open(p, encoding='utf-8', errors='replace') as f:
        return f.read()


def main():
    results = []

    def ck(name, cond):
        results.append((name, bool(cond)))

    css = read('luci-app-oaf/htdocs/luci-static/resources/oaf/css/common.css') or ''
    ud = read('luci-app-oaf/luasrc/view/oaf/user_detail.htm') or ''
    wl = read('luci-app-oaf/luasrc/view/oaf/whitelist.htm') or ''
    km = read('oaf/src/fwx_main.c') or ''
    rm = read('open-app-filter/files/rule_manager.lua') or ''
    ini = read('open-app-filter/files/appfilter.init') or ''
    ubus = read('open-app-filter/src/fwx_ubus.c') or ''
    urlc = read('open-app-filter/src/fwx_url_record.c') or ''
    ksrcmk = read('oaf/src/Makefile') or ''
    usrcmk = read('open-app-filter/src/Makefile') or ''
    lm = read('luci-app-oaf/Makefile') or ''
    am = read('open-app-filter/Makefile') or ''

    # --- 结构完整性 ---
    for d in ('luci-app-oaf', 'open-app-filter', 'oaf', 'prebuilt',
              'scripts/pkg-scripts/luci-app-oaf', 'docs', '.github/workflows'):
        ck('目录存在: ' + d, os.path.isdir(os.path.join(ROOT, d)))
    ck('LICENSE 保留(GPL-2.0)', 'GNU GENERAL PUBLIC LICENSE' in (read('LICENSE') or ''))
    ck('中文语言包素材 lmo', os.path.getsize(os.path.join(ROOT, 'prebuilt/oaf.zh-cn.lmo')) > 1000
       if os.path.isfile(os.path.join(ROOT, 'prebuilt/oaf.zh-cn.lmo')) else False)

    # CI 打包素材：这些文件若被 .gitignore 漏掉，Actions 会在打包阶段失败
    pack_files = [
        'pkgdata/appfilter/lib/apk/packages/appfilter.conffiles',
        'pkgdata/appfilter/lib/apk/packages/appfilter.conffiles_static',
        'pkgdata/appfilter/lib/apk/packages/appfilter.list',
        'pkgdata/appfilter/lib/upgrade/keep.d/appfilter',
        'pkgdata/kmod-oaf/lib/apk/packages/kmod-oaf.list',
    ]
    pack_missing = [p for p in pack_files if not os.path.isfile(os.path.join(ROOT, p))]
    ck('CI 打包素材齐全(pkgdata)', not pack_missing)

    # --- 前端界面与交互修复 ---
    ck('白名单容器强制覆盖', '#oafWhitelistSection { overflow-x: hidden !important; }' in css)
    ck('白名单容器 id', 'id="oafWhitelistSection"' in wl)
    ck('状态标签 PC 定位', 'width: -moz-fit-content' in ud and '/* width: fit-content; */' in ud)
    ck('状态标签三色纯文字', all(x in ud for x in (
        '.url-record-action-normal', '.url-record-action-allowed', '.url-record-action-blocked')))
    ck('图例 plain 多行', "option.legend.type = 'plain'" in ud)
    ck('拼音检索模块', os.path.isfile(os.path.join(
        ROOT, 'luci-app-oaf/htdocs/luci-static/resources/oaf/oaf_pinyin.js')))
    ck('图表容器无死宽', 'min-width: 600px' not in ud and 'min-width: 680px' not in wl)

    # --- 内核模块（URL 黑名单 conntrack 持久化）---
    ck('内核 drop 持久化(2 处)', km.count('Drop URL blacklist') == 2)
    ck('内核 update_url 返回动作码', 'return act;' in km)
    ck('内核 conntrack DROP 位', 'fwx_ct_set_bit(ct, FWX_CT_DROP_BIT, 1)' in km)
    ck('内核 URL 记录模块参与编译', 'fwx_url_record.o' in ksrcmk)
    ck('内核 URL 记录源文件', os.path.isfile(os.path.join(ROOT, 'oaf/src/fwx_url_record.c')))

    # --- 服务进程（多用户 / URL 名单互斥 / URL 记录）---
    ck('多用户规则匹配', 'pc_rule_matches_mac' in ubus)
    ck('URL 黑白名单互斥', 'conflict_visitor' in urlc)
    ck('服务 URL 记录模块参与编译', 'fwx_url_record.o' in usrcmk)
    ck('rule_manager 多用户', 'normalize_user_macs' in rm)
    ck('init 含 show_visit_url', 'show_visit_url' in ini)
    ck('init 兜底创建默认配置', 'ensure_default_configs' in ini)
    ck('服务脚本执行位(随包安装)', '$(INSTALL_BIN) ./files/rule_manager.lua' in am
       or '$(INSTALL_BIN) ./files/hnat.sh' in am)

    # --- 版本 ---
    ck('LuCI Makefile 7.1.0', 'PKG_VERSION:=7.1.0' in lm)
    ck('appfilter Makefile 7.1.0', 'PKG_VERSION:=7.1.0' in am)

    # --- CSS 花括号配平 ---
    ck('common.css 花括号配平', css.count('{') == css.count('}'))

    # --- 所有 .htm 内联 <script> 花括号粗配平（不做 JS 语法解析，交给 node）---
    bad_html = []
    vdir = os.path.join(ROOT, 'luci-app-oaf/luasrc/view/oaf')
    for dp, _, fns in os.walk(vdir):
        for fn in fns:
            if not fn.endswith('.htm'):
                continue
            s = read(os.path.relpath(os.path.join(dp, fn), ROOT)) or ''
            for m in re.finditer(r'<script[^>]*>(.*?)</script>', s, re.S | re.I):
                js = m.group(1)
                if js.count('{') != js.count('}'):
                    bad_html.append(fn)
                    break
    ck('HTML 内联脚本花括号配平', not bad_html)

    # --- 无编辑器临时文件 / 编译产物混入 ---
    junk = []
    for base in ('luci-app-oaf', 'open-app-filter', 'oaf'):
        for dp, _, fns in os.walk(os.path.join(ROOT, base)):
            for fn in fns:
                if fn.endswith(('.swp', '.swo', '.o', '.ko', '.pyc')) or fn == 'oafd':
                    junk.append(os.path.relpath(os.path.join(dp, fn), ROOT))
    ck('无临时/二进制产物混入', not junk)

    width = max(len(n) for n, _ in results)
    failed = []
    for name, ok in results:
        print('  %-*s %s' % (width, name, 'OK' if ok else 'FAIL'))
        if not ok:
            failed.append(name)
    print('\n共 %d 项，通过 %d 项，失败 %d 项' % (len(results), len(results) - len(failed), len(failed)))
    if failed:
        print('\n失败项：')
        for n in failed:
            print('  - ' + n)
        if junk:
            print('\n混入的临时文件：')
            for n in junk:
                print('  - ' + n)
        if pack_missing:
            print('\n缺失的打包素材（CI 会构建失败）：')
            for n in pack_missing:
                print('  - ' + n)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
