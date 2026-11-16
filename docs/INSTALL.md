# 安装说明（iStoreOS 25.12.6 / x86_64）

> ⚠️ **适用范围**：内核 `6.12.112` 的 iStoreOS 25.12.6 / OpenWrt 25.12.6
> x86_64。`kmod-oaf` 的 vermagic 与该内核强绑定，其它版本/架构不能装。

---

## 1. 需要哪几个包

| 顺序 | 包 | 作用 | 是否必须 |
|---|---|---|---|
| 1 | `appfilter-7.1.0.apk` | `oafd` 服务、`rule_manager`、init、配置 | 必须 |
| 2 | `kmod-oaf-6.12.112.7.1.0.apk` | 内核 netfilter 模块 `oaf.ko` | 必须 |
| 3 | `luci-app-oaf-7.1.0.apk` | LuCI 界面 | 必须 |
| 4 | `luci-i18n-oaf-zh-cn-26.302.00000.apk` | 简体中文语言包 | 建议 |

三个主包的依赖是 `luci-app-oaf → appfilter + kmod-oaf`，
一起装可让 `apk` 正确解析依赖。若已装过简体中文语言包且文案未变，
可跳过第 4 个包。

---

## 2. 上传并安装

把 4 个 apk 传到路由器 `/tmp/`：

```bash
# 在你的电脑上（Windows 可用 WinSCP / scp）
scp *.apk root@192.168.1.1:/tmp/
```

在路由器 SSH 里执行：

```bash
cd /tmp
apk add --allow-untrusted \
  ./appfilter-7.1.0.apk \
  ./kmod-oaf-6.12.112.7.1.0.apk \
  ./luci-app-oaf-7.1.0.apk \
  ./luci-i18n-oaf-zh-cn-26.302.00000.apk
```

> 若只升级其中一两个包，一次 `apk add` 只列那一两个文件即可，
> 版本号必须**不低于**已装版本，否则 apk 判为降级拒绝安装。
> 正式版使用纯版本号；同版本重新编译的包会带构建尾标（如 `7.1.0-r1`），
> 文件名相应变化，安装时以实际产物为准。详见 `docs/CI.md`。

---

## 3. 重启（关键）

```bash
# 清 LuCI 缓存，避免页面仍是旧版
rm -f /tmp/luci-indexcache.* /tmp/luci-modulecache/* 2>/dev/null
/etc/init.d/uhttpd restart
/etc/init.d/rpcd reload 2>/dev/null

reboot        # 装/换内核模块后必须重启
```

**为什么必须 reboot**：`apk` 只把新的 `oaf.ko` 落到磁盘，
已经加载进内核的旧模块会一直运行到下次开机。不重启时，
页面可能显示新版，但数据面（规则拦截）仍是旧模块。

浏览器按 **Ctrl+F5** 强制刷新，避免 JS/CSS 缓存。

---

## 4. 验收清单

```bash
# 内核模块已加载
lsmod | grep oaf

# 两个进程都在（这一条最关键）
ps w | grep -E 'oafd|rule_manager' | grep -v grep

# 两个文件有执行位
ls -l /usr/bin/oafd /usr/bin/rule_manager /usr/bin/hnat.sh

# 内核版本与 vermagic
uname -r                                   # 6.12.112
modinfo /lib/modules/6.12.112/oaf.ko | grep vermagic

# 配置文件就绪
uci show fwx.advanced.disable_hnat
ls -l /etc/config/urlfilter
```

界面侧逐项确认：

- [ ] 所有页面布局正常（PC 与手机各看一遍）
- [ ] 应用过滤规则：给某设备禁止某应用，实测被拦截
- [ ] 上网控制规则：给某设备设时段限制，实测生效
- [ ] URL 黑名单：把 `www.baidu.com` 加进某设备黑名单，该设备访问被拦截
- [ ] 终端详情页能看到 URL 访问记录
- [ ] 移动端白名单页不能左右拖动

---

## 5. 已知边界

- URL 黑/白名单只对**能解析出 HTTP Host / HTTPS SNI 的 TCP 连接**生效。
  纯 IP 访问、DoH/DoT、QUIC/HTTP3(UDP) 不在拦截范围。
- 硬件/软件加速（HNAT / flow offloading）可能绕过内核过滤。
  若规则不生效，在「设置 → 高级」勾选**自动关闭加速**并重启，
  或检查：
  ```bash
  uci get fwx.advanced.disable_hnat
  uci get firewall.@defaults[0].flow_offloading
  cat /sys/kernel/debug/hnat/hook_toggle 2>/dev/null
  ```

---

## 6. 安装后规则不生效时

先确认三件事：

```bash
lsmod | grep oaf                          # 内核模块已加载
ps w | grep -E 'oafd|rule_manager' | grep -v grep   # 两个进程都在
ls -l /usr/bin/oafd /usr/bin/rule_manager /usr/bin/hnat.sh   # 均为 755
```

若三者都正常，检查硬件加速是否关闭（见上一节「已知边界」），
关闭加速后重启再测。刚换过内核模块必须 reboot。
