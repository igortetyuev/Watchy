#include "Watchy.h"
#include <Preferences.h>

#ifdef ARDUINO_ESP32S3_DEV
  Watchy32KRTC Watchy::RTC;
  #define ACTIVE_LOW 0
#else
  WatchyRTC Watchy::RTC;
  #define ACTIVE_LOW 1
#endif
GxEPD2_BW<WatchyDisplay, WatchyDisplay::HEIGHT> Watchy::display(
    WatchyDisplay{});

RTC_DATA_ATTR int guiState;
RTC_DATA_ATTR int menuIndex;
RTC_DATA_ATTR BMA423 sensor;
RTC_DATA_ATTR bool WIFI_CONFIGURED;
RTC_DATA_ATTR bool BLE_CONFIGURED;
RTC_DATA_ATTR weatherData currentWeather;
RTC_DATA_ATTR int weatherIntervalCounter = -1;
RTC_DATA_ATTR long gmtOffset = 0;
RTC_DATA_ATTR uint8_t faceIndex = 0;
RTC_DATA_ATTR uint8_t alarmHour = 7;
RTC_DATA_ATTR uint8_t alarmMinute = 0;
RTC_DATA_ATTR bool alarmEnabled = false;
RTC_DATA_ATTR int16_t snoozeAt = -1;
RTC_DATA_ATTR int16_t alarmHandled = -1;
RTC_DATA_ATTR bool alreadyInMenu         = true;
RTC_DATA_ATTR bool USB_PLUGGED_IN = false;
RTC_DATA_ATTR tmElements_t bootTime;
RTC_DATA_ATTR uint32_t lastIPAddress;
RTC_DATA_ATTR char lastSSID[30];

void Watchy::init(String datetime) {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause(); // get wake up reason
  #ifdef ARDUINO_ESP32S3_DEV
    Wire.begin(WATCHY_V3_SDA, WATCHY_V3_SCL);     // init i2c
  #else
    Wire.begin(SDA, SCL);                         // init i2c
  #endif
  RTC.init();
  // Init the display since is almost sure we will use it
  display.epd2.initWatchy();

  switch (wakeup_reason) {
  #ifdef ARDUINO_ESP32S3_DEV
  case ESP_SLEEP_WAKEUP_TIMER: // RTC Alarm
  #else
  case ESP_SLEEP_WAKEUP_EXT0: // RTC Alarm
  #endif
    RTC.read(currentTime);
    if (alarmDue()) {
      ringAlarm();
      break;
    }
    switch (guiState) {
    case WATCHFACE_STATE:
      showWatchFace(true); // partial updates on tick
      if (settings.vibrateOClock) {
        if (currentTime.Minute == 0) {
          // The RTC wakes us up once per minute
          vibMotor(75, 4);
        }
      }
      break;
    case MAIN_MENU_STATE:
      // Return to watchface if in menu for more than one tick
      if (alreadyInMenu) {
        guiState = WATCHFACE_STATE;
        showWatchFace(false);
      } else {
        alreadyInMenu = true;
      }
      break;
    }
    break;
  case ESP_SLEEP_WAKEUP_EXT1: // button Press
    RTC.read(currentTime);
    if (alarmDue()) {
      ringAlarm(); // shows the watch face; the button press is consumed
      break;
    }
    handleButtonPress();
    break;
  #ifdef ARDUINO_ESP32S3_DEV
  case ESP_SLEEP_WAKEUP_EXT0: // USB plug in
    pinMode(USB_DET_PIN, INPUT);
    USB_PLUGGED_IN = (digitalRead(USB_DET_PIN) == 1);
    if(guiState == WATCHFACE_STATE){
      RTC.read(currentTime);
      showWatchFace(true);
    }
    break;
  #endif
  default: // reset
    RTC.config(datetime);
    _bmaConfig();
    #ifdef ARDUINO_ESP32S3_DEV
    pinMode(USB_DET_PIN, INPUT);
    USB_PLUGGED_IN = (digitalRead(USB_DET_PIN) == 1);
    #endif    
    Preferences prefs;
    prefs.begin("watchy", true);
    gmtOffset = prefs.getLong("gmtOffset", settings.gmtOffset);
    faceIndex = prefs.getUChar("face", 0);
    alarmHour = prefs.getUChar("alarmH", 7);
    alarmMinute = prefs.getUChar("alarmM", 0);
    alarmEnabled = prefs.getBool("alarmOn", false);
    if (alarmHour > 23) {
      alarmHour = 23;
    }
    if (alarmMinute > 59) {
      alarmMinute = 59;
    }
    snoozeAt = -1;
    prefs.end();
    RTC.read(currentTime);
    RTC.read(bootTime);
    showWatchFace(false); // full update on reset
    vibMotor(75, 4);
    // For some reason, seems to be enabled on first boot
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    break;
  }
  deepSleep();
}
void Watchy::deepSleep() {
  display.hibernate();
  RTC.clearAlarm();        // resets the alarm flag in the RTC
  #ifdef ARDUINO_ESP32S3_DEV
  esp_sleep_enable_ext0_wakeup((gpio_num_t)USB_DET_PIN, USB_PLUGGED_IN ? LOW : HIGH); //// enable deep sleep wake on USB plug in/out
  rtc_gpio_set_direction((gpio_num_t)USB_DET_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)USB_DET_PIN);

  esp_sleep_enable_ext1_wakeup(
      BTN_PIN_MASK,
      ESP_EXT1_WAKEUP_ANY_LOW); // enable deep sleep wake on button press
  rtc_gpio_set_direction((gpio_num_t)UP_BTN_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)UP_BTN_PIN);

  rtc_clk_32k_enable(true);
  //rtc_clk_slow_freq_set(RTC_SLOW_FREQ_32K_XTAL);
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  int secToNextMin = 60 - timeinfo.tm_sec;
  esp_sleep_enable_timer_wakeup(secToNextMin * uS_TO_S_FACTOR);
  #else
  // Set GPIOs 0-39 to input to avoid power leaking out
  const uint64_t ignore = 0b11110001000000110000100111000010; // Ignore some GPIOs due to resets
  for (int i = 0; i < GPIO_NUM_MAX; i++) {
    if ((ignore >> i) & 0b1)
      continue;
    pinMode(i, INPUT);
  }
  esp_sleep_enable_ext0_wakeup((gpio_num_t)RTC_INT_PIN,
                               0); // enable deep sleep wake on RTC interrupt
  esp_sleep_enable_ext1_wakeup(
      BTN_PIN_MASK,
      ESP_EXT1_WAKEUP_ANY_HIGH); // enable deep sleep wake on button press
  #endif
  esp_deep_sleep_start();
}

