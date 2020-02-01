// This file handles the touchscreen and changing the system mode

#include "globalInclude.h"

// =============================================
// ================ Defines ====================
// =============================================
#define TOUCH_IRQ_PIN   4

// =============================================
// ================ Globals ====================
// =============================================
SemaphoreHandle_t touchSemaphore = NULL; // handle for the touch semaphore
extern const float epi;
// =============================================
// ================ touchISR ===================
// =============================================
void IRAM_ATTR touchISR() {
  // Unblock the task by releasing the semaphore.
  // Serial.println("ISR triggered!");
  xSemaphoreGiveFromISR( touchSemaphore, nullptr );
}

// =============================================
// ================ ModeMgr ====================
// =============================================
void modeMgr( void * parameter) {

  touchSemaphore = xSemaphoreCreateBinary();

  if ( esp_task_wdt_add(NULL) != ESP_OK) { // add task to WDT
    Serial.println("modeMgr: Unable to add alarmMgr to taskWDT!");
  }

  int touchIRQ = 1;
  int lastTouchIRQ = 1;
  unsigned long lastIRQTime = 0;
  unsigned long IRQtime;
  bool validTouch = false;
  uint8_t roomLightButtonCount = 0;
  uint8_t readLightButtonCount = 0;
  uint8_t nightLightButtonCount = 0;

  uint16_t x = 0, y = 0; // To store the touch coordinates

  // Set the calibration of the touchscreen
  uint16_t calData[5] = { 367, 3511, 197, 3648, 1 };
  tft.setTouch(calData);

  vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay start to give systems time to come up

  pinMode(TOUCH_IRQ_PIN, INPUT_PULLUP);
  attachInterrupt(TOUCH_IRQ_PIN, touchISR, FALLING);

  for (;;) {
    if ( xSemaphoreTake( touchSemaphore, 200 / portTICK_PERIOD_MS ) == pdTRUE ) // bail out to reset the WDT every 1/5 sec.
    {
      IRQtime = millis();
      if (xSemaphoreTake(tftMutex, (TickType_t) 130) == pdTRUE ) {
        validTouch = tft.getTouch(&x, &y);
        xSemaphoreGive(tftMutex);
      }
      else {
        Serial.println("modeMgr: unable to get tftMutex!");
      }

      if (IRQtime - lastIRQTime > 150 && validTouch) { // debounce and check that the tft driver agrees the touch was real
        lastIRQTime = IRQtime;
        //Serial.println("modeMgr: Got touch. X: " + String(x) + "   Y: " + String(y));

        disp.resetlastTouch(); // reset the blacklight dimming

        if (disp.getCurrentMode() == MAIN_MODE) {
          touchMainMode(x, y);
        }
        else if (disp.getCurrentMode() == CURRENT_WEATHER_MODE) {
          touchCurWeatherMode(x, y);
        }
        else if (disp.getCurrentMode() == FORECAST_MODE) {
          touchForecastMode(x, y);
        }
        else if (disp.getCurrentMode() == ALARM_DISPLAY_MODE) {
          touchAlarmDispMode(x, y);
        }
        else if (disp.getCurrentMode() == ALARM_SET_MODE) {
          touchAlarmSetMode(x, y);
        }
        else if (disp.getCurrentMode() == GEN_LIGHT_CTRL_MODE) {
          touchLightMode(x, y);
        }
        else {
          Serial.println("modeMgr: We are in a mode I do not recognize!");
        }
        //Serial.println("modeMgr: Now in mode " + String(currentMode));
      }
    }
    else {
      if ( digitalRead(TOUCH_IRQ_PIN) == LOW && disp.getScreenTouchActive() ) {
        if (xSemaphoreTake(tftMutex, (TickType_t) 130) == pdTRUE ) {
          validTouch = tft.getTouch(&x, &y);
          xSemaphoreGive(tftMutex);
        }
        else {
          Serial.println("modeMgr: unable to get tftMutex!");
        }

        if (readLightButton.contains(x, y)) {
          readLightButtonCount++;
        }
        else {
          readLightButtonCount = 0;
        }

        if (roomLightButton.contains(x, y)) {
          roomLightButtonCount++;
        }
        else {
          roomLightButtonCount = 0;
        }

        if (nightLightButton.contains(x, y)) {
          nightLightButtonCount++;
        }
        else {
          nightLightButtonCount = 0;
        }
      }
      else if ( disp.getScreenTouchActive() ) { // Cleanup
        readLightButtonCount = 0;
        roomLightButtonCount = 0;
        nightLightButtonCount = 0;
        disp.setScreenTouchActive(false);
      }

      if ( readLightButtonCount >= 4 ) {
        readLightButtonCount = 0;
        disp.setCurrentMode(GEN_LIGHT_CTRL_MODE);
        disp.setLightSubMode(READ_LIGHT_SUB_MODE);
        disp.setScreenTouchActive(false);
      }

      if ( roomLightButtonCount >= 4 ) {
        roomLightButtonCount = 0;
        disp.setCurrentMode(GEN_LIGHT_CTRL_MODE);
        disp.setLightSubMode(ROOM_LIGHT_SUB_MODE);
        disp.setScreenTouchActive(false);
      }

      if ( nightLightButtonCount >= 4 ) {
        nightLightButtonCount = 0;
        disp.setCurrentMode(GEN_LIGHT_CTRL_MODE);
        disp.setLightSubMode(NIGHT_LIGHT_SUB_MODE);
        disp.setScreenTouchActive(false);
      }
    }

    if (esp_task_wdt_reset() != ESP_OK) {
      Serial.println("modeMgr: Unable to reset modeMgr taskWDT!");
    }
  }
}

