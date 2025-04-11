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

const char* ntpServer = "pool.ntp.org";

// Store coordinates after first successful lookup
float storedLat = 0;
float storedLon = 0;

// additional variables
int scrollPosition = 100;
float maxTemp;
float minTemp;
bool firstUpdate = true;
unsigned long lastUpdate = 0;
int updatesCounter = 0;
unsigned long lastMillis = 0;

// colours
#define bck TFT_BLACK
unsigned short greys[13];

// static strings of data showed on right side 
const char* dataLabel[] = { "HUMID", "PRESS", "WIND" };
String dataLabelUnits[] = { "%", "hPa", "m/s" };

// data that changes
float temperature = 00.00;
float feelsLike = 00.00;
float weatherMetrics[3];
float tempHistory[24] = {};       //graph
float tempHistoryTemp[24] = {};   //graph
int tempHistoryGraph[24] = { 0 }; //graph

// scrolling message on bottom right side
String scrollMessage = "";
String conditions = "";
String sunriseTime = "";
String sunsetTime = "";


/*************************************************************
********************** HELPER FUNCTIONS **********************
**************************************************************/

// Function to convert UNIX timestamp to readable time
String formatUnixTime(long unixTime) {
  // Convert UNIX timestamp to readable time (HH:MM)
  int hours = (unixTime % 86400L) / 3600 + offsetGMT; // Adjust for timezone
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

// Function to get current weather data
void getWeatherData() {
  if (!getLocationCords()) {
    scrollMessage = "Error: Could not get location coordinates";
    return;
  }

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
      conditions.setCharAt(0, toupper(conditions[0])); // Capitalize first letter
      
      // Get sunrise/sunset times
      long sunrise = weatherDoc["sys"]["sunrise"];
      long sunset = weatherDoc["sys"]["sunset"];
      sunriseTime = formatUnixTime(sunrise);
      sunsetTime = formatUnixTime(sunset);
      
      // Update message
      scrollMessage = "#Conditions: " + conditions + "  #Feels like: " + formatTemperature(feelsLike) + "C" + "  #Sunrise: " + sunriseTime + "  #Sunset: " + sunsetTime;
    } else {
      scrollMessage = "Error: Failed to parse weather data";
    }
  } else {
    scrollMessage = "Error: Could not connect to weather service (" + String(weatherCode) + ")";
  }
  httpWeather.end();
}