void Watchy::handleButtonPress() {
  uint64_t wakeupBit = esp_sleep_get_ext1_wakeup_status();
  // Menu Button
  if (wakeupBit & MENU_BTN_MASK) {
    if (guiState ==
        WATCHFACE_STATE) { // enter menu state if coming from watch face
      showMenu(menuIndex, false);
    } else if (guiState ==
               MAIN_MENU_STATE) { // if already in menu, then select menu item
      switch (menuIndex) {
      case 0:
        showAbout();
        break;
      case 1:
        showBuzz();
        break;
      case 2:
        showAccelerometer();
        break;
      case 3:
        setTime();
        break;
      case 4:
        showWifiList();
        break;
      /*case 5:
        showUpdateFW();
        break;*/
      case 5:
        showSyncNTP();
        break;
      case 6:
        setTimezone();
        break;
      case 7:
        showFaces();
        break;
      case 8:
        setAlarm();
        break;
      default:
        break;
      }
    } /*else if (guiState == FW_UPDATE_STATE) {
      updateFWBegin();
    }*/
  }
  // Back Button
  else if (wakeupBit & BACK_BTN_MASK) {
    if (guiState == MAIN_MENU_STATE) { // exit to watch face if already in menu
      RTC.read(currentTime);
      showWatchFace(false);
    } else if (guiState == APP_STATE) {
      showMenu(menuIndex, false); // exit to menu if already in app
    } else if (guiState == FW_UPDATE_STATE) {
      showMenu(menuIndex, false); // exit to menu if already in app
    } else if (guiState == WATCHFACE_STATE) {
      return;
    }
  }
  // Up Button
  else if (wakeupBit & UP_BTN_MASK) {
    if (guiState == MAIN_MENU_STATE) { // increment menu index
      menuIndex--;
      if (menuIndex < 0) {
        menuIndex = MENU_LENGTH - 1;
      }
      showMenu(menuIndex, true);
    } else if (guiState == WATCHFACE_STATE) {
      return;
    }
  }
  // Down Button
  else if (wakeupBit & DOWN_BTN_MASK) {
    if (guiState == MAIN_MENU_STATE) { // decrement menu index
      menuIndex++;
      if (menuIndex > MENU_LENGTH - 1) {
        menuIndex = 0;
      }
      showMenu(menuIndex, true);
    } else if (guiState == WATCHFACE_STATE) {
      return;
    }
  }

  /***************** fast menu *****************/
  bool timeout     = false;
  long lastTimeout = millis();
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);
  while (!timeout) {
    if (millis() - lastTimeout > 5000) {
      timeout = true;
    } else {
      if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState ==
            MAIN_MENU_STATE) { // if already in menu, then select menu item
          switch (menuIndex) {
          case 0:
            showAbout();
            break;
          case 1:
            showBuzz();
            break;
          case 2:
            showAccelerometer();
            break;
          case 3:
            setTime();
            break;
          case 4:
            showWifiList();
            break;
          /*case 5:
            showUpdateFW();
            break;*/
          case 5:
            showSyncNTP();
            break;
          case 6:
            setTimezone();
            break;
          case 7:
            showFaces();
            break;
          case 8:
            setAlarm();
            break;
          default:
            break;
          }
        }/* else if (guiState == FW_UPDATE_STATE) {
          updateFWBegin();
        }*/
      } else if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState ==
            MAIN_MENU_STATE) { // exit to watch face if already in menu
          RTC.read(currentTime);
          showWatchFace(false);
          break; // leave loop
        } else if (guiState == APP_STATE) {
          showMenu(menuIndex, false); // exit to menu if already in app
        } else if (guiState == FW_UPDATE_STATE) {
          showMenu(menuIndex, false); // exit to menu if already in app
        }
      } else if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState == MAIN_MENU_STATE) { // increment menu index
          menuIndex--;
          if (menuIndex < 0) {
            menuIndex = MENU_LENGTH - 1;
          }
          showFastMenu(menuIndex);
        }
      } else if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState == MAIN_MENU_STATE) { // decrement menu index
          menuIndex++;
          if (menuIndex > MENU_LENGTH - 1) {
            menuIndex = 0;
          }
          showFastMenu(menuIndex);
        }
      }
    }
  }
}

void Watchy::showMenu(byte menuIndex, bool partialRefresh) {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&LANG_FONT);

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;

  const char *menuItems[] = {
      STR[S_MENU_ABOUT], STR[S_MENU_VIBRATE], STR[S_MENU_ACCEL],
      STR[S_MENU_SET_TIME], STR[S_MENU_WIFI],
      STR[S_MENU_NTP], STR[S_MENU_TIMEZONE], STR[S_MENU_FACE], STR[S_MENU_ALARM]};
  for (int i = 0; i < MENU_LENGTH; i++) {
    yPos = MENU_HEIGHT + (MENU_HEIGHT * i);
    display.setCursor(0, yPos);
    if (i == menuIndex) {
      display.getTextBounds(menuItems[i], 0, yPos, &x1, &y1, &w, &h);
      display.fillRect(x1 - 1, y1 - 10, 200, h + 15, GxEPD_BLACK);
      display.setTextColor(GxEPD_WHITE);
      printlnL(menuItems[i]);
    } else {
      display.setTextColor(GxEPD_BLACK);
      printlnL(menuItems[i]);
    }
  }

  display.display(partialRefresh);

  guiState = MAIN_MENU_STATE;
  alreadyInMenu = false;
}

void Watchy::showFastMenu(byte menuIndex) {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&LANG_FONT);

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;

  const char *menuItems[] = {
      STR[S_MENU_ABOUT], STR[S_MENU_VIBRATE], STR[S_MENU_ACCEL],
      STR[S_MENU_SET_TIME], STR[S_MENU_WIFI],
      STR[S_MENU_NTP], STR[S_MENU_TIMEZONE], STR[S_MENU_FACE], STR[S_MENU_ALARM]};
  for (int i = 0; i < MENU_LENGTH; i++) {
    yPos = MENU_HEIGHT + (MENU_HEIGHT * i);
    display.setCursor(0, yPos);
    if (i == menuIndex) {
      display.getTextBounds(menuItems[i], 0, yPos, &x1, &y1, &w, &h);
      display.fillRect(x1 - 1, y1 - 10, 200, h + 15, GxEPD_BLACK);
      display.setTextColor(GxEPD_WHITE);
      printlnL(menuItems[i]);
    } else {
      display.setTextColor(GxEPD_BLACK);
      printlnL(menuItems[i]);
    }
  }

  display.display(true);

  guiState = MAIN_MENU_STATE;
}

void Watchy::showAbout() {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&LANG_FONT);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(0, 20);

  printL(S_ABOUT_VERSION);
  display.println(WATCHY_LIB_VER);

  printL(S_ABOUT_REVISION);
  display.println(getBoardRevision());

  printL(S_ABOUT_BATTERY);
  float voltage = getBatteryVoltage();
  display.print(voltage);
  display.println("V");

  #ifndef ARDUINO_ESP32S3_DEV
  printL(S_ABOUT_UPTIME);
  RTC.read(currentTime);
  time_t b = makeTime(bootTime);
  time_t c = makeTime(currentTime);
  int totalSeconds = c-b;
  //int seconds = (totalSeconds % 60);
  int minutes = (totalSeconds % 3600) / 60;
  int hours = (totalSeconds % 86400) / 3600;
  int days = (totalSeconds % (86400 * 30)) / 86400; 
  display.print(days);
  printL(S_UNIT_DAYS);
  display.print(hours);
  printL(S_UNIT_HOURS);
  display.print(minutes);
  printlnL(S_UNIT_MINUTES);
  #endif
  
  if(WIFI_CONFIGURED){
    display.print("SSID: ");
    display.println(lastSSID);
    display.print("IP: ");
    display.println(IPAddress(lastIPAddress).toString());
  }else{
    printlnL(S_WIFI_NOT_CONNECTED);
  }
  display.display(false); // full refresh

  guiState = APP_STATE;
}

