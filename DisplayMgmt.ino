#include "globalInclude.h"

// This file contains most of the functions to handle the display
#include "modeMgmt.h"

#define HORIZ_DIV_POS 85
#define VERT_DIV_POS 125

#define LIGHT_SENSOR_IN_PIN 34
#define TFT_BACKLIGHT_OUT_PIN 15

#define SECONDS_IN_DAY 86400

//===================================================================
//======================== Globals ==================================
//===================================================================
extern alarmData alarm1;
extern alarmData alarm2;
extern alarmData alarm3;

//button objects
TFT_eSPI_Button hoursUp, hoursDown, minUp, minDown;
TFT_eSPI_Button roomLightButton, readLightButton, nightLightButton;
TFT_eSPI_Button hUpButton, hDownButton, sUpButton, sDownButton, bUpButton, bDownButton;
TFT_eSPI_Button daysButton[7];

TFT_eSPI_Button roomSubModeButton, readSubModeButton, nightSubModeButton;

//===================================================================
//================ Display Manager ==================================
//===================================================================
void dispMgr( void * parameter) {
  extern displayMgr disp;
  static int lastMode = 0;

  // Set up PWM for screen backlight
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BACKLIGHT_OUT_PIN, 0);

  controlBacklight(); // set the screen brightness

  if ( esp_task_wdt_add(NULL) != ESP_OK) { // add task to WDT
    Serial.println("dispMgr: Unable to add displayMgr to taskWDT!");
  }
  uint32_t ts;
  TickType_t xLastWakeTime;
  static int minuteNow = 0, minutePrevious = 0;
  Serial.println("dispMgr: Entering Screen Management task");

  // Setup tft display
  if (xSemaphoreTake(tftMutex, (TickType_t) 50 ) == pdTRUE ) {
    tft.begin();
    tft.setRotation(3);

    // read diagnostics (optional but can help debug problems)
    uint8_t x = tft.readcommand8(ILI9341_RDMODE);
    Serial.print("Display Power Mode: 0x"); Serial.println(x, HEX);
    x = tft.readcommand8(ILI9341_RDMADCTL);
    Serial.print("MADCTL Mode: 0x"); Serial.println(x, HEX);
    x = tft.readcommand8(ILI9341_RDPIXFMT);
    Serial.print("Pixel Format: 0x"); Serial.println(x, HEX);
    x = tft.readcommand8(ILI9341_RDIMGFMT);
    Serial.print("Image Format: 0x"); Serial.println(x, HEX);
    x = tft.readcommand8(ILI9341_RDSELFDIAG);
    Serial.print("Self Diagnostic: 0x"); Serial.println(x, HEX);

    tft.fillScreen(TFT_BLACK);
    xSemaphoreGive(tftMutex);
  }
  else {
    Serial.print("dispMgr: Unable to set up Display. Restarting.");
    ESP.restart();
  }

  drawTextString("Waiting for Time Sync.", tft.width() / 2, tft.height() / 2, FSSB12, 320, MC_DATUM, TFT_WHITE, TFT_BLACK);

  while (timeStatus() != timeSet) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Serial.println("dispMgr: Delaying start of screen. Status: " + String(timeStatus()));
  }

  ts = now();
  drawTime(ts, true);
  drawWeatherDisplay(true);

  if (esp_task_wdt_reset() != ESP_OK) {
    Serial.println("dispMgr: Unable to reset displayMgr taskWDT!");
  }
  const TickType_t xFrequency = 75 / portTICK_PERIOD_MS; // run the master display loop at 20Hz
  xLastWakeTime = xTaskGetTickCount();
  // Master Display Loop
  for (;;) {
    ts = now();
    minuteNow = minute(ts);

    controlBacklight(); // set the screen brightness

    if (disp.getFullReDraw() || lastMode != disp.getCurrentMode()) {
      sprite1.delSprite();
      sprite2.delSprite();
      if (disp.getFullReDraw()) {
        drawTime(ts, true);
      }
      disp.setFullReDraw(false);
      disp.setSpriteEnable(false);
      lastMode = disp.getCurrentMode();
      clearWorkingArea();
      if (disp.getCurrentMode() == MAIN_MODE) {
        drawWeatherDisplay(true);
      }
      else if (disp.getCurrentMode() == CURRENT_WEATHER_MODE) {
        drawCurrentWeatherDisplay(true);
      }
      else if (disp.getCurrentMode() == FORECAST_MODE) {
        drawForecastWeatherDisplay(true);
      }
      else if (disp.getCurrentMode() == ALARM_DISPLAY_MODE) {
        drawAlarmDisplay(true);
      }
      else if (disp.getCurrentMode() == ALARM_SET_MODE) {
        drawAlarmSetDisplay(true, disp.getAlarmEdit());
      }
      else if (disp.getCurrentMode() == GEN_LIGHT_CTRL_MODE) {
        drawLightSetDisplay(true);
      }
      disp.setSpriteEnable(true);
      continue;
    }

    if (minuteNow != minutePrevious) {
      drawTime(ts, false);
      minutePrevious = minuteNow;
    }

    if (disp.getDrawLowerScreen()) {
      disp.setDrawLowerScreen(false);
      disp.setSpriteEnable(false);
      // vTaskDelay(1 / portTICK_RATE_MS); // Delay a milisec to allow the weather system to catch up
      if (disp.getCurrentMode() == MAIN_MODE) {
        drawWeatherDisplay(false);
      }
      else if (disp.getCurrentMode() == CURRENT_WEATHER_MODE) {
        drawCurrentWeatherDisplay(false);
      }
      else if (disp.getCurrentMode() == FORECAST_MODE) {
        drawForecastWeatherDisplay(false);
      }
      else if (disp.getCurrentMode() == ALARM_DISPLAY_MODE) {
        drawAlarmDisplay(false);
      }
      else if (disp.getCurrentMode() == ALARM_SET_MODE) {
        drawAlarmSetDisplay(false, disp.getAlarmEdit());
      }
      else if (disp.getCurrentMode() == GEN_LIGHT_CTRL_MODE) {
        drawLightSetDisplay(false);
      }
      disp.setSpriteEnable(true);
    }

    if (esp_task_wdt_reset() != ESP_OK) {
      Serial.println("Unable to reset displayMgr taskWDT!");
    }

    if (disp.getDrawTimeSection()) {
      disp.setDrawTimeSection(false);
      drawTime(now(), false);
    }

    vTaskDelayUntil( &xLastWakeTime, xFrequency );
  }
}

