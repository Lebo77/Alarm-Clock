/*
   Duncan Pickard's over-engineered Alarm Clock

   Master file
*/
#include "globalInclude.h"

// Call up the SPIFFS FLASH filing system this is part of the ESP Core
#include <FS.h>
#include "scrolling_sprites.h"

#include <TimeLib.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "SPI.h"
#include "SPIFFS.h"
#include "alarm.h"
#include <DS3232RTC.h>      // https://github.com/JChristensen/DS3232RTC
#include <JSON_Decoder.h>
#include <DarkSkyWeather.h>
#include <math.h>
#include "SPIFFS_Support.h"
#include <esp_int_wdt.h>
#include <esp_task_wdt.h>
# include "DisplayMgr.h"
extern "C" {
#include <esp_wifi.h>
}
#include <Preferences.h>
#include "ledCtrl.h"

#define SECONDS_FROM_1970_TO_2000 946684800

// ------------------------------------ Global Variables -----------------------------------------

//  For Demo use
#ifdef DEMO_MODE
const char ssid[] = "***";  //  your network SSID (name)
const char pass[] = "***";       // your network password
const String latitude =  "***"; // 90.0000 to -90.0000 negative for Southern hemisphere
const String longitude = "***"; // 180.000 to -180.000 negative for West
const String api_key = "629d9cfeab8fed85a7ebfe86a175529d"; // Obtain this from your Dark Sky account
#else
const char ssid[] = "***";  //  your network SSID (name)
const char pass[] = "***";       // your network password
const String latitude =  "***"; // 90.0000 to -90.0000 negative for Southern hemisphere
const String longitude = "***"; // 180.000 to -180.000 negative for West
const String api_key = "629d9cfeab8fed85a7ebfe86a175529d"; // Obtain this from your Dark Sky account
#endif

// NTP buffer
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

// Display management object - holds global interprocess communication variables
displayMgr disp;


// LED light strip management object - holds global interprocess communication variables
ledCtrl ledMaster;

class rollingSprite;
rollingSprite sprite1;
rollingSprite sprite2;

// Non-Volitile storage object
Preferences prefs;

const unsigned int localPort = 8888;  // local port to listen for UDP packets
WiFiUDP Udp;

DS3232RTC RTC(false);  // set up but do not start the RTC
// a mutex to lock the RTC during updates
SemaphoreHandle_t rtcMutex;

// a mutex to lock the ledStrip updates and flash during updates
SemaphoreHandle_t ledMutex;

//A MUX to protect critical code sections
portMUX_TYPE criticalMutex = portMUX_INITIALIZER_UNLOCKED;

DS_Weather dsw; // Weather forecast library instance
DSW_current *current; // Pointers to structs that temporarily holds weather data
DSW_hourly  *hourly;  // Not used
DSW_daily   *daily;
SemaphoreHandle_t dswMutex;  // a mutex to lock the dswWeather objects

//  NTP Servers
const String ntpServerName[] = {"us.pool.ntp.org", "time.nist.gov", "time-a.timefreq.bldrdoc.gov", "time-b.timefreq.bldrdoc.gov", "time-c.timefreq.bldrdoc.gov", "time.google.com"} ;

int timeZone = -5; // Leave the NTP time zone as EST for boot up

// set up task handles
TaskHandle_t timeTask;
TaskHandle_t displayTask;
TaskHandle_t spriteTask;
TaskHandle_t modeTask;
TaskHandle_t alarmTask;
TaskHandle_t ledTask;
TaskHandle_t ledDriverTask;

// external alarms
extern alarmData alarm1;
extern alarmData alarm2;
extern alarmData alarm3;

// ------------------------------------ Function prototypes -----------------------------------------
// Functions found in THIS file
void timeTmr( void * parameter);
void setDST(time_t);
bool setRTC(time_t ts);
const char* wl_status_to_string(wl_status_t status);
bool connectToWifi();
void disconnectFromWiFi();
time_t getNtpTime();
time_t getClockTime();
void sendNTPpacket(IPAddress &address);
bool isDST(time_t tn);
bool getCurrentWeather();

// Functions found in DisplayMgmt file
void dispMgr( void * parameter);
void drawWiFiStatus(bool state);
void drawTime(time_t ts, bool repaint);
void drawWeatherIcon(String weatherIcon, int x, int y, bool big);
void drawWeatherDisplay(bool);
void drawCurrentWeatherDisplay(bool);
void drawForecastWeatherDisplay(bool);
void drawAlarmDisplay(bool);
void drawTextString(String msg, uint16_t x, uint16_t y);
void clearWorkingArea();
String assembleDateStr(time_t ts);
String assembleTimeStr(time_t ts);
String formatWindString(float windSpeed, float windGust , float windBearing);
String formatPrecipString(int type, int prob, float intensity);
String getDayOfWeek(int i);
String getMonthOfYear(int i);

