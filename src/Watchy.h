#ifndef WATCHY_H
#define WATCHY_H

#include <Arduino.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Arduino_JSON.h>
#include <GxEPD2_BW.h>
#include <Wire.h>
#include "Cyrillic9pt7b.h"
#include "DSEG7_Classic_Bold_53.h"
#include "Display.h"
#include "BLE.h"
#include "bma.h"
#include "config.h"
#include "esp_chip_info.h"
#ifdef ARDUINO_ESP32S3_DEV
  #include "Watchy32KRTC.h"
  #include "soc/rtc.h"
  #include "soc/rtc_io_reg.h"
  #include "soc/sens_reg.h"
  #include "esp_sleep.h"
  #include "rom/rtc.h"
  #include "soc/soc.h"
  #include "soc/rtc_cntl_reg.h"
  #include "time.h"
  #include "esp_sntp.h"
  #include "hal/rtc_io_types.h"
  #include "driver/rtc_io.h"
  #define uS_TO_S_FACTOR 1000000ULL  //Conversion factor for micro seconds to seconds
  #define ADC_VOLTAGE_DIVIDER ((360.0f+100.0f)/360.0f) //Voltage divider at battery ADC  
#else
  #include "WatchyRTC.h"
#endif

typedef struct weatherData {
  int8_t temperature;
  int16_t weatherConditionCode;
  bool isMetric;
  String weatherDescription;
  bool external;
  tmElements_t sunrise;
  tmElements_t sunset;
} weatherData;

typedef struct watchySettings {
  // Weather Settings
  String cityID;
  String lat;
  String lon;
  String weatherAPIKey;
  String weatherURL;
  String weatherUnit;
  String weatherLang;
  int8_t weatherUpdateInterval;
  // NTP Settings
  String ntpServer;
  int gmtOffset;
  //
  bool vibrateOClock;
} watchySettings;

struct WifiNet {
  char ssid[33];
  char pass[65];
};

class Watchy {
public:
  #ifdef ARDUINO_ESP32S3_DEV
   static Watchy32KRTC RTC;
  #else
   static WatchyRTC RTC;
  #endif
  static GxEPD2_BW<WatchyDisplay, WatchyDisplay::HEIGHT> display;
  tmElements_t currentTime;
  watchySettings settings;

public:
  explicit Watchy(const watchySettings &s) : settings(s) {} // constructor
  void init(String datetime = "");
  void deepSleep();
  float getBatteryVoltage();
  uint8_t getBoardRevision();
  void vibMotor(uint8_t intervalMs = 100, uint8_t length = 20);

  virtual void handleButtonPress();
  void showMenu(byte menuIndex, bool partialRefresh);
  void showFastMenu(byte menuIndex);
  void showAbout();
  void showBuzz();
  void showAccelerometer();
  void showUpdateFW();
  void showSyncNTP();
  bool syncNTP();
  bool syncNTP(long gmt);
  bool syncNTP(long gmt, String ntpServer);
  void setTime();
  void setTimezone();
  void setAlarm();
  bool alarmDue();
  void ringAlarm();
  WifiNet wifiNets[MAX_WIFI_NETWORKS];
  uint8_t wifiCount = 0;
  void loadWifiNetworks();
  void saveWifiNetworks();
  void showWifiList();
  void showWifiNetworkMenu(uint8_t idx);
  void setupWifi(int8_t slot);
  bool connectWiFi();
  weatherData getWeatherData();
  void updateFWBegin();

  void showWatchFace(bool partialRefresh);
  virtual void drawWatchFace(); // override this method for different watch
                                // faces
  virtual uint8_t faceCount() { return 1; }
  virtual void drawFace(uint8_t idx);
  void showFaces();

private:
  void _bmaConfig();
  static void _configModeCallback(WiFiManager *myWiFiManager);
  static uint16_t _readRegister(uint8_t address, uint8_t reg, uint8_t *data,
                                uint16_t len);
  static uint16_t _writeRegister(uint8_t address, uint8_t reg, uint8_t *data,
                                 uint16_t len);
  weatherData _getWeatherData(String cityID, String lat, String lon, String units, String lang,
                             String url, String apiKey, uint8_t updateInterval);                                 
};

extern RTC_DATA_ATTR int guiState;
extern RTC_DATA_ATTR int menuIndex;
extern RTC_DATA_ATTR BMA423 sensor;
extern RTC_DATA_ATTR bool WIFI_CONFIGURED;
extern RTC_DATA_ATTR bool BLE_CONFIGURED;
extern RTC_DATA_ATTR bool USB_PLUGGED_IN;
extern RTC_DATA_ATTR uint8_t faceIndex;
extern RTC_DATA_ATTR uint8_t alarmHour;
extern RTC_DATA_ATTR uint8_t alarmMinute;
extern RTC_DATA_ATTR bool alarmEnabled;
extern RTC_DATA_ATTR int16_t snoozeAt; // minute of day, -1 = none
extern RTC_DATA_ATTR int16_t alarmHandled; // target minute that already rang (or was snoozed/dismissed)

// --- Cyrillic text helpers ---
// Cyrillic9pt7b stores Cyrillic glyphs as single-byte codes 0x80..0xC1 so the
// standard (uint8_t) drawChar can reach them. These helpers decode UTF-8 and
// map Russian letters to those codes; ASCII passes through unchanged.
static inline uint8_t ruCpToByte(uint16_t cp) {
    if (cp >= 0x410 && cp <= 0x42F) return 0x80 + (cp - 0x410); // А-Я
    if (cp >= 0x430 && cp <= 0x44F) return 0xA0 + (cp - 0x430); // а-я
    if (cp == 0x401) return 0xC0; // Ё
    if (cp == 0x451) return 0xC1; // ё
    return '?';
}

static inline void printRU(const char *s) {
    while (*s) {
        uint8_t c = (uint8_t)*s;
        if (c < 0x80) {
            Watchy::display.write(c);
            s++;
        } else if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
            uint16_t cp = ((uint16_t)(c & 0x1F) << 6) | (s[1] & 0x3F);
            s += 2;
            Watchy::display.write(ruCpToByte(cp));
        } else {
            s++; // skip malformed byte
        }
    }
}

static inline void printlnRU(const char *s) {
    printRU(s);
    Watchy::display.write('\n');
}

#endif
