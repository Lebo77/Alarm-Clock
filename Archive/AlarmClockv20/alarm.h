// This file contains the main class that handles alarms

#include "globalInclude.h"

// defines for the Days byte field
#define SUNDAY    (1 << 0) /* 00000001 */
#define MONDAY    (1 << 1) /* 00000010 */
#define TUESDAY   (1 << 2) /* 00000100 */
#define WENDSDAY  (1 << 3) /* 00001000 */
#define THURSDAY  (1 << 4) /* 00010000 */
#define FRIDAY    (1 << 5) /* 00100000 */
#define SATURDAY  (1 << 6) /* 01000000 */
#define ACTIVE    (1 << 7) /* 10000000 */

#ifndef DEMO_MODE
#define SNOOZE_TIME 5 // snooze for 5 minutes per cycle
#else 
#define SNOOZE_TIME 1 // snooze for 5 minutes per cycle
#endif

#include <Preferences.h>
extern Preferences prefs;

extern SemaphoreHandle_t ledMutex;

// alarmData - A class to hold the state of an alarm
class alarmData {

  private:
    int8_t alarmHour; // 1-24
    int8_t alarmMinute; // 0-59
    uint8_t alarmDays; // bitfield for days and active state
    bool sunriseActive;

    int8_t snoozeHour;
    int8_t snoozeMinute;
    // bool snoozed;
    uint8_t snoozecount = 0;
    String alarmID;

  public:

    TFT_eSPI_Button button;
    TFT_eSPI_Button dawnButton;

    int8_t getAlarmHour() {
      return alarmHour;
    }

    int8_t getAlarmMinute() {
      return alarmMinute;
    }

    void initAlarm(String id) {
      alarmID = id;
      char varName[15];
      char tmpID[10];
      alarmID.toCharArray(tmpID, 10);
      strcpy(varName, tmpID);
      strcat(varName, "Hour");
      alarmHour = prefs.getChar(varName, 12);
      snoozeHour  = alarmHour;
      strcpy(varName, tmpID);
      strcat(varName, "Min");
      alarmMinute = prefs.getChar(varName, 0);
      snoozeMinute = alarmMinute;
      strcpy(varName, tmpID);
      strcat(varName, "Days");
      alarmDays = prefs.getUChar(varName, 127);
      snoozecount = 0;
      strcpy(varName, tmpID);
      strcat(varName, "Sunrise");
      sunriseActive = prefs.getBool(varName, false);
    }

    String getAlarmID (void) {
      return alarmID;
    }

    void saveAlarmData() {
      int8_t tmpAlarmHour; // 1-24
      int8_t tmpAlarmMinute; // 0-59
      uint8_t tmpAlarmDays; // bitfield for days and active state
      bool tmpSunriseActive;
      char varName[15];
      char tmpID[10];
      alarmID.toCharArray(tmpID, 10);

      if (xSemaphoreTake(ledMutex, (TickType_t) 1000) == pdTRUE ) {
        strcpy(varName, tmpID);
        strcat(varName, "Hour");
        tmpAlarmHour = prefs.getChar(varName, 15);
        if (tmpAlarmHour != alarmHour) {
          prefs.putChar(varName, alarmHour);
        }
        strcpy(varName, tmpID);
        strcat(varName, "Min");
        tmpAlarmMinute = prefs.getChar(varName, 100);
        if (tmpAlarmMinute != alarmMinute) {
          prefs.putChar(varName, alarmMinute);
        }
        strcpy(varName, tmpID);
        strcat(varName, "Days");
        tmpAlarmDays = prefs.getUChar(varName, NULL);
        if (tmpAlarmDays != alarmDays) {
          prefs.putUChar(varName, alarmDays);
        }
        strcpy(varName, tmpID);
        strcat(varName, "Sunrise");
        tmpSunriseActive = prefs.getUChar(varName, NULL);
        if (tmpSunriseActive != sunriseActive) {
          prefs.putBool(varName, sunriseActive);
        }
        xSemaphoreGive(ledMutex);
      }
      else {
        Serial.println("alarmData.saveAlarmData: Unable to get the ledMutex. Data NOT WRITTEN TO FLASH!");
      }
    }

    void setAlarmTime (time_t ts) {
      alarmHour = hour(ts);
      alarmMinute = minute(ts);
    }

    void hoursMod(int modification) {
      if (abs(modification) > 12) {
        return; // don't allow more then 12 hour change.
      }
      alarmHour += modification;

      if (alarmHour > 24) {
        alarmHour -= 24;
      }
      else if (alarmHour < 1) {
        alarmHour += 24;
      }
      snoozeHour = alarmHour;

    }

    void minuteMod(int modification) {
      if (abs(modification) > 30) {
        return; // don't allow more then 30 minute change.
      }
      alarmMinute += modification;

      if (alarmMinute > 59) {
        alarmMinute -= 60;
      }
      else if (alarmMinute < 0) {
        alarmMinute += 60;
      }
      snoozeMinute = alarmMinute;
    }

    void setAlarmDays (bool days[], uint8_t num_elements) { // takes an array of 7 bool values, Sunday-Saturday
      int i;

      if (num_elements != 7) {
        Serial.println("alarmData.setAlarmDays: ERROR! The array must contain 7 values! Number of elements: " + String(num_elements) );
        Serial.println("alarmData.setAlarmDays: Size of days: " + String(sizeof(days)) + " sizeof first element: " + String(sizeof(days[0])));
        return;
      }

      for (i = 0; i < 7; i++) {
        if (days[i]) {
          alarmDays = alarmDays | (1 << i);
        }
      }
    }

