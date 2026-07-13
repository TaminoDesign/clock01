// MOTAT Desk Clock - ESP32 + WeAct 3.7" B&W e-paper (GDEY037T03)
// Features: NTP time, LED strip, light button, buzzer alarm, pot alarm set, snooze button, alarm LED
//
// ---- E-paper wiring ----
// VCC->3V3, GND->GND, DIN->D23, CLK->D18, CS->D5, DC->D26, RST->D27, BUSY->D4
//
// ---- LED strip ----
// VCC->VIN, GND->GND, DIN->D13
//
// ---- Light button ----  D14 + GND
// ---- Snooze button ----  D33 + GND
// ---- Buzzer ----  +->D25, -->GND
// ---- Alarm LED ----  +->D32, -->GND
// ---- Potentiometer ----  Left->GND, Middle->D34, Right->3V3

#include <GxEPD2_BW.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "time.h"
#include <FastLED.h>

// ---- WiFi credentials ----
const char* ssid     = "Wifi_name";
const char* password = "Wifi_password";


// ---- NTP settings (New Zealand) ----
const char* ntpServer          = "pool.ntp.org";
const long  gmtOffset_sec      = 12 * 3600;
const int   daylightOffset_sec = 0;

// ---- Open-Meteo API (NZ) ---- Town taken away for privacy
const char* weatherURL = "https://api.open-meteo.com/v1/forecast";

// ---- E-paper pins ----
#define CS_PIN   5
#define DC_PIN   26
#define RES_PIN  27
#define BUSY_PIN 4

// ---- LED strip ----
#define LED_PIN   13
#define NUM_LEDS  10
CRGB leds[NUM_LEDS];
bool ledsOn = true;

// ---- Buttons ----
#define LIGHT_BUTTON_PIN  14
#define SNOOZE_BUTTON_PIN 33

// ---- Buzzer ----
#define BUZZER_PIN 25

// ---- Alarm LED ----
#define ALARM_LED_PIN 32

// ---- Potentiometer ----
#define POT_PIN 34

// ---- Layout ----
#define SCREEN_W  416
#define SCREEN_H  240
#define DIVIDER_X 195
#define LEFT_W    185
#define RIGHT_X   203
#define PADDING   8

// ---- Weather state ----
struct DayForecast {
  char day[4];   // e.g. "Mon"
  int  tempMax;
  char condition[12]; // e.g. "Sunny"
};
DayForecast forecasts[4];
bool weatherLoaded = false;
unsigned long lastWeatherFetch = 0;
#define WEATHER_INTERVAL 3600000 // fetch every 1 hour

// ---- Alarm state ----
bool alarmRinging  = false;
int  alarmHour     = -1;
bool alarmIsAM     = true;
int  lastAlarmHour = -2;
unsigned long popupShownAt = 0;
bool showingPopup  = false;

GxEPD2_BW<GxEPD2_370_GDEY037T03, GxEPD2_370_GDEY037T03::HEIGHT> display(
  GxEPD2_370_GDEY037T03(CS_PIN, DC_PIN, RES_PIN, BUSY_PIN)
);

int lastMinute = -1;

// ---- WMO weather code to short description ----
const char* weatherCodeToString(int code) {
  if (code == 0)            return "Sunny";
  if (code <= 2)            return "P.Cloudy";
  if (code == 3)            return "Cloudy";
  if (code <= 49)           return "Foggy";
  if (code <= 59)           return "Drizzle";
  if (code <= 69)           return "Rainy";
  if (code <= 79)           return "Snowy";
  if (code <= 84)           return "Showers";
  if (code <= 99)           return "Stormy";
  return "Unknown";
}

// ---- Fetch weather from Open-Meteo ----
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(weatherURL);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      JsonArray times    = doc["daily"]["time"];
      JsonArray temps    = doc["daily"]["temperature_2m_max"];
      JsonArray codes    = doc["daily"]["weathercode"];

      // Day name lookup
      const char* dayNames[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

      for (int i = 0; i < 4; i++) {
        // Parse date string "YYYY-MM-DD"
        String dateStr = times[i].as<String>();
        struct tm t = {};
        strptime(dateStr.c_str(), "%Y-%m-%d", &t);
        mktime(&t);
        strncpy(forecasts[i].day, dayNames[t.tm_wday], 3);
        forecasts[i].day[3] = '\0';
        forecasts[i].tempMax = (int)round(temps[i].as<float>());
        strncpy(forecasts[i].condition, weatherCodeToString(codes[i].as<int>()), 11);
        forecasts[i].condition[11] = '\0';
      }
      weatherLoaded = true;
      lastWeatherFetch = millis();
      Serial.println("Weather fetched OK");
    }
  } else {
    Serial.printf("Weather fetch failed: %d\n", httpCode);
  }
  http.end();
}