//===================================================================
//==================== Control Backlight ============================
//===================================================================
void controlBacklight ( void ) {
  uint16_t lightReading;
  uint16_t backlightCtrl;
  static uint16_t filteredBacklightCtrl = 128;
  static uint16_t loopCount = 0;

  lightReading = analogRead(LIGHT_SENSOR_IN_PIN);

  if (lightReading < 127) {
    backlightCtrl = 8;
  }
  else if (lightReading < 2500) {
    backlightCtrl = 8 + (int)(lightReading / 10.46);
  }
  else {
    backlightCtrl = 275; // overkill to allow the filter to saturate
  }
  filteredBacklightCtrl = (int)(((float)filteredBacklightCtrl * 0.95) + ((float)backlightCtrl * 0.05));

  uint16_t blTemp;

  if (disp.checkRecentTouch())    blTemp = filteredBacklightCtrl;
  else blTemp = filteredBacklightCtrl >> 2;
  if (blTemp > 255) blTemp = 255; // bounds check
  if (blTemp < 3) blTemp = 3; // bounds check
  ledcWrite(0, blTemp);
}

//===================================================================
//==================== Draw Time ====================================
//===================================================================
// Draw the "time" section of the screen
void drawTime(time_t ts, bool repaint)
{
  int xpos = tft.width() / 2; // Half the screen width
  int padding;
  int16_t timeVertPos = 70, dateVertPos = 20;
  String dateString = assembleDateStr(ts);
  String hours = assembleTimeStr(ts);
  static int wifiStatusPrevious = -1;
  static int dayPrevious = 0;
  static int dayNow = 0;

  if (repaint) {
    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      tft.fillRect(0, 0, 320, HORIZ_DIV_POS + 1, TFT_BLACK); // Clear the upper part of the screen
      // Draw the divider
      tft.drawFastHLine(0, HORIZ_DIV_POS - 1, tft.width(), TFT_BLUE);
      tft.drawFastHLine(0, HORIZ_DIV_POS, tft.width(), TFT_BLUE);
      tft.drawFastHLine(0, HORIZ_DIV_POS + 1, tft.width(), TFT_BLUE);
      xSemaphoreGive(tftMutex);
    }
    else {
      Serial.print("drawTime: Unable to run inital screen setup. Try again next time.");
    }
  }

  Serial.println("drawTime: Time Redraw. - " + hours);

  if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
    padding = tft.textWidth(" 99:99 PM ", GFXFF);
    tft.setTextPadding(padding);
    tft.setFreeFont(FSSB24);
    tft.setTextDatum(C_BASELINE); // Centre text on x,y position
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(hours, xpos, timeVertPos, GFXFF);
    xSemaphoreGive(tftMutex);
  }

  dayNow = day(ts);
  if (dayNow != dayPrevious  || repaint) {
    dayPrevious = dayNow;
    Serial.println("drawTime: Date Redraw. - " + dateString);

    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      tft.setFreeFont(FSS9);
      tft.setTextDatum(C_BASELINE); // Centre text on x,y position
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      padding = tft.textWidth(" Saturday Dec. 99 9999 ", GFXFF);
      tft.setTextPadding(padding);
      tft.drawString(dateString, xpos, 20, GFXFF);
      xSemaphoreGive(tftMutex);
    }
    else {
      Serial.print("drawTime: Unable to draw date to display. unable to obtain Mutex");
    }
  }

  // Draw WiFi Indicator
  if (disp.getCurrWiFiStatus() != wifiStatusPrevious || repaint) {
    wifiStatusPrevious = disp.getCurrWiFiStatus();
    drawWiFiStatus(disp.getCurrWiFiStatus());
  }

  drawAlarmIndicator(repaint);

  drawReadingLightButton(repaint);

  drawRoomLightButton(repaint);

  drawNightLightButton(repaint);
}

//===================================================================
//============== Draw Alarm Indicator ===============================
//===================================================================
void drawAlarmIndicator(bool repaint) {
  static bool lastAlarmAct;
  static bool lastSnoozeAct;
  static bool lastRingAct;

  bool alarmAct = (alarm1.isActive() || alarm2.isActive() || alarm3.isActive());
  bool snoozeAct = (alarm1.isSnoozed() || alarm2.isSnoozed() || alarm3.isSnoozed());
  uint8_t ringAct = disp.getAlarmRinging();
  if (alarmAct != lastAlarmAct || snoozeAct != lastSnoozeAct || ringAct != lastRingAct || repaint) {
    if (snoozeAct || ringAct != 0) {
      String alarmIcon = ("/alarmicon/alarm_red_sm.bmp");
      char iconChar[31];
      alarmIcon.toCharArray(iconChar, 31);
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        drawBmp(iconChar, 30, 2);
        xSemaphoreGive(tftMutex);
      }
      else {
        Serial.print("drawAlarmIndicator: Unable to draw alarm status to display. Unable to obtain Mutex");
      }
    }
    else if (alarmAct) {
      String alarmIcon = ("/alarmicon/alarm_sm.bmp");
      char iconChar[31];
      alarmIcon.toCharArray(iconChar, 31);
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        drawBmp(iconChar, 30, 2);
        xSemaphoreGive(tftMutex);
      }
      else {
        Serial.print("drawAlarmIndicator: Unable to draw alarm status to display. Unable to obtain Mutex");
      }
    }
    else {
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        tft.fillRect(30, 2, 20, 20, TFT_BLACK);
        xSemaphoreGive(tftMutex);
      }
      else {
        Serial.print("drawAlarmIndicator: Unable to erase alarm status display. Unable to obtain Mutex.");
      }
    }
    lastAlarmAct = alarmAct;
    lastSnoozeAct = snoozeAct;
    lastRingAct = ringAct;
  }
}

