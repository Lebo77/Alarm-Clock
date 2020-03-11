// This file holds variables in a singleton
#include "globalInclude.h"

class displayMgr
{
  private:
    volatile bool currWiFiStatus = false;
    volatile bool weatherValid = false;
    volatile bool drawLowerScreen = false;
    volatile bool drawTimeSection = false;
    volatile bool fullReDraw = true;
    volatile bool spriteEnable = false;
    volatile uint8_t alarmRinging = 0;
    volatile time_t lastTouch = now();
    volatile uint16_t backlightTimeout = 30;
    volatile uint8_t currentMode = 0;
    volatile uint8_t alarmEdit = 0;
    volatile bool screenTouchActive = false;
    volatile uint8_t lightSubMode = 0;


    public:
      // get/set CurrWiFiStatus
      void setCurrWiFiStatus (bool value) {
      currWiFiStatus = value;
    }
    bool getCurrWiFiStatus (void) {
      return currWiFiStatus;
    }

    // get/set weatherValid
    void setWeatherValid (bool value) {
      weatherValid = value;
    }
    bool getWeatherValid (void) {
      return weatherValid;
    }

    // get/set drawLowerScreen
    void setDrawLowerScreen (bool value) {
      drawLowerScreen = value;
    }
    bool getDrawLowerScreen (void) {
      return drawLowerScreen;
    }

    // get/set drawLowerScreen
    void setDrawTimeSection (bool value) {
      drawTimeSection = value;
    }
    bool getDrawTimeSection (void) {
      return drawTimeSection;
    }

    // get/set drawLowerScreen
    void setFullReDraw (bool value) {
      fullReDraw = value;
    }
    bool getFullReDraw (void) {
      return fullReDraw;
    }

    // get/set spriteEnable
    void setSpriteEnable (bool value) {
      spriteEnable = value;
    }
    bool getSpriteEnable (void) {
      return spriteEnable;
    }

    // get/set alarmRinging
    void setAlarmRinging (uint8_t value) {
      alarmRinging = value;
    }
    uint8_t getAlarmRinging (void) {
      return alarmRinging;
    }

    // get/set currentMode
    void setCurrentMode (uint8_t value) {
      currentMode = value;
    }
    uint8_t getCurrentMode (void) {
      return currentMode;
    }

    // get/set alarmEdit
    void setAlarmEdit (uint8_t value) {
      alarmEdit = value;
    }
    uint8_t getAlarmEdit (void) {
      return alarmEdit;
    }

    // touchscreen alerts
    void resetlastTouch (void) {
      lastTouch = now();
    }
    void setBLTimeout (uint16_t newTimeout) {
      backlightTimeout = newTimeout;
    }

    // get/set lightSubMode
    void setLightSubMode (uint8_t val) {
      lightSubMode = val;
    }
    uint8_t getLightSubMode (void) {
      return lightSubMode;
    }

    bool checkRecentTouch(void) {
      time_t ts = now();
      if (ts - lastTouch > backlightTimeout) return false;
      else return true;
    }

    void setScreenTouchActive(bool value) {
      screenTouchActive = value;
    }
    bool getScreenTouchActive( void ) {
      return screenTouchActive;
    }

};
