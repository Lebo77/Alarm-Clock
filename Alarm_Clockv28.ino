/*
   Duncan Pickard's over-engineered Alarm Clock

   Master file
*/
#include "globalInclude.h"

// Set up the SPIFFS FLASH filing system
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
#include <OpenWeather.h>
#include <math.h>
#include "SPIFFS_Support.h"
#include <esp_int_wdt.h>
#include <esp_task_wdt.h>
#include "DisplayMgr.h"
extern "C" {
#include <esp_wifi.h>
}
#include <Preferences.h>
#include "ledCtrl.h"

#define SECONDS_FROM_1970_TO_2000 946684800

// ------------------------------------ Global Variables -----------------------------------------

//  For Demo use

const char ssid[] = "yourSSID";  //  your network SSID (name)
const char pass[] = "yourpassword";       // your network password
const String latitude =  "40.000"; // 90.0000 to -90.0000 negative for Southern hemisphere
const String longitude = "-70.00000"; // 180.000 to -180.000 negative for West
const String api_key = "00000000000000000000000000000000"; // Obtain this from your Dark Sky account
                

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
rollingSprite sprite3;

// Non-Volitile storage object
Preferences prefs;

const unsigned int localPort = 8888;  // local port to listen for UDP packets
WiFiUDP Udp;

DS3232RTC RTC(false);  // set up but do not start the RTC
// a mutex to lock the RTC during updates
SemaphoreHandle_t rtcMutex;

// a mutex to lock the ledStrip updates and flash during updates
SemaphoreHandle_t ledMutex;

//A mutex to protect critical code sections
portMUX_TYPE criticalMutex = portMUX_INITIALIZER_UNLOCKED;

// a mutex to protect OpenWeather data
SemaphoreHandle_t owMutex;  // a mutex to lock the owWeather objects

// a mutex to protect OpenWeather data
SemaphoreHandle_t wifiMutex;  // a mutex to lock the wifi interface

OW_Weather ow; // Weather forecast library instance
OW_current *currentA;
OW_hourly *hourlyA;
OW_daily  *dailyA;
OW_current *currentB;
OW_hourly *hourlyB;
OW_daily  *dailyB;
OW_current *current = currentA;
OW_hourly *hourly = hourlyA;
OW_daily  *daily = dailyA;
OW_current *notCurrent = currentB;
OW_hourly *notHourly = hourlyB;
OW_daily *notDaily = dailyB;


uint32_t OwAPICalls = 0;

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
void timeMgr( void * parameter);
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
void drawTextString(String msg, uint16_t x, uint16_t y, const GFXfont * font, uint16_t padding, uint8_t alignment, uint32_t color, uint32_t bg);
void clearWorkingArea();
void drawAlarmSetDisplay(bool repaint, int alarmNumber);
void drawLightSetDisplay(bool repaint);
String assembleDateStr(time_t ts);
String assembleTimeStr(time_t ts);
String assembleHourlyTimeStr(time_t ts);
String formatWindString(float windSpeed, float windGust , float windBearing);
String formatPrecipString(int type, int prob, float intensity);
String getDayOfWeek(int i);
String getMonthOfYear(int i);
const String getMeteoconIcon(uint16_t id, bool today);

// Functions found in modeMgmt file
void IRAM_ATTR touchISR();
void modeMgr( void * parameter);

// Functions found in scrolling_sprites file
void spriteMgr( void * parameter);

// Functions found in alarmMgmt file
void alarmMgr( void * parameter );

// Functions found in LedMgmt file
void ledMgr( void * parameter);
void ledDriver( void * parameter);