//===================================================================
//============== Draw Reading Light Indicator =======================
//===================================================================
void drawReadingLightButton(bool repaint) {
  static bool lastReadLightState;
  String lightIcon;

  readLightButton.initButton(&tft, 26, 55, 50, 50, TFT_WHITE, TFT_BLACK, TFT_RED, "", 1);

  bool currentState = ledMaster.getReadLightState();

  if (currentState != lastReadLightState || repaint) {
    lastReadLightState = currentState;

    if (ledMaster.getReadLightState()) {
      lightIcon = "/lightIcon/book_on.bmp";
    }
    else {
      lightIcon = "/lightIcon/book_off.bmp";
    }

    if (repaint) {
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        readLightButton.drawButton(false);
        xSemaphoreGive(tftMutex);
      }
      else {
        Serial.print("drawReadingLightButton: Unable to draw light status to display. Unable to obtain Mutex");
      }
    }

    char iconChar[31];
    lightIcon.toCharArray(iconChar, 31);
    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      drawBmp(iconChar, 6, 35);
      xSemaphoreGive(tftMutex);
    }
    else {
      Serial.print("drawReadingLightButton: Unable to draw light status to display. Unable to obtain Mutex");
    }
  }
}

//===================================================================
//================= Draw Room Light Indicator =======================
//===================================================================
void drawRoomLightButton(bool repaint) {
  static bool lastRoomLightState;
  String lightIcon;

  roomLightButton.initButton(&tft, 295, 20, 50, 41, TFT_WHITE, TFT_BLACK, TFT_RED, "", 1);

  bool currentState = ledMaster.getRoomLightState();

  if (currentState != lastRoomLightState || repaint) {
    lastRoomLightState = currentState;

    if (currentState) {
      lightIcon = "/lightIcon/light_on30.bmp";
    }
    else {
      lightIcon = "/lightIcon/light_off30.bmp";
    }

    if (repaint) {
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        roomLightButton.drawButton(false);
        xSemaphoreGive(tftMutex);
      }
      else {
        Serial.print("drawRoomLightButton: Unable to draw light status to display. Unable to obtain Mutex");
      }
    }

    char iconChar[31];
    lightIcon.toCharArray(iconChar, 31);
    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      drawBmp(iconChar, 280, 5);
      xSemaphoreGive(tftMutex);
    }
    else {
      Serial.print("drawRoomLightButton: Unable to draw light status to display. Unable to obtain Mutex");
    }
  }
}

//===================================================================
//================= Draw Night Light Indicator =======================
//===================================================================
void drawNightLightButton(bool repaint) {
  static bool lastNightLightState;
  String lightIcon;

  nightLightButton.initButton(&tft, 295, 61, 50, 41, TFT_WHITE, TFT_BLACK, TFT_RED, "", 1);

  bool currentState = ledMaster.getNightLightState();

  if (currentState != lastNightLightState || repaint) {
    lastNightLightState = currentState;

    if (currentState) {
      lightIcon = "/lightIcon/night_on30.bmp";
    }
    else {
      lightIcon = "/lightIcon/night_off30.bmp";
    }

    if (repaint) {
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        nightLightButton.drawButton(false);
        xSemaphoreGive(tftMutex);
      }
      else {
        Serial.print("drawNightLightButton: Unable to draw light status to display. Unable to obtain Mutex");
      }
    }

    char iconChar[31];
    lightIcon.toCharArray(iconChar, 31);
    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      drawBmp(iconChar, 280, 47);
      xSemaphoreGive(tftMutex);
    }
    else {
      Serial.print("drawNightLightButton: Unable to draw light status to display. Unable to obtain Mutex");
    }
  }
}