void Watchy::showBuzz() {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&LANG_FONT);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(70, 80);
  printlnL(S_BUZZ);
  display.display(false); // full refresh
  vibMotor();
  showMenu(menuIndex, false);
}

void Watchy::vibMotor(uint8_t intervalMs, uint8_t length) {
  pinMode(VIB_MOTOR_PIN, OUTPUT);
  bool motorOn = false;
  for (int i = 0; i < length; i++) {
    motorOn = !motorOn;
    digitalWrite(VIB_MOTOR_PIN, motorOn);
    delay(intervalMs);
  }
}

void Watchy::setTime() {

  guiState = APP_STATE;

  RTC.read(currentTime);

  #ifdef ARDUINO_ESP32S3_DEV
  uint8_t minute = currentTime.Minute;
  uint8_t hour   = currentTime.Hour;
  uint8_t day    = currentTime.Day;
  uint8_t month  = currentTime.Month;
  uint8_t year   = currentTime.Year;  
  #else
  int8_t minute = currentTime.Minute;
  int8_t hour   = currentTime.Hour;
  int8_t day    = currentTime.Day;
  int8_t month  = currentTime.Month;
  int8_t year   = tmYearToY2k(currentTime.Year);
  #endif

  int8_t setIndex = SET_HOUR;

  int8_t blink = 0;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  display.setFullWindow();

  while (1) {

    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      setIndex++;
      if (setIndex > SET_DAY) {
        break;
      }
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      if (setIndex != SET_HOUR) {
        setIndex--;
      }
    }

    blink = 1 - blink;

    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_HOUR:
        hour == 23 ? (hour = 0) : hour++;
        break;
      case SET_MINUTE:
        minute == 59 ? (minute = 0) : minute++;
        break;
      case SET_YEAR:
        year == 99 ? (year = 0) : year++;
        break;
      case SET_MONTH:
        month == 12 ? (month = 1) : month++;
        break;
      case SET_DAY:
        day == 31 ? (day = 1) : day++;
        break;
      default:
        break;
      }
    }

    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_HOUR:
        hour == 0 ? (hour = 23) : hour--;
        break;
      case SET_MINUTE:
        minute == 0 ? (minute = 59) : minute--;
        break;
      case SET_YEAR:
        year == 0 ? (year = 99) : year--;
        break;
      case SET_MONTH:
        month == 1 ? (month = 12) : month--;
        break;
      case SET_DAY:
        day == 1 ? (day = 31) : day--;
        break;
      default:
        break;
      }
    }

    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&DSEG7_Classic_Bold_53);

    display.setCursor(5, 80);
    if (setIndex == SET_HOUR) { // blink hour digits
      display.setTextColor(blink ? GxEPD_BLACK : GxEPD_WHITE);
    }
    if (hour < 10) {
      display.print("0");
    }
    display.print(hour);

    display.setTextColor(GxEPD_BLACK);
    display.print(":");

    display.setCursor(108, 80);
    if (setIndex == SET_MINUTE) { // blink minute digits
      display.setTextColor(blink ? GxEPD_BLACK : GxEPD_WHITE);
    }
    if (minute < 10) {
      display.print("0");
    }
    display.print(minute);

    display.setTextColor(GxEPD_BLACK);

    display.setFont(&LANG_FONT);
    display.setCursor(45, 150);
    if (setIndex == SET_YEAR) { // blink minute digits
      display.setTextColor(blink ? GxEPD_BLACK : GxEPD_WHITE);
    }
    display.print(2000 + year);

    display.setTextColor(GxEPD_BLACK);
    display.print("/");

    if (setIndex == SET_MONTH) { // blink minute digits
      display.setTextColor(blink ? GxEPD_BLACK : GxEPD_WHITE);
    }
    if (month < 10) {
      display.print("0");
    }
    display.print(month);

    display.setTextColor(GxEPD_BLACK);
    display.print("/");

    if (setIndex == SET_DAY) { // blink minute digits
      display.setTextColor(blink ? GxEPD_BLACK : GxEPD_WHITE);
    }
    if (day < 10) {
      display.print("0");
    }
    display.print(day);
    display.display(true); // partial refresh
  }

  tmElements_t tm;
  tm.Month  = month;
  tm.Day    = day;
  #ifdef ARDUINO_ESP32S3_DEV
  tm.Year   = year;
  #else
  tm.Year   = y2kYearToTm(year);
  #endif
  tm.Hour   = hour;
  tm.Minute = minute;
  tm.Second = 0;

  RTC.set(tm);

  showMenu(menuIndex, false);
}

void Watchy::setTimezone() {

  guiState = APP_STATE;

  long offset = gmtOffset;
  bool save   = false;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // wait until the MENU press that opened this screen is released,
  // otherwise it would be read again as "save" on the first iteration
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  while (1) {

    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      save = true;
      break;
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      break;
    }
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      offset += 1800;
      if (offset > 14 * 3600) {
        offset = -12 * 3600;
      }
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      offset -= 1800;
      if (offset < -12 * 3600) {
        offset = 14 * 3600;
      }
    }

    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&LANG_FONT);

    display.setCursor(0, 30);
    printlnL(S_TZ_TITLE);

    char buf[16];
    snprintf(buf, sizeof buf, "UTC%c%02ld:%02ld", offset < 0 ? '-' : '+',
             labs(offset) / 3600, (labs(offset) % 3600) / 60);
    display.setTextSize(2);
    display.setCursor(20, 110);
    display.print(buf);
    display.setTextSize(1);

    display.setCursor(0, 170);
    printlnL(S_HINT_MENU_SAVE);
    printlnL(S_HINT_BACK_CANCEL);
    display.display(true); // partial refresh
  }

  if (save && offset != gmtOffset) {
    RTC.read(currentTime);
    time_t t = makeTime(currentTime) + (offset - gmtOffset);
    tmElements_t tm;
    breakTime(t, tm);
    RTC.set(tm);
    gmtOffset = offset;
    Preferences prefs;
    prefs.begin("watchy", false);
    prefs.putLong("gmtOffset", gmtOffset);
    prefs.end();
  }

  showMenu(menuIndex, false);
}

bool Watchy::alarmDue() {
  int16_t now    = currentTime.Hour * 60 + currentTime.Minute;
  int16_t target = snoozeAt >= 0 ? snoozeAt
                 : (alarmEnabled ? alarmHour * 60 + alarmMinute : -1);
  if (target < 0) {
    return false;
  }
  int16_t late = (now - target + 1440) % 1440; // minutes since target, wraps midnight
  if (late > ALARM_LATE_MIN) {
    if (alarmHandled == target) {
      alarmHandled = -1; // window passed: re-arm for the next day
    }
    return false;
  }
  return alarmHandled != target;
}

