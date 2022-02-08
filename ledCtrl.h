// This file defines the ledCtrl class

#include "globalInclude.h"

#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>

#include <Preferences.h>
extern Preferences prefs;

extern SemaphoreHandle_t ledMutex;

const uint8_t PixelPin = 13;  // make sure to set this to the correct pin, ignored for Esp8266

// A small number
const float epi = 0.00001;

// For Demo use
#ifdef DEMO_MODE
const uint16_t PixelCount = 10; // make sure to set this to the number of pixels in your strip
#define READ_LIGHT_START_INDEX 0
#define READ_LIGHT_STOP_INDEX 4
#define ROOM_LIGHT_START_INDEX 5
#define ROOM_LIGHT_STOP_INDEX 9
#else
const uint16_t PixelCount = 156; // make sure to set this to the number of pixels in your strip
#define READ_LIGHT_START_INDEX 0
#define READ_LIGHT_STOP_INDEX 65
#define ROOM_LIGHT_START_INDEX 66
#define ROOM_LIGHT_STOP_INDEX 155
#endif

NeoPixelBus<NeoGrbwFeature, NeoSk6812Method> strip(PixelCount, PixelPin);
class ledCtrl {

  private:
    bool roomLightState = false;
    bool sunriseLightState = false;
    bool readLightState = false;
    bool nightLightState = false;
    char varName[7];

    bool flashState = false;

    // what the user set it to
    HsbColor readColor;
    HsbColor roomColor;
    HsbColor nightColor;

    // What the last value sent to the strip was
    HsbColor lastReadColor;
    HsbColor lastRoomColor;
    HsbColor lastNightColor;

    // What the rest of the program said it should be now
    HsbColor activeReadColor;
    HsbColor activeRoomColor;

    HsbColor blackColor;

    NeoGamma<NeoGammaTableMethod> colorGamma;

  public:

    void ledInit ( void ) {
      // read the saved values from EEPOM
      strcpy(varName, "RoomH");
      float roomH =  prefs.getFloat(varName, 0.1);
      strcpy(varName, "RoomS");
      float roomS =  prefs.getFloat(varName, 0.5);
      strcpy(varName, "RoomB");
      float roomB =  prefs.getFloat(varName, 1.0);
      strcpy(varName, "ReadH");
      float readH =  prefs.getFloat(varName, 0.0);
      strcpy(varName, "ReadS");
      float readS =  prefs.getFloat(varName, 0.0);
      strcpy(varName, "ReadB");
      float readB =  prefs.getFloat(varName, 0.5);
      strcpy(varName, "NightH");
      float nightH =  prefs.getFloat(varName, 0.1);
      strcpy(varName, "NightS");
      float nightS =  prefs.getFloat(varName, 0.5);
      strcpy(varName, "NightB");
      float nightB =  prefs.getFloat(varName, 1.0);

      roomColor = HsbColor(roomH, roomS, roomB);
      readColor = HsbColor(readH, readS, readB);
      nightColor = HsbColor(nightH, nightS, nightB);
      lastReadColor = HsbColor(0, 0, 0);
      lastRoomColor = HsbColor(0, 0, 0);
      lastNightColor = HsbColor(0, 0, 0);
      blackColor = HsbColor(0, 0, 0);
      activeReadColor = blackColor;
      activeRoomColor = blackColor;
    }

    void Show( void ) {
      strip.Show();
    }

    void Dirty ( void ) {
      strip.Dirty();
    }

    bool IsDirty( void ) {
      return strip.IsDirty();
    }

    uint16_t getPixelCount ( void ) {
      return PixelCount;
    }

    bool CanShow () {
      return strip.CanShow();
    }

    void startFlashing( void ) {
      flashState = true;
    }

    void stopFlashing( void ) {
      flashState = false;
    }

    void toggleFlashing( void ) {
      if (flashState == true )  flashState = false;
      else flashState = true;
    }

    void saveLedData() {
      float tmpVar;

      if (xSemaphoreTake(ledMutex, (TickType_t) 100) == pdTRUE ) {
        tmpVar = prefs.getFloat("RoomH", -1.0);
        if (tmpVar != roomColor.H) prefs.putFloat("RoomH", roomColor.H);
        tmpVar = prefs.getFloat("RoomS", -1.0);
        if (tmpVar != roomColor.S) prefs.putFloat("RoomS", roomColor.S);
        tmpVar = prefs.getFloat("RoomB", -1.0);
        if (tmpVar != roomColor.B) prefs.putFloat("RoomB", roomColor.B);

        tmpVar = prefs.getFloat("ReadH", -1.0);
        if (tmpVar != readColor.H) prefs.putFloat("ReadH", readColor.H);
        tmpVar = prefs.getFloat("ReadS", -1.0);
        if (tmpVar != readColor.S) prefs.putFloat("ReadS", readColor.S);
        tmpVar = prefs.getFloat("ReadB", -1.0);
        if (tmpVar != readColor.B) prefs.putFloat("ReadB", readColor.B);

        tmpVar = prefs.getFloat("NightH", -1.0);
        if (tmpVar != nightColor.H) prefs.putFloat("NightH", nightColor.H);
        tmpVar = prefs.getFloat("NightS", -1.0);
        if (tmpVar != nightColor.S) prefs.putFloat("NightS", nightColor.S);
        tmpVar = prefs.getFloat("NightB", -1.0);
        if (tmpVar != nightColor.B) prefs.putFloat("NightB", nightColor.B);
        xSemaphoreGive(ledMutex);
      }
      else {
        Serial.println("ledCtrl.saveLedData: Unable to get the ledMutex. Data NOT WRITTEN TO FLASH!");
      }
    }