//===================================================================
//============== Draw Weather Display ===============================
//===================================================================
void drawWeatherDisplay(bool repaint) {
  int padding;
  int showTomorrow;
  int lineLength;
  String msgTmp;
  static int lastCurTemp;
  static String lastHighTemp;
  static String lastLowTemp;
  static String lastHumid;
  static String lastPercip;
  static String lastCurrMsg;
  static String lastIcon = " ";
  bool redrawCurrStat = false;

  if (hour() < 14 || hour() == 24) { // After 2pm show tomorrow's forcast
    showTomorrow = 0;
  }
  else {
    showTomorrow = 1;
  }

  if (repaint) {
    if (xSemaphoreTake(tftMutex, (TickType_t) 100) == pdTRUE ) {
      tft.drawFastVLine(VERT_DIV_POS - 1, HORIZ_DIV_POS, tft.height(), TFT_BLUE);
      tft.drawFastVLine(VERT_DIV_POS, HORIZ_DIV_POS, tft.height(), TFT_BLUE);
      tft.drawFastVLine(VERT_DIV_POS + 1, HORIZ_DIV_POS, tft.height(), TFT_BLUE);
      xSemaphoreGive(tftMutex);
    }
    else {
      Serial.println("drawWeatherDisplay: Unable to run inital screen setup. Try again next time.");
    }
  }

  // Draw the weather icon and text
  if (disp.getWeatherValid() == true) {
    if (xSemaphoreTake(dswMutex, (TickType_t) 50) != pdTRUE ) {
      Serial.println("drawWeatherDisplay: Unable to get dfwMutex to update weather display!");
      return;
    }

    String weatherIcon = "";
    String currentSummary = current->summary;
    currentSummary.toLowerCase();
    if (currentSummary.indexOf("light rain") >= 0 && (current->icon == ICON_RAIN)) weatherIcon = "lightRain";
    else if (currentSummary.indexOf("drizzle") >= 0 && (current->icon == ICON_RAIN)) weatherIcon = "drizzle";
    else weatherIcon = getMeteoconIcon(current->icon);

    if (weatherIcon != lastIcon || repaint) { // save some work drawing the weather icon
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        drawWeatherIcon(weatherIcon, 12, 105, true);
        xSemaphoreGive(tftMutex);
        lastIcon = weatherIcon;
        redrawCurrStat = true;
      }
      else {
        Serial.println("drawWeatherDisplay: Unable to push new weather icon.");
      }
    }

    drawTextString("Currently", 62, 90, FSS9, 124, TC_DATUM, TFT_GREEN, TFT_BLACK);

    msgTmp = String(current->summary) + ", " + String((int)round(current->temperature)) + "F";
    lineLength = tft.textWidth(msgTmp, GFXFF);
    if (msgTmp != lastCurrMsg || repaint || redrawCurrStat || (int)round(current->temperature) != lastCurTemp) {
      redrawCurrStat = false;
      lastCurrMsg = msgTmp;
      sprite1.delSprite();
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        tft.fillRect(4, 205, 120 , 20, TFT_BLACK);
        xSemaphoreGive(tftMutex);
      }
      if (lineLength >= 115) {
        sprite1.newSprite(msgTmp, 20, 115, 2, 20, TFT_GREEN, 5, 205);
      }
      else {
        drawTextString(msgTmp, 62, 205, FSS9, 124, TC_DATUM, TFT_GREEN, TFT_BLACK);
      }
    }

    if (showTomorrow == 1) {
      drawTextString("Tomorrow's Forecast", 140, 90, FSS9, 194, TL_DATUM, TFT_YELLOW, TFT_BLACK);
    }
    else {
      drawTextString("Today's Forecast", 140, 90, FSS9, 194, TL_DATUM, TFT_YELLOW, TFT_BLACK);
    }

    drawTextString("Powered by Dark Sky", 140, 215, FSS9, 194, TL_DATUM, TFT_DARKGREY, TFT_BLACK);

    msgTmp = String(daily->summary[showTomorrow]);
    lineLength = tft.textWidth(msgTmp, GFXFF);
    if (msgTmp != sprite2.currMsg() || repaint) {
      sprite2.delSprite();
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        tft.fillRect(135, 115, 185 , 20, TFT_BLACK);
        xSemaphoreGive(tftMutex);
      }
      if (lineLength >= 185) {
        sprite2.newSprite(msgTmp, 20, 180, 2, 30, TFT_YELLOW, 140, 112);
      }
      else {
        drawTextString(msgTmp, 140, 112, FSS9, 194, TL_DATUM, TFT_YELLOW, TFT_BLACK);
      }
    }

    msgTmp = "High: " + String((int)round(daily->temperatureHigh[showTomorrow])) + " F";
    if (lastHighTemp != msgTmp || repaint) {
      drawTextString(msgTmp, 140, 135, FSS9, 194, TL_DATUM, TFT_YELLOW, TFT_BLACK);
      lastHighTemp = msgTmp;
    }

    msgTmp = "Low: " + String((int)round(daily->temperatureLow[showTomorrow])) + " F";
    if (lastLowTemp != msgTmp || repaint) {
      drawTextString(msgTmp, 140, 155, FSS9, 194, TL_DATUM, TFT_YELLOW, TFT_BLACK);
      lastLowTemp = msgTmp;
    }

    msgTmp = formatPrecipString(daily->precipType[showTomorrow], daily->precipProbability[showTomorrow], daily->precipIntensity[showTomorrow]);
    if (lastPercip != msgTmp || repaint) {
      drawTextString(msgTmp, 140, 175, FSS9, 194, TL_DATUM, TFT_YELLOW, TFT_BLACK);
      lastPercip = msgTmp;
    }

    msgTmp = "Humidity: " + String(daily->humidity[showTomorrow]) + "%";
    if (lastHumid != msgTmp || repaint) {
      drawTextString(msgTmp, 140, 195, FSS9, 194, TL_DATUM, TFT_YELLOW, TFT_BLACK);
      lastHumid = msgTmp;
    }
    xSemaphoreGive(dswMutex);
  }
  else
  {
    drawTextString("Currently", 62, 90, FSS9, 124, TC_DATUM, TFT_GREEN, TFT_BLACK);

    // turn off the sprites if the weather is invalid
    sprite1.delSprite();
    sprite2.delSprite();

    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {// clear the forecastSprite
      tft.fillRect(130, 115, 190 , 20, TFT_BLACK);
      xSemaphoreGive(tftMutex);
    }
    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) { // clear the currentSprite
      tft.fillRect(4, 205, 120 , 20, TFT_BLACK);
      xSemaphoreGive(tftMutex);
    }
    drawTextString("Wating for Weather...", 149, 90, FSS9, 194, TL_DATUM, TFT_YELLOW, TFT_BLACK);
  }
}

//===================================================================
//================ Draw Current Weather =============================
//===================================================================
void drawCurrentWeatherDisplay(bool repaint) {
  String msgTmp;
  int lineLength;
  static int lastCurTemp;
  static String lastPercip;
  static String lastCurrMsg;
  static String lastHumid;
  static String lastPress;
  static String lastWind;
  static String lastIcon = " ";

  if (disp.getWeatherValid() == true) {
    if (xSemaphoreTake(dswMutex, (TickType_t) 100) != pdTRUE ) {
      Serial.println("drawWeatherDisplay: Unable to get dfwMutex to update weather display!");
      return;
    }

    /*
        // Set text paramiters for this section
        tft.setFreeFont(FSS9);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextPadding(310);
    */
    if (repaint) {
      drawTextString("Current Weather", tft.width() / 2, 90, FSS9, 310, TC_DATUM, TFT_GREEN, TFT_BLACK);
    }

    // draw weather text
    msgTmp = "Summary: " + String(current->summary);
    // lineLength = tft.textWidth(msgTmp, GFXFF);
    if (msgTmp != lastCurrMsg || repaint ) {
      lastCurrMsg = msgTmp;
      drawTextString(msgTmp, 5, 110, FSS9, 310, TL_DATUM, TFT_GREEN, TFT_BLACK);
    }

    msgTmp = formatPrecipString(current->precipType, current->precipProbability, current->precipIntensity) + " - " + String(current->precipProbability) + "%";
    if (lastPercip != msgTmp || repaint) {
      drawTextString(msgTmp, 5, 130, FSS9, 310, TL_DATUM, TFT_GREEN, TFT_BLACK);
      lastPercip = msgTmp;
    }

    msgTmp = "Temprature: " + String(current->temperature) + " F";
    // lineLength = tft.textWidth(msgTmp, GFXFF);
    if (msgTmp != lastCurrMsg || repaint ) {
      lastCurrMsg = msgTmp;
      drawTextString(msgTmp, 5, 150, FSS9, 310, TL_DATUM, TFT_GREEN, TFT_BLACK);
    }

    msgTmp = "Humidity: " + String(current->humidity) + "%";
    if (lastHumid != msgTmp || repaint) {
      drawTextString(msgTmp, 5, 170, FSS9, 310, TL_DATUM, TFT_GREEN, TFT_BLACK);
      lastHumid = msgTmp;
    }

    msgTmp = "Presure: " + String(current->pressure) + "mb";
    if (lastPress != msgTmp || repaint) {
      drawTextString(msgTmp, 5, 190, FSS9, 310, TL_DATUM, TFT_GREEN, TFT_BLACK);
      lastPress = msgTmp;
    }

    msgTmp = formatWindString(current->windSpeed, current->windGust, current->windBearing);
    //msgTmp = "I am the test case, and this is WAY too long to fit in the availible space.";
    lineLength = tft.textWidth(msgTmp, GFXFF);
    if (msgTmp != sprite1.currMsg() || repaint) {
      sprite1.delSprite();
      if (xSemaphoreTake(tftMutex, (TickType_t) 35) == pdTRUE ) {
        tft.fillRect(5, 210, 310, 20, TFT_BLACK);
        xSemaphoreGive(tftMutex);
      }
      if (lineLength >= 310) {
        sprite1.newSprite(msgTmp, 20, 310, 2, 30, TFT_GREEN, 5, 210);
      }
      else {
        drawTextString(msgTmp, 5, 210, FSS9, 310, TL_DATUM, TFT_GREEN, TFT_BLACK);
      }
    }

    xSemaphoreGive(dswMutex);
  }
  else {
    drawTextString("Waiting for Weather Data...", tft.width() / 2, 90, FSS9, 310, TC_DATUM, TFT_GREEN, TFT_BLACK);
  }
}

