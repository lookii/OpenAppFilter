#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""免编译打包 LuCI 前端 / 中文语言包（noarch）。

这两个包全是纯文本/资源文件，不需要交叉编译。用 OpenWrt SDK 自带的
`apk mkpkg` 直接打，产物与 iStoreOS 25.12.6 固件 installed 数据库中的包元数据
完全对齐（origin / depends / arch / provides / 安装脚本 / .list 清单）。

用法：
  python3 scripts/pack-noarch.py --apk /path/to/sdk/staging_dir/host/bin/apk \
      --version 7.1.0 --out out/

参数 --version 只影响 luci-app-oaf；语言包版本号沿用 26.302.00000。
正式版使用纯版本号；同一版本号仅为修复缺陷重新编译时，追加构建尾标
-rN（如 7.1.0-r1）形成递增版本，以便 apk 允许覆盖安装。
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 与 iStoreOS 25.12.6 固件 installed 数据库对齐的元数据
DESC = {
    'luci-app-oaf': 'LuCI app for OAF',
    'luci-i18n-oaf-zh-cn': 'Translation for luci-app-oaf - 简体中文 (Simplified Chinese)',
}
ORIGIN = {
    'luci-app-oaf': 'feeds/oaf/feeds/oaf/luci-app-oaf',
    'luci-i18n-oaf-zh-cn': 'feeds/oaf/feeds/oaf/luci-app-oaf',
}
DEPS = {
    'luci-app-oaf': ['appfilter', 'kmod-oaf', 'libc', 'luci-compat',
                     'luci-lib-jsonc', 'luci-lua-runtime'],
    'luci-i18n-oaf-zh-cn': ['libc', 'luci-app-oaf'],
}
I18N_VER = '26.302.00000'

# 源码目录 -> 包内目录
MAPPING = [
    ('luasrc/controller', 'usr/lib/lua/luci/controller'),
    ('luasrc/model/cbi/oaf', 'usr/lib/lua/luci/model/cbi/oaf'),
    ('luasrc/view/oaf', 'usr/lib/lua/luci/view/oaf'),
    ('htdocs/luci-static/resources/oaf', 'www/luci-static/resources/oaf'),
]

LUCIDIR = 'luci-app-oaf'


