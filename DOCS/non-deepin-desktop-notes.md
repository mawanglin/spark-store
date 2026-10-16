# 非 deepin 桌面（Kubuntu / KDE / GNOME…）适配说明

星火商店 4.x 客户端基于 DTK（deepin 工具集）开发，默认按 DDE 桌面的习惯启动。
这份文档记录在非 deepin 桌面上「字体不显示 / 界面文字异常 / 主题错乱」的成因，
以及本仓库为适配所做的改动和排查方法。

## 1. 客户端是怎么拿到字体的

- 工程里**不带任何字体**，`.ui` 文件里也只写了字号，没有字体族；
- 原生控件（标题栏、左侧菜单、设置页）的文字来自 **Qt 平台主题 / 当前调色板**；
- 首页列表、应用详情页实际是 `QWebEngineView`（Chromium），文字由 **fontconfig** 决定。

因此「字体不显示」有两个层面：**用错了字体来源**（程序的问题）与 **系统字体链损坏**
（环境的问题，Chromium 比原生控件敏感得多：字体文件存在但为空会一个字都不画）。

## 2. 本次去掉的三处 deepin 硬编码

| 位置 | 旧行为 | 新行为 |
| --- | --- | --- |
| `Utils::isDDE()` | 非 DDE 就把 `XDG_CURRENT_DESKTOP` 改写成 `Deepin` | 只判断，不改环境变量（改写会让 xdg-desktop-portal 选错后端，`color-scheme` 读不到） |
| `Utils::setQPAPlatform()` | 无条件设 `QT_QPA_PLATFORM=dxcb;xcb`（X11） | 只在 deepin 系环境启用 `dxcb`；其它发行版保留 Qt 默认插件；用户已设置则不再覆盖 |
| `main.cpp` | 伪造 argv 注入 `-platformtheme deepin` | 只在 deepin 系环境注入（否则会绕过 KDE 的 plasma-integration，丢掉系统字体/配色） |

新增 `Utils::isDeepinFamily()`：会话变量 + `/etc/os-release` 判断，
不依赖 `QCoreApplication`，可以在 `QApplication` 构造之前调用。

> deepin / UOS 上的行为与改动前一致，差异只出现在其它发行版。

## 3. 背景色与文字色同源

窗口（`DBlurEffectWidget`）与卡片的背景色原先写死成 `#2a2b2b / #f3f7f8`、
`#252525 / #fbfbfb / #ffffff`，还会额外写死 `QLabel` 的文字色；
而其它文字颜色来自 DTK 调色板。两套来源在非 DDE 桌面上可能不一致，
结果就是**文字画了但看不见**（白字画在浅色底上）。

现在背景/描边色统一由当前调色板派生：

```cpp
Utils::paletteWindowColor(fallback)      // 窗口底色
Utils::paletteCardColor(dark)            // 卡片底色
Utils::paletteCardBorderColor(dark)      // 卡片描边
```

顺带修正 `ThemeChecker`：D-Bus 读不到 `color-scheme` 时不再跳过调色板同步，
两个判断都未知时以当前调色板兜底判断深色/浅色。

## 4. 排查：先看这行日志

启动时会打印一份 UI 环境快照：

```
UI environment:
  platform plugin      : xcb
  default font         : Noto Sans CJK SC (10.5pt)
  logical dpi          : 96
  dark theme           : no
  XDG_CURRENT_DESKTOP  : KDE
  QT_QPA_PLATFORM      : (空)
  QT_QPA_PLATFORMTHEME : (空)
```

- `platform plugin` 变成 `dxcb`：说明还在用 deepin 平台插件（本机被判定为 deepin 系？看 `/etc/os-release`）；
- `default font` 为空/异常：问题在系统 fontconfig，先 `fc-match sans-serif`、`fc-match "Noto Sans CJK SC"`；
- `dark theme` 与界面实际观感不符：主题判断问题，检查 `color-scheme` 是否能读到。

再配合系统侧检查：

```bash
fc-match sans-serif            # 默认无衬线字体解析到哪个文件
fc-list | wc -l                # 字体数量
cat /etc/fonts/local.conf ~/.config/fontconfig/fonts.conf   # 自定义别名（尤其是 binding="strong"）
rm -rf ~/.cache/fontconfig && fc-cache -f -v                # 字体缓存损坏时重建
```

## 5. 覆盖与开关

| 变量 | 作用 |
| --- | --- |
| `QT_QPA_PLATFORM` | 已设置则客户端不再覆盖（例如 `QT_QPA_PLATFORM=xcb` 强制 X11） |
| `QT_QPA_PLATFORMTHEME` | 同上，交给系统和桌面环境 |

## 6. 构建安装包

本地打 deb：`dpkg-buildpackage -b -us -uc`
（`debian/rules` 的并行度取 `$(JOBS)`，未设置时按 CPU 核数；也可用 `DEB_BUILD_OPTIONS=parallel=N`）。

CI：`.github/workflows/package-linux.yml` 在 Ubuntu 22.04 上构建三种包：

| 格式 | 做法 | 定位 |
| --- | --- | --- |
| `.deb` | `dpkg-buildpackage` | 发行版规范打包，主产物 |
| `.rpm` | 由 `.deb` 用 fpm 转换，失败回退 alien | 便于在 Fedora / openSUSE 上试装 |
| `.AppImage` | 取 `.deb` 的载荷 + linuxdeploy / linuxdeploy-plugin-qt | 免安装，`chmod +x` 后直接跑 |

产物下载位置：

1. Actions → 对应运行记录 → 页面底部 **Artifacts** → `spark-store-packages-ubuntu-22.04`
   （zip，内含三种包 + `SHA256SUMS.txt`）；
2. 命令行：`gh run download -R <owner>/<repo> -n spark-store-packages-ubuntu-22.04`；
3. 打 tag（`refs/tags/*`）时会**自动附加到同名 Release**，可在 Releases 页面直接下载。

rpm / AppImage 属于“尽力而为”的附加产物：失败不会让运行变红，只会在运行摘要与 Annotation
里以 warning 标出；`.deb` 失败才会让运行变红。

> 注意：AppImage 只打包主程序（GUI）。安装/升级功能依赖 aptss/apt，属于 Debian 系专有，
> 在非 Debian 系统上请用该发行版自身的方式安装软件。