//===================================================================
//================ Draw Forecast Weather =============================
//===================================================================
void drawForecastWeatherDisplay(bool repaint) {
  String msgTmp;
  int lineLength;
  int dayCounter;
  static String lastHighTemp;
  static String lastLowTemp;
  static String lastIcon[3] = (" ", " ", " ");
  uint8_t tmpDay;
  static uint8_t lastDays[3] = {0};
  time_t ts = now();

  if (hour() < 14 || hour() == 24) { // After 2pm show the following day's forcast
    dayCounter = 1;
  }
  else {
    dayCounter = 2;
  }

  if (disp.getWeatherValid() == true) {
    if (xSemaphoreTake(dswMutex, (TickType_t) 100) != pdTRUE ) {
      Serial.println("drawWeatherDisplay: Unable to get dfwMutex to update weather display!");
      return;
    }

    if (repaint) {
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        tft.drawFastHLine(0, 110, tft.width(), TFT_WHITE);
        tft.drawFastVLine(tft.width() / 3, 110, tft.height(), TFT_WHITE);
        tft.drawFastVLine(2 * tft.width() / 3, 110, tft.height(), TFT_WHITE);
        xSemaphoreGive(tftMutex);
      }
    }

    // draw weather text
    msgTmp = "Forecast: " + String(daily->overallSummary);
    //msgTmp = "I am the test case, and this is WAY too long to fit in the availible space.";
    lineLength = tft.textWidth(msgTmp, GFXFF);
    if (msgTmp != sprite1.currMsg() || repaint) {
      sprite1.delSprite();
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        tft.fillRect(5, 90, 310, 20, TFT_BLACK);
        xSemaphoreGive(tftMutex);
      }
      if (lineLength >= 310) {
        sprite1.newSprite(msgTmp, 20, 310, 2, 30, TFT_YELLOW, 5, 90);
      }
      else {
        drawTextString(msgTmp, tft.width() / 2, 90, FSS9, 320, TC_DATUM, TFT_YELLOW, TFT_BLACK);
      }
    }

    for (int i = 0; i <  3; i++) {
      tmpDay = weekday(ts + (dayCounter * SECONDS_IN_DAY));
      if (lastDays[i] != tmpDay || repaint) {
        msgTmp = getDayOfWeek(weekday(ts + (dayCounter * SECONDS_IN_DAY)));
        drawTextString(msgTmp, 52 + (107 * i), 115, FSS9, 105, TC_DATUM, TFT_YELLOW, TFT_BLACK);
        lastDays[i] = tmpDay;
      }

      String weatherIcon = "";
      String dailySummary = daily->summary[dayCounter];
      dailySummary.toLowerCase();
      if (dailySummary.indexOf("light rain") >= 0 && (daily->icon[dayCounter] == ICON_RAIN)) weatherIcon = "lightRain";
      else if (dailySummary.indexOf("drizzle") >= 0 && (daily->icon[dayCounter] == ICON_RAIN)) weatherIcon = "drizzle";
      else weatherIcon = getMeteoconIcon(daily->icon[dayCounter]);

      if (weatherIcon != lastIcon[i] || repaint) { // save some work drawing the weather icon
        if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
          drawWeatherIcon(weatherIcon, 28 + (107 * i), 135, false);
          xSemaphoreGive(tftMutex);
          lastIcon[i] = weatherIcon;
        }
        else {
          Serial.println("drawWeatherDisplay: Unable to push new weather icon.");
        }
      }

      msgTmp = "H: " + String((int)round(daily->temperatureHigh[dayCounter])) + " F";
      if (lastHighTemp != msgTmp || repaint) {
        drawTextString(msgTmp, 52 + (107 * i), 185, FSS9, 105, TC_DATUM, TFT_YELLOW, TFT_BLACK);
        lastHighTemp = msgTmp;
      }

      msgTmp = "L: " + String((int)round(daily->temperatureLow[dayCounter])) + " F";
      if (lastLowTemp != msgTmp || repaint) {
        drawTextString(msgTmp, 52 + (107 * i), 205, FSS9, 105, TC_DATUM, TFT_YELLOW, TFT_BLACK);
        lastLowTemp = msgTmp;
      }
      dayCounter++;
    }
    xSemaphoreGive(dswMutex);
  }
  else {
    drawTextString("Waiting for Weather Data...", tft.width() / 2, 90, FSS9, 320, TC_DATUM, TFT_YELLOW, TFT_BLACK);
  }
}

//===================================================================
//================== Draw Alarm Display =============================
//===================================================================
void drawAlarmDisplay(bool repaint) {
  String msgTmp;
  int lineLength;
  String alarmIcon;
  char iconChar[30];
  const int alarmIndHoriz = 285;
  static bool alarm1LastActive;
  static bool alarm2LastActive;
  static bool alarm3LastActive;
  static bool alarm1LastSunrise;
  static bool alarm2LastSunrise;
  static bool alarm3LastSunrise;

  if (repaint) {
    drawTextString("Set Alarms", tft.width() / 2, 90, FSS9, 320, TC_DATUM, TFT_WHITE, TFT_BLACK);
  }

  if (alarm1.isActive() != alarm1LastActive || alarm1.isSunriseActive() != alarm1LastSunrise || repaint) {
    drawAlarmDisplayElem(repaint, 1, 110);
    alarm1LastActive = alarm1.isActive();
    alarm1LastSunrise = alarm1.isSunriseActive();
    drawAlarmIndicator(false);
  }
  if (alarm2.isActive() != alarm2LastActive || alarm2.isSunriseActive() != alarm2LastSunrise || repaint) {
    drawAlarmDisplayElem(repaint, 2, 153);
    alarm2LastActive = alarm2.isActive();
    alarm2LastSunrise = alarm2.isSunriseActive();
    drawAlarmIndicator(false);
  }
  if (alarm3.isActive() != alarm3LastActive || alarm3.isSunriseActive() != alarm3LastSunrise || repaint) {
    drawAlarmDisplayElem(repaint, 3, 197);
    alarm3LastActive = alarm3.isActive();
    alarm3LastSunrise = alarm3.isSunriseActive();
    drawAlarmIndicator(false);
  }
}

