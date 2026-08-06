#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <driver/touch_pad.h>
#include <Wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Buzzer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

/* PINOUT
38 - buzzer
5 - SDA
6 - SCL
*/

// OLED definitions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#define SDA 5
#define SCL 6
char buffer[32];

// logo definition
const unsigned char logo[] PROGMEM = {
    0x80, 0x40, 0xbf, 0x40, 0xb3, 0x40, 0xb3, 0x40, 0xb3, 0x40, 0xa1, 0x40, 0xa1, 0x40, 0xbf, 0x40,
    0xde, 0xc0, 0xe1, 0xc0};

// Buzzer definitions
Buzzer buzzer(38);

// toggle button
bool button_state = false;
int button = 21;
volatile unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;

// function prototypes
void turnOn();
void turnOff();
void updateWeather();
void drawSineWave(int y0, int height, int width);
void buttonToggle();

// weather API deifnitions
const float lat = 41.7143938;
const float lon = -88.0181318;
String serverPath = "https://api.open-meteo.com/v1/forecast?latitude=41.7143938&longitude=-88.0181318&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&temperature_unit=fahrenheit&forecast_days=1";
const unsigned long timerDelay = 60000;
int lastWeather = 0;
int timeUpdate = 0;
HTTPClient http;
char currentTemp[8];
char lowTemp[8];
char highTemp[8];

// neopixel definitions
#define LED_PIN 48
#define LED_COUNT 1
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// bluetooth definitions
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#define SERVICE_UUID "4fa87c0d-1234-4a2e-afe1-c746357d0d64"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
volatile bool toggle = false;
bool deviceConnected = false;
const unsigned long timeout = 100;
volatile unsigned long lastPacketTime = 0;

// wifi definitions
const char *ntpServer = "us.pool.ntp.org";
const long gmtOffset_sec = -3600 * 6;
const int daylightOffset_sec = 3600;
struct tm timeinfo;
int hour;
int adjustedHour;
int minute;
int seconds;
unsigned long lastUpdate = 0;

// machine state enum
enum MachineStates
{
  ON,
  OFF
};
MachineStates currentState = OFF;

// Callback class to handle data written from the phone app
class MyCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string value = pCharacteristic->getValue();

    if (value.length() > 0)
    {
      Serial.print("Received Value: ");
      for (int i = 0; i < value.length(); i++)
      {
        Serial.print(value[i]);
      }
      Serial.println();

      if (value == "1")
      {
        toggle = !toggle;
        if (toggle)
        {
          turnOn();
        }
        else
        {
          turnOff();
        }
      }
    }
  }
};

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
    // Serial.println("Phone connected!");
  };

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
    toggle = false;
    Serial.println("Phone disconnected. Restarting advertising...");
    // Restart advertising so it can be discovered again
    BLEDevice::startAdvertising();
  }
};

void setup()
{
  Serial.begin(115200);
  delay(1000);
  pinMode(47, OUTPUT);
  digitalWrite(47, HIGH);
  strip.begin();
  strip.show();

  WiFi.begin("Pach", "artek122!");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
  }
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // screen itinialization
  Wire.begin(SDA, SCL);
  while (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println(F("SSD1306 allocation failed, trying again"));
    delay(100);
  }
  display.clearDisplay();
  display.display();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.ssd1306_command(0xF0);

  // bluetooth initialization
  BLEDevice::init("fart cannon");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
  pCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // functions that help with iPhone connections
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  strip.setPixelColor(0, 0, 255, 0);
  strip.show();
  delay(500);
  strip.setPixelColor(0, 0, 0, 0);
  strip.show();

  updateWeather();
  turnOn();
  toggle = 1;

  // button initialization
  pinMode(button, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(button), buttonToggle, RISING);
}

