# reTerminal Sticky Bus ETA

[English](README.md)

一个跑在 reTerminal Sticky（ESP32-S3 + 电子纸）上的公交到站提醒应用。设备上电后自动连接 WiFi、同步时间（NTP + RTC），周期性从公交 API 拉取线路到站时间并显示在电子纸上；同时提供设备端的 WiFi 配置页，可以在屏幕上直接输入/修改 WiFi 和公交站点信息。

> 基于 Seeed 官方 [Sticky Dashboard Demo](https://www.seeedstudio.com/sticky/docs/zh/device-guide/esp-basics/) 二次开发，保留其设备驱动分层。

## 目录

- [功能总览](#功能总览)
- [页面说明](#页面说明)
- [配置系统](#配置系统)
- [WiFi 配置页详解](#wifi-配置页详解)
- [后台数据流与更新逻辑](#后台数据流与更新逻辑)
- [编译与烧录](#编译与烧录)
- [操作方式](#操作方式)
- [工程结构](#工程结构)
- [常见问题](#常见问题)

## 功能总览

- **公交实时到站**：支持新加坡（busrouter.sg）和香港（KMB 九巴）两个数据源，显示线路号、下一班到站时刻和预计分钟数。
- **设备端 WiFi 配置**：GT911 触摸屏 + 虚拟键盘，直接在设备上扫描热点、输入 SSID/密码、保存到 NVS 后自动重启生效。
- **NVS 配置回填**：重启后配置页自动显示已保存的 SSID/密码，随时确认当前配置。
- **NTP 自动校时**：连接成功后多服务器尝试同步，UTC+8 偏移写入 RTC，屏幕时钟与公交站本地时间一致。
- **分钟局刷**：大时钟每分钟局部刷新（电子纸黑白局刷波形），避免整页闪烁。
- **状态图标**：右上角 WiFi 连接状态图标（OK / no wifi）+ 电池电量图标与百分比。
- **自动低功耗**：1 小时没有按硬件按键后显示公交站牌风格的 `Sleep...` 页面并进入深度睡眠；按刷新/AI 键唤醒后回到 Bus 主页面并刷新。

## 页面说明

共 4 个页面，按 `UP`/`DOWN` 按键或左右滑动切换：

| 页面 | 内容 | 数据来源 | 更新时机 |
| --- | --- | --- | --- |
| **Bus**（默认页） | 大时钟、星期/日期、定位图标 + 站名、公交图标、线路到站列表、右上角 WiFi/电池状态 | 后台 `bus_eta` 任务 + RTC + BQ27220 | 见下方「Bus 页更新逻辑」 |
| **Sensor** | SHT40 温度和湿度 | 进入页面时现场读取 `sticky_sht40_read()` | 进入页面 / 刷新时 |
| **Battery** | BQ27220 电池百分比 + 充电状态（闪电图标） | 进入页面时现场读取 `sticky_battery_read()` | 进入页面 / 刷新时 |
| **WiFi Config** | WiFi 扫描列表、SSID/密码输入、虚拟键盘、连接状态 | GT911 触摸 + `esp_wifi` 扫描 + NVS | 见下方「WiFi 配置页详解」 |

### Bus 页布局

```
┌─────────────────────────────────────────────┐
│ 20:35                    Bus ETA  [WiFi][⚡85%]│  ← 右列：标题 + 状态区
│ Sun  2026-09-29                             │
│ 📍 Bef Neil Rd                              │  ← 定位图标 + 站名（最多2行自动换行）
│ 🚌（公交图标）                              │  ← 左列下半
│ ────────────────────────────               │
│ [131]  20:36  1min                          │  ← 线路徽章（黑底白字）+ 到站时刻 + 分钟
│ [132]  20:38  3min                          │
│ [960]  20:41  6min                          │
└─────────────────────────────────────────────┘
```

- 左列：`scale=8` 大时钟（5 字符约 240px 宽）、星期 + 日期、定位图标 + 站名（站名超宽自动换行、超长省略号）、公交车图标（200×200 位图）。
- 右列：`Bus ETA` 标题；右上角状态区 = WiFi 图标（连接成功显示满格，否则显示 no wifi 信号图标）+ 电池边框、填充条和百分比（未读取到时显示 `--%`）。
- 线路列表最多显示 9 条：线路号黑底白字徽章 + 到站时刻（`HH:MM`）+ 预计分钟数（`Xmin`）。
- 无数据时显示当前状态（`No WiFi` / `Loading...` / `HTTP error` / `No arrivals` 等）。

### Bus 页更新逻辑

| 触发 | 行为 |
| --- | --- |
| `BusDataReady`（后台拉完数据） | 同步共享数据到 `AppState`，整页黑白全刷 |
| `ClockMinuteTick`（每分钟） | **局部刷新**：只重画时钟区域 + 补读一次电池 + 重画右上角状态区（局刷波形，不整页闪烁） |
| 切换到 Bus 页 | 同步共享数据 + 读一次电池，整页全刷 |
| 短按 `AI` 键 | 整页全刷 |

> 分钟局刷会顺带补读电池：刚上电 BQ27220 可能未就绪导致首帧电量空白，即使一直停在 Bus 页，1 分钟内也会自动补上。

### Bus Setup 页面

- City、Name、Station ID、Refresh 输入框已加大并纵向均匀排列，右侧为放大的 10×5 虚拟键盘。
- 配置站点时可填写 Name 和/或 Station ID，至少需要一个。填写 Station ID 时，以在线查询到的站点名称为准，会覆盖手动输入的名称；只填名称时会反查并补全 ID。
- Station ID 查询遇到网络错误时会提示重试，不会把未经验证的手填名称与该 ID 一起保存。请确认 WiFi 在线后再保存。
- Refresh 可单独保存，不必同时配置站点。站点信息保持原样时，只更新刷新间隔，不会覆盖数据源、站名或 Station ID。留空时保留已有刷新间隔；若没有已保存值，则使用固件默认值。填写时必须是 1–60 的整数分钟。
- 保存成功后将站点来源、标准站名、ID 和（若填写）刷新间隔写入 NVS，并在 1.5 秒后重启。

## 配置系统

配置优先级：**NVS namespace `config` > 编译期宏（`bus_config.h` 兜底）**。

### NVS 键（优先）

设备端 WiFi 配置页和 Playground 网页烧录器都写这组键，完全一致：

| 键 | 含义 | 示例 |
| --- | --- | --- |
| `wifiSsid` | WiFi 名称 | `"MyWiFi"` |
| `wifiPassword` | WiFi 密码 | `"my-password"` |
| `busProvider` | 公交数据源；含 `hong`/`kmb`（不区分大小写）→ 香港 KMB，其余 → 新加坡 | `"singapore"` / `"hongkongkmb"` |
| `busId` | 站点 ID：新加坡 DataMall BusStopCode（如 `10011`）；香港 KMB 站柱编码（如 `TS000001`） | `"10011"` |
| `busName` | 站点显示名称（Bus 页左上角大字） | `"Bef Neil Rd"` |
| `refreshSeconds` | 自动刷新间隔（秒），i32，范围 30~3600 | `120` |

### 编译期宏（兜底）—— `main/bus_config.h`

```c
#define BUS_WIFI_SSID      "YOUR_WIFI_SSID"   // 占位符，本地开发改成自己的
#define BUS_WIFI_PASSWORD  ""
#define BUS_USE_SINGAPORE  1                   // 1=新加坡 0=香港KMB
#define BUS_STOP_ID        "10011"
#define BUS_STOP_NAME      "Bef Neil Rd"
#define BUS_REFRESH_SECONDS 120                // 默认拉取间隔（NVS refreshSeconds 可覆盖）
#define BUS_NTP_SERVER     "pool.ntp.org"      // NTP 服务器
#define BUS_UTC_OFFSET_SECONDS  (8 * 3600)     // 公交站时区（新加坡/香港均 UTC+8）
```

> ⚠️ **No secrets 规则**：提交到 Playground 仓库时不得包含真实 WiFi 密码，必须保留占位符。本地开发改 `bus_config.h` 或直接在设备上配置。

### 数据源 API

| 数据源 | 接口 | 解析 |
| --- | --- | --- |
| 新加坡 busrouter.sg | `https://arrivelah2.busrouter.sg/?id=<站点ID>` | `services[]` 取 `no`、`next.time`（HH:MM）、`next.duration_ms`（分钟） |
| 香港 KMB | `https://data.etabus.gov.hk/v1/transport/kmb/stop-eta/<站点ID>` | `data[]` 取 `route`、`eta`（ISO 时间，只显示 `eta_seq==1` 的下一班），剩余分钟按 `BUS_UTC_OFFSET_SECONDS` 计算 |

## WiFi 配置页详解

**入口**：按键/滑动翻到第 4 页（Bus → Sensor → Battery → WiFi Config）。

### 页面布局

```
┌────────────────────────────────────────────────────┐
│ WiFi Setup                                         │
│ ┌──────────────┐  q  w  e  r  t  y  u  i  o  p    │
│ │SSID  MyWiFi  │  SFT a  s  d  f  g  h  j  k  l   │
│ ┌──────────────┐  SFT z  x  c  v  b  n  m  [BS]   │
│ │PWD   ••••••• │  0  1  2  3  4  5  6  7  8  9    │
│ ┌──────────────┐  @  .  -  _  空格 CLEAR SAVE     │
│ │① MyWiFi     │                                  │
│ │② MyOffice5G │  Target: SSID                     │
│ │③ ...        │  Connected: MyWiFi                │
│ │             │  (操作反馈: 12 network(s) ...)     │
│ └──────────────┘                                  │
└────────────────────────────────────────────────────┘
```

### 各区域用法

| 区域 | 操作 |
| --- | --- |
| **SSID / PWD 输入行** | 点一下切换输入目标（当前目标显示在键盘上方 `Target:`），随后点键盘按键输入；SSID 最多 32 字符、密码最多 64 字符 |
| **WiFi 列表** | 自动扫描并显示周围热点（最多 12 个，按信号强度降序，列表区显示前 7 行）；**点选某一项** → 自动把 SSID 填入 SSID 行并把输入目标切到 PWD，直接输密码即可 |
| **连接状态行** | `Connected: <SSID>`（已连接）/ `Not connected`（未连接），由 WiFi 事件实时更新 |
| **操作反馈行** | 扫描结果（`12 network(s)`）、保存结果（`Saved! Rebooting...`）、错误信息等 |
| **虚拟键盘** | 见下表 |

### 虚拟键盘

5 行 10 列布局：

```
q  w  e  r  t  y  u  i  o  p
SFT a  s  d  f  g  h  j  k  l
SFT z  x  c  v  b  n  m [BS]
0  1  2  3  4  5  6  7  8  9
@  .  -  _  空格 CLEAR SAVE
```

| 键 | 功能 |
| --- | --- |
| 字母键 | 输入小写；按 `SFT` 后输入大写（键盘上的字母会同步显示大写） |
| `SFT`（×2） | 大小写切换开关：按一下切大写（SFT 键反白高亮），再按回小写 |
| `BS` | 退格删除最后一个字符 |
| `CLEAR` | 清空当前输入行 |
| `SAVE` | 保存到 NVS（namespace `config`）→ 提示 `Saved! Rebooting...` → 1.5 秒后自动重启，重启后新配置生效 |
| 数字 / 符号 | 直接输入对应字符（`@ . - _` 与空格） |

### WiFi 配置页更新逻辑

| 触发 | 行为 |
| --- | --- |
| 进入页面 | 立即触发一次扫描；从 NVS 回填已保存的 SSID/密码到输入框 |
| 扫描任务 | 启动延迟 3 秒，之后每 20 秒扫描一次；完成后刷新列表。**若你正在输入（3 秒内点过键），本次扫描只更新列表数据、不重绘页面**，避免 ePaper 全刷打断输入 |
| 点按输入行 / 列表 / 键盘键 | 更新状态后整页重绘 |
| WiFi 连接 / 断开事件 | 立即更新 `Connected:` 行并重绘，不用等扫描周期 |

> 扫描任务在设备仍处于「占位 SSID 反复重连」状态下会被 `esp_wifi` 拒绝（`STA is connecting`），代码会自动断开 STA 重试一次。

## 后台数据流与更新逻辑

```
WiFi/NTP/HTTP (bus_eta_task, 8KB 栈)
        │ 互斥锁 s_data_mutex 保护 SharedBusData
        ▼
BusDataReady 事件 ──► AppState.bus_eta ──► bus_page_render ──► Canvas ──► 电子纸
```

### 后台主流程（`bus_eta_task`）

1. **WiFi 初始化**：创建 STA + 默认 netif（DHCP 客户端）、注册 WiFi/IP 事件、用 `user_config` 的 SSID/密码 `esp_wifi_start()`。
2. **阻塞等 IP**：最多 30 秒（`kWifiTimeoutMs`）。关联成功（`STA_CONNECTED`）→ 通知配置页显示 SSID；拿到 IP（`GOT_IP`）→ 置事件位。
3. **NTP 校时**：按 `pool.ntp.org` → `ntp.aliyun.com` → `time.google.com` 顺序尝试（各 15 秒超时），成功后把 **UTC 时间 + `BUS_UTC_OFFSET_SECONDS`（UTC+8）** 写入 RTC。NTP 未同步成功前不拉公交数据，避免 RTC 停留在错误旧时间。
4. **周期拉取**：连接且 NTP 就绪后，每 `BUS_REFRESH_SECONDS`（默认 120 秒）拉一次公交数据（HTTP GET + CA 证书验证 + JSON 解析），写入共享区并发 `BusDataReady`。
5. **断线处理**：事件位兜底检测关联是否还活着；`DISCONNECTED` 事件立即把共享区标记为 `No WiFi` 并通知 UI（WiFi 图标马上变 no wifi）。未连接时每 30 秒重试一次（**先 `disconnect` 再 `connect`**：若 SSID 不存在时立即重连会形成 connect→fail 死循环，且让 WiFi 驱动永远处于 connecting，导致配置页扫描被拒绝）。

### 事件流（`app.cpp`）

| 事件 | 触发者 | 处理 |
| --- | --- | --- |
| `ClockMinuteTick` | `app_clock` 定时器（对齐 RTC 下一分钟） | Bus 页：局刷时钟 + 补读电池 + 重画状态区；其他页：局刷状态栏时间 |
| `BusDataReady` | `bus_eta` 后台 | 同步共享数据；在 Bus 页时整页重绘 |
| `WifiScanDone` | 配置页扫描任务 | 在配置页时重绘（输入中跳过） |
| `RefreshPage` | 按键、输入、保存、扫描完成 | 整页重绘当前页 |
| `PreviousPage` / `NextPage` | 按键 / 触摸滑动 | 切页并重绘；进入配置页时请求立即扫描 |
| `EnterDeepSleep` | 长按 AI 键约 2 秒 | 蜂鸣提示 → 保存 `AppState` 到 RTC 内存 → 画睡眠页 → 停触摸 → 屏幕睡眠 → 深度睡眠（AI 键唤醒恢复原页面） |
| `AutoSleep` | 1 小时没有硬件按键事件 | 显示 Sleep 页面并进入深度睡眠；按刷新/AI 键唤醒后回到 Bus 主页面 |

## 编译与烧录

### 环境要求

- reTerminal Sticky（ESP32-S3，32MB Flash，8MB PSRAM）
- **ESP-IDF v5.4**（本工程基于 v5.4 开发；**v5.4.4 下 GT911 触摸会失效**，请勿使用）
- USB-C 数据线

### 一键编译（推荐，v5.4）

工程根目录已准备好一键脚本，**在 PowerShell 中运行**：

```powershell
.\build.ps1
```

脚本会设置 `IDF_PATH=...\esp-idf-v5.4`、`IDF_TOOLS_PATH=D:\Espressif` 等环境变量并执行 `idf.py build`；带参数 `flash` 可编译后烧录（默认串口 `COM93`）：

```powershell
.\build.ps1 flash
```

### 手动编译

在 **ESP-IDF v5.4 终端**（cmd）中：

```bat
cd D:\user\Clair\reTerminal\Sticky\Sticky_Bus_ETA
idf.py set-target esp32s3
idf.py build
idf.py -p COM93 flash monitor
```

如果用的是独立安装的 ESP-IDF（工具在 `D:\Espressif` 而非 `~/.espressif`），PowerShell 里 `export.ps1` 可能报 `Python virtual environment not found`，手动设置后即可：

```powershell
$env:IDF_PATH='D:\Espressif\frameworks\esp-idf-v5.4'
$env:IDF_TOOLS_PATH='D:\Espressif'
$env:IDF_PYTHON_ENV_PATH='D:\Espressif\python_env\idf5.4_py3.11_env'
$env:PATH="D:\Espressif\python_env\idf5.4_py3.11_env\Scripts;D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;D:\Espressif\tools\cmake\3.30.2\bin;D:\Espressif\tools\ninja\1.12.1;D:\Espressif\tools\idf-git\2.44.0\cmd;$env:PATH"
& 'D:\Espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe' 'D:\Espressif\frameworks\esp-idf-v5.4\tools\idf.py' -p COM93 flash monitor
```

### 注意事项

- **切换 IDF 版本必须 `idf.py fullclean`**：v5.4 与 v5.4.4 的工具链（`xtensa-esp-elf` 版本不同）不可混用，旧 build 缓存会导致 ldgen 报错（`Unable to parse section info file ...`）。仅删除 `build/` 也可以。
- **ldgen 偶发失败**：并行编译时偶现 `ParseException: ... libxxx.a`，直接重跑 `idf.py build` 即可，无需处理。
- **串口占用**：烧录报 `PermissionError(13)` / `port is busy` 时，关闭串口监视器（Web Serial、SSCOM 等）再试。

## 操作方式

| 输入 | 功能 |
| --- | --- |
| `UP` / `DOWN` 按键 | 上一页 / 下一页 |
| 向右 / 向左滑动 | 上一页 / 下一页（只识别水平滑动，避免误触） |
| 短按 `AI` | 刷新当前页面 |
| 长按 `AI`（约 2 秒） | 蜂鸣提示后进入深度睡眠；睡眠中按 `AI` 唤醒并恢复原页面 |
| 1 小时没有按硬件按键 | 显示公交站牌图标和 `Sleep...` 后进入深度睡眠；按刷新/AI 键唤醒并回到 Bus 主页面 |
| WiFi 配置页点按 | 选择输入行 / 热点 / 键盘按键（见上） |

## 工程结构

```text
Sticky_Bus_ETA/
|-- main/
|   |-- main.cpp                  启动流程：NVS → 外设 → 初始页面 → 事件循环
|   |-- bus_config.h              用户配置宏（占位符 / 数据源 / 刷新间隔 / 时区）
|   |-- pin_config.h              Sticky 引脚和 I2C 地址定义
|   |-- board/                    共享 I2C/GPIO 总线等板级资源
|   |-- devices/                  硬件驱动：display/buttons/touch/buzzer/
|   |   |                          sht40/rtc/battery/charger/power
|   |-- app/
|   |   |-- app.cpp               事件循环、页面导航、绘制、刷新
|   |   |-- app_data.cpp          设备读取结果 → AppState
|   |   |-- app_clock.cpp         分钟定时器（对齐 RTC 下一分钟）
|   |   |-- app_event.cpp         事件队列
|   |   |-- bus_eta.cpp           WiFi/NTP/HTTP/JSON 后台任务
|   |   |-- user_config.cpp       NVS 优先 / 宏兜底的配置读取
|   |-- pages/                    页面绘制：bus/sensor/battery/wifi_config/sleep + icons.h
|   `-- ui/                       Canvas、点阵字体、状态栏
`-- components/                  第三方组件与屏幕/触摸驱动（gt911、seeed_epaper 等）
```

数据流分层：

```text
硬件 → devices → app_data/共享区 → AppState → pages → Canvas → 电子纸
WiFi/HTTP → bus_eta_task → SharedBusData（互斥锁）→ BusDataReady 事件 → Bus 页
```

## 常见问题

- **触摸没反应 / `sensor=2048x2048`**：确认用的是 **ESP-IDF v5.4**（不是 v5.4.4）。v5.4 下 GT911 正常显示 `sensor=480x800`。
- **保存 WiFi 后重启，配置页输入框是空的**：那是正常现象（内存状态重启清零），但**配置已保存成功**——重启后日志 `User config: wifi=...` 和右上角 WiFi 图标会证明。配置页启动时会自动回填已保存的 SSID/密码。
- **连不上 WiFi / 配置页扫不到热点**：设备用占位 SSID（`YOUR_WIFI_SSID`）反复重连时会占用 STA 状态导致扫描被拒；代码会自动断开重试。正确做法：进配置页输一次真实 WiFi 保存重启。
- **时钟是 UTC 而不是本地时间**：确认 `BUS_UTC_OFFSET_SECONDS` 与公交站时区一致（新加坡/香港均为 `8 * 3600`）。
- **烧录失败 `PermissionError(13)`**：串口被监视器占用，关闭后重试。
- **切换 IDF 版本后构建报 ldgen 错误**：先 `idf.py fullclean`（或删除 `build/`）再构建。
