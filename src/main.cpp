/*************************************************************
******************* INCLUDES & DEFINITIONS *******************
**************************************************************/

// Board libraries
#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <ESP32Time.h>

// Font libraries
#include "NotoSansBold15.h"
#include "tinyFont.h"
#include "smallFont.h"
#include "midleFont.h"
#include "bigFont.h"
#include "font18.h"

/* 
Create display and sprite objects:
 - lcd: Main display object
 - sprite: Primary drawing surface
 - errSprite: For error messages
 - rtc: For time functions
*/
TFT_eSPI lcd = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&lcd);
TFT_eSprite errSprite = TFT_eSprite(&lcd);
ESP32Time rtc(0);

//#################### EDIT THIS SECTION ###################
int offsetGMT = 2; // GMT+(your offset)
String location = "CITY_NAME"; // your city/town
String countryCode = "CODE"; // Country code (GB / US / ZA / etc)
String owmAPI = "YOUR_API_KEY"; // your Open Weather Map API key
String units = "metric";  // metric, imperial
//##########################################################

// Button pins
int BootButton = 0; // GPIO0 for left button (used to decrease brightness)
int KeyButton = 14; // GPIO14 for right button (used to increase brightness)

const char* ntpServer = "pool.ntp.org";

// Store coordinates after first successful lookup
float storedLat = 0;
float storedLon = 0;

// Additional variables
int brightness = 175; // initial brightness (half of 100-250 in steps of 25 - lower than 80 causes screen flickering)
int scrollPosition = 100;
bool firstUpdate = true;
unsigned long lastUpdate = 0;
int updatesCounter = 0;
unsigned long lastMillis = 0;
long lastFrameTime = 0;  // for FPS calculation
int framesPerSecond = 0; // current FPS

// Colours
#define bck TFT_BLACK
unsigned short greys[13];

// Static strings of data showed on right side 
const char* dataLabel[] = { "HUMID", "PRESS", "WIND" };
String dataLabelUnits[] = { "%", "hPa", "m/s" };

// Weather data variables
float temperature = 00.00;
float feelsLike = 00.00;
float maxTemp;
float minTemp;
float weatherMetrics[3];
float tempHistory[24] = {};
float tempHistoryTemp[24] = {};
int tempHistoryGraph[24] = { 0 };

// Scrolling message on bottom right side
String scrollMessage = "";
String conditions = "";
String sunriseTime = "";
String sunsetTime = "";

// Retry mechanism variables
unsigned long lastRetryTime = 0;
const unsigned long retryInterval = 10000; // 10 seconds between retries
int timeSyncRetries = 0;
int weatherRetries = 0;
const int maxRetries = 3;
bool timeSyncNeeded = false;
bool weatherSyncNeeded = false;


/*************************************************************
********************** HELPER FUNCTIONS **********************
**************************************************************/

// Function to adjust screen brightness using buttons
void adjustBrightness() {
  // Static variables to store previous button states
  static uint8_t prevBootBtn = HIGH, prevKeyBtn = HIGH;
  const int step = 25; // step size (25 provides 7 steps between 100-250)
  
  // Read current button states (active LOW)
  uint8_t currBootBtn = digitalRead(BootButton);
  uint8_t currKeyBtn = digitalRead(KeyButton);
  
  // Detect falling edge (HIGH->LOW transition) on Boot button
  if (prevBootBtn == HIGH && currBootBtn == LOW) {
    brightness = constrain(brightness - step, 100, 250); // decrease brightness, constrained to 100-250 range
    analogWrite(TFT_BL, brightness); // apply new brightness to backlight pin
  }
  
  // Detect falling edge (HIGH->LOW transition) on Key button
  if (prevKeyBtn == HIGH && currKeyBtn == LOW) {
    brightness = constrain(brightness + step, 100, 250); // increase brightness, constrained to 100-250 range
    analogWrite(TFT_BL, brightness); // apply new brightness to backlight pin
  }
  
  // Store current button states for next iteration
  prevBootBtn = currBootBtn;
  prevKeyBtn = currKeyBtn;
}