void loop()
{

  if (button_state)
  {
    button_state = false;
    if (toggle)
    {
      turnOff();
    }
    else
    {
      turnOn();
    }
    toggle = !toggle;
  }
  if (millis() - timeUpdate >= 1000)
  {
    timeUpdate = millis();
    getLocalTime(&timeinfo);
    hour = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
    seconds = timeinfo.tm_sec;
    adjustedHour = hour % 12;
  }

  switch (currentState)
  {
  case ON:

    if (millis() - lastWeather >= timerDelay)
    {
      lastWeather = millis();
      updateWeather();
    }

    if (millis() - lastUpdate >= 1000)
    {
      lastUpdate = millis();
      display.clearDisplay();

      if (adjustedHour == 0)
      {
        adjustedHour = 12;
      }

      if (minute < 10.0)
      {
        // Serial.printf("%i:%i%i:%i", (int)adjustedHour, 0, (int)minute, seconds);
        sprintf(buffer, "%i:%i%i", (int)adjustedHour, 0, (int)minute);
      }
      else if (minute >= 10.0)
      {
        // Serial.printf("%i:%i:%i", (int)adjustedHour, (int)minute, seconds);
        sprintf(buffer, "%i:%i", (int)adjustedHour, (int)minute);
      }

      if (hour >= 12)
      {
        // Serial.println(" PM");
        sprintf(buffer + strlen(buffer), " PM");
      }
      else
      {
        // Serial.println(" AM");
        sprintf(buffer + strlen(buffer), " AM");
      }
      display.setTextSize(2);
      display.setCursor(2, 2);
      display.print(buffer);

      // current temperature display
      int16_t x1, y1;
      uint16_t w1, h1;
      display.setCursor(2, 20);
      display.setTextSize(2);
      display.print(currentTemp);
      display.print((char)247);
      display.print("F");
      display.getTextBounds(currentTemp, 2, 20, &x1, &y1, &w1, &h1);
      display.drawRect(0, 18, (w1 + 2 + 24), (h1 + 2), SSD1306_WHITE);

      // high temperature display
      display.setTextSize(2);
      display.setCursor(55, 20);
      display.print("HI:");
      display.print(highTemp);
      display.print((char)247);
      // display.print("F");

      // low temperature display
      display.setTextSize(2);
      display.setCursor(55, 40);
      display.print("LO:");
      display.print(lowTemp);
      display.print((char)247);
      // display.print("F");
    }

    // add borders
    int16_t x, y;
    uint16_t w, h;
    display.getTextBounds(buffer, 1, 2, &x, &y, &w, &h);
    display.drawRect(0, 0, 100, (h + 2), SSD1306_WHITE);
    display.fillRect(0, (h + 2) - 2, 128, 3, SSD1306_WHITE);
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);

    // add icon in top right corner
    // display.drawBitmap(100, 2, logo, 10, 10, SSD1306_WHITE);

    drawSineWave(8, 6, 27);
    display.display();

    break;
  case OFF:
    // Serial.println("Machine is OFF");
    delay(200);
    break;
  }

  if (hour == 6 && minute == 0 && seconds == 0)
  {
    currentState = ON;
    buzzer.sound(NOTE_E5, 100);
    buzzer.sound(NOTE_F5, 100);
    buzzer.sound(NOTE_B6, 1000);
    delay(1000);
    buzzer.sound(NOTE_E5, 100);
    buzzer.sound(NOTE_F5, 100);
    buzzer.sound(NOTE_B6, 1000);
    delay(1000);
    buzzer.sound(NOTE_E5, 100);
    buzzer.sound(NOTE_F5, 100);
    buzzer.sound(NOTE_B6, 1000);
  }
}

void updateWeather()
{
  http.begin(serverPath);
  int httpResponseCode = http.GET();

  if (httpResponseCode == HTTP_CODE_OK)
  {
    String payload = http.getString();
    JsonDocument doc;
    deserializeJson(doc, payload);

    // Current Temperature
    float tempCurrent = doc["current"]["temperature_2m"];
    sprintf(currentTemp, "%.0f", tempCurrent);

    // Today's High & Low (First element in the daily arrays)
    float tempMax = doc["daily"]["temperature_2m_max"][0];
    float tempMin = doc["daily"]["temperature_2m_min"][0];
    sprintf(highTemp, "%.0f", tempMax);
    sprintf(lowTemp, "%.0f", tempMin);

    // weather code
    int weatherCode = doc["current"]["weather_code"];
  }

  http.end();
}

void drawSineWave(int y0, int height, int width) // generated by chatGPT not gonna lie
{
  static float phase = 0;   // scroll offset
  const float freq = 0.30;  // wave frequency
  const float amp = height; // wave amplitude

  // Erase only the region we draw
  display.fillRect(100, y0 - height - 1, width, (height * 2) + 2, 0);

  // Draw sine wave in this band
  for (int x = 0; x < width; x++)
  {
    float rad = (x + phase) * freq;
    int y = y0 + sin(rad) * amp;
    display.drawPixel(x + 100, y, SSD1306_WHITE);
  }

  phase += 1; // makes the wave scroll
}

void turnOn()
{
  display.ssd1306_command(SSD1306_DISPLAYON);
  display.setCursor(0,0);
  currentState = ON;
  strip.setPixelColor(0, 0, 30, 0);
  strip.show();
  updateWeather();
  buzzer.sound(NOTE_E5, 100);
  buzzer.sound(NOTE_F5, 100);
  buzzer.sound(NOTE_G5, 100);
}

void turnOff()
{
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  currentState = OFF;
  strip.setPixelColor(0, 0, 0, 0);
  strip.show();
  display.clearDisplay();
  display.display();
  buzzer.sound(NOTE_G5, 100);
  buzzer.sound(NOTE_F5, 100);
  buzzer.sound(NOTE_E5, 100);
}

void buttonToggle()
{
  
  unsigned long currentTime = millis();
  if (currentTime - lastDebounceTime >= debounceDelay)
  {
    lastDebounceTime = currentTime;
    button_state = true;
  }
    

}