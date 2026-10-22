#ifndef CONFIG_H
#define CONFIG_H

// Versioning
#define WATCHY_LIB_VER "1.4.14"

//pins

#ifdef ARDUINO_ESP32S3_DEV //V3

#define WATCHY_V3_SDA 12
#define WATCHY_V3_SCL 11

#define WATCHY_V3_SS    33
#define WATCHY_V3_MOSI  48
#define WATCHY_V3_MISO  46
#define WATCHY_V3_SCK   47

#define MENU_BTN_PIN  7
#define BACK_BTN_PIN  6
#define UP_BTN_PIN    0
#define DOWN_BTN_PIN  8

#define DISPLAY_CS    33
#define DISPLAY_DC    34
#define DISPLAY_RES   35
#define DISPLAY_BUSY  36
#define ACC_INT_1_PIN 14
#define ACC_INT_2_PIN 13
#define VIB_MOTOR_PIN 17
#define BATT_ADC_PIN 9
#define CHRG_STATUS_PIN 10
#define USB_DET_PIN 21
#define RTC_INT_PIN -1 //not used

#define MENU_BTN_MASK (BIT64(7))
#define BACK_BTN_MASK (BIT64(6))
#define UP_BTN_MASK   (BIT64(0))
#define DOWN_BTN_MASK (BIT64(8))
#define ACC_INT_MASK  (BIT64(14))
#define BTN_PIN_MASK  (MENU_BTN_MASK|BACK_BTN_MASK|UP_BTN_MASK|DOWN_BTN_MASK)

#else //V1,V1.5,V2

#if !defined(ARDUINO_WATCHY_V10) && !defined(ARDUINO_WATCHY_V15) && !defined(ARDUINO_WATCHY_V20)

#pragma message "Please install the latest ESP32 Arduino Core (2.0.5+) and choose Watchy as the target board"
#pragma message "Hardware revision is not defined at the project level, please define in config.h. Defaulting to ARDUINO_WATCHY_V20"

#define ARDUINO_WATCHY_V20

#define MENU_BTN_PIN 26
#define BACK_BTN_PIN 25
#define DOWN_BTN_PIN 4
#define DISPLAY_CS 5
#define DISPLAY_RES 9
#define DISPLAY_DC 10
#define DISPLAY_BUSY 19
#define ACC_INT_1_PIN 14
#define ACC_INT_2_PIN 12
#define VIB_MOTOR_PIN 13
#define RTC_INT_PIN 27

#if defined (ARDUINO_WATCHY_V10)
    #define UP_BTN_PIN 32
    #define BATT_ADC_PIN 33
    #define UP_BTN_MASK  (BIT64(32))
    #define RTC_TYPE 1 //DS3231
#elif defined (ARDUINO_WATCHY_V15)
    #define UP_BTN_PIN 32
    #define BATT_ADC_PIN 35
    #define UP_BTN_MASK  (BIT64(32))
    #define RTC_TYPE 2 //PCF8563
#elif defined (ARDUINO_WATCHY_V20)
    #define UP_BTN_PIN 35
    #define BATT_ADC_PIN 34
    #define UP_BTN_MASK  (BIT64(35))
    #define RTC_TYPE 2 //PCF8563
#endif

#define MENU_BTN_MASK (BIT64(26))
#define BACK_BTN_MASK (BIT64(25))
#define DOWN_BTN_MASK (BIT64(4))
#define ACC_INT_MASK  (BIT64(14))
#define BTN_PIN_MASK  (MENU_BTN_MASK|BACK_BTN_MASK|UP_BTN_MASK|DOWN_BTN_MASK)

#endif

#endif

//display
#define DISPLAY_WIDTH 200
#define DISPLAY_HEIGHT 200
// wifi
#define WIFI_AP_TIMEOUT 60
#define WIFI_AP_SSID    "Watchy AP"
#define MAX_WIFI_NETWORKS 5
#define WIFI_LONGPRESS_MS 700
// menu
#define WATCHFACE_STATE -1
#define MAIN_MENU_STATE 0
#define APP_STATE       1
#define FW_UPDATE_STATE 2
#define MENU_HEIGHT     20
#define MENU_LENGTH     9
// set time
#define SET_HOUR   0
#define SET_MINUTE 1
#define SET_YEAR   2
#define SET_MONTH  3
#define SET_DAY    4
#define HOUR_12_24 24
// alarm
#define ALARM_SNOOZE_MIN      10
#define ALARM_REPEAT_DELAY_MS 2000   // hold MENU this long before auto-repeat
#define ALARM_REPEAT_MS       200    // auto-repeat period (5 Hz)
#ifndef ALARM_RING_MIN
#define ALARM_RING_MIN        5      // give up ringing after this many minutes (battery)
#endif
#define ALARM_RING_MAX_MS     (ALARM_RING_MIN * 60000UL)
#define ALARM_LATE_MIN        5      // fire up to this many minutes after the target if a wake was missed
#define ALARM_BTN_RELEASE_MS  50     // a release must hold this long to count (debounce)
#define SET_ALARM_HOUR   0
#define SET_ALARM_MINUTE 1
#define SET_ALARM_ON     2
// night mode: inside [NIGHT_START_MIN, NIGHT_END_MIN) minutes-of-day the RTC
// wakes the watch every NIGHT_WAKE_MIN minutes instead of every minute.
// start == end disables it, start > end wraps midnight. NIGHT_WAKE_MIN is
// 1..60 — the RTC alarm matches minutes only, so it cannot sleep longer.
#ifndef NIGHT_START_MIN
#define NIGHT_START_MIN 0
#endif
#ifndef NIGHT_END_MIN
#define NIGHT_END_MIN   0
#endif
#ifndef NIGHT_WAKE_MIN
#define NIGHT_WAKE_MIN  30
#endif
// 1 = no radios at night: automatic weather/NTP Wi-Fi use is skipped inside
// the night window (manual menu actions still work)
#ifndef NIGHT_NO_WIRELESS
#define NIGHT_NO_WIRELESS 0
#endif
// 1 = a wrist flick (accelerometer tilt/double-tap on ACC_INT1) wakes the
// watch and redraws the face if the last update was >= WRIST_REFRESH_MIN_S
// ago (NIGHT_WAKE_MIN minutes inside the night window). Screen-only: the
// wireless schedule is not touched. Not supported on ARDUINO_ESP32S3_DEV
// (its ext1 wake is ANY_LOW, the accel interrupt is active high).
#ifndef WRIST_REFRESH
#define WRIST_REFRESH 0
#endif
#ifndef WRIST_REFRESH_MIN_S
#define WRIST_REFRESH_MIN_S 60
#endif
// BLE OTA
#define BLE_DEVICE_NAME        "Watchy BLE OTA"
#define WATCHFACE_NAME         "Watchy 7 Segment"
#define SOFTWARE_VERSION_MAJOR 1
#define SOFTWARE_VERSION_MINOR 0
#define SOFTWARE_VERSION_PATCH 0
#define HARDWARE_VERSION_MAJOR 1
#define HARDWARE_VERSION_MINOR 0

#endif