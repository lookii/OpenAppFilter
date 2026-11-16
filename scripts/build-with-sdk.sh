#!/usr/bin/env bash
# -*- coding: utf-8 -*-
# 用官方 OpenWrt 25.12.6 SDK 编译内核模块与守护进程，再重打包成 4 个 apk。
#
# 设计取舍（重要）：
#  官方 SDK tarball 不含 feeds/luci/luci.mk 与 po2lmo，若走完整
#  `make package/luci-app-oaf/compile` 需要 feeds update -a 拉 luci feed 并
#  编译 host 工具，链路长且易失败。LuCI 前端与中文包本来就是纯文本 noarch，
#  因此这里只用 SDK 编译真正需要编译的两个 C 包（oaf.ko / oafd），
#  再用 SDK 自带的 apk 工具打包，产物元数据与固件 installed 库完全对齐。
#
# 用法：
#   ./scripts/build-with-sdk.sh <SDK目录> [输出目录]
# 环境变量（可选）：
#   LUCI_VER / APPFILTER_VER / KMOD_VER / I18N_VER  覆盖各包版本号
#   正式版用纯版本号；同版本仅修缺陷重编译时追加 -rN 尾标以递增版本
#   SKIP_FEEDS=1  跳过 feeds update（SDK 已预置 feeds 时）
set -euo pipefail

SDK="${1:?用法: build-with-sdk.sh <SDK目录> [输出目录]}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${2:-$REPO/out}"
LUCI_VER="${LUCI_VER:-7.1.0}"
APPFILTER_VER="${APPFILTER_VER:-7.1.0}"
KMOD_VER="${KMOD_VER:-6.12.112.7.1.0}"
I18N_VER="${I18N_VER:-26.302.00000}"

echo "SDK  : $SDK"
echo "REPO : $REPO"
echo "OUT  : $OUT"
echo

# ---------- 0. 环境自检 ----------
for f in staging_dir/host/bin/apk include/kernel.mk include/package.mk; do
  [ -e "$SDK/$f" ] || { echo "SDK 缺少 $f，请确认是 OpenWrt 25.12.6 x86/64 SDK"; exit 1; }
done
APK="$SDK/staging_dir/host/bin/apk"
[ -x "$APK" ] || chmod +x "$APK"
LINUX_DIR="$(find "$SDK/build_dir" -maxdepth 3 -type d -name 'linux-6.12.112' | head -1)"
[ -n "$LINUX_DIR" ] || { echo "SDK 缺少展开的 6.12.112 内核源码树"; exit 1; }
echo "内核源码树: $LINUX_DIR"

# ---------- 1. 挂载源码包到 SDK ----------
# 官方 SDK 里没有同名包，直接复制；若已存在则先移除。
for pkg in oaf open-app-filter; do
  rm -rf "$SDK/package/$pkg"
  cp -a "$REPO/$pkg" "$SDK/package/$pkg"
done
echo "已挂载 oaf / open-app-filter 到 SDK/package/"

# ---------- 2. feeds（提供 libubox/libuci/libubus/json-c/sqlite3/curl 定义）----------
# open-app-filter 的 DEPENDS 需要这些库，SDK 会先编译依赖到 staging。
if [ "${SKIP_FEEDS:-0}" != "1" ]; then
  cd "$SDK"
  n=0
  until ./scripts/feeds update -a; do
    n=$((n + 1))
    [ "$n" -ge 3 ] && { echo "feeds update 连续失败 3 次"; exit 1; }
    echo "feeds update 失败，第 $n 次重试…"
    sleep 5
  done
  ./scripts/feeds install -a >/dev/null 2>&1 || {
    echo "feeds install -a 有告警，继续（SDK 自带 .config 已含所需包）"
  }
  echo "feeds 就绪"
fi

# ---------- 3. 打开两个包的编译开关 ----------
cd "$SDK"
grep -q '^CONFIG_PACKAGE_kmod-oaf=' .config || echo 'CONFIG_PACKAGE_kmod-oaf=m' >>.config
grep -q '^CONFIG_PACKAGE_appfilter=' .config || echo 'CONFIG_PACKAGE_appfilter=m' >>.config
make defconfig >/dev/null 2>&1 || true

# ---------- 4. 编译（内核模块 + 守护进程）----------
echo "== 编译 kmod-oaf =="
make package/oaf/compile V=s -j"$(nproc)"
echo "== 编译 appfilter (oafd) =="
make package/open-app-filter/compile V=s -j"$(nproc)"

# ---------- 5. 定位编译产物 ----------
KO="$(find "$SDK/build_dir" -type f -name 'oaf.ko' -size +100k 2>/dev/null | head -1)"
OAFD="$(find "$SDK/build_dir" -type f -name 'oafd' -size +100k 2>/dev/null | head -1)"
[ -n "$KO" ] || { echo "找不到编译出的 oaf.ko"; exit 1; }
[ -n "$OAFD" ] || { echo "找不到编译出的 oafd"; exit 1; }
echo "oaf.ko : $KO ($(stat -c%s "$KO") B)"
echo "oafd   : $OAFD ($(stat -c%s "$OAFD") B)"

# ---------- 6. 打包 4 个 apk ----------
mkdir -p "$OUT"
echo "== 打包 appfilter / kmod-oaf =="
python3 "$REPO/scripts/pack-native.py" \
  --apk "$APK" --oafd "$OAFD" --ko "$KO" \
  --appfilter-version "$APPFILTER_VER" --kmod-version "$KMOD_VER" \
  --out "$OUT"
echo "== 打包 luci-app-oaf / i18n =="
python3 "$REPO/scripts/pack-noarch.py" \
  --apk "$APK" --version "$LUCI_VER" --i18n-version "$I18N_VER" \
  --out "$OUT"

# ---------- 7. 统一校验和 ----------
cd "$OUT"
sha256sum *.apk > SHA256SUMS
echo
echo "===== 最终产物 ====="
ls -l *.apk
echo
echo "===== apk verify ====="
for p in *.apk; do "$APK" verify --allow-untrusted "$p"; done
echo
echo "全部完成，产物在: $OUT"