// Function to update weather data
void updateData() {
  // Update scrolling message position
  scrollPosition--;
  if (scrollPosition < -450) scrollPosition = 180; // Changed -420 to -450 | 100 to 180

  // Update weather data every 5 minutes (300000 ms)
  if (millis() > lastUpdate + 300000) { 
    lastUpdate = millis();
    updatesCounter++;

    // Reset counter if it reaches 1000 (not enough space for 4 digits)
    if (updatesCounter >= 1000) {
      updatesCounter = 1;
    }

    getWeatherData();

    // Sync time with NTP
    setTime();

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

// Function to draw the display
void drawDisplay() {
  errSprite.fillSprite(greys[10]);
  errSprite.setTextColor(greys[1], greys[10]);
  errSprite.drawString(scrollMessage, scrollPosition, 4);

  sprite.fillSprite(TFT_BLACK);
  sprite.drawLine(138, 10, 138, 164, greys[6]);
  sprite.setTextDatum(0);

  // LEFTSIDE
  sprite.loadFont(midleFont);
  sprite.setTextColor(greys[1], TFT_BLACK);
  sprite.drawString("WEATHER", 6, 10);
  sprite.unloadFont();

  sprite.loadFont(font18);
  sprite.setTextColor(greys[7], TFT_BLACK);
  sprite.drawString("LOC:", 11, 110); // Shifted right 5px (from 6)
  sprite.setTextColor(greys[2], TFT_BLACK);
  if (units == "metric")
    sprite.drawString("C", 19, 50); // Shifted right 5px (from 14)
  if (units == "imperial")
    sprite.drawString("F", 19, 50); // Shifted right 5px (from 14)

  sprite.setTextColor(greys[3], TFT_BLACK);
  sprite.drawString(location, 45, 110); // Shifted right 5px (from 40)
  sprite.fillCircle(13, 52, 2, greys[2]); // Shifted right 5px (from 8)
  sprite.unloadFont();

  // draw time without seconds
  sprite.loadFont(tinyFont);
  sprite.setTextColor(greys[4], TFT_BLACK);
  sprite.drawString(rtc.getTime().substring(0, 5), 11, 132); // Shifted right 5px (from 6)
  sprite.unloadFont();

  // draw some static text
  sprite.setTextColor(greys[5], TFT_BLACK);
  sprite.drawString("INTERNET", 86, 10);
  sprite.drawString("STATION", 86, 20);
  
  sprite.setTextColor(greys[7], TFT_BLACK);
  sprite.drawString("SECONDS", 80, 157); // Shifted right 5px (from 75)

  // draw temperature
  sprite.setTextDatum(4);
  sprite.loadFont(bigFont);
  sprite.setTextColor(greys[0], TFT_BLACK);
  sprite.drawFloat(temperature, 1, 74, 80); // Shifted right 5px (from 69)
  sprite.unloadFont();

  // draw seconds rectangle
  sprite.fillRoundRect(80, 132, 42, 22, 2, greys[2]); // Shifted right 5px (from 75)
  // draw seconds
  sprite.loadFont(font18);
  sprite.setTextColor(TFT_BLACK, greys[2]);
  sprite.drawString(rtc.getTime().substring(6, 8), 101, 145); // Shifted right 5px (from 96)
  sprite.unloadFont();

  sprite.setTextDatum(0);

  // RIGHT SIDE
  sprite.loadFont(font18);
  sprite.setTextColor(greys[1], TFT_BLACK);
  sprite.drawString("LAST 12 HOURS", 144, 10);
  sprite.unloadFont();

  sprite.fillRect(144, 28, 84, 2, greys[10]);

  sprite.setTextColor(greys[3], TFT_BLACK);
  if (units == "metric") {
    sprite.drawString("MIN:" + String(minTemp) + "C", 252, 10);
    sprite.drawString("MAX:" + String(maxTemp) + "C", 252, 20);
  }
  if (units == "imperial") {
    sprite.drawString("MIN:" + String(minTemp) + "F", 252, 10);
    sprite.drawString("MAX:" + String(maxTemp) + "F", 252, 20);
  }
  sprite.fillSmoothRoundRect(144, 34, 174, 60, 3, greys[10], bck);
  sprite.drawLine(170, 39, 170, 88, TFT_WHITE);
  sprite.drawLine(170, 88, 314, 88, TFT_WHITE);

  sprite.setTextDatum(4);

  for (int j = 0; j < 24; j++)
    for (int i = 0; i < tempHistoryGraph[j]; i++)
      sprite.fillRect(173 + (j * 6), 83 - (i * 4), 4, 3, greys[2]);

  sprite.setTextColor(greys[2], greys[10]);
  sprite.drawString("MAX", 158, 42);
  sprite.drawString("MIN", 158, 86);

  sprite.loadFont(font18);
  sprite.setTextColor(greys[7], greys[10]);
  sprite.drawString("T", 158, 65); // Changed from 58 to 65
  sprite.unloadFont();

  for (int i = 0; i < 3; i++) {
    sprite.fillSmoothRoundRect(144 + (i * 60), 100, 54, 32, 3, greys[9], bck);
    sprite.setTextColor(greys[3], greys[9]);
    sprite.drawString(dataLabel[i], 144 + (i * 60) + 27, 107);
    sprite.setTextColor(greys[2], greys[9]);
    sprite.loadFont(font18);
    sprite.drawString(String((int)weatherMetrics[i])+dataLabelUnits[i], 144 + (i * 60) + 27, 124);
    sprite.unloadFont();

    sprite.fillSmoothRoundRect(144, 148, 174, 16, 2, greys[10], bck);
    errSprite.pushToSprite(&sprite, 148, 150);
  }

  sprite.setTextColor(greys[4], bck);
  sprite.drawString("CURRENT INFO", 182, 142); // Changed from 141 to 142
  sprite.setTextColor(greys[7], bck); // Changed from greys[9] to greys[7]
  sprite.drawString("UPDATES:", 272, 142); // Changed from 277 to 272 | 141 to 142
  sprite.drawString(String(updatesCounter), 300, 142); // Changed from 305 to 300 | 141 to 142

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
  
  // Display WiFi connection message
  lcd.println("\nConnecting to WiFi - please wait...");
  
  // Configure WiFiManager
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(10); // 10 second timeout for initial connection
  wifiManager.setConnectTimeout(10); // 10 second connection timeout
  
  // Attempt WiFi connection
  if (!wifiManager.autoConnect("T-Display-S3", "123456789")) {
    lcd.println("\nConnection timed out!");
    lcd.println("\nA WiFi network has been created:");
    lcd.println("SSID: T-Display-S3");
    lcd.println("Password: 123456789");
    lcd.println("\nConnect to it and navigate to:");
    lcd.println("192.168.4.1");
    lcd.println("in a browser to setup your WiFi.");
    
    // Start configuration portal
    wifiManager.setConfigPortalTimeout(0); // keep portal open indefinitely
    wifiManager.startConfigPortal("T-DisplayS3", "123456789");
    
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
      lcd.println("Check internet connection and restart to try again.");
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
  lcd.println("\nFetching weather data...");
  
  // Attempt to fetch location data
  if (!getLocationCords()) {
    lcd.println("\nFailed to get location!");
    lcd.println("Check location name in the code and restart to try again.");
    while(1); // halt execution
  }
  getWeatherData();

  // Weather data fetch complete
  lcd.println("Weather data received!");
  
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
  updateData();
  drawDisplay();
}