    bool* getDaysArray() {

      bool* days = new bool[7];

      for (int i = 0; i < 7; i++) {
        if (alarmDays & (1 << i)) {
          days[i] = true;
        }
        else {
          days[i] = false;
        }
      }
      return days;
    }

    void activate() {
      alarmDays = alarmDays | ACTIVE;
    }

    void deactivate() {
      alarmDays = alarmDays & ~ACTIVE;
      resetSnooze(); // just in case this happens while snoozed
    }

    bool isActive() {
      if (alarmDays & ACTIVE) {
        return true;
      }
      else {
        return false;
      }
    }

    bool alarmTest(time_t ts) { // is it time to triger the alarm?
      if ( (alarmDays & ACTIVE) && (hour(ts) == snoozeHour) && (minute(ts) == snoozeMinute) ) {
        if (alarmDays & (1 << (weekday(ts) - 1) ) || snoozecount ) {
          return true;
        }
      }
      return false;
    }

    uint32_t secondsToAlarm ( time_t ts ) { // only returns valid information for alarms in the next 2 days
      int16_t inMinutes = minute(ts);
      int16_t inHour = hour(ts);
      int16_t inSeconds = second(ts);
      uint8_t inDay = weekday(ts) - 1;
      int16_t tmpSnoozeHour = snoozeHour;

      // Is the alarm set for today and does it happen in the future?
      if (alarmDays & (1 << inDay)) { // yes
        if ((tmpSnoozeHour > inHour) || ((tmpSnoozeHour == inHour) && (snoozeMinute > inMinutes))) { // The alarm is in the future
          int32_t alarmSecs = (tmpSnoozeHour * 3600) + (snoozeMinute * 60);
          int32_t calcInSeconds = (inHour * 3600) + (inMinutes * 60) + inSeconds;
          //Serial.println("secondsToAlarm: time remaining: " + String(alarmSecs - calcInSeconds));
          //Serial.println(" ");
          return alarmSecs - calcInSeconds;
        }
      }
      uint8_t tomorrow = inDay + 1;
      if (tomorrow == 7) tomorrow = 0; // tomorrow may be a Sunday

      if ((alarmDays & (1 << tomorrow))) { // tomorrow has an alarm
        int32_t alarmSecs = ((tmpSnoozeHour + 24) * 3600) + (snoozeMinute * 60);
        int32_t calcInSeconds = (inHour * 3600) + (inMinutes * 60) + inSeconds;
        //Serial.println("secondsToAlarm: time remaining: " + String(alarmSecs - calcInSeconds));
        //Serial.println(" ");
        return alarmSecs - calcInSeconds;
      }

      return 1000000;
    }

    bool toggleStatus() {
      if (isActive()) {
        deactivate();
        return false;
      }
      else {
        activate();
        return true;
      }
    }

    // snooze functions
    void snooze (void) {
      snoozeMinute += SNOOZE_TIME;
      snoozecount++;

      if (snoozeMinute > 59) {
        snoozeHour++;
        snoozeMinute -= 60;
        if (snoozeHour > 24) {
          snoozeHour -= 24;
        }
      }
    }

    uint8_t isSnoozed (void) {
      return snoozecount;
    }

    void resetSnooze ( void ) {
      snoozeHour = alarmHour;
      snoozeMinute = alarmMinute;
      snoozecount = 0;
    }

    String formatAlarmTime() {
      String formatMinutes;
      if (alarmMinute < 10) {
        formatMinutes = "0" + String(alarmMinute);
      }
      else {
        formatMinutes = String(alarmMinute);
      }

      if (alarmHour == 24) { // midnight
        return "12:" + formatMinutes + " AM";
      }
      if (alarmHour == 12) { // noon
        return "12:" + formatMinutes + " PM";
      }
      else if (alarmHour > 12) {
        return String(alarmHour - 12) + ":" + formatMinutes + " PM";
      }
      else {
        return String(alarmHour) + ":" + formatMinutes + " AM";
      }
    }

    String formatAlarmDays() {
      String rtnVal;

      if (alarmDays & SUNDAY) {
        rtnVal = "Su";
      }
      else {
        rtnVal = "__";
      }

      if (alarmDays & MONDAY) {
        rtnVal = rtnVal + "Mo";
      }
      else {
        rtnVal = rtnVal + "__";
      }

      if (alarmDays & TUESDAY) {
        rtnVal = rtnVal + "Tu";
      }
      else {
        rtnVal = rtnVal + "__";
      }

      if (alarmDays & WENDSDAY) {
        rtnVal = rtnVal + "We";
      }
      else {
        rtnVal = rtnVal + "__";
      }

      if (alarmDays & THURSDAY) {
        rtnVal = rtnVal + "Th";
      }
      else {
        rtnVal = rtnVal + "__";
      }

      if (alarmDays & FRIDAY) {
        rtnVal = rtnVal + "Fr";
      }
      else {
        rtnVal = rtnVal + "__";
      }

      if (alarmDays & SATURDAY) {
        rtnVal = rtnVal + "Sa";
      }
      else {
        rtnVal = rtnVal + "__";
      }

      return rtnVal;
    }

    void toggleDay(uint8_t flipDay) {
      uint8_t key = (1 << flipDay);
      if (alarmDays & key ) {
        alarmDays = alarmDays & ~key;
      }
      else {
        alarmDays = alarmDays | key;
      }
    }

    void setSunrise( bool val ) {
      sunriseActive = val;
    }

    void toggleSunrise( void ) {
      if (sunriseActive) sunriseActive = false;
      else sunriseActive = true;
    }

    bool isSunriseActive ( void) {
      return sunriseActive;
    }
};
