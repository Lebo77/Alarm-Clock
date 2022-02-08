// This file manages the Alarms for the clock.

#include "globalInclude.h"

#include "AudioFileSourceSPIFFS.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"

// #define ALARM_OFF_BUTTON 35
#define ALARM_OFF_BUTTON 39
#define VOLUME_POT 36

#define LRCLK_PIN 26
#define BCLK_PIN 25
#define DOUT_PIN 33
#define AUDIO_SHUTDOWN_PIN 32

#define MAX_ALARM_TIME 3600

//Time in ms to wat before shutting off the alarm vs. snoozing
#define ALARM_BUTTON_TIME 1750

// =============================================
// ================ Globals ====================
// =============================================
AudioGeneratorWAV *wav;
AudioFileSourceSPIFFS *file;
AudioOutputI2S *out;

// create the three alarms
class alarmData;
alarmData alarm1;
alarmData alarm2;
alarmData alarm3;

SemaphoreHandle_t alarmSemaphore = NULL; // handle for the alarm semaphore

//===================================================================
//====================== Start WAV ==================================
//===================================================================
void startWAV(String filename) {
  delete file;
  char filenameChar[31];
  filename.toCharArray(filenameChar, 31);
  file = new AudioFileSourceSPIFFS(filenameChar);
  digitalWrite(AUDIO_SHUTDOWN_PIN, HIGH);
  wav->begin(file, out);
}

//===================================================================
//====================== Ring Alarm =================================
//===================================================================
void ringAlarm( uint8_t alarmNum ) {

  unsigned long buttonDownTime;

  time_t alarmStartTime = now(); // Don't play the alarm for more than an hour

  alarmData *workAlarm;
  if (alarmNum == 1) {
    workAlarm = &alarm1;
  }
  else if (alarmNum == 2) {
    workAlarm = &alarm2;
  }
  else if (alarmNum == 3) {
    workAlarm = &alarm3;
  }

  TickType_t xLastWakeTime;
  const TickType_t xFrequency = 10 / portTICK_PERIOD_MS;
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    time_t ts = now();


    // set the volume
    float snoozeAdjust = (0.15 * (float)(workAlarm->isSnoozed()));
    if (snoozeAdjust > 0.8) snoozeAdjust = 0.8;
    float volumeIn =  (((float)analogRead(VOLUME_POT) / 1900.0) ) + 0.3 + snoozeAdjust; // allow up to a 2.46 amplification, with a minimum of 0.3 + snoozeAdjust
    out->SetGain(volumeIn);

    if (xSemaphoreTake(ledMutex, (TickType_t) 5) == pdTRUE ) {
      if (ts - alarmStartTime > MAX_ALARM_TIME) { // Turn off the alarm after the maximum allowable time
        Serial.println("ringAlarm: " + workAlarm->getAlarmID() + " has timed out. Stopping alarm audio.");
        disp.setAlarmRinging(0);
        wav->stop();
        digitalWrite(AUDIO_SHUTDOWN_PIN, LOW);
        drawAlarmIndicator(false);
        return;
      }

      if (disp.getAlarmRinging()) {
        if (digitalRead(ALARM_OFF_BUTTON) == LOW) { // Stop the alarm
          Serial.println("ringAlarm: Saw Alarm Off button. Stopping alarm audio.");
          disp.setAlarmRinging(0);
          wav->stop();
          digitalWrite(AUDIO_SHUTDOWN_PIN, LOW);
          ledMaster.stopFlashing(); // stop the flashing if any
          buttonDownTime = millis();
          disp.resetlastTouch(); // backlight dimming
        }
        else if (!workAlarm->isActive()) { // someone turned off the alarm directly
          Serial.println("ringAlarm: Alarm is no longer active. Stopping audio.");
          disp.setAlarmRinging(0);
          wav->stop();
          digitalWrite(AUDIO_SHUTDOWN_PIN, LOW);
          ledMaster.stopFlashing(); // stop the flashing if any
          disp.resetlastTouch(); // backlight dimming
          xSemaphoreGive(ledMutex);
          return;
        }
        else if (wav->isRunning()) { // fill the buffer
          if (!wav->loop()) wav->stop();
        }
        else { // restart the audio
          startWAV("/alarm" + String(disp.getAlarmRinging()) + ".wav");
          //If snoozing has been going for a while, flash the lights.
          if (workAlarm->isSnoozed() >= 3 ) ledMaster.startFlashing();
        }
      }
      else { // The button has been pressed, but are we snoozed or are we turning off the alarm?
        unsigned long buttonTotalTime = millis() - buttonDownTime;
        if (digitalRead(ALARM_OFF_BUTTON) == HIGH) { // the button has been released
          if (buttonTotalTime < ALARM_BUTTON_TIME) { // less than 1.75 seconds and we are snoozed
            Serial.println ("ringAlarm: Time elapsed: " + String(buttonTotalTime) + "ms");
            Serial.println ("ringAlarm: Button released snoozing " + workAlarm->getAlarmID());
            workAlarm->snooze();
            xSemaphoreGive(ledMutex);
            ledMaster.stopFlashing(); // if flashing, stop
            return;
          }
        }
        if (millis() - buttonDownTime > ALARM_BUTTON_TIME) { // over the time seconds and we discontinue the alarm
          Serial.println ("ringAlarm: Time elapsed: " + String(buttonTotalTime) + "ms");
          Serial.println ("ringAlarm: Button released reseting " + workAlarm->getAlarmID());
          workAlarm->resetSnooze();
          drawAlarmIndicator(false);
          if (workAlarm->isSunriseActive()) { // Sunrise alarm is active
            ledMaster.roomLightOn(); // turn room light on
            disp.setDrawTimeSection(true);
          }
          xSemaphoreGive(ledMutex);
          ledMaster.stopFlashing(); // if flashing, stop
          return;
        }
      }

      if (esp_task_wdt_reset() != ESP_OK) {
        Serial.println("ringAlarm: Unable to reset alarmMgr taskWDT!");
      }
      xSemaphoreGive(ledMutex);
      vTaskDelayUntil( &xLastWakeTime, xFrequency );
    }
  }
}