// Function to convert UNIX timestamp to readable time
String formatUnixTime(long unixTime) {
  // Convert UNIX timestamp to readable time (HH:MM)
  int hours = (unixTime % 86400L) / 3600 + offsetGMT; // adjust for timezone
  int minutes = (unixTime % 3600) / 60;
  
  if (hours >= 24) hours -= 24;
  if (hours < 0) hours += 24;
  
  String timeStr = "";
  if (hours < 10) timeStr += "0";
  timeStr += String(hours);
  timeStr += ":";
  if (minutes < 10) timeStr += "0";
  timeStr += String(minutes);
  
  return timeStr;
}

// Function to format temperature to 1 decimal place
String formatTemperature(float temp) {
  // Round to 1 decimal place
  char buffer[10];
  dtostrf(temp, 4, 1, buffer); // 4 characters total, 1 decimal place
  return String(buffer);
}

// Function to set time
void setTime() {
  configTime(3600 * offsetGMT, 0, ntpServer);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    rtc.setTimeStruct(timeinfo);
  }
}

// Function to get coordinates for the location (required for weather data call)
bool getLocationCords() {
  if (storedLat != 0 && storedLon != 0) return true;

  String urlLocation = "";
  // Replace any spaces in the location name with URL syntax
  for (int i = 0; i < location.length(); i++) {
    if (location[i] == ' ') {
      urlLocation += "%20";
    } else {
      urlLocation += location[i];
    }
  }
  
  String geoUrl = "http://api.openweathermap.org/geo/1.0/direct?q=" + urlLocation + "," + countryCode + "&limit=1&appid=" + owmAPI;
  
  HTTPClient http;
  http.begin(geoUrl);
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error && doc.size() > 0) {
      storedLat = doc[0]["lat"];
      storedLon = doc[0]["lon"];
      http.end();
      return true; // coordinates found
    }
  }
  http.end();
  return false; // coordinates not found
}

// Function to get current weather data (returns true if successful)
bool getWeatherData() {
  // Get current weather with stored coordinates
  String weatherUrl = "https://api.openweathermap.org/data/2.5/weather?lat=" + 
                     String(storedLat, 6) + "&lon=" + String(storedLon, 6) + 
                     "&units=" + units + "&appid=" + owmAPI;
  
  HTTPClient httpWeather;
  httpWeather.begin(weatherUrl);
  int weatherCode = httpWeather.GET();
  
  if (weatherCode == HTTP_CODE_OK) {
    String payload = httpWeather.getString();
    
    JsonDocument weatherDoc;
    DeserializationError error = deserializeJson(weatherDoc, payload);
    
    if (!error) {
      // Clean up city name (remove " Airport" if present)
      String cityName = weatherDoc["name"].as<String>();
      if (cityName.endsWith(" Airport")) {
        cityName = cityName.substring(0, cityName.length() - 8);
      }
      
      temperature = weatherDoc["main"]["temp"];
      
      // Update min and max temperatures on startup
      if (firstUpdate) {
        minTemp = temperature;
        maxTemp = temperature;
        firstUpdate = false;
      }

      feelsLike = weatherDoc["main"]["feels_like"];
      weatherMetrics[0] = weatherDoc["main"]["humidity"];
      weatherMetrics[1] = weatherDoc["main"]["pressure"];
      weatherMetrics[2] = weatherDoc["wind"]["speed"];
      
      // Get weather description
      conditions = weatherDoc["weather"][0]["description"].as<String>();
      conditions.setCharAt(0, toupper(conditions[0])); // capitalize first letter
      
      // Get sunrise/sunset times
      long sunrise = weatherDoc["sys"]["sunrise"];
      long sunset = weatherDoc["sys"]["sunset"];
      sunriseTime = formatUnixTime(sunrise);
      sunsetTime = formatUnixTime(sunset);
      
      // Update message
      scrollMessage = "#Conditions: " + conditions + "  #Feels like: " + formatTemperature(feelsLike) + "C" + "  #Sunrise: " + sunriseTime + "  #Sunset: " + sunsetTime;
      httpWeather.end();
      return true;
    }
  }
  
  httpWeather.end();
  return false;
}

