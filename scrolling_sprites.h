// This contains the class for scrolling sprites

#include "globalInclude.h"

#include <TFT_eSPI.h>
#include "TFT_eSPI.h"
#include "Free_Fonts.h"

// Set up Pins - Only need DC and CS for hardware SPI
#define TFT_DC 16
#define TFT_CS 17

// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC
TFT_eSPI tft = TFT_eSPI();       // Invoke custom library
SemaphoreHandle_t tftMutex;

// The rollingSprite class
class rollingSprite
{

  private:
    unsigned int c_sprHeight;
    unsigned int c_sprWidth;
    String c_spriteMsg;
    int c_scrollGap;
    int c_scrollSpeed;
    int c_txtWidth;
    int c_spriteACount;
    int c_spriteBCount;
    bool c_spriteValid = false;
    uint16_t c_x;
    uint16_t c_y;
    uint32_t c_txtColor;

    SemaphoreHandle_t spriteMutex;

    TFT_eSprite spr = TFT_eSprite(&tft); // Sprite object

  public:

    bool newSprite(String spriteMsg, unsigned int sprHeight, unsigned int sprWidth, int scrollSpeed, int scrollGap, uint32_t color, uint16_t x, uint16_t y) {
      if (spriteMutex == NULL) {
        spriteMutex = xSemaphoreCreateMutex();
      }

      if (xSemaphoreTake(spriteMutex, (TickType_t) 30) == pdTRUE ) {
        // set up the class vairables
        c_spriteMsg = spriteMsg;
        c_sprHeight = sprHeight;
        c_sprWidth = sprWidth;
        c_scrollSpeed = scrollSpeed;
        c_scrollGap = scrollGap;
        c_txtColor = color;
        c_x = x;
        c_y = y;

        c_spriteValid = false;
        spr.setColorDepth(8);
        spr.setFreeFont(FSS9);

        spr.createSprite(c_sprWidth, c_sprHeight);
        spr.fillSprite(TFT_BLACK);
        spr.setTextColor(c_txtColor);
        spr.setTextWrap(false);
        xSemaphoreGive(spriteMutex);
      }
      else {
        Serial.println("newSprite: Unable to update sprite!");
      }

      if (xSemaphoreTake(spriteMutex, (TickType_t) 30) == pdTRUE ) {
        c_txtWidth = spr.textWidth(c_spriteMsg, GFXFF);
        c_spriteACount = 0;
        c_spriteBCount = c_scrollGap + c_txtWidth;
        c_spriteValid = true;
        xSemaphoreGive(spriteMutex);
        return true;
      }
      else {
        Serial.println("newSprite: Unable to update sprite!");
      }
      return false;
    }

    bool delSprite() {
      if (spriteMutex != NULL || c_spriteValid == true) {
        if (xSemaphoreTake(spriteMutex, (TickType_t) 50) == pdTRUE ) {
          c_spriteValid = false;
          c_x = 0;
          c_y = 0;
          c_spriteACount = 0;
          c_spriteBCount = 0;
          spr.deleteSprite();
          c_spriteMsg = " ";
          xSemaphoreGive(spriteMutex);
        }
        else {
          Serial.print("delSprite: Unable to delete sprite!!!");
          return false;
        }
      }
      return true;
    }

    bool drawSprite() {
      if (c_spriteValid == true) {

        if (xSemaphoreTake(spriteMutex, (TickType_t) 5) == pdTRUE ) {
          c_spriteACount -= c_scrollSpeed;
          c_spriteBCount -= c_scrollSpeed;
          spr.fillSprite(TFT_BLACK);
          spr.setTextColor(c_txtColor);
          spr.drawString(c_spriteMsg, c_spriteACount, 0, GFXFF);
          spr.drawString(c_spriteMsg, c_spriteBCount, 0, GFXFF);
          xSemaphoreGive(spriteMutex);
        }
        else {
          Serial.println("drawSprite: Unable to update sprite!");
        }

        if (xSemaphoreTake(spriteMutex, (TickType_t) 5) == pdTRUE ) {
          if (c_spriteACount < (-1 * (c_txtWidth))) {
            c_spriteACount += ((c_txtWidth + c_scrollGap) * 2);
          }
          if (c_spriteBCount < (-1 * (c_txtWidth))) {
            c_spriteBCount += ((c_txtWidth + c_scrollGap) * 2);
          }
          xSemaphoreGive(spriteMutex);
        }
        else {
          Serial.println("drawSprite: Unable to reset sprite counts!");
        }

        if (xSemaphoreTake(tftMutex, (TickType_t) 40 / portTICK_PERIOD_MS) == pdTRUE ) {
          spr.pushSprite(c_x, c_y);
          xSemaphoreGive(tftMutex);
        }
        else {
          Serial.println("drawSprite: Unable to push sprite!");
          return false;
        }
      }
      return true;
    }

    bool isValid() {
      return c_spriteValid;
    }

    String currMsg() {
      return c_spriteMsg;
    }
};