void Watchy::ringAlarm() {
  Serial.printf("ALARM ring %02d:%02d\n", currentTime.Hour, currentTime.Minute);
  // mark the target handled so a later wake inside the late window does not
  // ring again after a dismiss
  alarmHandled = snoozeAt >= 0 ? snoozeAt : alarmHour * 60 + alarmMinute;
  snoozeAt = -1; // current snooze has fired

  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&LANG_FONT);
  display.setCursor(0, 30);
  printlnL(S_ALARM_TITLE);
  display.setFont(&DSEG7_Classic_Bold_53);
  display.setCursor(5, 110);
  if (currentTime.Hour < 10) {
    display.print("0");
  }
  display.print(currentTime.Hour);
  display.print(":");
  if (currentTime.Minute < 10) {
    display.print("0");
  }
  display.print(currentTime.Minute);
  display.setFont(&LANG_FONT);
  display.setCursor(0, 165);
  printlnL(S_ALARM_HINT_SNOOZE);
  display.setCursor(0, 190);
  printlnL(S_ALARM_HINT_OFF);
  display.display(false); // full refresh

  pinMode(VIB_MOTOR_PIN, OUTPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);

  uint32_t start = millis();
  bool snooze    = false;
  bool done      = false;
  while (!done) {
    bool motorOn = ((millis() - start) / 1000) % 2 == 0;
    digitalWrite(VIB_MOTOR_PIN, motorOn);

    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW ||
        digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      // wait for release; if both right buttons are ever held together
      // during the press, dismiss instead of snoozing
      bool both = false;
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW ||
             digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
        if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW &&
            digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
          both = true;
        }
        delay(10);
      }
      snooze = !both;
      done   = true;
    }
    if (millis() - start > ALARM_RING_MAX_MS) {
      snooze = false;
      done   = true;
    }
    delay(10);
  }
  digitalWrite(VIB_MOTOR_PIN, LOW);

  // make sure both right buttons are released so the press does not leak
  // into the next wake as a button event
  while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW ||
         digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  if (snooze) {
    RTC.read(currentTime);
    snoozeAt = (currentTime.Hour * 60 + currentTime.Minute + ALARM_SNOOZE_MIN) % 1440;
    Serial.printf("ALARM snooze -> %d\n", snoozeAt);
  } else {
    Serial.println("ALARM off");
  }

  RTC.read(currentTime);
  showWatchFace(false); // sets guiState = WATCHFACE_STATE
}

void Watchy::setAlarm() {
  guiState = APP_STATE;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // wait until the MENU press that opened this screen is released
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  uint8_t hour   = alarmHour;
  uint8_t minute = alarmMinute;
  bool on        = alarmEnabled;
  int8_t focus   = SET_ALARM_HOUR;
  bool dirty     = true;

  auto step = [&]() {
    switch (focus) {
    case SET_ALARM_HOUR:
      hour = (hour + 1) % 24;
      break;
    case SET_ALARM_MINUTE:
      minute = (minute + 1) % 60;
      break;
    case SET_ALARM_ON:
      on = !on;
      break;
    }
  };

  auto draw = [&]() {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&DSEG7_Classic_Bold_53);

    display.setCursor(5, 80);
    if (hour < 10) {
      display.print("0");
    }
    display.print(hour);
    display.print(":");

    display.setCursor(108, 80);
    if (minute < 10) {
      display.print("0");
    }
    display.print(minute);

    if (focus == SET_ALARM_HOUR || focus == SET_ALARM_MINUTE) {
      int16_t x1, y1;
      uint16_t w, h;
      // fixed "00" so the frame does not depend on the current digits
      display.getTextBounds("00", focus == SET_ALARM_HOUR ? 5 : 108, 80,
                            &x1, &y1, &w, &h);
      display.drawRoundRect(x1 - 5, y1 - 5, w + 10, h + 10, 6, GxEPD_BLACK);
    }

    display.setFont(&LANG_FONT);
    display.drawRect(20, 128, 18, 18, GxEPD_BLACK);
    if (on) {
      display.drawLine(24, 137, 28, 142, GxEPD_BLACK);
      display.drawLine(25, 137, 29, 142, GxEPD_BLACK);
      display.drawLine(28, 142, 35, 131, GxEPD_BLACK);
      display.drawLine(29, 142, 36, 131, GxEPD_BLACK);
    }
    display.setCursor(48, 143);
    printL(S_ALARM_ENABLED);
    if (focus == SET_ALARM_ON) {
      display.drawRoundRect(12, 120, 150, 34, 6, GxEPD_BLACK);
    }

    display.setCursor(0, 190);
    printlnL(S_HINT_BACK_SAVE);
    display.display(true); // partial refresh
  };

  display.setFullWindow();

  while (1) {
    if (dirty) {
      draw();
      dirty = false;
    }

    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      focus = (focus + 2) % 3;
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      dirty = true;
      continue;
    }
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      focus = (focus + 1) % 3;
      while (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      dirty = true;
      continue;
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      alarmHour    = hour;
      alarmMinute  = minute;
      alarmEnabled = on;
      snoozeAt     = -1;
      alarmHandled = -1;
      Preferences prefs;
      prefs.begin("watchy", false);
      prefs.putUChar("alarmH", alarmHour);
      prefs.putUChar("alarmM", alarmMinute);
      prefs.putBool("alarmOn", alarmEnabled);
      prefs.end();
      Serial.printf("ALARM set %02d:%02d on=%d\n", alarmHour, alarmMinute,
                    alarmEnabled);
      break;
    }
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      uint32_t pressStart = millis();
      uint32_t stepsDone  = 0;
      step();
      stepsDone++;
      draw();
      // while held: time-based auto-repeat for hour/minute (a partial
      // refresh takes ~300-500 ms, longer than ALARM_REPEAT_MS, so count
      // steps by elapsed time and let the screen catch up)
      while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
        if (focus != SET_ALARM_ON &&
            millis() - pressStart >= ALARM_REPEAT_DELAY_MS) {
          uint32_t due =
              (millis() - pressStart - ALARM_REPEAT_DELAY_MS) / ALARM_REPEAT_MS + 1;
          bool stepped = false;
          while (stepsDone < due) {
            step();
            stepsDone++;
            stepped = true;
          }
          if (stepped) {
            draw();
          }
        }
        delay(10);
      }
      continue;
    }
    delay(10);
  }

  showMenu(menuIndex, false);
}