//===================================================================
//============= Draw Alarm Display Element ==========================
//===================================================================
void drawAlarmDisplayElem(bool repaint, uint8_t alarmNumber, uint16_t yOff) {

  int lineLength;
  static String lastTime;
  static String lastDays;
  String tmpString;
  String lightIcon;
  String alarmIcon;
  char iconChar[31];

  // set up the pointer to the alarm we are working on
  alarmData *workAlarm;
  if (alarmNumber == 1) {
    workAlarm = &alarm1;
  }
  else if (alarmNumber == 2) {
    workAlarm = &alarm2;
  }
  else if (alarmNumber == 3) {
    workAlarm = &alarm3;
  }

  if (repaint) {
    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      tft.drawFastHLine(0, yOff, tft.width(), TFT_WHITE);
      xSemaphoreGive(tftMutex);
    }
    workAlarm->button.initButton(&tft, 302, yOff + 22, 34, 36, TFT_WHITE, TFT_BLACK, TFT_RED, "", 1);
    workAlarm->dawnButton.initButton(&tft, 264, yOff + 22, 34, 36, TFT_WHITE, TFT_BLACK, TFT_BLACK, "", 1);
  }

  tmpString = workAlarm->formatAlarmTime();
  if (tmpString != lastTime || repaint) {
    lastTime = tmpString;
    drawTextString(tmpString, 3, yOff + 14, FSS9, 80, TL_DATUM, TFT_WHITE, TFT_BLACK);
  }
  tmpString = workAlarm->formatAlarmDays();
  if (tmpString != lastDays || repaint) {
    lastDays = tmpString;
    drawTextString(tmpString, 84, yOff + 14, FSS9, 165, TL_DATUM, TFT_WHITE, TFT_BLACK);
  }

  if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
    workAlarm->button.drawButton();
    xSemaphoreGive(tftMutex);
  }

  if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
    workAlarm->dawnButton.drawButton(false);
    xSemaphoreGive(tftMutex);
  }

  if (workAlarm->isActive()) {
    alarmIcon = "/alarmicon/alarm_red_sm.bmp";
  }
  else {
    alarmIcon = "/alarmicon/alarm_sm.bmp";
  }

  alarmIcon.toCharArray(iconChar, 31);
  if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
    drawBmp(iconChar, 293, yOff + 12);
    xSemaphoreGive(tftMutex);
  }
  else {
    Serial.print("drawAlarmDisplayElem: Unable to draw Alarm status to display. Unable to obtain Mutex");
  }

  if (workAlarm->isSunriseActive()) {
    lightIcon = "/lightIcon/sunrise_on.bmp";
  }
  else {
    lightIcon = "/lightIcon/sunrise_off.bmp";
  }

  lightIcon.toCharArray(iconChar, 31);
  if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
    drawBmp(iconChar, 250, yOff + 7);
    xSemaphoreGive(tftMutex);
  }
  else {
    Serial.print("drawAlarmDisplayElem: Unable to draw Sunrise status to display. Unable to obtain Mutex");
  }
}

//===================================================================
//================== Draw Alarm SET Display =========================
//===================================================================
void drawAlarmSetDisplay(bool repaint, int alarmNumber) {

  static String lastAlarmTime[3];

  const uint8_t button_width = 50;
  const uint8_t button_height = 35;

  static int8_t lastHour;
  static int8_t lastMinute;
  String tmpMsg;

  char * shortDow[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};


  // set up the pointer to the alarm we are working on
  alarmData *workAlarm;
  if (alarmNumber == 1) {
    workAlarm = &alarm1;
  }
  else if (alarmNumber == 2) {
    workAlarm = &alarm2;
  }
  else if (alarmNumber == 3) {
    workAlarm = &alarm3;
  }

  if (repaint) {
    drawTextString("Set Alarm #" + String(alarmNumber), tft.width() / 2, 90, FSS9, 200, TC_DATUM, TFT_WHITE, TFT_BLACK);

    hoursUp.initButton(&tft, 27, 115, button_width, button_height, TFT_WHITE, TFT_LIGHTGREY, TFT_BLACK, "+", 1);
    hoursDown.initButton(&tft, 27, 160, button_width, button_height, TFT_WHITE, TFT_LIGHTGREY, TFT_BLACK, "-", 1);
    minUp.initButton(&tft, 293, 115, button_width, button_height, TFT_WHITE, TFT_LIGHTGREY, TFT_BLACK, "+", 1);
    minDown.initButton(&tft, 293, 160, button_width, button_height, TFT_WHITE, TFT_LIGHTGREY, TFT_BLACK, "-", 1);

    for (int i = 0; i < 7; i++) {
      daysButton[i].initButton(&tft, (i * 47) + 19, 210, 35, 40, TFT_WHITE, TFT_RED, TFT_DARKGREY, shortDow[i] , 1);
    }
  }

  tmpMsg = workAlarm->formatAlarmTime();
  if (lastAlarmTime[alarmNumber - 1] != tmpMsg || repaint) {
    drawTextString(tmpMsg, tft.width() / 2, 152, FSSB24, 216, C_BASELINE, TFT_WHITE, TFT_BLACK);
    lastAlarmTime[alarmNumber - 1] = tmpMsg;

    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      tft.setFreeFont(FSSB12);
      hoursUp.drawButton();
      hoursDown.drawButton();
      xSemaphoreGive(tftMutex);
    }

    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      tft.setFreeFont(FSSB12);
      minUp.drawButton();
      minDown.drawButton();
      xSemaphoreGive(tftMutex);
    }
  }

  bool *days = workAlarm->getDaysArray();
  for (int i = 0; i < 7; i++) {
    if (days[i]) {
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        tft.setFreeFont(FSS9);
        daysButton[i].drawButton();
        xSemaphoreGive(tftMutex);
      }
    }
    else {
      if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
        tft.setFreeFont(FSS9);
        daysButton[i].drawButton(true);
        xSemaphoreGive(tftMutex);
      }
    }
  }
}