// =============================================
// ============== Main Mode ====================
// =============================================
void touchMainMode(uint16_t x, uint16_t y) {
  if (y < HORIZ_DIV_POS) {

    if (readLightButton.contains(x, y)) {
      ledMaster.readLightToggle();
      drawReadingLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (roomLightButton.contains(x, y)) {
      ledMaster.roomLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (nightLightButton.contains(x, y)) {
      ledMaster.nightLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else {
      disp.setCurrentMode(ALARM_DISPLAY_MODE);
    }
  }
  else if (x < VERT_DIV_POS) {
    disp.setCurrentMode(CURRENT_WEATHER_MODE);
  }
  else {
    disp.setCurrentMode(FORECAST_MODE);
  }
}

// =============================================
// ========== Cur. Weather Mode ================
// =============================================
void touchCurWeatherMode(uint16_t x, uint16_t y) {
  if (y < HORIZ_DIV_POS) {
    if (readLightButton.contains(x, y)) {
      ledMaster.readLightToggle();
      drawReadingLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (roomLightButton.contains(x, y)) {
      ledMaster.roomLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (nightLightButton.contains(x, y)) {
      ledMaster.nightLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else {
      disp.setCurrentMode(MAIN_MODE);
    }
  }
  else {
    disp.setCurrentMode(MAIN_MODE);
  }
}

// =============================================
// ============== Forecast Mode ================
// =============================================
void touchForecastMode(uint16_t x, uint16_t y) {
  if (y < HORIZ_DIV_POS) {
    if (readLightButton.contains(x, y)) {
      ledMaster.readLightToggle();
      drawReadingLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (roomLightButton.contains(x, y)) {
      ledMaster.roomLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (nightLightButton.contains(x, y)) {
      ledMaster.nightLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else {
      disp.setCurrentMode(MAIN_MODE);
    }
  }
  else {
    disp.setCurrentMode(MAIN_MODE);
  }
}

// =============================================
// =========== Alarm Disp. Mode ================
// =============================================
void touchAlarmDispMode(uint16_t x, uint16_t y) {
  if (y < HORIZ_DIV_POS) {

    if (readLightButton.contains(x, y)) {
      ledMaster.readLightToggle();
      drawReadingLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (roomLightButton.contains(x, y)) {
      ledMaster.roomLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (nightLightButton.contains(x, y)) {
      ledMaster.nightLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else {
      alarm1.saveAlarmData();
      alarm2.saveAlarmData();
      alarm3.saveAlarmData();
      disp.setCurrentMode(MAIN_MODE);
    }
  }
  else if (y < 153) {
    if (alarm1.button.contains(x, y)) {
      alarm1.toggleStatus();
      alarm1.saveAlarmData();
      disp.setDrawLowerScreen(true);
    }
    else if (alarm1.dawnButton.contains(x, y)) {
      alarm1.toggleSunrise();
      alarm1.saveAlarmData();
      disp.setDrawLowerScreen(true);
    }
    else {
      disp.setAlarmEdit(1);
      disp.setCurrentMode(ALARM_SET_MODE);
    }
  }
  else if (y < 197) {
    if (alarm2.button.contains(x, y)) {
      alarm2.toggleStatus();
      alarm2.saveAlarmData();
      disp.setDrawLowerScreen(true);
    }
    else if (alarm2.dawnButton.contains(x, y)) {
      alarm2.toggleSunrise();
      alarm2.saveAlarmData();
      disp.setDrawLowerScreen(true);
    }
    else {
      disp.setAlarmEdit(2);
      disp.setCurrentMode(ALARM_SET_MODE);
    }
  }
  else {
    if (alarm3.button.contains(x, y)) {
      alarm3.toggleStatus();
      alarm3.saveAlarmData();
      disp.setDrawLowerScreen(true);
    }
    else if (alarm3.dawnButton.contains(x, y)) {
      alarm3.toggleSunrise();
      alarm3.saveAlarmData();
      disp.setDrawLowerScreen(true);
    }
    else {
      disp.setAlarmEdit(3);
      disp.setCurrentMode(ALARM_SET_MODE);
    }
  }
}

// =============================================
// ============= Alarm Set Mode ================
// =============================================
void touchAlarmSetMode(uint16_t x, uint16_t y) {
  alarmData *workAlarm;
  if (disp.getAlarmEdit() == 1) {
    workAlarm = &alarm1;
  }
  else if (disp.getAlarmEdit() == 2) {
    workAlarm = &alarm2;
  }
  else if (disp.getAlarmEdit() == 3) {
    workAlarm = &alarm3;
  }

  if (y < HORIZ_DIV_POS) {
    if (readLightButton.contains(x, y)) {
      ledMaster.readLightToggle();
      drawReadingLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (roomLightButton.contains(x, y)) {
      ledMaster.roomLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (nightLightButton.contains(x, y)) {
      ledMaster.nightLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else {
      workAlarm->saveAlarmData(); // Save the data to NVS
      disp.setCurrentMode(ALARM_DISPLAY_MODE);
    }
  }
  else if (hoursUp.contains(x, y)) {
    workAlarm->hoursMod(1);
    if (xSemaphoreTake(tftMutex, (TickType_t) 60) == pdTRUE ) {
      tft.setFreeFont(FSSB12);
      hoursUp.drawButton(true);
      xSemaphoreGive(tftMutex);
    }
    disp.setDrawLowerScreen(true);
  }
  else if (hoursDown.contains(x, y)) {
    workAlarm->hoursMod(-1);
    if (xSemaphoreTake(tftMutex, (TickType_t) 60) == pdTRUE ) {
      tft.setFreeFont(FSSB12);
      hoursDown.drawButton(true);
      xSemaphoreGive(tftMutex);
    }
    disp.setDrawLowerScreen(true);
  }
  else if (minUp.contains(x, y)) {
    workAlarm->minuteMod(1);
    if (xSemaphoreTake(tftMutex, (TickType_t) 60) == pdTRUE ) {
      tft.setFreeFont(FSSB12);
      minUp.drawButton(true);
      xSemaphoreGive(tftMutex);
    }
    disp.setDrawLowerScreen(true);
  }
  else if (minDown.contains(x, y)) {
    workAlarm->minuteMod(-1);
    if (xSemaphoreTake(tftMutex, (TickType_t) 60) == pdTRUE ) {
      tft.setFreeFont(FSSB12);
      minDown.drawButton(true);
      xSemaphoreGive(tftMutex);
    }
    disp.setDrawLowerScreen(true);
  }
  else {
    for (int i = 0; i < 7; i++) {
      if (daysButton[i].contains(x, y)) {
        workAlarm->toggleDay(i);
        disp.setDrawLowerScreen(true);
      }
    }
  }
}

// =============================================
// ========== General Light Mode ===============
// =============================================
void touchLightMode(uint16_t x, uint16_t y) {

  static HsbColor lastReadColor;
  static HsbColor lastRoomColor;
  static HsbColor lastNightColor;


  //set up variables and function pointers
  HsbColor *lastColor;
  bool lightState;
  HsbColor (ledCtrl::*getHsb) ( void );
  void (ledCtrl::*setHsb) ( float, float, float );
  void (ledCtrl::*setActiveHsb) ( float, float, float );
  if (disp.getLightSubMode() == READ_LIGHT_SUB_MODE) {
    lastColor = &lastReadColor;
    lightState = ledMaster.getReadLightState();
    getHsb = &ledCtrl::getReadLightColorHSB;
    setHsb = &ledCtrl::setReadLightColorHSB;
    setActiveHsb = &ledCtrl::setActiveReadLightColorHSB;
  }
  else if (disp.getLightSubMode() == ROOM_LIGHT_SUB_MODE) {
    lastColor = &lastRoomColor;
    lightState = ledMaster.getRoomLightState();
    getHsb = &ledCtrl::getRoomLightColorHSB;
    setHsb = &ledCtrl::setRoomLightColorHSB;
    setActiveHsb = &ledCtrl::setActiveRoomLightColorHSB;
  }
  else if (disp.getLightSubMode() == NIGHT_LIGHT_SUB_MODE) {
    lastColor = &lastNightColor;
    lightState = ledMaster.getNightLightState();
    getHsb = &ledCtrl::getNightLightColorHSB;
    setHsb = &ledCtrl::setNightLightColorHSB;
    setActiveHsb = &ledCtrl::setActiveNightLightColorHSB;
  }

  HsbColor curColor = (ledMaster.*getHsb)();

  if (y < HORIZ_DIV_POS) {
    if (readLightButton.contains(x, y)) {
      ledMaster.readLightToggle();
      drawReadingLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (roomLightButton.contains(x, y)) {
      ledMaster.roomLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else if (nightLightButton.contains(x, y)) {
      ledMaster.nightLightToggle();
      drawRoomLightButton(false);
      drawNightLightButton(false);
      disp.setScreenTouchActive(true);
    }
    else {
      disp.setCurrentMode(MAIN_MODE);
      ledMaster.saveLedData();
    }
  }
  else {
    if (bUpButton.contains(x, y)) {
      (ledMaster.*setHsb)(curColor.H, curColor.S, curColor.B + 0.05);
      if (lightState) {
        HsbColor tmpColor = (ledMaster.*getHsb)();
        (ledMaster.*setActiveHsb) (tmpColor.H, tmpColor.S, tmpColor.B);
      }
      if (xSemaphoreTake(tftMutex, (TickType_t) 60) == pdTRUE ) {
        tft.setFreeFont(FSSB9);
        if (curColor.B != 1.0) bUpButton.drawButton(true);
        else bUpButton.drawButton(false);
        xSemaphoreGive(tftMutex);
        disp.setDrawLowerScreen(true);
      }
    }
    else if (bDownButton.contains(x, y)) {
      (ledMaster.*setHsb)(curColor.H, curColor.S, curColor.B - 0.05);
      if (lightState) {
        HsbColor tmpColor = (ledMaster.*getHsb)();
        (ledMaster.*setActiveHsb) (tmpColor.H, tmpColor.S, tmpColor.B);
      }
      if (xSemaphoreTake(tftMutex, (TickType_t) 60) == pdTRUE ) {
        tft.setFreeFont(FSSB9);
        if (abs(curColor.B - epi) > 0.1) bDownButton.drawButton(true);
        else bDownButton.drawButton(false);
        xSemaphoreGive(tftMutex);
        disp.setDrawLowerScreen(true);
      }
    }
    if (hUpButton.contains(x, y)) {
      (ledMaster.*setHsb)(curColor.H + 0.05, curColor.S, curColor.B);
      if (lightState) {
        HsbColor tmpColor = (ledMaster.*getHsb)();
        (ledMaster.*setActiveHsb)(tmpColor.H, tmpColor.S, tmpColor.B);
      }
      if (xSemaphoreTake(tftMutex, (TickType_t) 60) == pdTRUE ) {
        tft.setFreeFont(FSSB9);
        hUpButton.drawButton(true);
        xSemaphoreGive(tftMutex);
        disp.setDrawLowerScreen(true);
      }
    }
    else if (hDownButton.contains(x, y)) {
      (ledMaster.*setHsb)(curColor.H - 0.05, curColor.S, curColor.B);
      if (lightState) {
        HsbColor tmpColor = (ledMaster.*getHsb)();
        (ledMaster.*setActiveHsb)(tmpColor.H, tmpColor.S, tmpColor.B);
      }
      tft.setFreeFont(FSSB9);
      if (xSemaphoreTake(tftMutex, (TickType_t) 60) == pdTRUE ) {
        hDownButton.drawButton(true);
        xSemaphoreGive(tftMutex);
        disp.setDrawLowerScreen(true);
      }
    }
    if (sUpButton.contains(x, y)) {
      (ledMaster.*setHsb)(curColor.H, curColor.S + 0.10, curColor.B);
      if (lightState) {
        HsbColor tmpColor = (ledMaster.*getHsb)();
        (ledMaster.*setActiveHsb)(tmpColor.H, tmpColor.S, tmpColor.B);
      }
      if (xSemaphoreTake(tftMutex, (TickType_t) 60) == pdTRUE ) {
        tft.setFreeFont(FSSB9);
        if (curColor.S != 1.0) sUpButton.drawButton(true);
        else sUpButton.drawButton(false);
        xSemaphoreGive(tftMutex);
        disp.setDrawLowerScreen(true);
      }
    }
    else if (sDownButton.contains(x, y)) {
      (ledMaster.*setHsb)(curColor.H, curColor.S - 0.10, curColor.B);
      if (lightState) {
        HsbColor tmpColor = (ledMaster.*getHsb)();
        (ledMaster.*setActiveHsb)(tmpColor.H, tmpColor.S, tmpColor.B);
      }
      if (xSemaphoreTake(tftMutex, (TickType_t) 60) == pdTRUE ) {
        tft.setFreeFont(FSSB9);
        if (curColor.S != 0.0) sDownButton.drawButton(true);
        else sDownButton.drawButton(false);
        xSemaphoreGive(tftMutex);
        disp.setDrawLowerScreen(true);
      }
    }
    else if ( roomSubModeButton.contains(x, y) ) {
      disp.setCurrentMode(GEN_LIGHT_CTRL_MODE);
      disp.setLightSubMode(ROOM_LIGHT_SUB_MODE);
      disp.setDrawLowerScreen(true);
    }
    else if ( readSubModeButton.contains(x, y) ) {
      disp.setCurrentMode(GEN_LIGHT_CTRL_MODE);
      disp.setLightSubMode(READ_LIGHT_SUB_MODE);
      disp.setDrawLowerScreen(true);
    }
    else if ( nightSubModeButton.contains(x, y) ) {
      disp.setCurrentMode(GEN_LIGHT_CTRL_MODE);
      disp.setLightSubMode(NIGHT_LIGHT_SUB_MODE);
      disp.setDrawLowerScreen(true);
    }

  }
  *lastColor = curColor;
}
