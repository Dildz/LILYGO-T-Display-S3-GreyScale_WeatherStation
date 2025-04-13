# T-Display-S3 Weather Station

A weather display for the LilyGO T-Display-S3 featuring:
- Real-time weather data from OpenWeatherMap
- Temperature history graph
- Sunrise/sunset times
- NTP time synchronization
- Adjustable display brightness
- Performance monitoring (FPS counter)

## Features

- Current weather conditions with temperature, humidity, pressure, and wind speed
- 12-hour temperature history graph
- Sunrise and sunset times with automatic timezone adjustment
- Scrolling weather information display
- NTP time synchronization with configurable GMT offset
- Display brightness adjustment using hardware buttons
- Performance monitoring with real-time FPS counter
- Automatic weather data updates every 5 minutes
- WiFi configuration portal for easy setup

## Hardware Configuration
| Function      | GPIO Pin |
|---------------|----------|
| Left Button   | 0        |
| Right Button  | 14       |
| LCD Backlight | 38       |
| LCD Power     | 15       |

## Setup Instructions

1. **Edit Configuration**: Before uploading, edit these variables in the code:
   ```cpp
   int offsetGMT = 2; // Set your GMT offset
   String location = "CITY_NAME"; // Your city/town (London/New York/Sydney/etc)
   String countryCode = "CODE"; // Country code (GB/US/ZA/etc)
   String owmAPI = "YOUR_API_KEY"; // OpenWeatherMap API key
   String units = "metric"; // "metric" or "imperial"
   ```
2. **How to get a OWM API key**:
   - Register a free account on [openweathermap.org](https://openweathermap.org/)
   - Click on your account icon & find "My API keys"
   - Generate a new key, then copy and replace YOUR_API_KEY in the code

## Notes

- First build the project in PlatformIO to download the various libraries.
- In the ~.pio\libdeps\lilygo-t-display-s3\TFT_eSPI\User_Setup_Select.h file, make sure to:
  - **comment out** line 27 (#include <User_Setup.h>) and,
  - **uncomment** line 133 (#include <User_Setups/Setup206_LilyGo_T_Display_S3.h>)
- Only once the User_Setup_Select.h has been modified should the code be uploaded to the T-Display-S3.

## Credits

This project is inspired by [Volos Projects - tDisplayS3WeatherStation](https://github.com/VolosR/tDisplayS3WeatherStation)