// Functions found in modeMgmt file
void IRAM_ATTR touchISR();
void modeMgr( void * parameter);

// Functions found in scrolling_sprites file
void spriteMgr( void * parameter);

void setup()
{
  Serial.begin(250000);

  Serial.println("*******************************************************************");
  Serial.println("*******************************************************************");
  Serial.println("***                                                             ***");
  Serial.println("***     ***    *****   ***     ***    ***  *****    **   **     ***");
  Serial.println("***     *  *   *       *  *   *   *  *   *   *      **   **     ***");
  Serial.println("***     ***    *****   ***    *   *  *   *   *      **   **     ***");
  Serial.println("***     *  *   *       *  *   *   *  *   *   *                  ***");
  Serial.println("***     *  *   *****   ***     ***    ***    *      **   **     ***");
  Serial.println("***                                                             ***");
  Serial.println("*******************************************************************");
  Serial.println("*******************************************************************");

  if (tftMutex == NULL) {
    tftMutex = xSemaphoreCreateMutex();
  }
  if (tftMutex != NULL) {
    Serial.println("Setup:tftMutex Created.");
  }
  if (dswMutex == NULL) {
    dswMutex = xSemaphoreCreateMutex();
  }
  if (dswMutex != NULL) {
    Serial.println("Setup:dswMutex Created.");
  }
  if (rtcMutex == NULL) {
    rtcMutex = xSemaphoreCreateMutex();
  }
  if (rtcMutex != NULL) {
    Serial.println("Setup:rtcMutex Created.");
  }
  if (ledMutex == NULL) {
    ledMutex = xSemaphoreCreateMutex();
  }
  if (ledMutex != NULL) {
    Serial.println("Setup:ledMutex Created.");
  }

  esp_task_wdt_init(30, true); // Task WDT set for 30 seconds and reboot

  prefs.begin("alarms", false);

  if (!SPIFFS.begin()) {
    Serial.println("Setup: ERROR! SPIFFS initialisation failed!");
  }
  else {
    Serial.println("Setup: SPIFFS initialised.");
  }

  xSemaphoreGive(rtcMutex); // give the RTC mutex something to access

  // spawn the display manager process
  xTaskCreatePinnedToCore(
    dispMgr, /* Function to implement the task */
    "dispMgr", /* Name of the task */
    10000,  /* Stack size in words */
    NULL,  /* Task input parameter */
    1,  /* Priority of the task */
    &displayTask,  /* Task handle. */
    1 /* Core */
  );

  int i2CRtn = I2C_ClearBus(true); // Reset the RTC bus w/ a 2.5 sec delay for RTC start-up
  if (i2CRtn != 0) {
    Serial.println(F("Setup: I2C bus error. Could not clear"));
    if (i2CRtn == 1) {
      Serial.println(F("Setup: SCL clock line held low"));
    } else if (i2CRtn == 2) {
      Serial.println(F("Setup: SCL clock line held low by slave clock stretch"));
    } else if (i2CRtn == 3) {
      Serial.println(F("Setup: SDA data line held low"));
    }
  } else { // bus clear
    Serial.println("Setup: I2C setup finished");
  }

  // spawn the time manager process
  xTaskCreate(
    timeMgr, /* Function to implement the task */
    "timeMgr", /* Name of the task */
    10000,  /* Stack size in words */
    NULL,  /* Task input parameter */
    1,  /* Priority of the task */
    &timeTask  /* Task handle. */
  );

  // spawn the sprite manager process
  xTaskCreate(
    spriteMgr, /* Function to implement the task */
    "spriteMgr", /* Name of the task */
    2000,  /* Stack size in words */
    NULL,  /* Task input parameter */
    3,  /* Priority of the task */
    &spriteTask);  /* Task handle. */

  // spawn the mode manager process
  xTaskCreatePinnedToCore(
    modeMgr, // Function to implement the task
    "modeMgr", // Name of the task
    4000,  // Stack size in words
    NULL,  // Task input parameter
    2,  // Priority of the task
    &modeTask, // Task handle.
    1);  // Core

  // spawn the alarm manager process
  xTaskCreatePinnedToCore(
    alarmMgr, /* Function to implement the task */
    "alarmMgr", /* Name of the task */
    4000,  /* Stack size in words */
    NULL,  /* Task input parameter */
    5,  /* Priority of the task */
    &alarmTask,  /* Task handle. */
    1 // Core
  );

  // spawn the led manager process
  xTaskCreatePinnedToCore(
    ledMgr, /* Function to implement the task */
    "ledMgr", /* Name of the task */
    2000,  /* Stack size in words */
    NULL,  /* Task input parameter */
    1,  /* Priority of the task */
    &ledTask,  /* Task handle. */
    1 // Core
  );

  // spawn the led manager process
  xTaskCreatePinnedToCore(
    ledDriver, // Function to implement the task
    "ledDriver", // Name of the task
    2000,  // Stack size in words
    NULL,  // Task input parameter
    4,  // Priority of the task
    &ledDriverTask,  // Task handle.
    0 // Core
  );
}