void Watchy::showFaces() {
  guiState = APP_STATE;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // wait until the MENU press that opened this screen is released
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  uint8_t sel = faceIndex;
  uint8_t n   = faceCount();
  if (sel >= n) {
    sel = 0;
  }

  RTC.read(currentTime);
  display.setFullWindow();

  while (1) {
    drawFace(sel);
    display.display(false); // full refresh

    // wait for a button press
    while (digitalRead(UP_BTN_PIN) != ACTIVE_LOW &&
           digitalRead(DOWN_BTN_PIN) != ACTIVE_LOW &&
           digitalRead(MENU_BTN_PIN) != ACTIVE_LOW &&
           digitalRead(BACK_BTN_PIN) != ACTIVE_LOW) {
      delay(10);
    }

    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      sel = (sel + n - 1) % n;
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      continue;
    }
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      sel = (sel + 1) % n;
      while (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      continue;
    }
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      faceIndex = sel;
      Preferences prefs;
      prefs.begin("watchy", false);
      prefs.putUChar("face", faceIndex);
      prefs.end();
      break;
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      break;
    }
  }

  showMenu(menuIndex, false);
}

void Watchy::showAccelerometer() {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&LANG_FONT);
  display.setTextColor(GxEPD_BLACK);

  Accel acc;

  long previousMillis = 0;
  long interval       = 200;

  guiState = APP_STATE;

  pinMode(BACK_BTN_PIN, INPUT);

  while (1) {

    unsigned long currentMillis = millis();

    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      break;
    }

    if (currentMillis - previousMillis > interval) {
      previousMillis = currentMillis;
      // Get acceleration data
      bool res          = sensor.getAccel(acc);
      uint8_t direction = sensor.getDirection();
      display.fillScreen(GxEPD_WHITE);
      display.setCursor(0, 30);
      if (res == false) {
        printlnL(S_ACCEL_ERROR);
      } else {
        display.print("  X:");
        display.println(acc.x);
        display.print("  Y:");
        display.println(acc.y);
        display.print("  Z:");
        display.println(acc.z);

        display.setCursor(30, 130);
        switch (direction) {
        case DIRECTION_DISP_DOWN:
          printlnL(S_DIR_DISP_DOWN);
          break;
        case DIRECTION_DISP_UP:
          printlnL(S_DIR_DISP_UP);
          break;
        case DIRECTION_BOTTOM_EDGE:
          printlnL(S_DIR_BOTTOM);
          break;
        case DIRECTION_TOP_EDGE:
          printlnL(S_DIR_TOP);
          break;
        case DIRECTION_RIGHT_EDGE:
          printlnL(S_DIR_RIGHT);
          break;
        case DIRECTION_LEFT_EDGE:
          printlnL(S_DIR_LEFT);
          break;
        default:
          printlnL(S_DIR_ERROR);
          break;
        }
      }
      display.display(true); // full refresh
    }
  }

  showMenu(menuIndex, false);
}

void Watchy::showWatchFace(bool partialRefresh) {
  display.setFullWindow();
  // At this point it is sure we are going to update
  display.epd2.asyncPowerOn();
  drawWatchFace();
  display.display(partialRefresh); // partial refresh
  guiState = WATCHFACE_STATE;
}

void Watchy::drawWatchFace() {
  drawFace(faceIndex);
}

void Watchy::drawFace(uint8_t idx) {
  display.setFont(&DSEG7_Classic_Bold_53);
  display.setCursor(5, 53 + 60);
  if (currentTime.Hour < 10) {
    display.print("0");
  }
  display.print(currentTime.Hour);
  display.print(":");
  if (currentTime.Minute < 10) {
    display.print("0");
  }
  display.println(currentTime.Minute);
}

weatherData Watchy::getWeatherData() {
  return _getWeatherData(settings.cityID, settings.lat, settings.lon,
    settings.weatherUnit, settings.weatherLang, settings.weatherURL,
    settings.weatherAPIKey, settings.weatherUpdateInterval);
}

weatherData Watchy::_getWeatherData(String cityID, String lat, String lon, String units, String lang,
                                   String url, String apiKey,
                                   uint8_t updateInterval) {
  currentWeather.isMetric = units == String("metric");
  if (weatherIntervalCounter < 0) { //-1 on first run, set to updateInterval
    weatherIntervalCounter = updateInterval;
  }
  if (weatherIntervalCounter >=
      updateInterval) { // only update if WEATHER_UPDATE_INTERVAL has elapsed
                        // i.e. 30 minutes
    if (connectWiFi()) {
      HTTPClient http; // Use Weather API for live data if WiFi is connected
      http.setConnectTimeout(3000); // 3 second max timeout
      String weatherQueryURL = url;
      if(cityID != ""){
        weatherQueryURL.replace("{cityID}", cityID);
      }else{
        weatherQueryURL.replace("{lat}", lat);
        weatherQueryURL.replace("{lon}", lon);
      }
      weatherQueryURL.replace("{units}", units);
      weatherQueryURL.replace("{lang}", lang);
      weatherQueryURL.replace("{apiKey}", apiKey);
      http.begin(weatherQueryURL.c_str());
      int httpResponseCode = http.GET();
      if (httpResponseCode == 200) {
        String payload             = http.getString();
        JSONVar responseObject     = JSON.parse(payload);
        currentWeather.temperature = int(responseObject["main"]["temp"]);
        currentWeather.weatherConditionCode =
            int(responseObject["weather"][0]["id"]);
        currentWeather.weatherDescription =
		        JSONVar::stringify(responseObject["weather"][0]["main"]);
	      currentWeather.external = true;
		        breakTime((time_t)(int)responseObject["sys"]["sunrise"], currentWeather.sunrise);
		        breakTime((time_t)(int)responseObject["sys"]["sunset"], currentWeather.sunset);
        // sync NTP during weather API call using the configured timezone
        syncNTP(gmtOffset);
      } else {
        // http error
      }
      http.end();
      // turn off radios
      WiFi.mode(WIFI_OFF);
      btStop();
    } else { // No WiFi, use internal temperature sensor
      uint8_t temperature = sensor.readTemperature(); // celsius
      if (!currentWeather.isMetric) {
        temperature = temperature * 9. / 5. + 32.; // fahrenheit
      }
      currentWeather.temperature          = temperature;
      currentWeather.weatherConditionCode = 800;
      currentWeather.external             = false;
    }
    weatherIntervalCounter = 0;
  } else {
    weatherIntervalCounter++;
  }
  return currentWeather;
}

float Watchy::getBatteryVoltage() {
  #ifdef ARDUINO_ESP32S3_DEV
    return analogReadMilliVolts(BATT_ADC_PIN) / 1000.0f * ADC_VOLTAGE_DIVIDER;
  #else
  if (RTC.rtcType == DS3231) {
    return analogReadMilliVolts(BATT_ADC_PIN) / 1000.0f *
           2.0f; // Battery voltage goes through a 1/2 divider.
  } else {
    return analogReadMilliVolts(BATT_ADC_PIN) / 1000.0f * 2.0f;
  }
  #endif
}

uint8_t Watchy::getBoardRevision() {
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  if(chip_info.model == CHIP_ESP32){ //Revision 1.0 - 2.0
    Wire.beginTransmission(0x68); //v1.0 has DS3231
    if (Wire.endTransmission() == 0){
      return 10;
    }
    delay(1);
    Wire.beginTransmission(0x51); //v1.5 and v2.0 have PCF8563
    if (Wire.endTransmission() == 0){
        pinMode(35, INPUT);
        if(digitalRead(35) == 0){
          return 20; //in rev 2.0, pin 35 is BTN 3 and has a pulldown
        }else{
          return 15; //in rev 1.5, pin 35 is the battery ADC
        }
    }
  }
  if(chip_info.model == CHIP_ESP32S3){ //Revision 3.0
    return 30;
  }
  return -1;
}