//===================================================================
//==================== Draw Light SET Display =======================
//===================================================================
void drawLightSetDisplay(bool repaint) {

  HsbColor curColor;
  static HsbColor lastRoomColor;
  static HsbColor lastReadColor;
  static HsbColor lastNightColor;
  HsbColor lastColor;
  static uint8_t lastSubMode = 99;

  const uint8_t button_width = 56;
  const uint8_t button_height = 35;

  if (disp.getLightSubMode() == READ_LIGHT_SUB_MODE) {
    lastColor = lastReadColor;
    curColor = ledMaster.getReadLightColorHSB();
  }
  else if (disp.getLightSubMode() == ROOM_LIGHT_SUB_MODE) {
    lastColor = lastRoomColor;
    curColor = ledMaster.getRoomLightColorHSB();
  }
  else if (disp.getLightSubMode() == NIGHT_LIGHT_SUB_MODE) {
    lastColor = lastNightColor;
    curColor = ledMaster.getNightLightColorHSB();
  }

  if (repaint || disp.getLightSubMode() != lastSubMode) {
    if (disp.getLightSubMode() == READ_LIGHT_SUB_MODE) {
      drawTextString("Set Reading Light", 120, 90, FSS9, 230, TC_DATUM, TFT_WHITE, TFT_BLACK);
    }
    else if (disp.getLightSubMode() == ROOM_LIGHT_SUB_MODE) {
      drawTextString("Set Room Light", 120, 90, FSS9, 230, TC_DATUM, TFT_WHITE, TFT_BLACK);
    }
    else if (disp.getLightSubMode() == NIGHT_LIGHT_SUB_MODE) {
      drawTextString("Set Night Light", 120, 90, FSS9, 230, TC_DATUM, TFT_WHITE, TFT_BLACK);
    }
  }

  if (repaint) {
    // tft.setFreeFont(FSS9);
    bUpButton.initButton(&tft, 40, 135, button_width, button_height, TFT_WHITE, TFT_LIGHTGREY, TFT_BLACK, "B+", 1);
    bDownButton.initButton(&tft, 40, 220, button_width, button_height, TFT_WHITE, TFT_LIGHTGREY, TFT_BLACK, "B-", 1);
    hUpButton.initButton(&tft, 120, 135, button_width, button_height, TFT_WHITE, TFT_LIGHTGREY, TFT_BLACK, "H+", 1);
    hDownButton.initButton(&tft, 120, 220, button_width, button_height, TFT_WHITE, TFT_LIGHTGREY, TFT_BLACK, "H-", 1);
    sUpButton.initButton(&tft, 200, 135, button_width, button_height, TFT_WHITE, TFT_LIGHTGREY, TFT_BLACK, "S+", 1);
    sDownButton.initButton(&tft, 200, 220, button_width, button_height, TFT_WHITE, TFT_LIGHTGREY, TFT_BLACK, "S-", 1);

    tft.drawFastHLine(0, 110, 240, TFT_WHITE);
    tft.drawFastVLine(80, 110, tft.height(), TFT_WHITE);
    tft.drawFastVLine(160, 110, tft.height(), TFT_WHITE);
    tft.drawFastVLine(240, 87, tft.height(), TFT_WHITE);

    roomSubModeButton.initButton(&tft, 280, 115, 66, 40, TFT_WHITE, TFT_BLACK, TFT_WHITE, "Room", 1);
    readSubModeButton.initButton(&tft, 280, 163, 66, 40, TFT_WHITE, TFT_BLACK, TFT_WHITE, "Read", 1);
    nightSubModeButton.initButton(&tft, 280, 212, 66, 40, TFT_WHITE, TFT_BLACK, TFT_WHITE, "Night", 1);
  }

  if (curColor.H != lastColor.H || repaint) {
    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      tft.setFreeFont(FSSB9);
      hUpButton.drawButton();
      hDownButton.drawButton();
      xSemaphoreGive(tftMutex);

      drawTextString(String((int)(curColor.H * 100)), 120, 177, FSSB12, 75, MC_DATUM, TFT_WHITE, TFT_BLACK);
    }
  }

  if (curColor.S != lastColor.S || curColor.S == 0 || curColor.S == 1.0 || repaint) {
    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      tft.setFreeFont(FSSB9);
      sUpButton.drawButton();
      sDownButton.drawButton();
      xSemaphoreGive(tftMutex);

      drawTextString(String((int)(curColor.S * 100)), 200, 177, FSSB12, 75, MC_DATUM, TFT_WHITE, TFT_BLACK);
    }
  }

  if (curColor.B != lastColor.B || curColor.B == 0.1 || curColor.B == 1.0 || repaint) {
    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      tft.setFreeFont(FSSB9);
      bUpButton.drawButton();
      bDownButton.drawButton();
      xSemaphoreGive(tftMutex);

      drawTextString(String((int)(curColor.B * 100)), 40, 177, FSSB12, 75, MC_DATUM, TFT_WHITE, TFT_BLACK);
    }
  }
  lastColor = curColor;

  if (repaint || disp.getLightSubMode() != lastSubMode) {
    if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
      tft.setFreeFont(FSSB9);
      roomSubModeButton.drawButton(disp.getLightSubMode() == ROOM_LIGHT_SUB_MODE);
      readSubModeButton.drawButton(disp.getLightSubMode() == READ_LIGHT_SUB_MODE);
      nightSubModeButton.drawButton(disp.getLightSubMode() == NIGHT_LIGHT_SUB_MODE);
      xSemaphoreGive(tftMutex);
    }
  }
}

//===================================================================
//================ Draw WiFi Status =================================
//===================================================================
void drawWiFiStatus(bool state) {
  if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
    tft.drawCircle (10, 10, 8, TFT_WHITE);
    tft.drawCircle (10, 10, 9, TFT_WHITE);
    if (state == false) {
      tft.fillCircle (10, 10, 7, TFT_RED);
    }
    else {
      tft.fillCircle (10, 10, 7, TFT_GREEN);
    }
    xSemaphoreGive(tftMutex);
  }
  else {
    Serial.println("drawWiFiStatus: Unable to draw WiFi indicator. Try again next time.");
  }
}