void loop()
{
  // empty, all work done in tasks
  vTaskDelay(portMAX_DELAY);
  vTaskDelete( NULL );
}

//================================================================
//================ Time Manager ==================================
//================================================================
void timeMgr( void * parameter) {

  extern SemaphoreHandle_t alarmSemaphore;

  if (xSemaphoreTake(rtcMutex, (TickType_t) 250) == pdTRUE ) {
    RTC.begin(); // start the clock
    xSemaphoreGive(rtcMutex);
  }
  else {
    Serial.println("timeMgr: Unable to get RTC Mutex");
  }

  if ( esp_task_wdt_add(NULL) != ESP_OK) { // add task to WDT
    Serial.println("Unable to add timeMgr to taskWDT!");
  }

  static time_t lastDswCheck = 0;
  static time_t lastNtpCheck = 0;
  static uint8_t hourPrevious;
  static uint8_t minutePrevious;

  time_t tn;

  if (xSemaphoreTake(rtcMutex, (TickType_t) 250) == pdTRUE ) {
    tn = RTC.get();
    if (RTC.oscStopped(true) || tn <= 0) { // the clock has stopped and needs to be reset
      Serial.println("timeMgr: The RTC stopped. Resetting from the Internet");
      tn = getNtpTime();
      if (!RTC.set(tn)) {
        Serial.println("timeMgr: Write to RTC FAILED!!!");
      }
      setTime(tn + (timeZone * SECS_PER_HOUR) + 1 );
    }
    else {
      setTime(tn + (timeZone * SECS_PER_HOUR) + 1 );
    }
    xSemaphoreGive(rtcMutex);
  }
  else {
    Serial.println("timeMgr: Unable to get RTC Mutex");
  }

  setSyncInterval(60);
  setDST(now());
  setSyncProvider(getClockTime);

  if (esp_task_wdt_reset() != ESP_OK) {
    Serial.println("timeMgr: Unable to reset timeMgr taskWDT!");
  }

  TickType_t xLastWakeTime;
  const TickType_t xFrequency = 500 / portTICK_PERIOD_MS; // Run the loop twice a second
  xLastWakeTime = xTaskGetTickCount();

  for (;;) { // Begin main loop

    time_t ts = now();
    uint8_t minuteNow = minute(ts);

    if (minuteNow != minutePrevious) {
      minutePrevious = minuteNow;
      // Test the alarms
      if (alarm1.alarmTest(ts)) {
        Serial.println("timeMgr: ALARM1!!!");
        disp.setAlarmRinging(1);
        xSemaphoreGive(alarmSemaphore);
      }
      else if (alarm2.alarmTest(ts)) {
        Serial.println("timeMgr: ALARM2!!!");
        disp.setAlarmRinging(2);
        xSemaphoreGive(alarmSemaphore);
      }
      else if (alarm3.alarmTest(ts)) {
        Serial.println("timeMgr: ALARM3!!!");
        disp.setAlarmRinging(3);
        xSemaphoreGive(alarmSemaphore);
      }

      if (minuteNow % 5 == 0) {
        // Every 5 minutes, print heap status
        Serial.println("timeMgr: Current free data memory: " + String(xPortGetFreeHeapSize()));
        Serial.println("timeMgr: Minimum free data memory: " + String(xPortGetMinimumEverFreeHeapSize()));
      }
    }

    if (hour(ts) != hourPrevious) { // this is to check to see if we go into or out of DST
      Serial.println("timeMgr: BONG! new hour. Running hourly tasks.");
      hourPrevious = hour(ts);
      setDST(ts);
      //disp.setFullReDraw(true); // force a full screen re-paint
    }

    if (esp_task_wdt_reset() != ESP_OK) {
      Serial.println("timeMgr: Unable to reset timeMgr taskWDT!");
    }

    if (now() - lastNtpCheck > 3600) { // check NTP once an hour
      if (disp.getAlarmRinging() != 0)
      { // don't try and get the time if there is an alarm going off! Try in 2 minutes
        lastNtpCheck += 120;
      }
      else {
        if (setRTC(getNtpTime())) {
          Serial.println("timeMgr: RTC set from from NTP server");
          lastNtpCheck = ts;
        }
        else {
          Serial.println("timeMgr: Unable to set RTC from NTP server. try again in 15 minutes.");
          lastNtpCheck += 900;
        }
      }
    }

    if (esp_task_wdt_reset() != ESP_OK) {
      Serial.println("timeMgr: Unable to reset timeMgr taskWDT!");
    }

    if (ts - lastDswCheck > 300) { // Check the weather every 5 minutes

      if (disp.getAlarmRinging() != 0)
      { // don't try and get the weather if there is an alarm going off! try in 2 minutes
        lastDswCheck += 120;
      }
      else {
        Serial.println("timeMgr: Getting the weather from DSW.");
        if (getCurrentWeather()) {
          lastDswCheck = ts;
          disp.setWeatherValid(true);
        }
        else {
          lastDswCheck += 120; // check again in 2 minutes
          Serial.println("timeMgr: Failed to retrieve weather!");
          disp.setWeatherValid(false);
        }
      }
    }

    if (xPortGetMinimumEverFreeHeapSize() < 4096) { // Reset if we ever get to less than 4K of free memory
      Serial.println("timeMgr: Minimum free memory too low. Restarting.");
      ESP.restart();
    }

    if (esp_task_wdt_reset() != ESP_OK) {
      Serial.println("timeMgr: Unable to reset timeMgr taskWDT!");
    }

    vTaskDelayUntil( &xLastWakeTime, xFrequency );
  }
}

