# GitHub Actions 自动编译 apk

本仓库提供两个工作流：

| 工作流 | 文件 | 触发 | 耗时 | 作用 |
|---|---|---|---|---|
| **Build APK** | `.github/workflows/build-apk.yml` | push 到 main/master 的源码改动、`v*` tag、手动 | 首次 10–25 分钟，有 SDK 缓存后更短 | 用官方 SDK 编译并产出 4 个 apk |
| **Validate Sources** | `.github/workflows/validate.yml` | 每次 push / PR / 手动 | 十几秒 | 源码关键标记、JS、Lua 语法校验 |

---

## 设计思路（为什么不是简单的 `make package/*/compile`）

标准的 `make package/*/compile` 能产出 apk，但有三个现实问题：

1. **元数据不对齐**：本项目交付的 4 个 apk 的 `origin` / `depends`（带 ABI
   版本号）/ 版本号 / `conffiles` / 安装脚本全部对齐 iStoreOS 25.12.6 固件
   `installed` 数据库，**可直接覆盖安装**。标准 SDK 产物的版本号由 Makefile
   的 `PKG_VERSION` / `PKG_RELEASE` 决定，与本仓库发布版本号体系独立，
   混装容易造成 `apk` 版本比较混乱。
2. **官方 SDK tarball 不含** `feeds/luci/luci.mk` 与 `po2lmo`，完整编译
   `luci-app-oaf` 需要先 `feeds update -a` 拉 luci feed 并编译 host 工具，
   链路长、耗时长、易受网络波动影响。
3. **LuCI 前端与中文包本来就是纯文本 noarch**，不需要交叉编译。

因此采用**混合方案**：

```
官方 SDK (25.12.6 / linux-6.12.112)
   │
   ├─ make package/oaf/compile            → oaf.ko        ─┐
   ├─ make package/open-app-filter/compile → oafd          ─┤
   │                                                        ├─ pack-native.py  → appfilter / kmod-oaf apk
   └─ staging_dir/host/bin/apk (mkpkg 工具)                 │
        ├─ luci-app-oaf 源码 + pkgdata 素材 ─ pack-noarch.py → luci-app-oaf apk
        └─ prebuilt/oaf.zh-cn.lmo + uci-defaults ──────────→ luci-i18n-oaf-zh-cn apk
```

产物元数据与 iStoreOS 25.12.6 固件 `installed` 数据库对齐，可直接覆盖安装。

---

## 关键配置

工作流里这几个环境变量决定 SDK：

```yaml
SDK_URL: https://downloads.openwrt.org/releases/25.12.6/targets/x86/64/openwrt-sdk-25.12.6-x86-64_gcc-14.3.0_musl.Linux-x86_64.tar.zst
SDK_SHA256: 1864152619092e781fa36a401960c8e9ae42d6a364c44ee5f0b197ca5f3c0fcc
SDK_DIRNAME: openwrt-sdk-25.12.6-x86-64_gcc-14.3.0_musl.Linux-x86_64
```

- SDK 下载后做 **SHA256 强校验**，防篡改。
- 用 `actions/cache` 缓存解压后的 SDK 目录，key 绑定哈希，
  后续构建直接复用，不必重复下载 285 MB。
- 构建跑在 `ubuntu-22.04`，宿主依赖一次性 `apt install`。

### ⚠️ vermagic 强绑定（最重要的限制）

`oaf.ko` 的 vermagic 是 `6.12.112 SMP mod_unload`，与内核版本、
配置、编译工具链强绑定。本工作流固定使用 **OpenWrt 25.12.6 官方 SDK**
（gcc-14.3.0 / musl / linux-6.12.112），正好与 iStoreOS 25.12.6 一致。

**如果目标固件内核版本变了**（例如升级到 25.12.7 或换架构），必须：
1. 到 `https://downloads.openwrt.org/releases/<版本>/targets/x86/64/`
   找到对应的 `openwrt-sdk-*.tar.zst`；
2. 用同目录 `sha256sums` 里的值更新 `SDK_SHA256`；
3. 同步更新 `SDK_URL` / `SDK_DIRNAME`；
4. 若模块目录名变化，改 `scripts/pack-native.py` 里的 `KERNEL_MOD_DIR`
   与 `pkgdata/kmod-oaf/lib/apk/packages/kmod-oaf.list`。

