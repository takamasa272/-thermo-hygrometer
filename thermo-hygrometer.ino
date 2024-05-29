#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "wifi_credentials.h"     // SSID and Password
#include "ambient_credentials.h"  // keys for ambient
#include "gas_credentials.h"      // keys for Google Apps Script

#include <Adafruit_GFX.h>     // Core graphics library
#include <Adafruit_ST7735.h>  // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h>  // Hardware-specific library for ST7789
#include <SPI.h>

#include <CRC8.h>  // for AHT25
#include <Wire.h>  // for AHT25

#include <Ambient.h>

/* FOR OLED */
// #if defined(ARDUINO_FEATHER_ESP32) // Feather Huzzah32
#define TFT_CS 25
#define TFT_RST 27
#define TFT_DC 26

// OPTION 2 lets you interface the display using ANY TWO or THREE PINS,
// tradeoff being that performance is not as fast as hardware SPI above.
#define TFT_MOSI 14  // Data out
#define TFT_SCLK 13  // Clock out

/* ENABLE/DISABLE FEATURES */
// send data to ambient
const bool ENABLE_AMBIENT = true;
// send data to google apps script
const bool ENABLE_GAS = true;

/* FOR AHT25 */
const int PIN_I2C_SDA = 21;
const int PIN_I2C_SCL = 22;
// const int PIN_I2C_SDA = 16;
// const int PIN_I2C_SCL = 17;
const byte AHT25_ADDR = 0x38;
const double ERROR_VALUE = 999.0;
CRC8 crc;
double temperature = 0.0;
double humidity = 0.0;

// wifi info (from wifi_credentials.h)
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;

/* FOR Ambient */
unsigned int channelId = AMBIENT_CHANNELID;  // From ambient_credentials.h
const char* writeKey = AMBIENT_WRITEKEY;     // From ambient_credentials.h
WiFiClient client;
Ambient ambient;

/* FOR Google Apps Script */
const String gasUrl = GAS_URL;  // From gas_credentials.h

// ntp
struct tm timeInfo;
const char* dayofweek[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
char year[4][6];
char date[20], hour_minute[20];

bool kougo;

// display
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

void initAht25(void) {
  delay(100);
  Wire.beginTransmission(AHT25_ADDR);
  Wire.write(0x71);
  Wire.endTransmission();
  delay(10);

  crc.setPolynome(0x31);
  crc.setStartXOR(0xFF);
}

void updateAht25(void) {
  byte buf[7];
  uint32_t humidity_raw;
  uint32_t temperature_raw;
  byte state;

  Wire.beginTransmission(AHT25_ADDR);
  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);
  Wire.endTransmission();
  do {
    delay(80);
    Wire.requestFrom(AHT25_ADDR, 7);
    if (Wire.available() >= 7) {
      for (int i = 0; i < 7; i++) {
        buf[i] = Wire.read();
      }
    }
  } while ((buf[0] & 0x80) != 0);

  crc.restart();
  crc.add(buf, 6);

  if (buf[6] == crc.getCRC()) {
    state = buf[0];
    humidity_raw = ((uint32_t)buf[1] << 12) | ((uint32_t)buf[2] << 4) | (((uint32_t)buf[3] & 0xF0) >> 4);
    temperature_raw = (((uint32_t)buf[3] & 0x0F) << 16) | ((uint32_t)buf[4] << 8) | ((uint32_t)buf[5]);

    humidity = humidity_raw / 1048576.0 * 100;
    temperature = temperature_raw / 1048576.0 * 200 - 50;
  } else {
    // error
    humidity = ERROR_VALUE;
    temperature = ERROR_VALUE;
  }
}