//================================================================
//================ Get NTP Time ==================================
//================================================================
time_t getNtpTime() {

  char serverName [50];
  IPAddress ntpServerIP; // NTP server's ip address

  Serial.println("Wifi status is: " + String(wl_status_to_string(WiFi.status())));
  if (WiFi.status() != WL_CONNECTED) {
    if (connectToWifi() == false) {
      Serial.println("getNtpTime: Unable to get NTP Time. Disconnecting.");
      disp.setCurrWiFiStatus(false);
      disconnectFromWiFi();
      return 0;
    }
  }

  while (Udp.parsePacket() > 0) ; // discard any previously received packets

  for (int i = 0; i < (sizeof(ntpServerName) / sizeof(ntpServerName[0])); i++)
  {
    Serial.println("getNtpTime: Transmit NTP Request");
    ntpServerName[i].toCharArray(serverName, 50);
    WiFi.hostByName(serverName, ntpServerIP);
    Serial.print(serverName);
    Serial.print(": ");
    Serial.println(ntpServerIP);
    sendNTPpacket(ntpServerIP);
    uint32_t beginWait = millis();
    while (millis() - beginWait < 2500) {
      int size = Udp.parsePacket();
      if (size >= NTP_PACKET_SIZE) {
        Serial.println("getNtpTime: Receive NTP Response");
        Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
        unsigned long secsSince1900;
        // convert four bytes starting at location 40 to a long integer
        secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
        secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
        secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
        secsSince1900 |= (unsigned long)packetBuffer[43];
        disp.setCurrWiFiStatus(true);
        disconnectFromWiFi();
        drawWiFiStatus(true);
        //Serial.println("getNtpTime: recieved time: " + String(secsSince1900 - 2208988800UL));
        return secsSince1900 - 2208988800UL;
      }
      vTaskDelay(200 / portTICK_RATE_MS);
    }
    Serial.println("getNtpTime: No NTP Response from " + String(serverName));
    vTaskDelay(100 / portTICK_RATE_MS);
  }
  disp.setCurrWiFiStatus(false);
  Serial.println ("getNtpTime: Failed to get NTP time. Disconnecting");
  disconnectFromWiFi();
  drawWiFiStatus(false);
  return 0; // return 0 if unable to get the time
}

//================================================================
//================ Send NTP Packet ===============================
//================================================================
// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress & address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

