// This file hans the code that executes to scroll the sprites

#include "globalInclude.h"

#include <TFT_eSPI.h>
//==========================================================================================

void spriteMgr( void * parameter) {

  vTaskDelay(2000 / portTICK_PERIOD_MS); // let the other task get going

  if ( esp_task_wdt_add(NULL) != ESP_OK) { // add task to WDT
    Serial.println("Unable to add spriteMgr to taskWDT!");
  }

  TickType_t xLastWakeTime;
  const TickType_t xFrequency = 40 / portTICK_PERIOD_MS;
  xLastWakeTime = xTaskGetTickCount();

  // Initialise the xLastWakeTime variable with the current time.
  xLastWakeTime = xTaskGetTickCount();

  for ( ;; )
  {
    if (disp.getSpriteEnable()) {

      sprite1.drawSprite();
      sprite2.drawSprite();

    }
    if (esp_task_wdt_reset() != ESP_OK) {
      Serial.println("Unable to reset spriteMgr taskWDT!");
    }
    vTaskDelayUntil( &xLastWakeTime, xFrequency );    // Wait for the next cycle.
  }
}