//===================================================================
//================== Alarm Manager ==================================
//===================================================================
void alarmMgr( void * parameter ) {

  alarmSemaphore = xSemaphoreCreateBinary();

  pinMode(AUDIO_SHUTDOWN_PIN, OUTPUT);
  digitalWrite(AUDIO_SHUTDOWN_PIN, LOW);

  unsigned long buttonDownTime = 0;
  unsigned long buttonTotalTime;
  float volumeIn;

  // set the alarms' internal IDs
  alarm1.initAlarm("alarm1");
  alarm2.initAlarm("alarm2");
  alarm3.initAlarm("alarm3");

  if ( esp_task_wdt_add(NULL) != ESP_OK) { // add task to WDT
    Serial.println("dispMgr: Unable to add alarmMgr to taskWDT!");
  }

  pinMode(ALARM_OFF_BUTTON, INPUT);

  vTaskDelay(1000 / portTICK_PERIOD_MS);// let other tasks get going
  SPIFFS.begin();
  Serial.printf("alarmMgr: Alarm Manager running\n");

  //audioLogger = &Serial;

  wav = new AudioGeneratorWAV();
  out = new AudioOutputI2S();
  out->SetPinout(BCLK_PIN, LRCLK_PIN, DOUT_PIN);

  for (;;) {
    if ( xSemaphoreTake( alarmSemaphore, 200 ) == pdTRUE ) // reset the WDT and check buttons 5x a second
    {
      Serial.println("alarmMgr: we have an alarm to ring.");
      drawAlarmIndicator(false);
      ringAlarm(disp.getAlarmRinging());
    }

    bool buttonState = digitalRead(ALARM_OFF_BUTTON);

    if (buttonState == LOW) {
      disp.resetlastTouch(); // backlight dimming
    }

    if ((alarm1.isSnoozed() || alarm2.isSnoozed() || alarm3.isSnoozed()) && buttonState == LOW && buttonDownTime == 0) {
      buttonDownTime = millis();
      Serial.println("alarmMgr: Saw Alarm Off button.");
      //disp.resetlastTouch(); // backlight dimming
    }

    if (buttonDownTime != 0) {
      alarmData *workAlarm;
      if (alarm1.isSnoozed()) {
        workAlarm = &alarm1;
      }
      else if (alarm2.isSnoozed()) {
        workAlarm = &alarm2;
      }
      else if (alarm3.isSnoozed()) {
        workAlarm = &alarm3;
      }

      buttonTotalTime = millis() - buttonDownTime;

      if (buttonTotalTime > ALARM_BUTTON_TIME) {
        Serial.println ("alarmMgr: Time elapsed: " + String(buttonTotalTime) + "ms");
        Serial.println ("alarmMgr: Button released reseting " + workAlarm->getAlarmID());
        workAlarm->resetSnooze();
        drawAlarmIndicator(false);
        buttonDownTime = 0;
        if (workAlarm->isSunriseActive()) { // Sunrise alarm is active
          ledMaster.roomLightOn(); // turn room light on
          disp.setDrawTimeSection(true);
        }
      }
      else if (buttonState == HIGH && buttonTotalTime < ALARM_BUTTON_TIME) { // button was released early
        Serial.println ("alarmMgr: Time elapsed: " + String(buttonTotalTime) + "ms");
        Serial.println ("alarmMgr: Button released too early. No reset.");
        buttonDownTime = 0;
      }
    }

    if (esp_task_wdt_reset() != ESP_OK) {
      Serial.println("alarmMgr: Unable to reset alarmMgr taskWDT!");
    }
  }
}
