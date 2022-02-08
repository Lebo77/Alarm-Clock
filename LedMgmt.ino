// ledManager file

#include "globalInclude.h"

#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>

void ledMgr( void * parameter) {

#define LED_CTRL_LOOP_FREQUENCY 15

  TickType_t xLastWakeTime;

  bool lastReadState;
  bool lastRoomState;
  bool lastNightState;
  bool lastSunriseActive1;
  bool lastSunriseActive2;
  bool lastSunriseActive3;
  uint16_t loopcount = 0;

  const float sunriseTime = 900; // seconds to start sunrise time
  const HsbColor blackColor(0, 0, 0);

  const HsbColor sunriseStartColor = HsbColor(0.0f, 0.9f, 0.07f);
  const HsbColor sunriseEndColor = HsbColor(0.1f, 0.6f, 0.6f);

  vTaskDelay(500 / portTICK_PERIOD_MS);// let other tasks get going
  Serial.println("ledMgr: ledMgr running...");

  ledMaster.ledInit();

  if ( esp_task_wdt_add(NULL) != ESP_OK) { // add task to WDT
    Serial.println("dispMgr: Unable to add displayMgr to taskWDT!");
  }

  //setup section
  strip.Begin();
  strip.Show();

  int loopPeriod = round(1000 / LED_CTRL_LOOP_FREQUENCY); // run the master display loop at 15Hz
  const TickType_t xFrequency = loopPeriod / portTICK_PERIOD_MS;
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    loopcount++;
    if (loopcount >= 1000) {
      loopcount = 0;
    }

    // main loop
    if (loopcount % LED_CTRL_LOOP_FREQUENCY == 0 ) { // check the alarms once a second
      time_t tn = now();
      if (alarm1.isSunriseActive() && ledMaster.getRoomLightState() == false) {
        lastSunriseActive1 = true;
        uint32_t timeRemain = alarm1.secondsToAlarm(tn);
        if (timeRemain <= sunriseTime) {
          //Serial.println("ledMgr: timeRemain: " + String(timeRemain));
          float progress = (sunriseTime - (float)timeRemain) / sunriseTime;
          //Serial.println("ledMgr: progress: " + String(progress));
          HsbColor sunriseColor = HsbColor::LinearBlend<NeoHueBlendShortestDistance>(sunriseStartColor, sunriseEndColor, progress);
          ledMaster.setActiveSunriseRoomLightColorHSB(sunriseColor.H, sunriseColor.S, sunriseColor.B);
          ledMaster.sunriseLightOn();
        }
      }
      else if (alarm2.isSunriseActive() && ledMaster.getRoomLightState() == false) {
        lastSunriseActive2 = true;
        uint32_t timeRemain = alarm2.secondsToAlarm(tn);
        if (timeRemain <= sunriseTime) {
          float progress = (sunriseTime - (float)timeRemain) / sunriseTime;
          HsbColor sunriseColor = HsbColor::LinearBlend<NeoHueBlendShortestDistance>(sunriseStartColor, sunriseEndColor, progress);
          ledMaster.setActiveSunriseRoomLightColorHSB(sunriseColor.H, sunriseColor.S, sunriseColor.B);
          ledMaster.sunriseLightOn();
        }
      }
      else if (alarm3.isSunriseActive() && ledMaster.getRoomLightState() == false) {
        lastSunriseActive3 = true;
        uint32_t timeRemain = alarm3.secondsToAlarm(tn);
        if (timeRemain <= sunriseTime) {
          float progress = (sunriseTime - (float)timeRemain) / sunriseTime;
          HsbColor sunriseColor = HsbColor::LinearBlend<NeoHueBlendShortestDistance>(sunriseStartColor, sunriseEndColor, progress);
          ledMaster.setActiveSunriseRoomLightColorHSB(sunriseColor.H, sunriseColor.S, sunriseColor.B);
          ledMaster.sunriseLightOn();
        }
      }

      // handle if someone turns off the sunrise alarm function
      if (alarm1.isSunriseActive() == false && lastSunriseActive1 == true && ledMaster.getRoomLightState() == false) {
        lastSunriseActive1 = false;
        ledMaster.setActiveRoomLightColorHSB(blackColor.H, blackColor.S, blackColor.B);
        ledMaster.sunriseLightOff();
      }
      if (alarm2.isSunriseActive() == false && lastSunriseActive2 == true && ledMaster.getRoomLightState() == false) {
        lastSunriseActive2 = false;
        ledMaster.setActiveRoomLightColorHSB(blackColor.H, blackColor.S, blackColor.B);
        ledMaster.sunriseLightOff();
      }
      if (alarm3.isSunriseActive() == false && lastSunriseActive3 == true && ledMaster.getRoomLightState() == false) {
        lastSunriseActive3 = false;
        ledMaster.setActiveRoomLightColorHSB(blackColor.H, blackColor.S, blackColor.B);
        ledMaster.sunriseLightOff();
      }
    }

    if (loopcount % (10 * LED_CTRL_LOOP_FREQUENCY) == 0 ) { // send a message once every 10 sec. just to be safe
      ledMaster.Dirty();
      vTaskDelay(2 / portTICK_PERIOD_MS); // This is just to feed the IDLE watchdog
    }

    if (esp_task_wdt_reset() != ESP_OK) {
      Serial.println("Unable to reset ledMgr taskWDT!");
    }

    ledMaster.updateStrip();

    vTaskDelayUntil( &xLastWakeTime, xFrequency );
  }
}

// This task actually updates the led strip
void ledDriver( void * parameter) {

  TickType_t xLastWakeTime;

  uint16_t loopcount = 0;

  vTaskDelay(600 / portTICK_PERIOD_MS);// let other tasks get going
  Serial.println("ledDriver: ledMgr running...");

  if ( esp_task_wdt_add(NULL) != ESP_OK) { // add task to WDT
    Serial.println("dispMgr: Unable to add displayMgr to taskWDT!");
  }

  const TickType_t xFrequency = 33 / portTICK_PERIOD_MS; // run the master display loop at about 30Hz
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    if (ledMaster.CanShow() && ledMaster.IsDirty()) {
      if (xSemaphoreTake(ledMutex, (TickType_t) 10) == pdTRUE ) {
        disp.setSpriteEnable(false);
        taskENTER_CRITICAL(&criticalMutex);
        ledMaster.Show();
        taskEXIT_CRITICAL(&criticalMutex);
        xSemaphoreGive(ledMutex);
        disp.setSpriteEnable(true);
      }
    }
    if (esp_task_wdt_reset() != ESP_OK) {
      Serial.println("Unable to reset ledMgr taskWDT!");
    }
    vTaskDelayUntil( &xLastWakeTime, xFrequency );
  }
}