//===================================================================
//================ Draw Text String =================================
//===================================================================
void drawTextString(String msg, uint16_t x, uint16_t y) {

  if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
    tft.drawString(msg, x, y, GFXFF);
    xSemaphoreGive(tftMutex);
  }
  else {
    Serial.println("drawTextString: Unable to get mutex to write message: " + msg);
  }
}

void drawTextString(String msg, uint16_t x, uint16_t y, const GFXfont * font, uint16_t padding, uint8_t alignment, uint32_t color, uint32_t bg) {

  if (xSemaphoreTake(tftMutex, (TickType_t) 50) == pdTRUE ) {
    tft.setFreeFont(font);
    tft.setTextDatum(alignment);
    tft.setTextColor(color, bg);
    tft.setTextPadding(padding);
    tft.drawString(msg, x, y);
    xSemaphoreGive(tftMutex);
  }
  else {
    Serial.println("drawTextString: Unable to get mutex to write message: " + msg);
  }
}

//===================================================================
//================ Draw Weather Icon ================================
//===================================================================
void drawWeatherIcon(String weatherIcon, int x, int y, bool big)
{
  char iconChar[50];
  if (big) {
    weatherIcon = ("/icon/" + weatherIcon + ".bmp");
  }
  else {
    weatherIcon = ("/icon50/" + weatherIcon + ".bmp");
  }
  weatherIcon.toCharArray(iconChar, 50);
  drawBmp(iconChar, x, y);
  //Serial.println("Drawing weather icon: " + weatherIcon);
}

//===================================================================
//================ Clear the Working Area ===========================
//===================================================================
void clearWorkingArea() {
  if (xSemaphoreTake(tftMutex, (TickType_t) 75) == pdTRUE ) {
    tft.fillRect(0, HORIZ_DIV_POS + 2, tft.width(), (tft.height() - (HORIZ_DIV_POS + 2)), TFT_BLACK); // Clear the lower part of the screen
    xSemaphoreGive(tftMutex);
  }
  else {
    Serial.println("clearWorkingArea: Unable to clear the working area");
  }
}

//===================================================================
//================ Text Formatting  =================================
//===================================================================
String formatWindString(float windSpeed, float windGust , float windBearing) {
  String compass;
  String msg;

  if (windSpeed < 0.5 && windGust < 0.5) {
    return "No wind";
  }

  if (windBearing >= 337.5 || windBearing <= 22.5) {
    compass = "N";
  }
  else if (windBearing > 22.5 || windBearing < 67.5) {
    compass = "NE";
  }
  else if (windBearing >= 67.5 || windBearing <= 112.5) {
    compass = "E";
  }
  else if (windBearing > 112.5 || windBearing < 157.5) {
    compass = "SE";
  }
  else if (windBearing >= 157.5 || windBearing <= 202.5) {
    compass = "S";
  }
  else if (windBearing > 202.5 || windBearing < 247.5) {
    compass = "SW";
  }
  else if (windBearing >= 247.5 || windBearing <= 292.5) {
    compass = "W";
  }
  else if (windBearing > 292.5 || windBearing < 337.5) {
    compass = "NW";
  }
  else {
    Serial.println("formatWindString: Invalid Wind direction!: " + String(windBearing));
    compass = "ERROR";
  }
  msg = "Wind from " + compass + " @ " + String(windSpeed) + "mph";

  if ((windGust - windSpeed) > 2.0) {
    msg = msg + " gusting " + String(windGust) + "mph";
  }

  return msg;
}

String formatPrecipString(int type, int prob, float intensity) {
  String output;
  const float epsilon = 0.001; // a small number


  if (prob == 0 || intensity < epsilon) {
    output = "No Precipitation";
    return output;
  }

  switch (type)
  {
    case 1: output = "Rain "; break;
    case 2: output = "Sleet "; break;
    case 3: output = "Snow "; break;
    default: output = "Precipitation "; break;
  }

  if (output == "Precipitation ") {
    output = output + prob + "%";
    return output;
  }

  if (prob <= 10) {
    output = output + "very unlikely";
  }
  else if (prob > 10 && prob <= 25) {
    output = output + "unlikely";
  }
  else if (prob > 25 && prob <= 75) {
    output = output + "possible";
  }
  else if (prob > 75 && prob <= 90) {
    output = output + "likely";
  }
  else {
    output = output + "expected";
  }

  return output;
}

String assembleDateStr(time_t ts) {
  String dateString = getDayOfWeek(weekday(ts)) + " ";
  dateString = dateString + getMonthOfYear(month(ts)) + " " + String(day(ts));
  dateString = dateString + " " + String(year(ts));
  return dateString;
}

String assembleTimeStr(time_t ts) {
  String hours = String(hourFormat12(ts));
  if (minute(ts) < 10)
  {
    hours = hours + ":0" + String(minute(ts));
  }
  else
  {
    hours = hours + ":" + String(minute(ts));
  }

  if (isAM()) {
    hours = hours + " AM";
  }
  else {
    hours = hours + " PM";
  }
  return hours;
}

String getDayOfWeek(int i)
{
  switch (i)
  {
    case 1: return "Sunday"; break;
    case 2: return "Monday"; break;
    case 3: return "Tuesday"; break;
    case 4: return "Wedsday"; break;
    case 5: return "Thursday"; break;
    case 6: return "Friday"; break;
    case 7: return "Saturday"; break;
    default: return "---"; break;
  }
}

String getMonthOfYear(int i)
{
  switch (i)
  {
    case 1: return "Jan."; break;
    case 2: return "Feb."; break;
    case 3: return "Mar."; break;
    case 4: return "Apr."; break;
    case 5: return "May."; break;
    case 6: return "Jun."; break;
    case 7: return "Jul."; break;
    case 8: return "Aug."; break;
    case 9: return "Sep."; break;
    case 10: return "Oct."; break;
    case 11: return "Nov."; break;
    case 12: return "Dec."; break;
    default: return "---"; break;
  }
}

const char* wl_status_to_string(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD: return "WL_NO_SHIELD";
    case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
    case WL_CONNECTED: return "WL_CONNECTED";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED: return "WL_DISCONNECTED";
  }
}

//***************************************************************************************
//**                          Get the icon file name from the index number
//***************************************************************************************/
const char* getMeteoconIcon(uint8_t index)
{
  if (index > MAX_ICON_INDEX) index = 0;
  return dsw.iconName(index);
}