//================================================================
//================ Connect to WiFi ===============================
//================================================================
// Connect to the WiFi network
bool connectToWifi() {
  int retryCount = 0;
  int tryTimeout;
  while (WiFi.status() != WL_CONNECTED && retryCount < 3) {
    tryTimeout = 0;
    retryCount++;
    Serial.print("connectToWifi: WiFi disconnected. Reason code: ");
    Serial.println(wl_status_to_string(WiFi.status()));
    Serial.print("connectToWifi: Try #: ");
    Serial.println(retryCount);
    Serial.println("connectToWifi: WiFi connect");
    Serial.print("connectToWifi:     Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, pass);

    while (WiFi.status() != WL_CONNECTED && tryTimeout < 10) {
      tryTimeout++;
      vTaskDelay(500 / portTICK_PERIOD_MS);
      Serial.print(".");
    }
    Serial.println(" ");

    if (WiFi.status() != WL_CONNECTED) {
      disconnectFromWiFi();
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connection Failed");
    return false;
  }
  else {
    Serial.println();
    Serial.print("connectToWifi: IP number assigned by DHCP is ");
    Serial.println(WiFi.localIP());
    Serial.print("connectToWifi: DNS IP: ");
    Serial.println(WiFi.dnsIP());
    Serial.println("connectToWifi: Starting UDP");
    Udp.begin(localPort);
    Serial.print("connectToWifi: Local port: ");
    Serial.println(localPort);
    return true;
  }
}

//================================================================
//============= Disconnect From WiFi =============================
//================================================================
// Disconnect from the network
void disconnectFromWiFi() {
  ESP_LOGI(WiFi_TAG, "disconnectFromWiFi: WiFi closing down...");
  ESP_ERROR_CHECK( esp_wifi_disconnect());
  vTaskDelay(10 / portTICK_PERIOD_MS);
}

//================================================================
//====================== Set RTC =================================
//================================================================
bool setRTC(time_t ts) {

  if (xSemaphoreTake(rtcMutex, (TickType_t) 250) == pdTRUE ) {
    if (ts != 0) {
      RTC.set(ts);
      // Serial.println("setRTC: RTC time set to: " + String(ts));
      // Serial.println("setRTC: RTC time recieved: " + String(RTC.get()));
      xSemaphoreGive(rtcMutex);
      return true;
    }
    else {
      Serial.println("setRTC: Unable to set RTC time. recieved 0 for input.");
      xSemaphoreGive(rtcMutex);
      return false;
    }
  }
  else {
    Serial.println("setRTC: Unable to set RTC time. Unable to take rtcMutex.");
    xSemaphoreGive(rtcMutex);
    return false;
  }
}

//================================================================
//================ Get Clock Time ================================
//================================================================
time_t getClockTime() {

  static time_t lastTime = 0;
  if (xSemaphoreTake(rtcMutex, (TickType_t) 250) == pdTRUE ) { // no rush to do this
    time_t ts = RTC.get() + (timeZone * SECS_PER_HOUR) + 1 ; // +1 second to account for delays
    Serial.print("getClockTime: time from RTC: ");
    Serial.println(ts);
    if (ts < 0) {
      Serial.println("getClockTime: Got RTC time less than zero. This should not happen! Attempting reset of I2C bus.");
      I2C_ClearBus(false); // Reset the RTC bus
      vTaskDelay(500 / portTICK_PERIOD_MS); // time to get things sorted out
      RTC.begin(); // restart the RTC
      time_t ts = RTC.get() + (timeZone * SECS_PER_HOUR) + 1 ; // +1 second to account for delays

      if (ts < 0) {
        Serial.println("getClockTime: Failed.");
        xSemaphoreGive(rtcMutex);
        return 0;
      }
    }

    if (lastTime != 0 && (abs(ts - lastTime) > 3720)) { // rationality check time should not change by 1:02 in 60 seconds
      Serial.println("getClockTime: unreasonable time recieved from RTC.");
      Serial.println("getClockTime: ts: " + String(ts) + "   lastTime: " + String(lastTime));
      Serial.println("getClockTime: Difference: " + String(abs(ts - lastTime)));
      Serial.println("getClockTime: Attempting I2C bus reset.");
      I2C_ClearBus(false); // Reset the RTC bus
      RTC.begin(); // restart the RTC
      vTaskDelay(500 / portTICK_PERIOD_MS); // time to get things sorted out
      time_t ts = RTC.get() + (timeZone * SECS_PER_HOUR) + 1 ; // +1 second to account for delays
      if (abs(ts - lastTime) > 3720) {
        Serial.println("getClockTime: Failed.");
        xSemaphoreGive(rtcMutex);
        return 0;
      }
    }

    lastTime = ts;
    xSemaphoreGive(rtcMutex);
    return ts;
  }
  else {
    Serial.println("getClockTime: Unable to get RTC time. Unable to take rtcMutex.");
    return 0;
  }
}

//================================================================
//===================== Set DST ==================================
//================================================================
void setDST(time_t ts) // set DST Time Zone
{
  Serial.print("setDST: Checking DST: ");
  if (isDST(ts)) {
    Serial.println ("Yes. DST");
    timeZone = -4;  // USA Eastern DST
  }
  else {
    Serial.println ("No. Not DST");
    timeZone = -5; // USA Eastern
  }
}

//====================== Is DST ==================================
//================================================================
bool isDST(time_t tn)
{
  tn -= (3600 * 5); // apply US Eastern Time zone correction
  uint8_t myMonth = month(tn);
  uint8_t myDay = day(tn);
  uint8_t myWeekday = weekday(tn);
  uint8_t myHour = hour(tn);

  // January, February, and December are out.
  if (myMonth < 3 || myMonth > 11) {
    return false;
  }
  // April to October are in
  if (myMonth > 3 && myMonth < 11) {
    return true;
  }

  uint8_t previousSunday = myDay - (myWeekday - 1); // need weekday 0-6 not 1-7

  //In march, we are DST if our previous sunday was on or after the 8th.
  if (myMonth == 3) {
    if (previousSunday > 8) {
      return true;
    }
    else if (previousSunday < 8) {
      return false;
    }
    else { //previousSunday == 8)
      if (myWeekday == 1) { // it's spring forward Sunday!
        if (myHour < 2) {
          return false;
        }
        else {
          return true;
        }
      }
      else {
        return true;
      }
    }
  }

  //In november we must be before the first sunday to be dst.
  //That means the previous sunday must be before the 1st.
  if (previousSunday < 0) {
    return true;
  }
  else if (previousSunday > 0) {

    if (previousSunday == 1 && myWeekday == 1) { // it's fall back Sunday
      if (hour(tn) < 2) {
        return true;
      }
      else {
        return false;
      }
    }
    return false;
  }
}

//================================================================
//================ Get Current Weather ===========================
//================================================================
bool getCurrentWeather() {

  // For the Dark Skies query
  const String units = "us";  // See notes tab
  const String language = "en"; // See notes tab
  // Set both longitude and latitude to at least 4 decimal places - Wethersfield, CT

  // hourly not used by this sketch, set to nullptr
  hourly =  nullptr; //new DSW_hourly;

  static uint8_t weatherFails = 0;

  bool result = false;

  Serial.println("getCurrentWeather: Wifi status is: " + String(wl_status_to_string(WiFi.status())));

  if (WiFi.status() != WL_CONNECTED) {
    if (connectToWifi() == false) {
      Serial.println("getCurrentWeather: Unable to get Dark Skies Weather. Disconnecting.");
      disconnectFromWiFi();
      disp.setCurrWiFiStatus(false);
      return false;
    }
  }

  Serial.println("\ngetCurrentWeather: Requesting weather information from DarkSky.net... ");
  if (xSemaphoreTake(dswMutex, (TickType_t) 100) == pdTRUE ) {

    if (current != NULL) {
      Serial.println("getCurrentWeather: Clearing old weather data.");
      delete current;
      delete daily;
    }

    // create the objects to hold the weather data
    current = new DSW_current;
    daily =  new DSW_daily;

    result = dsw.getForecast(current, hourly, daily, api_key, latitude, longitude, units, language);
    xSemaphoreGive(dswMutex);
  }
  else {
    Serial.println("getCurrentWeather: Unable to take dswMutex.");
  }

  disconnectFromWiFi();

  if (current->summary == "" && (int)(current->pressure) == 0) { // This is an indication the DNS has failed
    ++weatherFails;
    Serial.println("getCurrentWeather: Possible failed weather connection. Count: " + String(weatherFails));
    result = false;
  }
  else {
    if (weatherFails > 0) { // if we lost weather and now have it back, redraw the working area of the screen
      disp.setDrawLowerScreen(true);
    }
    weatherFails = 0;
  }

  if (weatherFails >= 3) {
    ESP.restart();  // reboot
  }

  if (result == false) {
    return false;
  }

  if (result == true) {
    Serial.println("getCurrentWeather: Weather from Dark Sky Retrieved\n");

    /*
        Serial.println("############### Current weather ###############\n");
        String dateString = assembleDateStr(current->time + timeZone * SECS_PER_HOUR);
        String timeString = assembleTimeStr(current->time + timeZone * SECS_PER_HOUR);
        Serial.print("Current time             : "); Serial.println(dateString + " " + timeString);
        Serial.print("Current summary          : "); Serial.println(current->summary);
        Serial.print("Current icon             : "); Serial.println(getMeteoconIcon(current->icon));
        Serial.print("Current precipInten      : "); Serial.println(current->precipIntensity);
        Serial.print("Current precipType       : "); Serial.println(getMeteoconIcon(current->precipType));
        Serial.print("Current precipProbability: "); Serial.println(current->precipProbability);
        Serial.print("Current temperature      : "); Serial.println(current->temperature);
        Serial.print("Current humidity         : "); Serial.println(current->humidity);
        Serial.print("Current pressure         : "); Serial.println(current->pressure);
        Serial.print("Current wind speed       : "); Serial.println(current->windSpeed);
        Serial.print("Current wind gust        : "); Serial.println(current->windGust);
        Serial.print("Current wind dirn        : "); Serial.println(current->windBearing);

        int i = 0;

        Serial.println("###############  Daily weather  ###############\n");
        Serial.print("Daily summary     : "); Serial.println(daily->overallSummary);
        Serial.println();

        Serial.println("############### Today's Forecast ###############\n");
        dateString = assembleDateStr(daily->time[i] + timeZone * SECS_PER_HOUR);
        timeString = assembleTimeStr(daily->time[i] + timeZone * SECS_PER_HOUR);
        Serial.print("Daily summary   ");
        Serial.print(i); Serial.print(" : "); Serial.println(daily->summary[i]);
        Serial.print("time              : "); Serial.println(dateString + " " + timeString);
        Serial.print("Icon              : "); Serial.println(getMeteoconIcon(daily->icon[i]));
        timeString = assembleTimeStr(daily->sunriseTime[i] + timeZone * SECS_PER_HOUR);
        Serial.print("sunriseTime       : "); Serial.println(timeString);
        timeString = assembleTimeStr(daily->sunsetTime[i] + timeZone * SECS_PER_HOUR);
        Serial.print("sunsetTime        : "); Serial.println(timeString);
        Serial.print("Moon phase        : "); Serial.println(daily->moonPhase[i]);
        Serial.print("precipIntensity   : "); Serial.println(daily->precipIntensity[i]);
        Serial.print("precipProbability : "); Serial.println(daily->precipProbability[i]);
        Serial.print("precipType        : "); Serial.println(getMeteoconIcon(daily->precipType[i]));
        Serial.print("precipAccumulation: "); Serial.println(daily->precipAccumulation[i]);
        Serial.print("temperatureHigh   : "); Serial.println(daily->temperatureHigh[i]);
        Serial.print("temperatureLow    : "); Serial.println(daily->temperatureLow[i]);
        Serial.print("humidity          : "); Serial.println(daily->humidity[i]);
        Serial.print("pressure          : "); Serial.println(daily->pressure[i]);
        Serial.print("windSpeed         : "); Serial.println(daily->windSpeed[i]);
        Serial.print("windGust          : "); Serial.println(daily->windGust[i]);
        Serial.print("windBearing       : "); Serial.println(daily->windBearing[i]);
        Serial.print("cloudCover        : "); Serial.println(daily->cloudCover[i]);
        Serial.print("uvIndex           : "); Serial.println(daily->uvIndex[i]);
        Serial.println();

        i = 1;
        Serial.println("############### Tomorrow's Forecast ###############\n");
        dateString = assembleDateStr(daily->time[i] + timeZone * SECS_PER_HOUR);
        timeString = assembleTimeStr(daily->time[i] + timeZone * SECS_PER_HOUR);
        Serial.print("Daily summary   ");
        Serial.print(i); Serial.print(" : "); Serial.println(daily->summary[i]);
        Serial.print("time              : "); Serial.println(dateString + " " + timeString);
        Serial.print("Icon              : "); Serial.println(getMeteoconIcon(daily->icon[i]));
        timeString = assembleTimeStr(daily->sunriseTime[i] + timeZone * SECS_PER_HOUR);
        Serial.print("sunriseTime       : "); Serial.println(timeString);
        timeString = assembleTimeStr(daily->sunsetTime[i] + timeZone * SECS_PER_HOUR);
        Serial.print("sunsetTime        : "); Serial.println(timeString);
        Serial.print("Moon phase        : "); Serial.println(daily->moonPhase[i]);
        Serial.print("precipIntensity   : "); Serial.println(daily->precipIntensity[i]);
        Serial.print("precipProbability : "); Serial.println(daily->precipProbability[i]);
        Serial.print("precipType        : "); Serial.println(getMeteoconIcon(daily->precipType[i]));
        Serial.print("precipAccumulation: "); Serial.println(daily->precipAccumulation[i]);
        Serial.print("temperatureHigh   : "); Serial.println(daily->temperatureHigh[i]);
        Serial.print("temperatureLow    : "); Serial.println(daily->temperatureLow[i]);
        Serial.print("humidity          : "); Serial.println(daily->humidity[i]);
        Serial.print("pressure          : "); Serial.println(daily->pressure[i]);
        Serial.print("windSpeed         : "); Serial.println(daily->windSpeed[i]);
        Serial.print("windGust          : "); Serial.println(daily->windGust[i]);
        Serial.print("windBearing       : "); Serial.println(daily->windBearing[i]);
        Serial.print("cloudCover        : "); Serial.println(daily->cloudCover[i]);
        Serial.print("uvIndex           : "); Serial.println(daily->uvIndex[i]);
        Serial.println();
    */
    disp.setDrawLowerScreen(true);
  }
  else {
    Serial.println("getCurrentWeather: Failed to get weather.");
  }

  disp.setCurrWiFiStatus(result);
  return result;
}

//================================================================
//==================== I2C Clear Bus =============================
//================================================================
int I2C_ClearBus(bool waitTime) {
#if defined(TWCR) && defined(TWEN)
  TWCR &= ~(_BV(TWEN)); //Disable the Atmel 2-Wire interface so we can control the SDA and SCL pins directly
#endif

  pinMode(SDA, INPUT_PULLUP); // Make SDA (data) and SCL (clock) pins Inputs with pullup.
  pinMode(SCL, INPUT_PULLUP);

  if (waitTime) {
    delay(2500);  
  }
  // Wait 2.5 secs. This is strictly only necessary on the first power
  // up of the DS3231 module to allow it to initialize properly,
  // but is also assists in reliable programming of FioV3 boards as it gives the
  // IDE a chance to start uploaded the program
  // before existing sketch confuses the IDE by sending Serial data.

  boolean SCL_LOW = (digitalRead(SCL) == LOW); // Check is SCL is Low.
  if (SCL_LOW) { //If it is held low Arduno cannot become the I2C master.
    return 1; //I2C bus error. Could not clear SCL clock line held low
  }

  boolean SDA_LOW = (digitalRead(SDA) == LOW);  // vi. Check SDA input.
  int clockCount = 20; // > 2x9 clock

  while (SDA_LOW && (clockCount > 0)) { //  vii. If SDA is Low,
    clockCount--;
    // Note: I2C bus is open collector so do NOT drive SCL or SDA high.
    pinMode(SCL, INPUT); // release SCL pullup so that when made output it will be LOW
    pinMode(SCL, OUTPUT); // then clock SCL Low
    delayMicroseconds(10); //  for >5uS
    pinMode(SCL, INPUT); // release SCL LOW
    pinMode(SCL, INPUT_PULLUP); // turn on pullup resistors again
    // do not force high as slave may be holding it low for clock stretching.
    delayMicroseconds(10); //  for >5uS
    // The >5uS is so that even the slowest I2C devices are handled.
    SCL_LOW = (digitalRead(SCL) == LOW); // Check if SCL is Low.
    int counter = 20;
    while (SCL_LOW && (counter > 0)) {  //  loop waiting for SCL to become High only wait 2sec.
      counter--;
      delay(100);
      SCL_LOW = (digitalRead(SCL) == LOW);
    }
    if (SCL_LOW) { // still low after 2 sec error
      return 2; // I2C bus error. Could not clear. SCL clock line held low by slave clock stretch for >2sec
    }
    SDA_LOW = (digitalRead(SDA) == LOW); //   and check SDA input again and loop
  }
  if (SDA_LOW) { // still low
    return 3; // I2C bus error. Could not clear. SDA data line held low
  }

  // else pull SDA line low for Start or Repeated Start
  pinMode(SDA, INPUT); // remove pullup.
  pinMode(SDA, OUTPUT);  // and then make it LOW i.e. send an I2C Start or Repeated start control.
  // When there is only one I2C master a Start or Repeat Start has the same function as a Stop and clears the bus.
  /// A Repeat Start is a Start occurring after a Start with no intervening Stop.
  delayMicroseconds(10); // wait >5uS
  pinMode(SDA, INPUT); // remove output low
  pinMode(SDA, INPUT_PULLUP); // and make SDA high i.e. send I2C STOP control.
  delayMicroseconds(10); // x. wait >5uS
  pinMode(SDA, INPUT); // and reset pins as tri-state inputs which is the default state on reset
  pinMode(SCL, INPUT);
  return 0; // all ok
}