    // ================================
    // ===== Sunrise Light Code =======
    // ================================
    void sunriseLightOn() {
      sunriseLightState = true;
    }

    void sunriseLightOff() {
      sunriseLightState = false;
    }

    // ================================
    // ===== Room Light Code ==========
    // ================================
    void roomLightOn() {
      nightLightState = false;
      sunriseLightState = false;
      roomLightState = true;
      activeRoomColor = roomColor;
    }
    void roomLightOff() {
      roomLightState = false;
      activeRoomColor = blackColor;
    }

    void roomLightToggle() {
      if (roomLightState == false) {
        nightLightState = false;
        sunriseLightState = false;
        roomLightState = true;
        activeRoomColor = roomColor;

      }
      else {
        roomLightState = false;
        activeRoomColor = blackColor;
      }
    }
    bool getRoomLightState() {
      return roomLightState;
    }

    void setRoomLightColorHSB (float h, float s, float b) {
      if (h >= (1.0 - epi)) h -= 1.0;
      else if (h < (0.0 + epi) ) h += 1.0;

      if (s > 1.0) s = 1.0;
      else if (s < 0.0) s = 0.0;

      if (b > 1.0) b = 1.0;
      else if (b < 0.1) b = 0.1;

      // round to nearest 0.5
      h = round(h * 20) / 20;
      s = round(s * 20) / 20;
      b = round(b * 20) / 20;

      if (h == 1.0) {
        h = 0.0;
      }

      roomColor = HsbColor(h, s, b);
      if (roomLightState) lightRoomLight(roomColor);
    }

    void setActiveRoomLightColorHSB(float h, float s, float b) {
      if (h >= (1.0 - epi)) h -= 1.0;
      else if (h < (0.0 + epi) ) h += 1.0;

      if (s > 1.0) s = 1.0;
      else if (s < 0.0) s = 0.0;

      if (b > 1.0) b = 1.0;
      else if (b < 0.0) b = 0.0;

      // round to nearest 0.5
      h = round(h * 20) / 20;
      s = round(s * 20) / 20;
      b = round(b * 20) / 20;

      if (h == 1.0) {
        h = 0.0;
      }

      activeRoomColor = HsbColor(h, s, b);
      if (roomLightState) {
        lightRoomLight(activeRoomColor);
      }
    }

    void setActiveSunriseRoomLightColorHSB(float h, float s, float b) {
      activeRoomColor = HsbColor(h, s, b);
      if (sunriseLightState) {
        lightRoomLight(activeRoomColor);
      }
    }

    HsbColor getRoomLightColorHSB ( void ) {
      return roomColor;
    }

    // ================================
    // === Reading Light Code =========
    // ================================
    void readLightOn() {
      readLightState = true;
      activeReadColor = readColor;
    }

    void readLightOff() {
      readLightState = false;
      activeReadColor = blackColor;
    }

    void readLightToggle() {
      if (readLightState == false) {
        readLightState = true;
        activeReadColor = readColor;
      }
      else {
        readLightState = false;
        activeReadColor = blackColor;
      }
    }
    bool getReadLightState() {
      return readLightState;
    }

    void setReadLightColorHSB (float h, float s, float b) {
      if (h >= (1.0 - epi)) h -= 1.0;
      else if (h < (0.0 + epi) ) h += 1.0;

      if (s > 1.0) s = 1.0;
      else if (s < 0.0) s = 0.0;

      if (b > 1.0) b = 1.0;
      else if (b < 0.1) b = 0.1;

      // round to nearest 0.5
      h = round(h * 20) / 20;
      s = round(s * 20) / 20;
      b = round(b * 20) / 20;

      if (h == 1.0) {
        h = 0.0;
      }

      readColor = HsbColor(h, s, b);
    }

    void setActiveReadLightColorHSB(float h, float s, float b) {
      if (h >= (1.0 - epi)) h -= 1.0;
      else if (h < (0.0 + epi) ) h += 1.0;

      if (s > 1.0) s = 1.0;
      else if (s < 0.0) s = 0.0;

      if (b > 1.0) b = 1.0;
      else if (b < 0.0) b = 0.0;

      // round to nearest 0.5
      h = round(h * 20) / 20;
      s = round(s * 20) / 20;
      b = round(b * 20) / 20;

      if (h == 1.0) {
        h = 0.0;
      }

      activeReadColor = HsbColor(h, s, b);
    }