void setup()
{
  Serial.begin(115200);

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
  if (owMutex == NULL) {
    owMutex = xSemaphoreCreateMutex();
  }
  if (owMutex != NULL) {
    Serial.println("Setup:owMutex Created.");
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
  if (wifiMutex == NULL) {
    wifiMutex = xSemaphoreCreateMutex();
  }
  if (wifiMutex != NULL) {
    Serial.println("Setup:wifiMutex Created.");
  }

  esp_task_wdt_init(60, true); // Task WDT set for 60 seconds and reboot if it expires

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
    Serial.println(F("Setup: I2C setup finished"));
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
    spriteMgr, // Function to implement the task
    "spriteMgr", // Name of the task
    2000,  // Stack size in words
    NULL,  // Task input parameter
    3,  // Priority of the task
    &spriteTask);  // Task handle.

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
    alarmMgr, // Function to implement the task
    "alarmMgr", // Name of the task
    4000,  // Stack size in words
    NULL,  // Task input parameter
    5,  // Priority of the task
    &alarmTask,  // Task handle.
    1 // Core
  );

  // spawn the led manager process
  xTaskCreatePinnedToCore(
    ledMgr, // Function to implement the task
    "ledMgr", // Name of the task
    2000,  // Stack size in words
    NULL,  // Task input parameter
    1,  // Priority of the task
    &ledTask,  // Task handle.
    1 // Core
  );

  // spawn the led driver process
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

  static uint8_t hourPrevious;
  static uint8_t minutePrevious;
  static uint8_t dayPrevious;

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

  // setSyncInterval(75);
  setDST(now());
  // setSyncProvider(getClockTime);

  if (esp_task_wdt_reset() != ESP_OK) {
    Serial.println("timeMgr: Unable to reset timeMgr taskWDT!");
  }

  if (getCurrentWeather()) {
    Serial.println("timeMgr: Startup Weather Check Successful");
    disp.setDrawLowerScreen(true);
  }
  else {
    Serial.println("timeMgr: Startup Weather Check Successful");
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

        setTime(getClockTime()); // set the CPU time from the RTC

        if (disp.getAlarmRinging() != 0)
        { // don't try and get the weather if there is an alarm going off. Try in 2.5 minutes
          Serial.println("timeMgr: Alarm is ringing. skip getting the weather.");
        }
        else {
          Serial.println("timeMgr: Getting the weather from OpenWeather.");
          if (getCurrentWeather()) {
          }
          else {
            Serial.println("timeMgr: Failed to retrieve weather!");
          }
        }
        disp.setDrawLowerScreen(true); // set the lower screen to be redrawn every 5 minutes
      }

      if (hour(ts) != hourPrevious) { // this is to check to see if we go into or out of DST
        Serial.println("timeMgr: BONG! new hour. Running hourly tasks.");
        hourPrevious = hour(ts);
        // check NTP once an hour
        if (disp.getAlarmRinging() == 0) { // don't try and get the time if there is an alarm going off! Try in 2 minutes
          if (setRTC(getNtpTime())) {
            Serial.println("timeMgr: RTC set from from NTP server");
          }
          else {
            Serial.println("timeMgr: Unable to set RTC from NTP server.");
          }
        }
        setDST(ts); // check to see if DST status changed
      }

      if (day(ts) != dayPrevious) { // Run Daily Tasks
        Serial.println("timeMgr: BONG! new day. Running daily tasks.");
        dayPrevious = day(ts);
        OwAPICalls  = 0;
        disp.setFullReDraw(true); // force a full screen re-paint
      }
    }

    if (xPortGetMinimumEverFreeHeapSize() < 2048) { // Reset if we ever get to less than 2K of free memory
      Serial.println("\n\ntimeMgr: ========================================");
      Serial.println("timeMgr: Minimum free memory too low. Restarting.");
      Serial.println("timeMgr: ========================================\n\n");
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

  if (xSemaphoreTake(wifiMutex, (TickType_t) 200) == pdTRUE ) {

    if (WiFi.status() != WL_CONNECTED) {
      if (connectToWifi() == false) {
        Serial.println("getNtpTime: Unable to get NTP Time. Disconnecting.");
        disp.setCurrWiFiStatus(false);
        disconnectFromWiFi();
        xSemaphoreGive(wifiMutex);
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
          xSemaphoreGive(wifiMutex);
          drawWiFiStatus(true);
          //Serial.println("getNtpTime: recieved time: " + String(secsSince1900 - 2208988800UL));
          Serial.print("getNtpTime: Recieved time: ");
          Serial.println(secsSince1900 - 2208988800UL);
          return secsSince1900 - 2208988800UL;
        }
      }
      Serial.println("getNtpTime: No NTP Response from " + String(serverName));
    }
    disp.setCurrWiFiStatus(false);
    Serial.println ("getNtpTime: Failed to get NTP time. Disconnecting");
    disconnectFromWiFi();
    xSemaphoreGive(wifiMutex);
    drawWiFiStatus(false);
    return 0; // return 0 if unable to get the time
  }
  else {
    Serial.println("getNtpTime: Unable to get wifi Mutex");
    return 0; // return 0 if unable to get the time
  }
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

    while (WiFi.status() != WL_CONNECTED && tryTimeout < 60) {
      tryTimeout++;
      vTaskDelay(250 / portTICK_PERIOD_MS);
      Serial.print(".");
    }
    Serial.println(" ");

    if (WiFi.status() != WL_CONNECTED) { // Failed to connect
      disconnectFromWiFi();
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("connectToWifi: Connection Failed");
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
  Serial.println("disconnectFromWiFi: WiFi closing down.");
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
      Serial.println("setRTC: RTC time set to: " + String(ts));
      time_t gotBack = RTC.get();
      Serial.println("setRTC: RTC time recieved: " + String(gotBack));

      if (abs(ts - gotBack) > 2) {
        Serial.println("setRTC: Failed to set time. May be caused by I2C bus error!");
        int clearBusRtn = I2C_ClearBus(false);
        if (clearBusRtn != 0) {
          Serial.println("setRTC: busReset Failed. Error code: " + clearBusRtn);
          xSemaphoreGive(rtcMutex);
          return (false);
        }
        else {
          Serial.println("setRTC: Clearbus succeeded");
          RTC.begin(); // restart the RTC
          RTC.set(ts);
        }
      }

      Serial.println("setRTC: Time set successfully");
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
  time_t ts;
  if (xSemaphoreTake(rtcMutex, (TickType_t) 250) == pdTRUE ) { // no rush to do this
    ts = RTC.get() + (timeZone * SECS_PER_HOUR) + 1 ; // +1 second to account for delays
    Serial.print("getClockTime: time from RTC: ");
    Serial.println(ts);
    if (ts < 0) { // negative time error
      Serial.println("getClockTime: Got RTC time less than zero. This should not happen! Attempting reset of I2C bus.");
      I2C_ClearBus(false); // Reset the RTC bus
      vTaskDelay(100 / portTICK_PERIOD_MS); // time to get things sorted out
      RTC.begin();
      ts = RTC.get() + (timeZone * SECS_PER_HOUR) + 1 ; // +1 second to account for delays
      Serial.print("getClockTime: RTC restarted. New time: ");
      Serial.println(ts);
      if (ts < 0) {
        Serial.println("getClockTime: Failed. Still negitive.");
        xSemaphoreGive(rtcMutex);
        return 0;
      }
    }

    if (lastTime != 0 && (abs(ts - lastTime) > 3720)) { // rationality check time should not change by 62 minutes in 60 seconds
      Serial.println("getClockTime: unreasonable time recieved from RTC.");
      Serial.println("getClockTime: ts: " + String(ts) + "   lastTime: " + String(lastTime));
      Serial.println("getClockTime: Difference: " + String(abs(ts - lastTime)));
      Serial.println("getClockTime: Attempting I2C bus reset.");
      I2C_ClearBus(false); // Reset the RTC bus
      RTC.begin(); // restart the RTC
      vTaskDelay(100 / portTICK_PERIOD_MS); // time to get things sorted out
      Serial.println("getClockTime: Resetting RTC from NTP source.");
      RTC.set(getNtpTime()); // reset the RTC
      ts = RTC.get() + (timeZone * SECS_PER_HOUR) + 1 ; // +1 second to account for delays
      Serial.print("getClockTime: RTC reset. New time: ");
      Serial.println(ts);
      /*
        if (abs(ts - lastTime) > 3720) {
        Serial.println("getClockTime: Failed.");
        xSemaphoreGive(rtcMutex);
        return 0;
        }
      */
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

//================================================================
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

  //In november we must be before the first sunday to be DST.
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

  // For the OpenWeather query
  const String units = "imperial";  // See notes tab
  const String language = "en"; // See notes tab
  // Set both longitude and latitude to at least 4 decimal places - Wethersfield, CT

  static uint8_t weatherFails = 0;
  bool result = false;

  Serial.println("getCurrentWeather: Wifi status is: " + String(wl_status_to_string(WiFi.status())));

  if (xSemaphoreTake(wifiMutex, (TickType_t) 500) == pdTRUE ) {

    if (WiFi.status() != WL_CONNECTED) {
      if (connectToWifi() == false) {
        disconnectFromWiFi();
        xSemaphoreGive(wifiMutex);
        disp.setCurrWiFiStatus(result);
        weatherFails++;
        Serial.println("getCurrentWeather: Unable to connect to WiFi. Weather Fail count: " + String(weatherFails));
        if (weatherFails >= 5) {
          disp.setWeatherValid(false);
          disp.setMainPgMessage(String("Weather Error!"));
          disp.setMainPgMessageColor(TFT_RED);
          disp.setFullReDraw(true);
        }
        return false;
      }
    }

    Serial.println("\ngetCurrentWeather: Requesting weather information from OpenWeather... ");

    if (current == currentA) {
      // clear the old data
      delete currentB;
      delete hourlyB;
      delete dailyB;
      // create the objects to hold the new weather data
      currentB = new OW_current;
      hourlyB = new OW_hourly;
      dailyB =  new OW_daily;

      notCurrent = currentB;
      notHourly = hourlyB;
      notDaily = dailyB;
    }
    else if (current == currentB) {
      // clear the old data
      delete currentA;
      delete hourlyA;
      delete dailyA;
      // create the objects to hold the new weather data
      currentA = new OW_current;
      hourlyA = new OW_hourly;
      dailyA =  new OW_daily;

      notCurrent = currentA;
      notHourly = hourlyA;
      notDaily = dailyA;
    }
    else {
      Serial.println("getCurrentWeather: ERROR should not get here!");
    }

    if (esp_task_wdt_reset() != ESP_OK) { // reset watchdog, just in case
      Serial.println("timeMgr: Unable to reset timeMgr taskWDT!");
    }

    Serial.println("getCurrentWeather: Entering 1st attempt.");
    Serial.println("getCurrentWeather: Wifi status is: " + String(wl_status_to_string(WiFi.status())));
    result = ow.getForecast(notCurrent, notHourly, notDaily, api_key, latitude, longitude, units, language);
    OwAPICalls++;
    Serial.println("getCurrentWeather: Exit 1st attempt.");

    if (esp_task_wdt_reset() != ESP_OK) { // reset watchdog, just in case
      Serial.println("timeMgr: Unable to reset timeMgr taskWDT!");
    }

    if (result == false) { //something went wrong. Try again
      vTaskDelay(10 / portTICK_PERIOD_MS); // wait just a tick
      Serial.println("getCurrentWeather: Failed to get weather on 1st attempt. Trying again.");
      if (current == currentA) {
        // clear the old data
        delete currentB;
        delete hourlyB;
        delete dailyB;
        // create the objects to hold the new weather data
        currentB = new OW_current;
        hourlyB = new OW_hourly;
        dailyB =  new OW_daily;

        notCurrent = currentB;
        notHourly = hourlyB;
        notDaily = dailyB;
      }
      else if (current == currentB) {
        // clear the old data
        delete currentA;
        delete hourlyA;
        delete dailyA;
        // create the objects to hold the new weather data
        currentA = new OW_current;
        hourlyA = new OW_hourly;
        dailyA =  new OW_daily;

        notCurrent = currentA;
        notHourly = hourlyA;
        notDaily = dailyA;
      }
      else {
        Serial.println("getCurrentWeather: ERROR should not get here!");
      }

      if (esp_task_wdt_reset() != ESP_OK) { // reset watchdog, just in case
        Serial.println("timeMgr: Unable to reset timeMgr taskWDT!");
      }
      Serial.println("getCurrentWeather: Entering 2nd attempt.");
      result = ow.getForecast(notCurrent, notHourly, notDaily, api_key, latitude, longitude, units, language);
      OwAPICalls++;
      Serial.println("getCurrentWeather: Exiting 2nd attempt.");

      if (esp_task_wdt_reset() != ESP_OK) { // reset watchdog, just in case
        Serial.println("timeMgr: Unable to reset timeMgr taskWDT!");
      }

      if (!result) {
        Serial.println("getCurrentWeather: Second attempt failed.");
        ++weatherFails;
        Serial.println("getCurrentWeather: Possible failed weather connection. Count: " + String(weatherFails));
      }
    }

    if (result) {
      Serial.println("getCurrentWeather: Successfully parsed weather JSON object.");

      if (xSemaphoreTake(owMutex, (TickType_t) 100) == pdTRUE ) {
        if (current == currentA) {
          current = currentB; hourly = hourlyB; daily = dailyB;
        }
        else if (current == currentB) {
          current = currentA; hourly = hourlyA; daily = dailyA;
        }
        else {
          Serial.println("getCurrentWeather: ERROR! invalid current ptr value! Should never see this!");
        }
        xSemaphoreGive(owMutex);
        disp.setWeatherValid(true);
        if (weatherFails > 0) { // if we lost weather and now have it back, redraw the working area of the screen
          disp.setFullReDraw(true);
          disp.setMainPgMessage(String("Data by OpenWeather"));
          //disp.setMainPgMessage(String("OW API Calls: ") + String(OwAPICalls));
          disp.setMainPgMessageColor(TFT_DARKGREY);
        }
        weatherFails = 0;
        // Debug section
        disp.setFullReDraw(true);
        disp.setMainPgMessage(String("Data by OpenWeather"));
        //disp.setMainPgMessage(String("OW API Calls: ") + String(OwAPICalls));
        disp.setMainPgMessageColor(TFT_DARKGREY);
      }
      else {
        Serial.println("getCurrentWeather: Unable to take owMutex.");
      }
    }

    disconnectFromWiFi();
    xSemaphoreGive(wifiMutex);

    if (weatherFails >= 3) {
      disp.setMainPgMessage(String("Weather Error!"));
      disp.setMainPgMessageColor(TFT_RED);
      disp.setWeatherValid(false);
      disp.setFullReDraw(true);
      return (false);
    }

    if (result) {
      Serial.println("getCurrentWeather: Weather from Open Weather Retrieved\n");
      /*
            int i = 0;

            Serial.println("############### Current weather ###############\n");
            Serial.print("dt (time)        : "); Serial.print(strTime(current->dt));
            Serial.print("sunrise          : "); Serial.print(strTime(current->sunrise));
            Serial.print("sunset           : "); Serial.print(strTime(current->sunset));
            Serial.print("temp             : "); Serial.println(current->temp);
            Serial.print("feels_like       : "); Serial.println(current->feels_like);
            Serial.print("pressure         : "); Serial.println(current->pressure);
            Serial.print("humidity         : "); Serial.println(current->humidity);
            Serial.print("dew_point        : "); Serial.println(current->dew_point);
            Serial.print("uvi              : "); Serial.println(current->uvi);
            Serial.print("clouds           : "); Serial.println(current->clouds);
            Serial.print("visibility       : "); Serial.println(current->visibility);
            Serial.print("wind_speed       : "); Serial.println(current->wind_speed);
            Serial.print("wind_gust        : "); Serial.println(current->wind_gust);
            Serial.print("wind_deg         : "); Serial.println(current->wind_deg);
            Serial.print("rain             : "); Serial.println(current->rain);
            Serial.print("snow             : "); Serial.println(current->snow);
            Serial.println();
            Serial.print("id               : "); Serial.println(current->id);
            Serial.print("main             : "); Serial.println(current->main);
            Serial.print("description      : "); Serial.println(current->description);
            Serial.print("icon             : "); Serial.println(current->icon);

            Serial.println();

            Serial.println("############### Hourly weather  ###############\n");
            for (int i = 0; i < MAX_HOURS; i++)
            {
              Serial.print("Hourly summary  "); if (i < 10) Serial.print(" "); Serial.print(i);
              Serial.println();
              Serial.print("dt (time)        : "); Serial.print(strTime(hourly->dt[i]));
              Serial.print("temp             : "); Serial.println(hourly->temp[i]);
              Serial.print("feels_like       : "); Serial.println(hourly->feels_like[i]);
              Serial.print("pressure         : "); Serial.println(hourly->pressure[i]);
              Serial.print("humidity         : "); Serial.println(hourly->humidity[i]);
              Serial.print("dew_point        : "); Serial.println(hourly->dew_point[i]);
              Serial.print("clouds           : "); Serial.println(hourly->clouds[i]);
              Serial.print("wind_speed       : "); Serial.println(hourly->wind_speed[i]);
              Serial.print("wind_gust        : "); Serial.println(hourly->wind_gust[i]);
              Serial.print("wind_deg         : "); Serial.println(hourly->wind_deg[i]);
              Serial.print("rain             : "); Serial.println(hourly->rain[i]);
              Serial.print("snow             : "); Serial.println(hourly->snow[i]);
              Serial.println();
              Serial.print("id               : "); Serial.println(hourly->id[i]);
              Serial.print("main             : "); Serial.println(hourly->main[i]);
              Serial.print("description      : "); Serial.println(hourly->description[i]);
              Serial.print("icon             : "); Serial.println(hourly->icon[i]);
              Serial.print("pop              : "); Serial.println(hourly->pop[i]);

              Serial.println();
            }


            Serial.println("###############  Daily weather  ###############\n");
            for (int i = 0; i < MAX_DAYS; i++)
            {
              Serial.print("Daily summary   "); if (i < 10) Serial.print(" "); Serial.print(i);
              Serial.println();
              Serial.print("dt (time)        : "); Serial.print(strTime(daily->dt[i]));
              Serial.print("sunrise          : "); Serial.print(strTime(daily->sunrise[i]));
              Serial.print("sunset           : "); Serial.print(strTime(daily->sunset[i]));

              Serial.print("temp.morn        : "); Serial.println(daily->temp_morn[i]);
              Serial.print("temp.day         : "); Serial.println(daily->temp_day[i]);
              Serial.print("temp.eve         : "); Serial.println(daily->temp_eve[i]);
              Serial.print("temp.night       : "); Serial.println(daily->temp_night[i]);
              Serial.print("temp.min         : "); Serial.println(daily->temp_min[i]);
              Serial.print("temp.max         : "); Serial.println(daily->temp_max[i]);

              Serial.print("feels_like.morn  : "); Serial.println(daily->feels_like_morn[i]);
              Serial.print("feels_like.day   : "); Serial.println(daily->feels_like_day[i]);
              Serial.print("feels_like.eve   : "); Serial.println(daily->feels_like_eve[i]);
              Serial.print("feels_like.night : "); Serial.println(daily->feels_like_night[i]);

              Serial.print("pressure         : "); Serial.println(daily->pressure[i]);
              Serial.print("humidity         : "); Serial.println(daily->humidity[i]);
              Serial.print("dew_point        : "); Serial.println(daily->dew_point[i]);
              Serial.print("uvi              : "); Serial.println(daily->uvi[i]);
              Serial.print("clouds           : "); Serial.println(daily->clouds[i]);
              Serial.print("visibility       : "); Serial.println(daily->visibility[i]);
              Serial.print("wind_speed       : "); Serial.println(daily->wind_speed[i]);
              Serial.print("wind_gust        : "); Serial.println(daily->wind_gust[i]);
              Serial.print("wind_deg         : "); Serial.println(daily->wind_deg[i]);
              Serial.print("rain             : "); Serial.println(daily->rain[i]);
              Serial.print("snow             : "); Serial.println(daily->snow[i]);
              Serial.print("pop              : "); Serial.println(daily->pop[i]);
              Serial.println();
              Serial.print("id               : "); Serial.println(daily->id[i]);
              Serial.print("main             : "); Serial.println(daily->main[i]);
              Serial.print("description      : "); Serial.println(daily->description[i]);
              Serial.print("icon             : "); Serial.println(daily->icon[i]);

              Serial.println();
            }
      */
      disp.setDrawLowerScreen(true);
    }

    disp.setCurrWiFiStatus(result);
    Serial.println("getCurrentWeather: OW API Calls: " + String(OwAPICalls));
    return (result);
  }
  else {
    Serial.println("getCurrentWeather: Unable to get wifi Mutex");
    disp.setCurrWiFiStatus(result);
    return (result);
  }
}

//***************************************************************************************
//**                          Convert unix time to a time string
// ***************************************************************************************
String strTime(time_t unixTime)
{
  unixTime += timeZone * SECS_PER_HOUR;
  return ctime(&unixTime);
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