否则 `modprobe oaf` 会报 `version magic ... should be ...` 而失败。

---

## 手动触发与版本号

**Actions → Build APK (OpenWrt 25.12.6 SDK) → Run workflow** 可填：

| 输入 | 默认值 | 说明 |
|---|---|---|
| `luci_ver` | `7.1.0` | LuCI 前端包版本 |
| `appfilter_ver` | `7.1.0` | 服务包版本 |
| `kmod_ver` | `6.12.112.7.1.0` | 内核模块包版本 |
| `publish_release` | `false` | 勾选则同时发 GitHub Release |

### 版本号约定

- **正式发行版使用纯版本号**，不带构建尾标（即上表默认值）。
- **仅修复缺陷、功能版本不变而需要重新编译时**，为版本号追加构建尾标
  形成递增版本，例如把 `7.1.0` 改为 `7.1.0-r1`、再次重编译改为
  `7.1.0-r2`。通过工作流的 `*_ver` 输入，或本地脚本的
  `--version` / `*_VER` 环境变量传入即可，无需改代码。
- **原因**：`apk` 拒绝安装版本不高于已装版本的包；同一纯版本号重复编译
  无法被识别为新版本，追加尾标才能正常覆盖升级。
- **版本号只增不减**；升级到新的功能版本时提升主版本号并去掉尾标。

> 若设备上曾安装过带构建尾标的同版本号包，改用纯版本号包时 `apk`
> 可能判定为版本不高于已装版本；此时可先卸载旧包再安装，或直接用
> 带递增尾标的新构建。

---

## 产物

构建成功后，在构建页底部 **Artifacts** 下载 `oaf-apk-25.12.6-x86_64`：

```
luci-app-oaf-7.1.0.apk
luci-i18n-oaf-zh-cn-26.302.00000.apk
appfilter-7.1.0.apk
kmod-oaf-6.12.112.7.1.0.apk
SHA256SUMS
```

每个包都经过 `apk verify --allow-untrusted` 校验。
勾了 `publish_release` 或推了 `v*` tag，这些文件会同时发布到 Releases。

---

## 校验工作流做什么

`validate.yml` 不下载 SDK，秒级反馈：

1. `scripts/check-sources.py` — **34 项关键标记**，防止后续改动破坏已实现的
   功能（URL 黑名单持久化、多用户规则与名单互斥、白名单容器覆盖、状态标签
   定位、Makefile 版本、CSS 配平、无二进制混入等）。
2. 独立 `.js` 文件 `node --check`。
3. `scripts/check-inline-js.py` — 抽取 LuCI `.htm` 内联 JS 校验；
   先剥离 `<%...%>` 模板标记、跳过 `<script src=...>` 外链。
4. Lua 语法（`luac5.1 -p`），宿主无 luac 时自动跳过不阻断。

本地可用同样命令复现：

```bash
python3 scripts/check-sources.py
python3 scripts/check-inline-js.py
```

---

## 常见问题

**Q：第一次构建为什么要这么久？**
需要下载并解压官方 SDK（约 285 MB）、`feeds update -a` 拉取依赖定义、
编译 `oaf.ko` 与 `oafd` 及其依赖库。之后 SDK 命中缓存会快很多。

**Q：`feeds update -a` 失败？**
脚本内置 3 次重试。若仍失败，多为 GitHub runner 到 `git.openwrt.org`
的网络波动，重跑一次工作流即可。

**Q：能编译 arm / 其它架构吗？**
改 SDK_URL 换成对应 target 的 SDK，并把 `pack-native.py` 里写死的
`arch:x86_64`、内核模块路径、依赖 ABI 版本号一并调整。本仓库当前
只针对 x86_64，改动前请先看 `docs/BUILD.md`。

**Q：产物能直接装到我现在这台路由器吗？**
能，只要内核仍是 `6.12.112`。安装后 `kmod-oaf` 需要 **reboot**
（内核模块无法热替换）。详见 `docs/INSTALL.md`。