// ---- Read alarm hour from potentiometer ----
int readAlarmHour() {
  int raw = analogRead(POT_PIN);
  if (raw < 200) return -1;
  return map(raw, 200, 4095, 1, 12);
}

// ---- Note frequencies ----
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define REST     0

// Ode to Joy melody
int melody[] = {
  NOTE_E4, NOTE_E4, NOTE_F4, NOTE_G4,
  NOTE_G4, NOTE_F4, NOTE_E4, NOTE_D4,
  NOTE_C4, NOTE_C4, NOTE_D4, NOTE_E4,
  NOTE_E4, NOTE_D4, NOTE_D4
};
int noteDurations[] = {
  4, 4, 4, 4,
  4, 4, 4, 4,
  4, 4, 4, 4,
  4, 2, 2
};

void playAlarmTone() {
  for (int i = 0; i < 15; i++) {
    int duration = 1000 / noteDurations[i];
    if (melody[i] == REST) {
      delay(duration);
    } else {
      tone(BUZZER_PIN, melody[i], duration);
      delay(duration * 1.3);
    }
    noTone(BUZZER_PIN);
  }
  delay(500); // pause between repeats
}

// ---- Draw weather panel ----
void drawWeatherPanel() {
  display.setFont(&FreeMono9pt7b);
  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK);

  if (!weatherLoaded) {
    display.setCursor(RIGHT_X + PADDING, 50);
    display.print("Loading");
    display.setCursor(RIGHT_X + PADDING, 68);
    display.print("weather...");
    return;
  }

  // Title
  display.setFont(&FreeMonoBold12pt7b);
  display.setCursor(RIGHT_X + PADDING, 25);
  display.print("Forecast");
  display.drawFastHLine(RIGHT_X + PADDING, 30, 170, GxEPD_BLACK);

  // 4 day forecast
  display.setFont(&FreeMono9pt7b);
  for (int i = 0; i < 4; i++) {
    char line[24];
    snprintf(line, sizeof(line), "%s %d'C %s",
             forecasts[i].day,
             forecasts[i].tempMax,
             forecasts[i].condition);
    display.setCursor(RIGHT_X + PADDING, 55 + i * 44);
    display.print(line);
  }
}

// ---- Show alarm popup ----
void showAlarmPopup(int hour, bool isAM) {
  char popupStr[20];
  if (hour == -1) {
    strcpy(popupStr, "Alarm: OFF");
  } else {
    snprintf(popupStr, sizeof(popupStr), "%d:00 %s", hour, isAM ? "AM" : "PM");
  }

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    int boxX = 60, boxY = 60, boxW = 296, boxH = 120;
    display.fillRect(boxX, boxY, boxW, boxH, GxEPD_BLACK);
    display.fillRect(boxX + 3, boxY + 3, boxW - 6, boxH - 6, GxEPD_WHITE);

    // Title
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(1);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds("ALARM SET", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(boxX + (boxW - w) / 2, boxY + 35);
    display.print("ALARM SET");

    // Time
    display.setFont(&FreeMonoBold24pt7b);
    display.getTextBounds(popupStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(boxX + (boxW - w) / 2, boxY + 90);
    display.print(popupStr);

    // Hint for AM/PM toggle
    if (hour != -1) {
      display.setFont(&FreeMono9pt7b);
      display.setTextSize(1);
      display.getTextBounds("Press snooze to toggle AM/PM", 0, 0, &x1, &y1, &w, &h);
      display.setCursor(boxX + (boxW - w) / 2, boxY + 112);
      display.print("Press snooze: toggle AM/PM");
    }

  } while (display.nextPage());

  showingPopup = true;
  popupShownAt = millis();
  lastMinute   = -1;
}

void displayTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time");
    return;
  }

  if (timeinfo.tm_min == lastMinute) return;
  lastMinute = timeinfo.tm_min;

  char hourStr[3];
  char minStr[3];
  char dateStr[12];
  strftime(hourStr, sizeof(hourStr), "%I", &timeinfo);
  strftime(minStr,  sizeof(minStr),  "%M", &timeinfo);
  strftime(dateStr, sizeof(dateStr), "%a %d %b", &timeinfo);

  // Check alarm trigger
  int currentHour12 = timeinfo.tm_hour % 12;
  if (currentHour12 == 0) currentHour12 = 12;
  bool currentIsAM = timeinfo.tm_hour < 12;
  if (alarmHour != -1 && alarmHour == currentHour12
      && alarmIsAM == currentIsAM && timeinfo.tm_min == 0) {
    alarmRinging = true;
  }

  Serial.printf("Updating: %s:%s | Alarm: %d %s\n",
                hourStr, minStr, alarmHour, alarmIsAM ? "AM" : "PM");

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Vertical divider
    display.drawFastVLine(DIVIDER_X, 10, SCREEN_H - 20, GxEPD_BLACK);

    // ---- LEFT PANEL: Time + Date ----
    display.setFont(&FreeMonoBold24pt7b);
    display.setTextSize(2);
    display.setTextColor(GxEPD_BLACK);

    int16_t x1, y1; uint16_t w, h;

    display.getTextBounds(hourStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((LEFT_W - w) / 2, 95);
    display.print(hourStr);

    display.getTextBounds(minStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((LEFT_W - w) / 2, 185);
    display.print(minStr);

    display.setTextSize(1);
    display.setFont(&FreeMonoBold12pt7b);
    display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((LEFT_W - w) / 2, 228);
    display.print(dateStr);

    // ---- RIGHT PANEL: Weather ----
    drawWeatherPanel();

  } while (display.nextPage());

  Serial.println("Display updated");
}