// Function to update weather data
void updateData() {
  // Update scrolling message position
  scrollPosition--;
  if (scrollPosition < -450) scrollPosition = 180; // changed -420 to -450 | 100 to 180

  // Current time for retry checks
  unsigned long currentMillis = millis();

  // Check if we need to retry time sync
  if (timeSyncNeeded && currentMillis > lastRetryTime + retryInterval) {
    if (timeSyncRetries < maxRetries) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        rtc.setTimeStruct(timeinfo);
        timeSyncNeeded = false;
        timeSyncRetries = 0;
      } else {
        timeSyncRetries++;
        lastRetryTime = currentMillis;
      }
    }
  }

  // Check if we need to retry weather data
  if (weatherSyncNeeded && currentMillis > lastRetryTime + retryInterval) {
    if (weatherRetries < maxRetries) {
      if (getWeatherData()) {  // modified to return bool (see next step)
        weatherSyncNeeded = false;
        weatherRetries = 0;
      } else {
        weatherRetries++;
        lastRetryTime = currentMillis;
      }
    }
  }

  // Regular update check (every 5 minutes)
  if (currentMillis > lastUpdate + 300000) { 
    lastUpdate = currentMillis;
    updatesCounter++;
    
    // Reset counter if it reaches 1000 (not enough space for 4 digits)
    if (updatesCounter >= 1000) {
      updatesCounter = 1;
    }

    // Try to sync time first
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      timeSyncNeeded = true;
      timeSyncRetries = 0;
      lastRetryTime = currentMillis;
    } else {
      rtc.setTimeStruct(timeinfo);
      timeSyncNeeded = false;
    }

    // Then try to get weather data
    if (!getWeatherData()) {  // modified to return bool (see next step)
      weatherSyncNeeded = true;
      weatherRetries = 0;
      lastRetryTime = currentMillis;
    } else {
      weatherSyncNeeded = false;
    }

    // Update min and max temperatures
    if (temperature < minTemp) {
      minTemp = temperature;
    }
    if (temperature > maxTemp) {
      maxTemp = temperature;
    }

    // Update temperature history graph (30-min intervals)
    tempHistory[23] = temperature;
    for (int i = 23; i > 0; i--) {
      tempHistory[i - 1] = tempHistoryTemp[i];
    }

    for (int i = 0; i < 24; i++) {
      tempHistoryTemp[i] = tempHistory[i];
    }

    // Calculate graph scaling based on current min/max
    for (int i = 0; i < 24; i++) {
      tempHistoryGraph[i] = map(tempHistory[i], minTemp, maxTemp, 0, 12);
    }
  }
}

// Function to calculate and update FPS
void updateFPS() {
  // Calculate FPS
  framesPerSecond = 1000 / (millis() - lastFrameTime);
  lastFrameTime = millis();
}

