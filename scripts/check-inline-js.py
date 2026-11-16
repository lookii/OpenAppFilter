#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""抽取 LuCI .htm 里的内联 JS 并做 node --check 语法校验。

LuCI 模板有两个坑，直接正则匹配会大量误报：
  1. `<script src="<%=resource%>/common.js">` 里的 `%>` 会把 `<script[^>]*>` 提前截断；
  2. 内联 JS 里可能残留 `<%:文案%>` / `<%=变量%>` 等模板标记，不是合法 JS。

处理：先把全部 `<%...%>` 换成合法标识符 `__TPL__`（在字符串/表达式位置都合法），
再匹配 `<script ...>`，跳过带 src= 的外链标签，去掉 `<![CDATA[` 包裹。
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIEW = os.path.join(ROOT, 'luci-app-oaf/luasrc/view/oaf')
NODE = os.environ.get('NODE_BIN', 'node')


def main():
    bad = []
    total = 0
    skipped = 0
    for dp, _, fns in os.walk(VIEW):
        for fn in sorted(fns):
            if not fn.endswith('.htm'):
                continue
            p = os.path.join(dp, fn)
            rel = os.path.relpath(p, ROOT)
            s = open(p, encoding='utf-8', errors='replace').read()
            # 1) 剥离模板标记（含跨行的 <% ... %> 块）
            s = re.sub(r'<%.*?%>', '__TPL__', s, flags=re.S)
            # 2) 匹配 script 标签，跳过外链
            for i, m in enumerate(re.finditer(
                    r'<script\b([^>]*)>(.*?)</script\s*>', s, re.S | re.I)):
                attrs, js = m.group(1), m.group(2)
                if 'src=' in attrs.lower():
                    skipped += 1
                    continue
                js = js.replace('//<![CDATA[', '').replace('//]]>', '')
                if not js.strip():
                    continue
                total += 1
                with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False,
                                                 encoding='utf-8') as t:
                    t.write(js)
                    tmp = t.name
                try:
                    r = subprocess.run([NODE, '--check', tmp],
                                       capture_output=True, text=True)
                finally:
                    os.unlink(tmp)
                if r.returncode != 0:
                    line = (r.stderr.strip().splitlines() or ['?'])[-1]
                    bad.append('%s #%d: %s' % (rel, i + 1, line))
    print('内联脚本: 校验 %d 段, 跳过外链 %d 段, 失败 %d 段' % (total, skipped, len(bad)))
    for b in bad:
        print('  FAIL', b)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