void setup() {
  Serial.begin(115200);
  Serial.println("Clock starting...");

  // ---- Init LEDs (dark purple) ----
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(220);
  fill_solid(leds, NUM_LEDS, CRGB(80, 0, 120));
  FastLED.show();

  // ---- Init pins ----
  pinMode(LIGHT_BUTTON_PIN,  INPUT_PULLUP);
  pinMode(SNOOZE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN,        OUTPUT);
  pinMode(ALARM_LED_PIN,     OUTPUT);
  digitalWrite(ALARM_LED_PIN, LOW);

  // ---- Init display ----
  display.init(115200, true, 20, false);
  display.setRotation(1);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(130, 110);
    display.print("Connecting");
    display.setCursor(120, 140);
    display.print("to WiFi...");
  } while (display.nextPage());

  // ---- Connect WiFi ----
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected!");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Time synced");

  fetchWeather();
  displayTime();
}

void loop() {
  static unsigned long lastDebounce       = 0;
  static unsigned long lastSnoozeDebounce = 0;
  static bool lastLightState  = HIGH;
  static bool lastSnoozeState = HIGH;

  // ---- Light button ----
  bool lightState = digitalRead(LIGHT_BUTTON_PIN);
  if (lightState == LOW && lastLightState == HIGH && millis() - lastDebounce > 200) {
    ledsOn = !ledsOn;
    fill_solid(leds, NUM_LEDS, ledsOn ? CRGB(80, 0, 120) : CRGB(0, 0, 0));
    FastLED.show();
    lastDebounce = millis();
  }
  lastLightState = lightState;

  // ---- Snooze button ----
  bool snoozeState = digitalRead(SNOOZE_BUTTON_PIN);
  if (snoozeState == LOW && lastSnoozeState == HIGH && millis() - lastSnoozeDebounce > 200) {
    if (alarmRinging) {
      // Dismiss alarm
      alarmRinging = false;
      noTone(BUZZER_PIN);
      Serial.println("Alarm dismissed");
    } else if (showingPopup && alarmHour != -1) {
      // Toggle AM/PM while popup is showing
      alarmIsAM = !alarmIsAM;
      Serial.printf("AM/PM toggled: %s\n", alarmIsAM ? "AM" : "PM");
      showAlarmPopup(alarmHour, alarmIsAM);
    }
    lastSnoozeDebounce = millis();
  }
  lastSnoozeState = snoozeState;

  // ---- Alarm ringing ----
  if (alarmRinging) {
    playAlarmTone();
  }

  // ---- Read pot and show popup if changed ----
  int newAlarmHour = readAlarmHour();
  if (newAlarmHour != lastAlarmHour) {
    alarmHour     = newAlarmHour;
    lastAlarmHour = newAlarmHour;
    digitalWrite(ALARM_LED_PIN, alarmHour != -1 ? HIGH : LOW);
    showAlarmPopup(alarmHour, alarmIsAM);
  }

  // ---- Clear popup after 6 seconds ----
  if (showingPopup && millis() - popupShownAt > 6000) {
    showingPopup = false;
    displayTime();
  }

  // ---- Refresh weather every hour ----
  if (millis() - lastWeatherFetch > WEATHER_INTERVAL) {
    fetchWeather();
  }

  // ---- Update clock display ----
  if (!showingPopup) {
    displayTime();
  }

  delay(100);
}