uint16_t Watchy::_readRegister(uint8_t address, uint8_t reg, uint8_t *data,
                               uint16_t len) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)address, (uint8_t)len);
  uint8_t i = 0;
  while (Wire.available()) {
    data[i++] = Wire.read();
  }
  return 0;
}

uint16_t Watchy::_writeRegister(uint8_t address, uint8_t reg, uint8_t *data,
                                uint16_t len) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(data, len);
  return (0 != Wire.endTransmission());
}

void Watchy::_bmaConfig() {

  if (sensor.begin(_readRegister, _writeRegister, delay) == false) {
    // fail to init BMA
    return;
  }

  // Accel parameter structure
  Acfg cfg;
  /*!
      Output data rate in Hz, Optional parameters:
          - BMA4_OUTPUT_DATA_RATE_0_78HZ
          - BMA4_OUTPUT_DATA_RATE_1_56HZ
          - BMA4_OUTPUT_DATA_RATE_3_12HZ
          - BMA4_OUTPUT_DATA_RATE_6_25HZ
          - BMA4_OUTPUT_DATA_RATE_12_5HZ
          - BMA4_OUTPUT_DATA_RATE_25HZ
          - BMA4_OUTPUT_DATA_RATE_50HZ
          - BMA4_OUTPUT_DATA_RATE_100HZ
          - BMA4_OUTPUT_DATA_RATE_200HZ
          - BMA4_OUTPUT_DATA_RATE_400HZ
          - BMA4_OUTPUT_DATA_RATE_800HZ
          - BMA4_OUTPUT_DATA_RATE_1600HZ
  */
  cfg.odr = BMA4_OUTPUT_DATA_RATE_100HZ;
  /*!
      G-range, Optional parameters:
          - BMA4_ACCEL_RANGE_2G
          - BMA4_ACCEL_RANGE_4G
          - BMA4_ACCEL_RANGE_8G
          - BMA4_ACCEL_RANGE_16G
  */
  cfg.range = BMA4_ACCEL_RANGE_2G;
  /*!
      Bandwidth parameter, determines filter configuration, Optional parameters:
          - BMA4_ACCEL_OSR4_AVG1
          - BMA4_ACCEL_OSR2_AVG2
          - BMA4_ACCEL_NORMAL_AVG4
          - BMA4_ACCEL_CIC_AVG8
          - BMA4_ACCEL_RES_AVG16
          - BMA4_ACCEL_RES_AVG32
          - BMA4_ACCEL_RES_AVG64
          - BMA4_ACCEL_RES_AVG128
  */
  cfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;

  /*! Filter performance mode , Optional parameters:
      - BMA4_CIC_AVG_MODE
      - BMA4_CONTINUOUS_MODE
  */
  cfg.perf_mode = BMA4_CONTINUOUS_MODE;

  // Configure the BMA423 accelerometer
  sensor.setAccelConfig(cfg);

  // Enable BMA423 accelerometer
  // Warning : Need to use feature, you must first enable the accelerometer
  // Warning : Need to use feature, you must first enable the accelerometer
  sensor.enableAccel();

  struct bma4_int_pin_config config;
  config.edge_ctrl = BMA4_LEVEL_TRIGGER;
  config.lvl       = BMA4_ACTIVE_HIGH;
  config.od        = BMA4_PUSH_PULL;
  config.output_en = BMA4_OUTPUT_ENABLE;
  config.input_en  = BMA4_INPUT_DISABLE;
  // The correct trigger interrupt needs to be configured as needed
  sensor.setINTPinConfig(config, BMA4_INTR1_MAP);

  struct bma423_axes_remap remap_data;
  remap_data.x_axis      = 1;
  remap_data.x_axis_sign = 0xFF;
  remap_data.y_axis      = 0;
  remap_data.y_axis_sign = 0xFF;
  remap_data.z_axis      = 2;
  remap_data.z_axis_sign = 0xFF;
  // Need to raise the wrist function, need to set the correct axis
  sensor.setRemapAxes(&remap_data);

  // Enable BMA423 isStepCounter feature
  sensor.enableFeature(BMA423_STEP_CNTR, true);
  // Enable BMA423 isTilt feature
  sensor.enableFeature(BMA423_TILT, true);
  // Enable BMA423 isDoubleClick feature
  sensor.enableFeature(BMA423_WAKEUP, true);

  // Reset steps
  sensor.resetStepCounter();

  // Turn on feature interrupt
  sensor.enableStepCountInterrupt();
  sensor.enableTiltInterrupt();
  // It corresponds to isDoubleClick interrupt
  sensor.enableWakeupInterrupt();
}

void Watchy::loadWifiNetworks() {
  Preferences prefs;
  prefs.begin("watchy", true);
  if (!prefs.isKey("wifiCount")) {
    prefs.end();
    wifiCount = 0;
#ifdef WIFI_SSID
    // First boot: seed the list from the build flags
    strlcpy(wifiNets[0].ssid, WIFI_SSID, sizeof(wifiNets[0].ssid));
    strlcpy(wifiNets[0].pass, WIFI_PASSWD, sizeof(wifiNets[0].pass));
    wifiCount = 1;
    saveWifiNetworks();
#endif
    return;
  }
  wifiCount = prefs.getUChar("wifiCount", 0);
  if (wifiCount > MAX_WIFI_NETWORKS) {
    wifiCount = MAX_WIFI_NETWORKS;
  }
  char key[12];
  for (uint8_t i = 0; i < wifiCount; i++) {
    snprintf(key, sizeof(key), "wifiSsid%u", i);
    String ssid = prefs.getString(key, "");
    snprintf(key, sizeof(key), "wifiPass%u", i);
    String pass = prefs.getString(key, "");
    strlcpy(wifiNets[i].ssid, ssid.c_str(), sizeof(wifiNets[i].ssid));
    strlcpy(wifiNets[i].pass, pass.c_str(), sizeof(wifiNets[i].pass));
  }
  prefs.end();
}

void Watchy::saveWifiNetworks() {
  Preferences prefs;
  prefs.begin("watchy", false);
  prefs.putUChar("wifiCount", wifiCount);
  char key[12];
  for (uint8_t i = 0; i < MAX_WIFI_NETWORKS; i++) {
    snprintf(key, sizeof(key), "wifiSsid%u", i);
    if (i < wifiCount) {
      prefs.putString(key, wifiNets[i].ssid);
      snprintf(key, sizeof(key), "wifiPass%u", i);
      prefs.putString(key, wifiNets[i].pass);
    } else {
      prefs.remove(key);
      snprintf(key, sizeof(key), "wifiPass%u", i);
      prefs.remove(key);
    }
  }
  prefs.end();
}

