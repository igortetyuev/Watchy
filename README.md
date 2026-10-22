# Watchy - Fully Open Source E-Paper Watch

> **This is a fork** of [sqfmi/Watchy](https://github.com/sqfmi/Watchy) 1.4.14
> with a localized UI, a multi-network Wi-Fi list, a user-settable time zone,
> switchable watch faces and a daily alarm. It is used by
> [igortetyuev/watchy-firmware](https://github.com/igortetyuev/watchy-firmware)
> as a git submodule. See [Changes in this fork](#changes-in-this-fork) below;
> the upstream README follows.

![Watchy](https://watchy.sqfmi.com/img/watchy_render.png)

**Buy Watchy from [Mouser](https://www.mouser.com/ProductDetail/SQFMI/SQFMI-WATCHY-10?qs=DRkmTr78QARN9VSJRzqRxw%3D%3D), [The Pi Hut](https://thepihut.com/collections/sqfmi), and [Crowd Supply](https://www.crowdsupply.com/sqfmi/watchy)**

[**Watchy Case & Accessories**](https://shop.sqfmi.com)

## Getting Started Guide
Follow the instructions here https://watchy.sqfmi.com/docs/getting-started

### Have Fun! :)

### Got Questions?

Join our [Discord](https://discord.gg/ZXDegGV8E7)

---

## Changes in this fork

Everything below is additive to the upstream API unless stated otherwise.
Tested on Watchy v2.0 (ESP32-PICO-D4, PCF8563); the v3 (ESP32-S3) paths are
kept but not exercised.

### Localization (i18n)

UI strings are no longer literals in `Watchy.cpp`. They live in
`lang/<code>.json` (one flat `"key": "text"` object per language; `en.json` is
the reference and defines key order) and are compiled into a generated header:

```
lang/en.json  ru.json  de.json  es.json  fr.json  pt.json
tools/gen_lang.py        PlatformIO pre-script -> include/lang_strings.h
src/Latin9pt7b.h         FreeSans 9 pt, glyphs 0x20-0xFF (ISO-8859-1)
src/Cyrillic9pt7b.h      FreeSans-style 9 pt, Cyrillic at codes 0x80-0xC1
```

`lang_strings.h` defines `LANG_CODE`, `LANG_CHARSET_LATIN1` or
`LANG_CHARSET_CYRILLIC`, `enum StrId { S_MENU_ABOUT, …, S__COUNT }` and
`static const char *const STR[S__COUNT]`. `Watchy.h` includes it and derives:

- `LANG_FONT` — the GFX font to use for UI text (`Latin9pt7b` or
  `Cyrillic9pt7b`); use `display.setFont(&LANG_FONT)`.
- `printL(const char *utf8)` / `printlnL(...)` and the `StrId` overloads
  `printL(S_ALARM_TITLE)` — decode UTF-8 and map code points to the active
  font's single-byte codes via `langCpToByte()`. **Always print non-ASCII
  text through these**; `display.print("é")` writes raw UTF-8 bytes and shows
  garbage. ASCII and digits pass through, so `display.print(42)` is fine.
- Replaces the earlier `printRU`/`printlnRU` helpers (removed).

**Using the library from a PlatformIO project:**

```ini
extra_scripts = pre:lib/Watchy/tools/gen_lang.py
build_flags   = -I include
```

`gen_lang.py` reads `LANG=<code>` from `<project>/.env` (default `en`), writes
`<project>/include/lang_strings.h` only when the content changes, and aborts
the build if the chosen language is missing keys present in `en.json` (or has
extra ones). The `-I include` flag makes the header visible to the library
sources as well as the sketch. The script resolves the language files as
`$PROJECT_DIR/lib/Watchy/lang`, i.e. it expects to live at `lib/Watchy`.

Arduino IDE users: run `python3 tools/gen_lang.py` is not possible (it is an
SCons script); instead generate the header once with PlatformIO or write
`lang_strings.h` by hand following the format above and put it next to
`Watchy.h`.

**Adding a language:** copy `lang/en.json`, translate every value (a 9 pt
line holds ≈18–20 characters on the 200 px screen, so keep them short), set
`"_charset"` to `latin1` or `cyrillic`. A script that needs other glyphs needs a
new font (generate with Adafruit's `fontconvert`, see the header comment of
`Latin9pt7b.h`) plus a `#if` branch in `langCpToByte()` and the font selection
in `Watchy.h`. Keys `wday_1..7` and `month_1..12` must stay consecutive —
sketches index them as `STR[S_WDAY_1 + wday - 1]`.

### Watch faces

```cpp
virtual uint8_t faceCount();          // default 1
virtual void    drawFace(uint8_t idx); // default: upstream digital face
void            showFaces();           // picker screen (menu item "Watch face")
extern RTC_DATA_ATTR uint8_t faceIndex;
```

`drawWatchFace()` now calls `drawFace(faceIndex)`. A sketch overrides
`faceCount()`/`drawFace()` instead of `drawWatchFace()` to offer several faces;
the picker cycles them with UP/DOWN (full refresh per preview), MENU saves,
BACK cancels. The selection is persisted in NVS (`face`) and restored on reset.

### Wi-Fi: network list instead of a single AP

```cpp
struct WifiNet { char ssid[33]; char pass[65]; };
WifiNet wifiNets[MAX_WIFI_NETWORKS];   // 5
uint8_t wifiCount;
void loadWifiNetworks();  void saveWifiNetworks();
void showWifiList();                   // menu item "Wi-Fi setup"
void showWifiNetworkMenu(uint8_t idx); // "Setup via AP" / "Delete"
void setupWifi(int8_t slot);           // WiFiManager portal; -1 = append
bool connectWiFi();
```

`connectWiFi()` scans first and tries the stored networks in order, skipping
those not seen in the scan (so hidden SSIDs are not supported), with an
explicit 10 s `waitForConnectResult` timeout and `WiFi.persistent(false)` so
the core does not rewrite its own credentials on every sync. The list is
stored in NVS as `wifiCount` + `wifiSsidN`/`wifiPassN` (N = 0..4). If the
`wifiCount` key is absent on first boot and the sketch defines `WIFI_SSID` /
`WIFI_PASSWD`, slot 0 is seeded from them. `showWifiList()` supports
reordering: long-press BACK (`WIFI_LONGPRESS_MS`, 700 ms) enters reorder mode,
UP/DOWN swap, BACK saves, MENU restores the previous order.

**Breaking:** `setupWifi()` now takes a slot index; `menuItems` has a Wi-Fi
*list* entry where upstream had the single-network setup.

### Time zone

```cpp
void setTimezone();                    // menu item "Time zone"
extern RTC_DATA_ATTR long gmtOffset;   // seconds, NVS key "gmtOffset"
```

The offset is user-set in 30-minute steps (−12:00 … +14:00) and is **not**
overwritten by the OpenWeatherMap `timezone` field anymore; `settings.gmtOffset`
is only the first-boot default. Because the RTC holds local time, saving a new
offset shifts the RTC by `(new − old)` so the face is correct immediately;
`syncNTP()` is called with the stored offset.

### Alarm

```cpp
void setAlarm();   // menu item "Alarm": HH:MM editor + "Enabled" checkbox
bool alarmDue();   // call with currentTime filled in
void ringAlarm();  // blocking: motor 1 s on / 1 s off until dismissed
extern RTC_DATA_ATTR uint8_t alarmHour, alarmMinute;
extern RTC_DATA_ATTR bool    alarmEnabled;
extern RTC_DATA_ATTR int16_t snoozeAt, alarmHandled; // minute of day, -1 = none
```

`init()` checks `alarmDue()` on **every** wake (RTC tick and button press)
before anything else, so the alarm fires from the watch face or the menu even
if the RTC wake arrives late: it rings within `[target, target +
ALARM_LATE_MIN]` (5 min) and `alarmHandled` prevents a second ring in that
window after dismissal. In the ring screen one right button snoozes for
`ALARM_SNOOZE_MIN` (10 min), both right buttons together dismiss; ringing stops
by itself after `ALARM_RING_MIN` minutes (default 5, overridable with a build
define / `.env`) to protect the battery, and the countdown restarts for the
post-snooze ring. In the
editor, UP/DOWN cycle focus (hours → minutes → Enabled), MENU increments
(minutes wrap 59→00 without carrying into hours; hold 2 s for 5 Hz
auto-repeat), BACK saves to NVS (`alarmH`, `alarmM`, `alarmOn`). All constants
are in `config.h`.

### Night mode

While local time is inside `[NIGHT_START_MIN, NIGHT_END_MIN)` (minutes of
day, defined in `config.h` or via build defines) `deepSleep()` arms the RTC
alarm `NIGHT_WAKE_MIN` minutes ahead instead of the usual next minute, so
the watch wakes less often at night. `start == end` disables it,
`start > end` wraps midnight. The RTC alarm matches minutes only, so the
interval is capped at 60; a pending alarm/snooze shortens the sleep so it
still rings on time, and `currentTime` is re-read first because minutes may
have passed in a menu. `WatchyRTC::clearAlarm(intervalMin)` takes the
interval (default 1): on DS3231 it switches between `ALM2_EVERY_MINUTE` and
`ALM2_MATCH_MINUTES`, on PCF8563 it simply targets a later minute.

`NIGHT_NO_WIRELESS=1` keeps the radios off at night: `getWeatherData()`
then skips the Wi-Fi fetch inside the window and returns the last cached
reading (or the internal sensor before the first fetch), leaving
`weatherIntervalCounter` untouched so the schedule resumes in the morning.
Manual menu actions still connect.

### Wrist flick refresh (`WRIST_REFRESH`)

`WRIST_REFRESH=1` adds `ACC_INT_MASK` to the ext1 wake mask: a BMA423
tilt/double-tap interrupt wakes the watch like a button press. The wake
redraws the face only when `guiState == WATCHFACE_STATE` and the last
`showWatchFace()` was `>= WRIST_REFRESH_MIN_S` (60 s) ago —
`NIGHT_WAKE_MIN` minutes inside the night window. `getWeatherData()` then
returns the cached reading via `screenOnlyRefresh`, so the wireless
schedule is untouched. The step-counter interrupt is remapped to INT2 so
walking does not wake the CPU, and `deepSleep()` calls `sensor.getINT()`
before arming ext1 to deassert the latched level interrupt. Not supported
on `ARDUINO_ESP32S3_DEV` (ext1 there is ANY_LOW, the accel pin is active
high).

### Menu and misc

- `MENU_LENGTH` is 9 and `MENU_HEIGHT` 20 px so nine rows fit the 200 px
  display. Adding an item means touching both `menuItems[]` arrays and both
  `switch (menuIndex)` blocks in `handleButtonPress()`.
- All blocking app screens wait for the opening MENU press to be released
  before polling, so the press is not read again as "select".
- NVS namespace `"watchy"` keys: `gmtOffset`, `face`, `alarmH`, `alarmM`,
  `alarmOn`, `wifiCount`, `wifiSsidN`, `wifiPassN`.
- `.gitattributes` marks `*.bin`/`*.png` as binary (upstream marked everything
  as text, which made fresh clones show spurious modifications).


