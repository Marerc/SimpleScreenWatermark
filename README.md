# SimpleScreenMark

一个轻量级的 Windows 屏幕水印工具。在屏幕上显示半透明的水印文字（主机名、IP 地址、日期时间等），常用于屏幕录制、演示或安全审计场景。

[English](README_EN.md)

<!-- 截图占位：可替换为实际截图 -->
<!-- ![screenshot](screenshot.png) -->

## 功能特性

- 在屏幕上显示可自定义的半透明水印
- 支持模板变量：`{hostname}`、`{ip}`、`{time}`
- 多显示器支持，每个屏幕独立覆盖
- 系统托盘图标，右键菜单操作
- 全局快捷键切换显示/隐藏（默认 `Ctrl+W`）
- INI 配置文件，修改后自动热重载
- 开机自启动（可选）
- 单实例运行，无外部依赖
- 单个 exe 文件，开箱即用

## 下载

从 [GitHub Releases](../../releases/latest) 页面下载最新的 `SimpleScreenMark.exe`，直接运行即可。

## 从源码构建

**环境要求：** CMake 3.15+、Visual Studio 2019+（MSVC）

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

构建产物位于 `build/Release/SimpleScreenMark.exe`。

## 配置说明

首次运行会自动在 `%APPDATA%\SimpleScreenMark\config.ini` 创建默认配置文件。右键托盘图标选择「编辑设置」可直接打开编辑，保存后自动生效。

### [Watermark] 水印设置

| 键 | 默认值 | 说明 |
|---|---|---|
| Template | `{hostname} {ip} {time}` | 水印文本模板 |
| FontName | `宋体` | 字体名称 |
| FontSize | `20` | 字号 |
| FontColor | `808080` | 字体颜色（十六进制 RGB） |
| Opacity | `50` | 不透明度（0-255） |
| SpacingX | `700` | 水平间距（像素） |
| SpacingY | `400` | 垂直间距（像素） |
| Angle | `-42` | 旋转角度 |
| Aliased | `1` | 锯齿渲染（1=开启，0=抗锯齿） |

### [Time] 时间设置

| 键 | 默认值 | 说明 |
|---|---|---|
| Format | `%Y-%m-%d` | 时间格式（strftime 格式） |
| RefreshInterval | `1` | 刷新间隔（秒，0=不刷新） |
| RandomRefreshRange | `0` | 随机附加刷新时间（秒，实际间隔 = RefreshInterval + 随机[0, RandomRefreshRange]） |

### [Dynamic] 动态偏移设置

| 键 | 默认值 | 说明 |
|---|---|---|
| RandomOffsetX | `0` | 每次刷新时水印 X 轴随机偏移范围（像素，0=不偏移） |
| RandomOffsetY | `0` | 每次刷新时水印 Y 轴随机偏移范围（像素，0=不偏移） |

### [Network] 网络设置

| 键 | 默认值 | 说明 |
|---|---|---|
| NIC | `auto` | 网卡名称（`auto`=自动选择） |

### [Hotkey] 快捷键设置

| 键 | 默认值 | 说明 |
|---|---|---|
| Hotkey | `Ctrl+W` | 切换显示/隐藏快捷键 |

支持的修饰键：`Ctrl`、`Alt`、`Shift`、`Win`。支持的按键：`A-Z`、`F1-F24`、`Space`、`Tab`、`Escape`。


### 模板变量

| 变量 | 说明 |
|---|---|
| `{hostname}` | 计算机主机名 |
| `{ip}` | 本机 IP 地址 |
| `{time}` | 当前时间（按 Format 格式化） |

## 许可证

[MIT License](LICENSE)