    HsbColor getReadLightColorHSB ( void ) {
      return readColor;
    }

    // ================================
    // ===== Night Light Code =========
    // ================================
    void nightLightOn() {
      nightLightState = true;
      roomLightState = false;
      activeRoomColor = nightColor;
    }
    void nightLightOff() {
      nightLightState = false;
      activeRoomColor = blackColor;
    }

    void nightLightToggle() {
      if (nightLightState == false) {
        nightLightState = true;
        roomLightState = false;
        activeRoomColor = nightColor;
      }
      else {
        nightLightState = false;
        activeRoomColor = blackColor;
      }
    }
    bool getNightLightState() {
      return nightLightState;
    }

    void setNightLightColorHSB (float h, float s, float b) {
      if (h >= (1.0 - epi)) h -= 1.0;
      else if (h < (0.0 + epi) ) h += 1.0;

      if (s > 1.0) s = 1.0;
      else if (s < 0.0) s = 0.0;

      if (b > 1.0) b = 1.0;
      else if (b < 0.1) b = 0.1;

      // round to nearest 0.5
      h = round(h * 20) / 20;
      s = round(s * 20) / 20;
      b = round(b * 20) / 20;

      if (h == 1.0) {
        h = 0.0;
      }

      nightColor = HsbColor(h, s, b);
      if (nightLightState) lightRoomLight(nightColor);
    }

    void setActiveNightLightColorHSB(float h, float s, float b) {
      if (h >= (1.0 - epi)) h -= 1.0;
      else if (h < (0.0 + epi) ) h += 1.0;

      if (s > 1.0) s = 1.0;
      else if (s < 0.0) s = 0.0;

      if (b > 1.0) b = 1.0;
      else if (b < 0.0) b = 0.0;

      // round to nearest 0.5
      h = round(h * 20) / 20;
      s = round(s * 20) / 20;
      b = round(b * 20) / 20;

      if (h == 1.0) {
        h = 0.0;
      }

      activeRoomColor = HsbColor(h, s, b);
      if (nightLightState) lightRoomLight(activeRoomColor);
    }


    HsbColor getNightLightColorHSB ( void ) {
      return nightColor;
    }

    // ================================
    // ===== Strip Setting Code =======
    // ================================

    void updateStrip ( void ) { // call this regularly!

      static bool flashOn = false;
      const uint16_t flashTriggerTime = 250;
      static unsigned long lastFlashTime  = 0;

      if (flashState && readLightState == false) { // if we are supposed to be flasheing the ReadLight and the readLight is not supposed to be constantly on

        unsigned long timeNow = millis();
        if (timeNow - lastFlashTime > flashTriggerTime) {
          if (flashOn) {
            lastReadColor = HsbColor(0, 0, 0.0f);
            flashOn = false;
            lastFlashTime = timeNow;
          }
          else {
            lastReadColor = HsbColor(0, 0, 1.0f);
            flashOn = true;
            lastFlashTime = timeNow;
          }
          lightReadLight(lastReadColor);
        }
      }
      else lightReadLight(activeReadColor);

      lightRoomLight (activeRoomColor);

    }


    void lightReadLight (HsbColor inColor) {

      static RgbwColor lastGammaColor = RgbwColor(0, 0, 0, 0);

      HsbColor filtColor = HsbColor::LinearBlend<NeoHueBlendShortestDistance>(lastReadColor, inColor, 0.45f);

      lastReadColor = filtColor;

      RgbwColor gammaColor = colorGamma.Correct(RgbwColor(filtColor));

      if (gammaColor != lastGammaColor) {
        if (xSemaphoreTake(ledMutex, (TickType_t) 10) == pdTRUE ) {
          strip.ClearTo(gammaColor, READ_LIGHT_START_INDEX, READ_LIGHT_STOP_INDEX);
          xSemaphoreGive(ledMutex);
          lastGammaColor = gammaColor;
        }
      }
    }

    void lightRoomLight (HsbColor inColor) {

      static RgbwColor lastGammaColor = RgbwColor(0, 0, 0, 0);

      HsbColor filtColor = HsbColor::LinearBlend<NeoHueBlendShortestDistance>(lastRoomColor, inColor, 0.45f);

      lastRoomColor = filtColor;

      RgbwColor gammaColor = colorGamma.Correct(RgbwColor(filtColor));

      if (gammaColor != lastGammaColor) {
        if (xSemaphoreTake(ledMutex, (TickType_t) 10) == pdTRUE ) {
          strip.ClearTo(gammaColor, ROOM_LIGHT_START_INDEX, ROOM_LIGHT_STOP_INDEX);
          xSemaphoreGive(ledMutex);
          lastGammaColor = gammaColor;
        }
      }
    }
};
