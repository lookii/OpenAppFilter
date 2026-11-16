# OpenAppFilter (OAF) — 7.1.0

基于 [destan19/OpenAppFilter](https://github.com/destan19/OpenAppFilter) 的衍生版本，
面向 **iStoreOS 25.12.6 / OpenWrt 25.12.6 (x86_64, linux-6.12.112)**，
包含界面重构、URL 访问记录 / 黑白名单、多用户管控规则。

> 本项目为 GPL-2.0 衍生版本，保留上游 LICENSE 与出处。个人可免费使用、
> 二次开发与分发；商业使用请联系上游作者授权。详见 `LICENSE` 与
> `README.upstream.md`。

---

## 这个版本包含什么

| 模块 | 目录 | 作用 |
|---|---|---|
| LuCI 界面 | `luci-app-oaf/` | Web UI：仪表板、用户/终端详情、应用过滤、上网控制、URL 访问记录与黑白名单、设置页 |
| 服务进程 | `open-app-filter/` | `oafd`（ubus 接口、DPI 数据落库）、`rule_manager`（规则下发内核）、init 脚本、配置 |
| 内核模块 | `oaf/` | `oaf.ko`：netfilter 钩子、DPI、URL 规则匹配与拦截 |

三个包配合完成管控。构建产物不随仓库提交；安装包由 GitHub Actions 自动编译，
或从 Release 页面下载。

### 相对上游的主要改动

**功能**
- **二阶段 URL 访问记录**：内核侧 `/proc/net/af_url_log` 环形缓冲（512 条、
  5 秒去重、单调递增 seq），用户态 sqlite 落库 + 2 秒采集 + 按天清理。
- **URL 黑/白名单**：per-MAC 规则表、大小写不敏感后缀匹配、黑名单优先；
  同一域名不能同时进黑白名单；设置页可勾选用户/天数/总开关。
- **多用户管控规则**：规则可绑定多个 MAC（`user_macs`），向后兼容单值
  `user_mac`；详情/缓存/今日状态三条路径统一匹配。
- **应用记录导出**：按筛选条件导出全量 CSV（逐页拉取，解决后端 200 条上限）。
- 规则/名单导入导出、拼音检索、设置页去重等。

**界面**
- 移动端与 PC 自适应重构；终端详情页 URL 访问记录独立标签页。
- 状态标签三色与 PC 端定位、白名单整页禁横向拖动、ECharts 图表居中与
  图例多行、表格列宽 / 表头居中 / 省略号按钮、顶栏残影、文字对比度等。

完整变更记录见 [CHANGELOG.md](CHANGELOG.md)。

---

## 快速开始

| 我想… | 看这里 |
|---|---|
| 装到路由器上 | [docs/INSTALL.md](docs/INSTALL.md) |
| 用 GitHub Actions 自动编译 apk | [docs/CI.md](docs/CI.md) |
| 在本地用 SDK 手动编译 | [docs/BUILD.md](docs/BUILD.md) |

---

## 目录结构

```
.
├── luci-app-oaf/            LuCI 界面源码（Makefile / luasrc / htdocs / po）
├── open-app-filter/         服务进程源码 + files/（配置、init、rule_manager）
├── oaf/                     内核模块源码（Makefile / src/*.c,*.h）
├── .github/workflows/       GitHub Actions
│   ├── build-apk.yml         自动编译 4 个 apk（SDK）
│   └── validate.yml          源码完整性 / JS / Lua 语法校验
├── scripts/                  编译、打包、校验脚本
├── pkgdata/                  apk 清单 / 配置保护素材（打包用）
├── prebuilt/                 中文语言包 lmo 素材（打包用）
├── docs/                     CI / 编译 / 安装文档
├── LICENSE                   GPL-2.0（上游保留）
├── README.upstream.md        上游 README 原文
└── CHANGELOG.md              版本变更记录
```

---

## 编译与构建

标准 OpenWrt 包结构，三个包各自带 `Makefile`，可用 25.12.6 SDK 编译：

```bash
make package/oaf/compile V=s
make package/open-app-filter/compile V=s
make package/luci-app-oaf/compile V=s
```

但**推荐用本仓库的 CI/脚本**，因为它会产出与 iStoreOS 25.12.6 固件
`installed` 数据库元数据对齐、可直接覆盖安装的 apk（标准 `make` 产物
的版本号由 Makefile 的 `PKG_RELEASE` 决定，与本仓库发布版本号体系独立）。
详见 `docs/BUILD.md`。

> ⚠️ 内核模块 vermagic 与内核版本强绑定。本版本仅适用于
> **linux-6.12.112 / x86_64**。更换固件内核版本必须同步更换 SDK。

---

## 版本号约定

正式发行版使用纯版本号，不带构建尾标；仅当功能版本不变、需要重新编译时，
才追加构建尾标形成递增版本。详见 `docs/CI.md`。

---

## 致谢与许可

- 上游项目：[destan19/OpenAppFilter](https://github.com/destan19/OpenAppFilter)，
  官网 [www.openappfilter.com](http://www.openappfilter.com)
- 许可：GPL-2.0，详见 `LICENSE`；上游 README 原文见 `README.upstream.md`
- 讨论组：https://t.me/openappfilter
