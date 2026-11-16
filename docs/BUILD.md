# 本地编译（不用 GitHub Actions）

如果你希望在自己机器上编译（而不是用 CI），按本文操作。
CI 工作流内部调用的也是同一套脚本，行为一致。

---

## 0. 前提

- Linux x86_64（Ubuntu 22.04 验证过；Debian 系均可）
- 能访问 `downloads.openwrt.org`
- 约 3 GB 空闲磁盘（SDK 解压后）
- 基础编译工具：`build-essential flex bison gawk gettext libncurses5-dev
  libssl-dev python3 zlib1g-dev libelf-dev zstd wget git rsync unzip file`

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential clang flex bison g++ gawk gcc-multilib gettext \
  git libncurses5-dev libssl-dev python3 python3-distutils \
  rsync unzip zlib1g-dev file wget zstd libelf-dev ecj fastjar \
  libtool automake autoconf pkg-config
```

> ⚠️ 内核模块与内核版本强绑定。以下 SDK 仅适用于
> **OpenWrt / iStoreOS 25.12.6，x86_64，linux-6.12.112**。

---

## 1. 下载并校验官方 SDK

```bash
export SDK_BASE=https://downloads.openwrt.org/releases/25.12.6/targets/x86/64
export SDK_FILE=openwrt-sdk-25.12.6-x86-64_gcc-14.3.0_musl.Linux-x86_64.tar.zst
export SDK_SHA256=1864152619092e781fa36a401960c8e9ae42d6a364c44ee5f0b197ca5f3c0fcc

mkdir -p ~/oaf-build && cd ~/oaf-build
wget -q --show-progress "$SDK_BASE/$SDK_FILE" -O "$SDK_FILE"

# 也可以直接取官方校验文件比对
wget -q "$SDK_BASE/sha256sums" -O sha256sums
grep -F "openwrt-sdk-" sha256sums

echo "$SDK_SHA256  $SDK_FILE" | sha256sum -c -
tar --zstd -xf "$SDK_FILE"
```

解压得到目录
`openwrt-sdk-25.12.6-x86-64_gcc-14.3.0_musl.Linux-x86_64/`。
这个 tarball 自带展开的 `linux-6.12.112` 内核源码树、
交叉工具链和 `staging_dir/host/bin/apk`，无需额外准备。

---

## 2. 一键编译 + 打包

```bash
cd <本仓库目录>
chmod +x scripts/build-with-sdk.sh
./scripts/build-with-sdk.sh ~/oaf-build/openwrt-sdk-25.12.6-x86-64_gcc-14.3.0_musl.Linux-x86_64 ./out
```

脚本依次完成：

1. 环境自检（SDK 结构、内核源码树）
2. 把 `oaf/`、`open-app-filter/` 挂到 SDK 的 `package/`
3. `./scripts/feeds update -a`（带 3 次重试）+ `feeds install -a`
4. `make package/oaf/compile`（得到 `oaf.ko`）
5. `make package/open-app-filter/compile`（得到 `oafd`）
6. `scripts/pack-native.py` → `appfilter` / `kmod-oaf` 两个 apk
7. `scripts/pack-noarch.py` → `luci-app-oaf` / 中文包两个 apk
8. 生成 `SHA256SUMS` 并逐个 `apk verify`

产物在 `./out/`。

### 版本号与重编译

默认产出**纯版本号**（`7.1.0` / `26.302.00000` / `6.12.112.7.1.0`），
不带构建尾标。仅当功能版本不变、为修复缺陷需要重新编译覆盖安装时，
追加构建尾标形成递增版本：

```bash
LUCI_VER=7.1.0-r1 APPFILTER_VER=7.1.0-r1 KMOD_VER=6.12.112.7.1.0-r1 \
  ./scripts/build-with-sdk.sh ~/oaf-build/<SDK目录> ./out
```

`apk` 拒绝安装版本不高于已装版本的包，同版本重复编译必须靠尾标区分；
版本号只增不减。详见 `docs/CI.md`。

### 只重新打前端包（秒级）

LuCI 前端是纯文本 noarch，改 CSS/JS 后**不需要编译**：

```bash
python3 scripts/pack-noarch.py \
  --apk ~/oaf-build/<SDK目录>/staging_dir/host/bin/apk \
  --version 7.1.0 --out ./out
```

需要覆盖升级时把 `--version` 换成带尾标的新版本（如 `7.1.0-r1`）。
前端迭代走这条路径即可，无需重编内核与守护进程。

---

## 3. 上游 README 里的标准编译方式（参考）

上游项目说明的是把整个仓库 clone 到已编译过的 OpenWrt 源码树：

```bash
git clone https://github.com/destan19/OpenAppFilter.git package/OpenAppFilter
echo "CONFIG_PACKAGE_luci-app-oaf=y" >> .config
make defconfig
make package/luci-app-oaf/compile V=s
make package/open-app-filter/compile V=s
make package/oaf/compile V=s
```

这能编出可用的标准包，但其版本号由 Makefile 的 `PKG_VERSION` /
`PKG_RELEASE` 决定，与本仓库的发布版本号体系独立，且 `conffiles` /
安装脚本等元数据不一定与固件 `installed` 数据库一致。因此本仓库用
`pack-*.py` 把编译产物按发布版本号重打包，保证元数据对齐、可直接覆盖安装。
两者源码相同、功能一致，只是打包元数据不同。

---

## 4. 校验

```bash
python3 scripts/check-sources.py       # 34 项关键标记
python3 scripts/check-inline-js.py     # 内联 JS（需 node）
cd out && sha256sum -c SHA256SUMS      # 产物完整性
```

---

## 5. 常见问题

**`feeds update -a` 很慢 / 失败**
它会 clone 多个 git 仓库，网络差时容易超时。脚本内置 3 次重试；
若 SDK 已经 `feeds install` 过，可设 `SKIP_FEEDS=1` 跳过：

```bash
SKIP_FEEDS=1 ./scripts/build-with-sdk.sh ~/oaf-build/<SDK目录> ./out
```

**编译报缺 `libubox` / `libuci` 等库**
这些库由 feeds 提供并先编译进 SDK 的 target staging。确认第 3 步
`feeds update/install` 成功；不要设 `SKIP_FEEDS=1`。

**找不到 `oaf.ko` / `oafd`**
说明 `make package/*/compile` 没成功。单独加 `V=s` 重跑看具体报错：

```bash
cd <SDK目录> && make package/open-app-filter/compile V=s
```

**`apk: not found` 或无执行位**
SDK 的 `staging_dir/host/bin/apk` 应可执行，脚本会自动 `chmod +x`。

**vermagic 不匹配**
`modinfo oaf.ko | grep vermagic` 与实机 `uname -r` 对比。
必须换用与目标内核完全一致的 SDK 重编。