// Function to draw the display
void drawDisplay() {
  // Update error sprite with scrolling message
  errSprite.fillSprite(greys[10]);
  errSprite.setTextColor(greys[1], greys[10]);
  errSprite.drawString(scrollMessage, scrollPosition, 4);
  
  // Clear main sprite and draw divider line
  sprite.fillSprite(TFT_BLACK);
  sprite.drawLine(138, 10, 138, 164, greys[6]);
  sprite.setTextDatum(0);
  
  // Left side elements
  sprite.loadFont(midleFont);
  sprite.setTextColor(greys[1], TFT_BLACK);
  sprite.drawString("WEATHER", 6, 10);
  sprite.unloadFont();
  
  sprite.loadFont(font18);
  sprite.setTextColor(greys[7], TFT_BLACK);
  sprite.drawString("LOC:", 11, 110); // changed from 6 to 11
  sprite.setTextColor(greys[2], TFT_BLACK);
  sprite.drawString(units == "metric" ? "C" : "F", 19, 50); // changed from 14 to 19
  sprite.fillCircle(13, 52, 2, greys[2]); // changed from 8 to 13
  
  sprite.setTextColor(greys[3], TFT_BLACK);
  sprite.drawString(location, 45, 110); // changed from 40 to 45
  sprite.unloadFont();
  
  // Draw time (without seconds)
  sprite.loadFont(tinyFont);
  sprite.setTextColor(greys[4], TFT_BLACK);
  sprite.drawString(rtc.getTime().substring(0, 5), 10, 132); // changed from 6 to 10
  sprite.unloadFont();
  
  // Static text elements
  sprite.setTextColor(greys[5], TFT_BLACK);
  sprite.drawString("INTERNET", 86, 10);
  sprite.drawString("STATION", 86, 20);
  
  // Main temperature display
  sprite.setTextDatum(4);
  sprite.loadFont(bigFont);
  sprite.setTextColor(greys[0], TFT_BLACK);
  sprite.drawFloat(temperature, 1, 74, 80);
  sprite.unloadFont();
  
  // Seconds display
  sprite.fillRoundRect(92, 132, 23, 22, 2, greys[2]); // changed from 80 to 90 | changed from 22 to 23
  sprite.loadFont(font18);
  sprite.setTextColor(TFT_BLACK, greys[2]);
  sprite.drawString(rtc.getTime().substring(6, 8), 103, 145); // changed from 96 to 103
  sprite.unloadFont();
  sprite.setTextDatum(0);

  // FPS display
  sprite.setTextColor(greys[7], TFT_BLACK);
  sprite.drawString("FPS:" + String(framesPerSecond), 92, 157);
  
  // Right side elements
  sprite.loadFont(font18);
  sprite.setTextColor(greys[1], TFT_BLACK);
  sprite.drawString("LAST 12 HOURS", 144, 10);
  sprite.unloadFont();
  
  sprite.fillRect(144, 28, 84, 2, greys[10]);
  
  // Min/Max temperature display
  sprite.setTextColor(greys[3], TFT_BLACK);
  String tempUnit = units == "metric" ? "C" : "F";
  sprite.drawString("MIN:" + String(minTemp) + tempUnit, 252, 10);
  sprite.drawString("MAX:" + String(maxTemp) + tempUnit, 252, 20);
  
  // Temperature graph
  sprite.fillSmoothRoundRect(144, 34, 174, 60, 3, greys[10], bck);
  sprite.drawLine(170, 39, 170, 88, TFT_WHITE);
  sprite.drawLine(170, 88, 314, 88, TFT_WHITE);
  
  sprite.setTextDatum(4);
  for (int j = 0; j < 24; j++) {
    for (int i = 0; i < tempHistoryGraph[j]; i++) {
      sprite.fillRect(173 + (j * 6), 83 - (i * 4), 4, 3, greys[2]);
    }
  }
  
  sprite.setTextColor(greys[2], greys[10]);
  sprite.drawString("MAX", 158, 42);
  sprite.drawString("MIN", 158, 86);
  
  sprite.loadFont(font18);
  sprite.setTextColor(greys[7], greys[10]);
  sprite.drawString("T", 158, 65); // changed from 58 to 65
  sprite.unloadFont();
  
  // Weather metrics boxes
  for (int i = 0; i < 3; i++) {
    sprite.fillSmoothRoundRect(144 + (i * 60), 100, 54, 32, 3, greys[9], bck);
    sprite.setTextColor(greys[3], greys[9]);
    sprite.drawString(dataLabel[i], 144 + (i * 60) + 27, 107);
    sprite.setTextColor(greys[2], greys[9]);
    sprite.loadFont(font18);
    sprite.drawString(String((int)weatherMetrics[i]) + dataLabelUnits[i], 144 + (i * 60) + 27, 124);
    sprite.unloadFont();
  }
  
  // Bottom status bar
  sprite.fillSmoothRoundRect(144, 148, 174, 16, 2, greys[10], bck);
  errSprite.pushToSprite(&sprite, 148, 150);
  
  sprite.setTextColor(greys[4], bck);
  sprite.drawString("CURRENT INFO", 182, 142); // changed from 141 to 142
  sprite.setTextColor(greys[7], bck);          // changed from greys[9] to greys[7]
  sprite.drawString("UPDATES: " + String(updatesCounter), 272, 142); // changed from 277 to 272 | 141 to 142
  
  // Push final sprite to display
  sprite.pushSprite(0, 0);
}