void Watchy::showWifiList() {
  guiState = APP_STATE;

  loadWifiNetworks();

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // wait until the MENU press that opened this screen is released
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  int8_t sel = 0;
  bool reorder = false;
  WifiNet backup[MAX_WIFI_NETWORKS];
  uint8_t backupCount = 0;

  display.setFullWindow();

  while (1) {
    uint8_t rows = wifiCount + (wifiCount < MAX_WIFI_NETWORKS ? 1 : 0);
    if (sel >= rows) {
      sel = rows - 1;
    }
    if (sel < 0) {
      sel = 0;
    }

    display.fillScreen(GxEPD_WHITE);
    display.setFont(&LANG_FONT);
    int16_t x1, y1;
    uint16_t w, h;
    for (uint8_t i = 0; i < rows; i++) {
      int16_t yPos = MENU_HEIGHT + (MENU_HEIGHT * i);
      const char *label = (i < wifiCount) ? wifiNets[i].ssid : STR[S_WIFI_ADD];
      display.setCursor(0, yPos);
      if (i == sel) {
        display.getTextBounds(label, 0, yPos, &x1, &y1, &w, &h);
        if (reorder) {
          display.drawRect(x1 - 1, y1 - 10, 200, h + 15, GxEPD_BLACK);
          display.drawRect(x1, y1 - 9, 198, h + 13, GxEPD_BLACK);
          display.setTextColor(GxEPD_BLACK);
        } else {
          display.fillRect(x1 - 1, y1 - 10, 200, h + 15, GxEPD_BLACK);
          display.setTextColor(GxEPD_WHITE);
        }
      } else {
        display.setTextColor(GxEPD_BLACK);
      }
      printlnL(label);
    }
    if (reorder) {
      display.setTextColor(GxEPD_BLACK);
      display.setCursor(0, 175);
      printlnL(S_HINT_BACK_SAVE);
      display.setCursor(0, 195);
      printlnL(S_HINT_MENU_CANCEL);
    }
    display.display(true); // partial refresh

    // wait for a button press
    while (digitalRead(UP_BTN_PIN) != ACTIVE_LOW &&
           digitalRead(DOWN_BTN_PIN) != ACTIVE_LOW &&
           digitalRead(MENU_BTN_PIN) != ACTIVE_LOW &&
           digitalRead(BACK_BTN_PIN) != ACTIVE_LOW) {
      delay(10);
    }

    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      if (reorder) {
        if (sel > 0) {
          std::swap(wifiNets[sel], wifiNets[sel - 1]);
          sel--;
        }
      } else {
        sel = (sel - 1 + rows) % rows;
      }
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      continue;
    }
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      if (reorder) {
        if (sel < wifiCount - 1) {
          std::swap(wifiNets[sel], wifiNets[sel + 1]);
          sel++;
        }
      } else {
        sel = (sel + 1) % rows;
      }
      while (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      continue;
    }
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      if (reorder) {
        memcpy(wifiNets, backup, sizeof(backup));
        wifiCount = backupCount;
        reorder = false;
      } else if (sel < wifiCount) {
        showWifiNetworkMenu(sel);
      } else {
        setupWifi(-1);
      }
      continue;
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      unsigned long pressStart = millis();
      bool longPress = false;
      while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
        if (millis() - pressStart >= WIFI_LONGPRESS_MS) {
          longPress = true;
        }
        delay(10);
      }
      if (reorder) {
        saveWifiNetworks();
        reorder = false;
      } else if (longPress) {
        if (sel < wifiCount && wifiCount >= 2) {
          memcpy(backup, wifiNets, sizeof(backup));
          backupCount = wifiCount;
          reorder = true;
        }
      } else {
        showMenu(menuIndex, false);
        return;
      }
      continue;
    }
  }
}

void Watchy::showWifiNetworkMenu(uint8_t idx) {
  guiState = APP_STATE;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // wait until the MENU press that opened this screen is released
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  const char *items[] = {STR[S_WIFI_SETUP_AP], STR[S_WIFI_DELETE]};
  int8_t sel = 0;

  display.setFullWindow();

  while (1) {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&LANG_FONT);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(0, MENU_HEIGHT);
    printlnL(wifiNets[idx].ssid);

    int16_t x1, y1;
    uint16_t w, h;
    for (int i = 0; i < 2; i++) {
      int16_t yPos = MENU_HEIGHT + (MENU_HEIGHT * (i + 1));
      display.setCursor(0, yPos);
      if (i == sel) {
        display.getTextBounds(items[i], 0, yPos, &x1, &y1, &w, &h);
        display.fillRect(x1 - 1, y1 - 10, 200, h + 15, GxEPD_BLACK);
        display.setTextColor(GxEPD_WHITE);
      } else {
        display.setTextColor(GxEPD_BLACK);
      }
      printlnL(items[i]);
    }
    display.display(true); // partial refresh

    // wait for a button press
    while (digitalRead(UP_BTN_PIN) != ACTIVE_LOW &&
           digitalRead(DOWN_BTN_PIN) != ACTIVE_LOW &&
           digitalRead(MENU_BTN_PIN) != ACTIVE_LOW &&
           digitalRead(BACK_BTN_PIN) != ACTIVE_LOW) {
      delay(10);
    }

    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      sel = (sel - 1 + 2) % 2;
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      continue;
    }
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      sel = (sel + 1) % 2;
      while (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      continue;
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      return;
    }
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
        delay(10);
      }
      if (sel == 0) {
        setupWifi(idx);
      } else {
        for (uint8_t i = idx; i + 1 < wifiCount; i++) {
          wifiNets[i] = wifiNets[i + 1];
        }
        wifiCount--;
        saveWifiNetworks();
      }
      return;
    }
  }
}

void Watchy::setupWifi(int8_t slot) {
  display.epd2.setBusyCallback(0); // temporarily disable lightsleep on busy
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  wifiManager.setTimeout(WIFI_AP_TIMEOUT);
  wifiManager.setAPCallback(_configModeCallback);
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&LANG_FONT);
  display.setTextColor(GxEPD_BLACK);
  if (!wifiManager.autoConnect(WIFI_AP_SSID)) { // WiFi setup failed
    printlnL(S_WIFI_SETUP_FAILED_1);
    printlnL(S_WIFI_SETUP_FAILED_2);
  } else {
    printlnL(S_WIFI_CONNECTED_TO);
    display.println(WiFi.SSID());
		printlnL(S_WIFI_LOCAL_IP);
		display.println(WiFi.localIP());
    if (slot >= 0) {
      strlcpy(wifiNets[slot].ssid, WiFi.SSID().c_str(), sizeof(wifiNets[slot].ssid));
      strlcpy(wifiNets[slot].pass, WiFi.psk().c_str(), sizeof(wifiNets[slot].pass));
    } else {
      strlcpy(wifiNets[wifiCount].ssid, WiFi.SSID().c_str(), sizeof(wifiNets[wifiCount].ssid));
      strlcpy(wifiNets[wifiCount].pass, WiFi.psk().c_str(), sizeof(wifiNets[wifiCount].pass));
      wifiCount++;
    }
    saveWifiNetworks();
    weatherIntervalCounter = -1; // Reset to force weather to be read again
    lastIPAddress = WiFi.localIP();
    WiFi.SSID().toCharArray(lastSSID, 30);
  }
  display.display(false); // full refresh
  // turn off radios
  WiFi.mode(WIFI_OFF);
  btStop();
  // enable lightsleep on busy
  display.epd2.setBusyCallback(WatchyDisplay::busyCallback);
  delay(3000);
}