def make_tree(name, files, tmp):
    """files: [(包内相对路径, 源绝对路径, mode)]"""
    tree = os.path.join(tmp, name + '.tree')
    if os.path.isdir(tree):
        shutil.rmtree(tree)
    os.makedirs(tree)
    os.chmod(tree, 0o755)
    for rel, src, mode in files:
        dst = os.path.join(tree, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        # 目录一律 755
        d = os.path.dirname(dst)
        while os.path.abspath(d) != os.path.abspath(tree):
            os.chmod(d, 0o755)
            d = os.path.dirname(d)
        shutil.copyfile(src, dst)
        os.chmod(dst, mode)
    return tree


def pkg(apk, name, version, files, scripts, out):
    tmp = os.path.join(out, '.tmp')
    os.makedirs(tmp, exist_ok=True)
    tree = make_tree(name, files, tmp)
    outfile = os.path.join(out, '%s-%s.apk' % (name, version))
    args = [apk, 'mkpkg', '-F', tree,
            '-I', 'name:' + name,
            '-I', 'version:' + version,
            '-I', 'description:' + DESC[name],
            '-I', 'arch:noarch',
            '-I', 'origin:' + ORIGIN[name],
            '-I', 'provides:' + name + '-any',
            '-I', 'depends:' + ' '.join(DEPS[name])]
    for typ, path in scripts:
        if os.path.isfile(path) and os.path.getsize(path) > 0:
            args += ['-s', typ + ':' + path]
    args += ['-o', outfile]
    subprocess.run(args, check=True)
    shutil.rmtree(tmp, ignore_errors=True)
    return outfile


def collect_luci():
    """返回 [(包内路径, 源路径, mode)]，并生成 .list 清单内容。"""
    src_root = os.path.join(ROOT, LUCIDIR)
    out = []
    paths = []
    for srcdir, dstdir in MAPPING:
        base = os.path.join(src_root, srcdir)
        for dp, _, fns in os.walk(base):
            for fn in sorted(fns):
                if fn.endswith('.swp') or fn.startswith('.'):
                    continue
                p = os.path.join(dp, fn)
                rel = os.path.relpath(p, base).replace(os.sep, '/')
                dst = dstdir + '/' + rel
                out.append((dst, p, 0o644))
                paths.append('/' + dst)
    # 注意：apk 的 .list 清单按 OpenWrt 惯例不含 .list 自身
    return out, paths


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apk', required=True, help='apk mkpkg 工具路径（SDK 的 staging_dir/host/bin/apk）')
    ap.add_argument('--version', default='7.1.0',
                    help='luci-app-oaf 版本号（同版本重编译可加 -rN 尾标）')
    ap.add_argument('--out', default=None, help='输出目录')
    ap.add_argument('--i18n-version', default=I18N_VER)
    a = ap.parse_args()

    if not os.path.isfile(a.apk):
        sys.exit('apk 工具不存在: ' + a.apk)
    out = a.out or os.path.join(ROOT, 'out')
    os.makedirs(out, exist_ok=True)

    made = []

    # ---------- luci-app-oaf ----------
    files, paths = collect_luci()
    listfile = os.path.join(out, '.luci-app-oaf.list.tmp')
    with open(listfile, 'w') as f:
        f.write('\n'.join(sorted(paths)) + '\n')
    files.append(('lib/apk/packages/luci-app-oaf.list', listfile, 0o644))

    sdir = os.path.join(ROOT, 'scripts/pkg-scripts/luci-app-oaf')
    scripts = [(t, os.path.join(sdir, t)) for t in
               ('post-install', 'post-upgrade', 'pre-deinstall')]
    made.append(pkg(a.apk, 'luci-app-oaf', a.version, files, scripts, out))
    os.remove(listfile)

    # ---------- luci-i18n-oaf-zh-cn ----------
    lmo = os.path.join(ROOT, 'prebuilt/oaf.zh-cn.lmo')
    uci = os.path.join(ROOT, 'scripts/pkg-scripts/luci-i18n-oaf-zh-cn.uci-defaults')
    i18n_paths = [
        'usr/lib/lua/luci/i18n/oaf.zh-cn.lmo',
        'etc/uci-defaults/luci-i18n-oaf-zh-cn',
    ]
    ilist = os.path.join(out, '.i18n.list.tmp')
    with open(ilist, 'w') as f:
        # 与固件原版一致：uci-defaults 在前，lmo 在后，且不含 .list 自身
        f.write('/etc/uci-defaults/luci-i18n-oaf-zh-cn\n'
                '/usr/lib/lua/luci/i18n/oaf.zh-cn.lmo\n')
    ifiles = [
        (i18n_paths[0], lmo, 0o644),
        (i18n_paths[1], uci, 0o755),
        ('lib/apk/packages/luci-i18n-oaf-zh-cn.list', ilist, 0o644),
    ]
    made.append(pkg(a.apk, 'luci-i18n-oaf-zh-cn', a.i18n_version, ifiles, [], out))
    os.remove(ilist)

    # ---------- 校验 + 汇总 ----------
    print('\n===== 产物 =====')
    for p in made:
        d = subprocess.check_output([a.apk, 'adbdump', p], text=True,
                                    stderr=subprocess.DEVNULL)
        import re
        def g(k):
            m = re.search(r'^  %s: (.*)$' % k, d, re.M)
            return m.group(1) if m else '?'
        ver = subprocess.check_output(
            [a.apk, 'verify', '--allow-untrusted', p], text=True,
            stderr=subprocess.STDOUT).strip()
        h = hashlib.sha256(open(p, 'rb').read()).hexdigest()
        print('  %-56s %9d B' % (os.path.basename(p), os.path.getsize(p)))
        print('      name=%s version=%s arch=%s  %s' %
              (g('name'), g('version'), g('arch'), ver))
        print('      sha256 %s' % h)

    with open(os.path.join(out, 'SHA256SUMS'), 'w') as f:
        for p in made:
            f.write('%s  %s\n' % (
                hashlib.sha256(open(p, 'rb').read()).hexdigest(),
                os.path.basename(p)))
    print('\n输出目录:', out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
