# reTerminal Sticky Bus ETA

[中文](README-ZH.md)

A bus arrival ETA app for the reTerminal Sticky (ESP32-S3 + e-paper). On boot it connects to WiFi, syncs time (NTP + RTC), periodically fetches bus arrival data from a public API and renders it on the e-paper. A device-side WiFi config page lets you scan hotspots, type SSID/password on the touchscreen, save to NVS and reboot into the new config. A Bus Setup page lets you choose the city (Singapore / Hong Kong) and type the stop name/ID on the touchscreen without re-flashing.

> Built on top of Seeed's official [Sticky Dashboard Demo](https://www.seeedstudio.com/sticky/docs/zh/device-guide/esp-basics/), keeping its device-driver layering.

## Pages (4, switch with UP/DOWN buttons or horizontal swipe)

| Page | Content | Data source | Refresh |
| --- | --- | --- | --- |
| **Bus** (default) | Big clock, weekday/date, pin icon + stop name, bus icon, arrival list, WiFi/battery status icons | background `bus_eta` task + RTC + BQ27220 | see below |
| **Sensor** | SHT40 temperature & humidity | read on page enter | on enter / AI-press refresh |
| **Battery** | BQ27220 percentage + charging bolt icon | read on page enter | on enter / AI-press refresh |
| **WiFi Config** | AP scan list, SSID/PWD rows, virtual keyboard, connection status | GT911 touch + `esp_wifi` scan + NVS | see below |

### Bus Setup page

- The City, Name, Station ID and Refresh rows are enlarged and spaced down the left side; the enlarged 10×5 keyboard is arranged on the right.
- Station setup can save Name and/or Station ID; at least one is needed when changing the station. If a Station ID is entered, its online lookup is authoritative and replaces any typed name with the directory name. A name alone is looked up to fill the ID. If ID lookup cannot reach the service, saving is stopped so an unverified name is not stored beside that ID.
- Refresh is optional and can be saved by itself. When the displayed station settings are left unchanged, saving a refresh interval updates only that interval and preserves the provider, name and Station ID. Clear Refresh to keep the saved interval; if none is saved, firmware default is used. A supplied value must be 1–60 whole minutes.
- `SAVE` writes `busProvider` (`singapore` / `hongkongkmb`), resolved `busName` and `busId` to NVS, shows `Saved! Rebooting...` and auto-reboots after 1.5 s.
- Saved values are backfilled from NVS on startup, so you can confirm the active city/stop at a glance.
- Like the WiFi page, this page uses partial refresh; a full monochrome refresh is inserted every 25 partial refreshes to clear A2 ghosting.

### Bus page

- Left column: scale-8 clock, `Sun 2026-09-29` date line, pin icon + stop name (auto wrap, max 2 lines), 200×200 bus bitmap icon.
- Right column: `Bus ETA` title; top-right status area = WiFi icon (full bars when connected, no-wifi bars otherwise) + battery outline/fill/percent (`--%` until first valid read); up to 9 arrival rows: black route badge + `HH:MM` arrival + `Xmin`.
- When no data: shows current status (`No WiFi` / `Loading...` / `HTTP error` / `No arrivals`).

**Bus page update logic**

| Trigger | Behavior |
| --- | --- |
| `BusDataReady` (data fetched) | sync shared data, full monochrome refresh |
| `ClockMinuteTick` (every minute) | **partial refresh** of clock area + battery re-read + status area redraw |
| Enter Bus page | sync data + read battery once, full refresh |
| Short-press `AI` | full refresh |

The minute partial refresh re-reads the battery: on cold boot the BQ27220 may not be ready, so even if you stay on Bus, the percent fills in within a minute.

## Configuration

Priority: **NVS namespace `config` > compile-time macros (`bus_config.h`)**.

### NVS keys (highest priority)

Written by both the device config page and the Playground web flasher:

| Key | Meaning | Example |
| --- | --- | --- |
| `wifiSsid` | WiFi SSID | `"MyWiFi"` |
| `wifiPassword` | WiFi password | `"my-password"` |
| `busProvider` | `hong`/`kmb` (case-insensitive) → HK KMB, anything else → Singapore. Written by the Bus Setup page | `"singapore"` |
| `busId` | Singapore DataMall BusStopCode (e.g. `10011`) or HK KMB stop code (e.g. `TS000001`). Written by the Bus Setup page | `"10011"` |
| `busName` | Stop display name. Written by the Bus Setup page | `"Bef Neil Rd"` |
| `refreshSeconds` | Auto-refresh interval in seconds (i32, 30–3600) | `120` |

### Compile-time macros (`main/bus_config.h`)