/* SHOW time and date */
void show_time(bool kougo) {
  getLocalTime(&timeInfo);  //tmオブジェクトのtimeInfoに現在時刻を入れ込む
  sprintf(year[0], "%04d/", timeInfo.tm_year + 1900);
  sprintf(date, "%02d/%02d", timeInfo.tm_mon + 1, timeInfo.tm_mday);  //日付に変換
  // 交互に. を表示したりけしたりする．
  if (kougo) {
    sprintf(hour_minute, "%02d:%02d.", timeInfo.tm_hour, timeInfo.tm_min);  //時間に変換
  } else {
    sprintf(hour_minute, "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);  //時間に変換
  }

  // fontsizes of values
  const int datesize = 3;

  tft.setCursor(0, 30);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(7);
  tft.println(hour_minute);

  tft.setTextSize(2);
  tft.println("\n[ Today ]");
  tft.print(year[0]);
  tft.setTextSize(datesize);
  tft.print(date);
  tft.print(" ");
  tft.println(dayofweek[timeInfo.tm_wday]);
}

void show_temp_humid(void) {
  // fontsizes of values
  const int datesize = 3;
  const int tempsize = 5;
  const int humidsize = 5;

  tft.setTextSize(2);
  tft.println("\n[ Temperature ]\n");
  tft.setTextSize(tempsize);
  if (temperature != ERROR_VALUE) {
    tft.print(temperature, 2);  // (temp, 小数点以下桁数)

    // display "℃"
    tft.print(" ");
    tft.setTextSize(tempsize - 2);
    tft.print("o");
    tft.setTextSize(tempsize);
    tft.println("C");
  } else {
    tft.print("ERROR");
  }

  tft.setTextSize(2);
  tft.println("\n[ Humidity ]\n");
  tft.setTextSize(humidsize);
  if (humidity != ERROR_VALUE) {
    tft.print(humidity, 2);
    tft.println(" %");
  } else {
    tft.print("ERROR");
  }
  
  // also print to serial
  Serial.print("[AHT25] temperature: ");
  Serial.print(temperature);
  Serial.print(", humidity: ");
  Serial.println(humidity);
}

void SendToAmbient(void) {
  ambient.set(1, (float)temperature);
  ambient.set(2, (float)humidity);

  ambient.send();
  Serial.println(" [AMBIENT] Data send to ambient");
}

void SendToGoogleApps(void) {
  //Google Spreadsheet
  String urlFinal = gasUrl + "?temperature=" + String(temperature) + "&humidity=" + String(humidity);

  HTTPClient http;
  http.begin(urlFinal.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = http.GET();
  Serial.print(" [GAS] HTTP Status Code: ");
  Serial.println(httpCode);
  //---------------------------------------------------------------------
  //getting response from google sheet
  String payload;
  if (httpCode > 0) {
    payload = http.getString();
    Serial.println(" [GAS] Payload: " + payload);
  }
  //---------------------------------------------------------------------
  http.end();
  Serial.println(" [GAS] Send data to Google SpreadSheet");
}

void setup(void) {
  Serial.begin(9600);
  Serial.print(F("Hello! ST77xx TFT Clock"));

  // OR use this initializer (uncomment) if using a 2.0" 320x240 TFT:
  tft.init(240, 320);  // Init ST7789 320x240

  // wifi connect
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println(" [Wi-Fi] Connecting to WiFi..");
  }
  Serial.print(" [Wi-Fi] Connected to the WiFi network, IP: ");
  Serial.println(WiFi.localIP());

  // NTP setting
  configTime(9 * 3600L, 0, "ntp.nict.jp", "time.google.com", "ntp.jst.mfeed.ad.jp");

  Serial.println(" [NTP] NTP has configured");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  initAht25();

  Serial.println(" [AHT25] AHT25 has configured");

  //  チャネルIDとライトキーを指定してAmbientの初期化
  if (ENABLE_AMBIENT) ambient.begin(channelId, writeKey, &client);

  Serial.println(F("<< Initialized >>"));
}

void loop() {
  // time
  if ((WiFi.status() == WL_CONNECTED)) {
    // measure Temperature and humidity
    updateAht25();
    Serial.println("[AHT25] Measured");

    delay(30);

    tft.setTextWrap(false);
    // clear display
    Serial.println("[ FILL with BLACK ]");
    tft.fillScreen(ST77XX_BLACK);

    // wifi connected
    long rssi = WiFi.RSSI();  // rcvd sig strength indicator [dBm]
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(0, 0);
    tft.setTextSize(2);
    tft.print("Wi-Fi [");
    tft.print(rssi);
    tft.println(" dBm]");
    Serial.print("[Wi-Fi] RSSI: ");
    Serial.print(rssi);
    Serial.println("dBm");

    // time and date
    show_time(kougo);

    show_temp_humid();

    // send to ambient (temp. and humid.)
    // あんまりデータ送るとよくないので，2回に1回にする．
    if (kougo) {
      if (ENABLE_AMBIENT) SendToAmbient();
      if (ENABLE_GAS) SendToGoogleApps();
    }

    kougo = kougo ? false : true;
    delay(29965);

  } else {
    Serial.println(F("Wi-Fi not connected"));
    tft.setTextWrap(false);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(0, 0);
    tft.setTextSize(2);
    tft.println("Wifi Disconnected");
  }
}