/*************************************************************
*********************** MAIN FUNCTIONS ***********************
**************************************************************/

// SETUP
void setup() {
  // Initialize hardware
  pinMode(15, OUTPUT);
  digitalWrite(15, 1);

  // Initialize display
  lcd.init();
  lcd.setRotation(1); // landscape orientation
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setCursor(0, 0);

  // Initialize LEDC
  ledcSetup(0, 10000, 8);
  ledcAttachPin(38, 0);
  ledcWrite(0, 130);
  
  // Display Wi-Fi connection message
  lcd.println("\nConnecting to Wi-Fi - please wait...");
  
  // Configure WiFiManager
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(10); // 10 second timeout for initial connection
  wifiManager.setConnectTimeout(10);      // 10 second connection timeout
  
  // Attempt Wi-Fi connection
  if (!wifiManager.autoConnect("T-Display-S3", "123456789")) {
    lcd.println("\nConnection timed out!");
    lcd.println("\nA Wi-Fi network has been created:");
    lcd.println("SSID: T-Display-S3");
    lcd.println("Password: 123456789");
    lcd.println("\nConnect and navigate to: 192.168.4.1");
    lcd.println("in a browser to setup your Wi-Fi.");
    
    // Start configuration portal
    wifiManager.setConfigPortalTimeout(0); // keep portal open indefinitely
    wifiManager.startConfigPortal("T-Display-S3", "123456789");
    
    // If we get here, configuration was completed
    lcd.fillScreen(TFT_BLACK);
    lcd.setCursor(0, 0);
    lcd.println("\nWiFi configuration complete!");
    lcd.println("\nRestarting in 3seconds...");
    delay(3000);
    ESP.restart();
  }
  
  // Connection successful
  lcd.println("\nWiFi connected!");
  lcd.print("SSID: ");
  lcd.println(WiFi.SSID());
  lcd.print("IP: ");
  lcd.println(WiFi.localIP());
  delay(2000);
  
  // Time sync message
  lcd.println("\nSyncing time - please wait...");
  
  // Configure NTP time
  configTime(3600 * offsetGMT, 0, ntpServer);
  
  // Attempt time sync
  struct tm timeinfo;
  unsigned long syncStart = millis();
  while(!getLocalTime(&timeinfo)) {
    if (millis() - syncStart > 10000) { // 10 second timeout
      lcd.println("\nTime sync failed!");
      lcd.println("Check internet connection and try again.");
      while(1); // halt execution
    }
  }
  
  // Time sync complete
  rtc.setTimeStruct(timeinfo);
  lcd.println("\nTime synchronized!");
  lcd.print("Current time: ");
  lcd.println(rtc.getTime());
  delay(2000);
  
  // Weather data message
  lcd.println("\nFetching weather data - please wait...");
  
  // Attempt to fetch location data
  if (!getLocationCords()) {
    lcd.println("\nFailed to get location!");
    lcd.println("Check location name in the code and try again.");
    while(1); // halt execution
  }
  getWeatherData();

  // Weather data fetch complete
  lcd.println("Weather data received!\nLoading final assets...");
  
  // Generate 13 levels of grey
  int co = 210;
  for (int i = 0; i < 13; i++) {
    greys[i] = lcd.color565(co, co, co);
    co = co - 20;
  }
  
  // Initialize sprites
  sprite.createSprite(320, 170);
  errSprite.createSprite(164, 15);
  
  // Ready message
  lcd.println("\nSystem ready!");
  lcd.println("Starting main display...");
  delay(2000);
}

// MAIN LOOP
void loop() {
  // Call functions & update display
  adjustBrightness();
  updateData();
  updateFPS();
  drawDisplay();

  delay(1); // small delay to free up CPU cycles
}