```c
#define BUS_WIFI_SSID        "YOUR_WIFI_SSID"   // placeholder; set yours locally
#define BUS_WIFI_PASSWORD    ""
#define BUS_USE_SINGAPORE    1                  // 1 = Singapore, 0 = HK KMB
#define BUS_STOP_ID          "10011"
#define BUS_STOP_NAME        "Bef Neil Rd"
#define BUS_REFRESH_SECONDS  120                // default interval (NVS refreshSeconds overrides)
#define BUS_NTP_SERVER       "pool.ntp.org"
#define BUS_UTC_OFFSET_SECONDS (8 * 3600)       // SGT/HKT are both UTC+8
```

> ⚠️ **No-secrets rule**: keep placeholders before publishing to the Playground registry — never commit real WiFi passwords.

### Data sources

| Source | Endpoint | Parsing |
| --- | --- | --- |
| Singapore busrouter.sg | `https://arrivelah2.busrouter.sg/?id=<stop>` | `services[]`: `no`, `next.time` (HH:MM), `next.duration_ms` |
| Hong Kong KMB | `https://data.etabus.gov.hk/v1/transport/kmb/stop-eta/<stop>` | `data[]`: `route`, `eta` (ISO time, `eta_seq==1` only), minutes computed with `BUS_UTC_OFFSET_SECONDS` |

## WiFi Config page

### Layout

```
┌────────────────────────────────────────────────────┐
│ WiFi Setup                                         │
│ ┌──────────────┐  q  w  e  r  t  y  u  i  o  p    │
│ │SSID  MyWiFi  │  SFT a  s  d  f  g  h  j  k  l   │
│ ┌──────────────┐  SFT z  x  c  v  b  n  m [BS]    │
│ │PWD   ••••••• │  0  1  2  3  4  5  6  7  8  9    │
│ ┌──────────────┐  @  .  -  _  sp  CLEAR SAVE      │
│ │① MyWiFi     │                                  │
│ │② MyOffice5G │  Target: SSID                     │
│ │③ ...        │  Connected: MyWiFi                │
│ │             │  (feedback: 12 network(s) ...)    │
│ └──────────────┘                                  │
└────────────────────────────────────────────────────┘
```

### Usage

| Area | Action |
| --- | --- |
| **SSID / PWD rows** | tap to switch input target (shown as `Target:` above keyboard); max 32 / 64 chars |
| **AP list** | auto-scanned (max 12 APs, sorted by RSSI; 7 rows shown); **tapping an entry fills SSID and switches target to PWD** |
| **Connection line** | `Connected: <SSID>` / `Not connected`, updated live by WiFi events |
| **Feedback line** | scan count, save result, errors |
| **Keyboard** | see below |

### Keyboard

```
q  w  e  r  t  y  u  i  o  p
SFT a  s  d  f  g  h  j  k  l
SFT z  x  c  v  b  n  m [BS]
0  1  2  3  4  5  6  7  8  9
@  .  -  _  sp  CLEAR SAVE
```

- Letter keys type lowercase; with `SFT` on they type uppercase (letters render uppercase too, SFT key inverts).
- `BS` backspace, `CLEAR` clear row, `SAVE` → write NVS → `Saved! Rebooting...` → auto reboot after 1.5 s.
- Digits/symbols insert directly.

### Config-page update logic

| Trigger | Behavior |
| --- | --- |
| Enter page | trigger an immediate scan; backfill saved SSID/password from NVS |
| Scan task | starts 3 s after boot, then every 20 s; **skips full redraw if you typed within the last 3 s** so input is not interrupted (data still updates, shows on next keypress) |
| Tap row / AP / key | update state, full redraw |
| WiFi connected/disconnected event | updates `Connected:` line immediately |

> While the device is stuck re-connecting to a placeholder SSID, `esp_wifi_scan_start` may be refused (`STA is connecting`); the code disconnects and retries once.

## Background flow (`bus_eta_task`)

1. **WiFi init**: STA + default netif (DHCP) + event registration; connect with `user_config` SSID/password.
2. **Block for IP** (max 30 s). `STA_CONNECTED` notifies the config page of the SSID; `GOT_IP` sets the event bit.
3. **NTP sync**: tries `pool.ntp.org` → `ntp.aliyun.com` → `time.google.com` (15 s each); on success writes **UTC + `BUS_UTC_OFFSET_SECONDS` (UTC+8)** to the RTC. Bus data is not fetched until NTP is synced, so the RTC never shows a stale wrong time.
4. **Periodic fetch**: every `BUS_REFRESH_SECONDS` (default 120 s), HTTP GET + CA bundle verify + JSON parse → shared data under mutex → `BusDataReady` event.
5. **Disconnect handling**: event bits track association; `DISCONNECTED` immediately marks `No WiFi` and notifies UI (icon flips right away). While disconnected, retries every 30 s (**disconnect before connect**: immediate reconnect to a non-existent SSID causes a connect→fail loop and keeps the driver stuck in `connecting`, which blocks scanning).

### Event loop (`app.cpp`)

