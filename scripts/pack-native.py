#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 SDK 编译出的 oaf.ko / oafd 重打包成与 iStoreOS 25.12.6 固件对齐的 apk。

为什么要重打包而不是直接用 `make package/*/compile` 产出的标准包：
  本项目交付的 4 个 apk 是基于 iStoreOS 25.12.6 固件原版包重新打包的，
  origin / depends（带 ABI 版本号）/ 版本号 / conffiles / 安装脚本 全部对齐
  固件 installed 数据库，可直接覆盖安装；发布版本号由本脚本统一指定，
  与 Makefile 的 PKG_RELEASE 无关。

输入：
  --oafd   SDK 编译出的守护进程二进制
  --ko     SDK 编译出的内核模块
输出：appfilter-*.apk、kmod-oaf-*.apk + SHA256SUMS
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DESC = {
    'kmod-oaf': 'oaf netfilter module',
    'appfilter': 'OAF service',
}
ORIGIN = {
    'kmod-oaf': 'feeds/oaf/feeds/oaf/oaf',
    'appfilter': 'feeds/oaf/feeds/oaf/open-app-filter',
}
DEPS = {
    'kmod-oaf': ['kernel=6.12.112~b4125196ecec75391093e1e2edeba490-r1',
                 'kmod-nf-conntrack', 'kmod-nf-reject', 'kmod-nf-reject6'],
    'appfilter': ['libblobmsg-json20260213', 'libc', 'libcurl4', 'libjson-c5',
                  'libpthread', 'libsqlite3-0', 'libubox20260213',
                  'libubus20251202', 'libuci-lua', 'libuci20250120'],
}
KMOD_VER_DEFAULT = '6.12.112.7.1.0'
APPFILTER_VER_DEFAULT = '7.1.0'
KERNEL_MOD_DIR = 'lib/modules/6.12.112'

# 源码 files/ 下的文件 -> 包内路径, mode
SRC_FILES = [
    ('appfilter.init', 'etc/init.d/appfilter', 0o755),
    ('rule_manager.lua', 'usr/bin/rule_manager', 0o755),
    ('hnat.sh', 'usr/bin/hnat.sh', 0o755),
    ('feature.bin', 'etc/fwxd/feature.bin', 0o644),
    ('custom_feature.cfg', 'etc/fwxd/custom_feature.cfg', 0o644),
    ('oaf_version', 'etc/oaf_version', 0o644),
    ('luci-app-oaf.json', 'usr/share/rpcd/acl.d/luci-app-oaf.json', 0o644),
    ('urlfilter.config', 'etc/config/urlfilter', 0o644),
]


def mkpkg(apk, name, version, arch, files, scripts, out):
    tmp = os.path.join(out, '.tmp-' + name)
    tree = os.path.join(tmp, 'tree')
    if os.path.isdir(tmp):
        shutil.rmtree(tmp)
    os.makedirs(tree)
    os.chmod(tree, 0o755)
    for rel, src, mode in files:
        dst = os.path.join(tree, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        d = os.path.dirname(dst)
        while os.path.abspath(d) != os.path.abspath(tree):
            os.chmod(d, 0o755)
            d = os.path.dirname(d)
        shutil.copyfile(src, dst)
        os.chmod(dst, mode)
    outfile = os.path.join(out, '%s-%s.apk' % (name, version))
    args = [apk, 'mkpkg', '-F', tree,
            '-I', 'name:' + name,
            '-I', 'version:' + version,
            '-I', 'description:' + DESC[name],
            '-I', 'arch:' + arch,
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


def scripts_for(name):
    d = os.path.join(ROOT, 'scripts/pkg-scripts', name)
    return [(t, os.path.join(d, t)) for t in ('post-install', 'post-upgrade', 'pre-deinstall')]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apk', required=True)
    ap.add_argument('--oafd', required=True, help='编译出的 oafd 二进制')
    ap.add_argument('--ko', required=True, help='编译出的 oaf.ko')
    ap.add_argument('--appfilter-version', default=APPFILTER_VER_DEFAULT)
    ap.add_argument('--kmod-version', default=KMOD_VER_DEFAULT)
    ap.add_argument('--out', default=None)
    a = ap.parse_args()

    for p in (a.apk, a.oafd, a.ko):
        if not os.path.isfile(p):
            sys.exit('文件不存在: ' + p)
    if os.path.getsize(a.oafd) < 100000:
        sys.exit('oafd 体积异常，编译可能失败: %d B' % os.path.getsize(a.oafd))
    if os.path.getsize(a.ko) < 100000:
        sys.exit('oaf.ko 体积异常，编译可能失败: %d B' % os.path.getsize(a.ko))

    out = a.out or os.path.join(ROOT, 'out')
    os.makedirs(out, exist_ok=True)
    made = []

    # ---------- kmod-oaf ----------
    kfiles = [
        (KERNEL_MOD_DIR + '/oaf.ko', a.ko, 0o644),
        ('lib/apk/packages/kmod-oaf.list',
         os.path.join(ROOT, 'pkgdata/kmod-oaf/lib/apk/packages/kmod-oaf.list'), 0o644),
    ]
    made.append(mkpkg(a.apk, 'kmod-oaf', a.kmod_version, 'x86_64',
                      kfiles, scripts_for('kmod-oaf'), out))

    # ---------- appfilter ----------
    srcdir = os.path.join(ROOT, 'open-app-filter/files')
    afiles = [(dst, os.path.join(srcdir, src), mode) for src, dst, mode in SRC_FILES]
    # 编译产物
    afiles.append(('usr/bin/oafd', a.oafd, 0o755))
    # 固化的清单 / 配置保护文件
    pdata = os.path.join(ROOT, 'pkgdata/appfilter')
    for rel in ('lib/apk/packages/appfilter.conffiles',
                'lib/apk/packages/appfilter.conffiles_static',
                'lib/apk/packages/appfilter.list',
                'lib/upgrade/keep.d/appfilter'):
        afiles.append((rel, os.path.join(pdata, rel), 0o644))
    made.append(mkpkg(a.apk, 'appfilter', a.appfilter_version, 'x86_64',
                      afiles, scripts_for('appfilter'), out))

    print('\n===== 产物 =====')
    for p in made:
        d = subprocess.check_output([a.apk, 'adbdump', p], text=True,
                                    stderr=subprocess.DEVNULL)
        def g(k):
            m = re.search(r'^  %s: (.*)$' % k, d, re.M)
            return m.group(1) if m else '?'
        v = subprocess.check_output([a.apk, 'verify', '--allow-untrusted', p],
                                    text=True, stderr=subprocess.STDOUT).strip()
        print('  %-52s %9d B' % (os.path.basename(p), os.path.getsize(p)))
        print('      version=%s arch=%s  %s' % (g('version'), g('arch'), v))

    with open(os.path.join(out, 'SHA256SUMS'), 'w') as f:
        for p in made:
            f.write('%s  %s\n' % (
                hashlib.sha256(open(p, 'rb').read()).hexdigest(),
                os.path.basename(p)))
    print('\n输出目录:', out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