void Watchy::_configModeCallback(WiFiManager *myWiFiManager) {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&LANG_FONT);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(0, 30);
  printlnL(S_WIFI_CONNECT_TO);
  display.print("SSID: ");
  display.println(WIFI_AP_SSID);
  display.print("IP: ");
  display.println(WiFi.softAPIP());
	printlnL(S_WIFI_MAC);
	display.println(WiFi.softAPmacAddress().c_str());
  display.display(false); // full refresh
}

bool Watchy::connectWiFi() {
  loadWifiNetworks();
  if (wifiCount == 0) {
    Serial.println("WIFI: no networks");
    WIFI_CONFIGURED = false;
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  int n = WiFi.scanNetworks();
  for (uint8_t i = 0; i < wifiCount; i++) {
    bool found = false;
    for (int j = 0; j < n; j++) {
      if (WiFi.SSID(j) == wifiNets[i].ssid) {
        found = true;
        break;
      }
    }
    if (!found) {
      Serial.printf("WIFI: '%s' not in range\n", wifiNets[i].ssid);
      continue;
    }
    Serial.printf("WIFI: connecting to '%s' ...\n", wifiNets[i].ssid);
    WiFi.begin(wifiNets[i].ssid, wifiNets[i].pass);
    if (WiFi.waitForConnectResult(10000) != WL_CONNECTED) {
      Serial.printf("WIFI: connect to '%s' failed, status=%d\n",
                    wifiNets[i].ssid, (int)WiFi.status());
      WiFi.disconnect();
      continue;
    }
    IPAddress ip;
    if (!WiFi.hostByName(settings.ntpServer.c_str(), ip)) {
      Serial.printf("WIFI: '%s' has no internet\n", wifiNets[i].ssid);
      WiFi.disconnect();
      continue;
    }
    lastIPAddress = WiFi.localIP();
    WiFi.SSID().toCharArray(lastSSID, 30);
    WIFI_CONFIGURED = true;
    Serial.printf("WIFI: connected, IP=%s\n",
                  WiFi.localIP().toString().c_str());
    WiFi.scanDelete();
    return true;
  }
  WiFi.scanDelete();
  WIFI_CONFIGURED = false;
  // turn off radios
  WiFi.mode(WIFI_OFF);
  btStop();
  return false;
}
/*
void Watchy::showUpdateFW() {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&LANG_FONT);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(0, 30);
  display.println("Please visit");
  display.println("watchy.sqfmi.com");
  display.println("with a Bluetooth");
  display.println("enabled device");
  display.println(" ");
  display.println("Press menu button");
  display.println("again when ready");
  display.println(" ");
  display.println("Keep USB powered");
  display.display(false); // full refresh

  guiState = FW_UPDATE_STATE;
}

void Watchy::updateFWBegin() {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&LANG_FONT);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(0, 30);
  printlnL(S_BLE_STARTED);
  display.println(" ");
  display.println("Watchy BLE OTA");
  display.println(" ");
  printlnL(S_BLE_WAITING);
  printlnL(S_BLE_CONNECTION);
  display.display(false); // full refresh

  BLE BT;
  BT.begin("Watchy BLE OTA");
  int prevStatus = -1;
  int currentStatus;

  while (1) {
    currentStatus = BT.updateStatus();
    if (prevStatus != currentStatus || prevStatus == 1) {
      if (currentStatus == 0) {
        display.setFullWindow();
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&LANG_FONT);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(0, 30);
        printlnL(S_BLE_CONNECTED);
        display.println(" ");
        printlnL(S_BLE_WAITING);
        printlnL(S_BLE_DOWNLOAD);
        display.display(false); // full refresh
      }
      if (currentStatus == 1) {
        display.setFullWindow();
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&LANG_FONT);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(0, 30);
        printlnL(S_FW_DOWNLOADING_1);
        printlnL(S_FW_DOWNLOADING_2);
        display.println(" ");
        display.print(BT.howManyBytes());
        display.println(" bytes");
        display.display(true); // partial refresh
      }
      if (currentStatus == 2) {
        display.setFullWindow();
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&LANG_FONT);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(0, 30);
        printlnL(S_FW_DONE_1);
        printlnL(S_FW_DONE_2);
        display.println(" ");
        printlnL(S_FW_REBOOTING);
        display.display(false); // full refresh

        delay(2000);
        esp_restart();
      }
      if (currentStatus == 4) {
        display.setFullWindow();
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&LANG_FONT);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(0, 30);
        printlnL(S_BLE_DISCONNECTED);
        display.println(" ");
        printlnL(S_FW_EXITING);
        display.display(false); // full refresh
        delay(1000);
        break;
      }
      prevStatus = currentStatus;
    }
    delay(100);
  }

  // turn off radios
  WiFi.mode(WIFI_OFF);
  btStop();
  showMenu(menuIndex, false);
}
*/
void Watchy::showSyncNTP() {
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&LANG_FONT);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(0, 30);
  printlnL(S_NTP_SYNCING);
  printL(S_NTP_GMT_OFFSET);
  display.println(gmtOffset);
  display.display(false); // full refresh
  if (connectWiFi()) {
    if (syncNTP()) {
      printlnL(S_NTP_OK);
      printlnL(S_NTP_CURRENT_TIME);

      RTC.read(currentTime);

      display.print(tmYearToCalendar(currentTime.Year));
      display.print("/");
      display.print(currentTime.Month);
      display.print("/");
      display.print(currentTime.Day);
      display.print(" - ");

      if (currentTime.Hour < 10) {
        display.print("0");
      }
      display.print(currentTime.Hour);
      display.print(":");
      if (currentTime.Minute < 10) {
        display.print("0");
      }
      display.println(currentTime.Minute);
    } else {
      printlnL(S_NTP_FAILED);
    }
    WiFi.mode(WIFI_OFF);
    btStop();
  } else {
    printlnL(S_WIFI_NOT_CONFIGURED);
  }
  display.display(true); // full refresh
  delay(3000);
  showMenu(menuIndex, false);
}

bool Watchy::syncNTP() { // NTP sync - call after connecting to WiFi and
                         // remember to turn it back off
  return syncNTP(gmtOffset,
                 settings.ntpServer.c_str());
}

bool Watchy::syncNTP(long gmt) {
  return syncNTP(gmt, settings.ntpServer.c_str());
}

bool Watchy::syncNTP(long gmt, String ntpServer) {
  // NTP sync - call after connecting to
  // WiFi and remember to turn it back off
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP, ntpServer.c_str(), gmt);
  timeClient.begin();
  if (!timeClient.forceUpdate()) {
    return false; // NTP sync failed
  }
  tmElements_t tm;
  breakTime((time_t)timeClient.getEpochTime(), tm);
  RTC.set(tm);
  return true;
}