| Event | Source | Handling |
| --- | --- | --- |
| `ClockMinuteTick` | `app_clock` timer (aligned to next RTC minute) | Bus: partial-refresh clock + battery re-read + status area; Bus Setup / WiFi Config: partial-refresh bottom-right clock; others: partial-refresh status-bar time |
| `BusDataReady` | background task | sync shared data; full refresh when on Bus |
| `WifiScanDone` | config scan task | redraw config page (skipped during input) |
| `RefreshPage` | buttons, input, save, scan | full redraw of current page |
| `PreviousPage` / `NextPage` | buttons / swipe | switch page; entering WiFi Config triggers immediate scan |
| `EnterDeepSleep` | long-press AI (~2 s) | beep → save `AppState` to RTC mem → sleep page → stop touch → display sleep → deep sleep (AI wakes and restores page) |
| `AutoSleep` | 1 hour without a hardware button press | sleep page → display/deep sleep; refresh key wakes directly to the Bus page |

## Build & flash

- Target: ESP32-S3, 32 MB flash, 8 MB PSRAM.
- **Use ESP-IDF v5.4** — the project is built on v5.4; **GT911 touch is broken under v5.4.4**, so don't use it.

In an **ESP-IDF v5.4 terminal**:

```bat
cd D:\user\Clair\reTerminal\Sticky\Sticky_Bus_ETA
idf.py set-target esp32s3
idf.py build
idf.py -p COM93 flash monitor
```

If you installed the toolchain into `D:\Espressif` (not `~/.espressif`) and `export.ps1` reports `Python virtual environment not found`, set the env manually (see README-ZH for the exact PowerShell snippet) or use the included `build.ps1`.

Notes:
- **Always `idf.py fullclean` after switching IDF versions** — v5.4 and v5.4.4 use different `xtensa-esp-elf` toolchains; stale caches cause ldgen errors (`Unable to parse section info file ...`). Deleting `build/` works too.
- **ldgen flaky failure**: occasional `ParseException ... libxxx.a` under parallel builds — just rerun `idf.py build`.
- **Port busy** (`PermissionError(13)`): close serial monitors (Web Serial, SSCOM, etc.) and retry.

## Operation

| Input | Action |
| --- | --- |
| `UP` / `DOWN` | previous / next page |
| swipe right / left | previous / next page (horizontal only, avoids false triggers) |
| short-press `AI` | refresh current page |
| long-press `AI` (~2 s) | beep + deep sleep; press `AI` to wake and restore page |
| no hardware button press for 1 hour | show `Sleep...` with a bus-stop sign, then enter deep sleep; press the refresh/AI key to wake to Bus and refresh data |
| tap on WiFi Config page | select input row / AP / keyboard key |

## Project layout

```text
Sticky_Bus_ETA/
|-- main/
|   |-- main.cpp                  boot: NVS -> peripherals -> first page -> event loop
|   |-- bus_config.h              user config macros (placeholders/source/interval/timezone)
|   |-- pin_config.h              Sticky pins & I2C addresses
|   |-- board/                    shared I2C/GPIO board resources
|   |-- devices/                  display/buttons/touch/buzzer/sht40/rtc/battery/charger/power
|   |-- app/
|   |   |-- app.cpp               event loop, page nav, render, refresh
|   |   |-- app_data.cpp          device reads -> AppState
|   |   |-- app_clock.cpp         minute timer aligned to RTC boundary
|   |   |-- app_event.cpp         event queue
|   |   |-- bus_eta.cpp           WiFi/NTP/HTTP/JSON background task
|   |   |-- user_config.cpp       NVS-first config loader
|   |-- pages/                    bus/sensor/battery/wifi_config/sleep + icons.h
|   `-- ui/                       Canvas, bitmap font, status bar
`-- components/                   third-party + screen/touch drivers (gt911, seeed_epaper...)
```

Data flow: `hardware → devices → app_data/shared → AppState → pages → Canvas → e-paper`; `WiFi/HTTP → bus_eta_task → SharedBusData (mutex) → BusDataReady → Bus page`.

## Troubleshooting

- **Touch dead / `sensor=2048x2048`**: make sure you are on **ESP-IDF v5.4** (not v5.4.4). On v5.4 GT911 reports `sensor=480x800`.
- **Config page inputs empty after reboot**: normal — RAM resets, but NVS saved fine (log `User config: wifi=...` and the WiFi icon prove it). The page backfills saved SSID/password on startup.
- **Can't connect / no APs in scan**: the device re-connecting to a placeholder SSID occupies STA state; the code disconnects and retries. Configure a real WiFi on the device and reboot.
- **Clock shows UTC**: check `BUS_UTC_OFFSET_SECONDS` matches the stop's timezone (SGT/HKT = `8 * 3600`).
- **Flash fails `PermissionError(13)`**: close the serial monitor and retry.
- **ldgen errors after switching IDF versions**: `idf.py fullclean` (or delete `build/`) then rebuild.
