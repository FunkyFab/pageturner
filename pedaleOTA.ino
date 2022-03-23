/**
   This sketch could be more simple but ...
   if the power consumption is too low, my battery stops.
   Hence, I've let the Wifi up, and used it for OTA updates.
   The RGB LED management for statuses adds also a bit of code.
   I wanted the pedals to be operationnals ASAP, without having to wait for the Wifi to be up
   (both features are unrelated), therefore I add some multitasking for that
*/
#include <Arduino.h>
#include <USBHIDKeyboard.h>
#include <USB.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>

#include "secrets.h"

//Multasking for starting the network => makes the device being ready in 500ms
#define MULTI_TASK

#define PEDAL1          33
#define PEDAL2          35
//Time for which we ignore two rising signals (debouncing)
#define IDLE_TIME       200


#define LED_PIN         18
#define DELAY_LED_OFF   1000
#define LED_BRIGHTNESS  3

USBHIDKeyboard Keyboard;
WiFiMulti wifiMulti;
Adafruit_NeoPixel pixels(1, LED_PIN, NEO_GRB + NEO_KHZ800);
long lastEvent;
byte previousRead1 = HIGH, previousRead2 = HIGH;

struct Pedal {
  const uint8_t PIN;
  bool pressed;
  long timestamp;
};

Pedal pedal1 = {PEDAL1, false, 0};
Pedal pedal2 = {PEDAL2, false, 0};


void ledRgb(byte red, byte green, byte blue) {
  pixels.setBrightness(LED_BRIGHTNESS);
  pixels.setPixelColor(0, red, green, blue);
  pixels.show();
}

void ledOff() {
  pixels.setBrightness(0);
  pixels.show();
}
//Called from setup() to initiate Wifi connection.
//Used only for OTA updates
void setupWifi() {
  wifiMulti.addAP(SSID_1, WIFI_PASS);
  wifiMulti.addAP(SSID_2, WIFI_PASS);
  Serial.print("\nConnecting Wifi...\n");
  ledRgb( 255, 0, 255);
  lastEvent = millis();
  while ((wifiMulti.run() != WL_CONNECTED)) {
    delay(500);
  }
  ledRgb(128, 255, 0);
  lastEvent = millis();
  Serial.println("Connected");
}
//Called from setup() to unable OTA update
void setupOTA() {
  const char *host = "ESP32-pedals";
  ArduinoOTA.setHostname(host);
  ArduinoOTA.setPassword(OTA_PASSWD);
  ArduinoOTA
  .onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
    ledRgb( 255, 255, 0);
    lastEvent = millis();
  })
  .onEnd([]() {
    Serial.println("\nEnd");
    ledRgb( 255, 255, 255);
    lastEvent = millis();
  })
  .onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    ledRgb( 255, 255, 128);
    lastEvent = millis();
  })
  .onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
}

boolean checkWifi() {
  ledRgb( 255, 255, 0);
  lastEvent = millis();
  WiFi.mode(WIFI_STA);
  Serial.println("Scanning Wifi");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No networks found");
  } else {
    for (int i = 0; i < n; ++i) {
      //Found proper Wifi SSID
      if ((String(SSID_1) == WiFi.SSID(i)) || (String(SSID_2) == WiFi.SSID(i))) {
        
        return true;
      }
    }
  }
  ledRgb( 255, 80, 0);
  lastEvent = millis();
  Serial.println("Wifi connection dropped");
  return false;
}

void manageNetworkTask(void *params) {
  if ( checkWifi()) {
    setupWifi();
    setupOTA();
#ifdef MULTI_TASK
    while (1) {
      //Handles OTA updates
      ArduinoOTA.handle();
      delay(100);
    }
#endif
  }
#ifdef MULTI_TASK
  else {
    //If no wifi, need to infinitely loop (a task must never end)
    while (1) {
      delay(1000);
    }
  }
#endif
}

void setupNetwork() {
#ifdef MULTI_TASK
  xTaskCreatePinnedToCore(
    manageNetworkTask
    ,  "setupNetwork"   // A name just for humans
    ,  2048  // This stack size can be checked & adjusted by reading the Stack Highwater
    ,  NULL
    ,  1  // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    ,  NULL
    ,  tskNO_AFFINITY);
#else
  manageNetworkTask(NULL);
#endif
}

void setupLed() {
  pixels.setBrightness(LED_BRIGHTNESS);
  pixels.begin();
  //Initially red
  ledRgb( 255, 0, 0);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Let's start");
  setupLed();
  USB.begin();
  Keyboard.begin();
  //Configuring the pin to be an input, with resistor pull-up enabled
  pinMode(PEDAL1, INPUT_PULLUP);
  //Configuring the pin to be an input, with resistor pull-up enabled;
  Serial.printf("Set pin %d as input\n", PEDAL2);
  pinMode(PEDAL2, INPUT_PULLUP);
  setupNetwork();
  ledOff();
}

void loop() {
  //Only capture HIGH->LOW transition
  byte currentRead1 = digitalRead(PEDAL1);
  byte currentRead2 = digitalRead(PEDAL2);
  long now = millis();
  if (( currentRead1 == LOW ) && (previousRead1 == HIGH) && ( now - pedal1.timestamp > IDLE_TIME ))  {
    pedal1.pressed = true;
    pedal1.timestamp = now;
  }
  if (( currentRead2 == LOW ) && (previousRead2 == HIGH) && ( now - pedal2.timestamp > IDLE_TIME ))  {
    pedal2.pressed = true;
    pedal2.timestamp = now;
  }
  if ( pedal1.pressed && pedal2.pressed) {
    pedal1.pressed = false;
    pedal2.pressed = false;
    ledRgb( 255, 0, 0);
    lastEvent = now;
  } else {
    if (pedal1.pressed) {
      pedal1.pressed = false;
      ledRgb( 0, 255, 0);
      lastEvent = now;
      Keyboard.write(KEY_RIGHT_ARROW);
    }
    if (pedal2.pressed) {
      pedal2.pressed = false;
      ledRgb( 0, 0, 255);
      lastEvent = now;
      Keyboard.write(KEY_LEFT_ARROW);
    }
  }
  if ( millis() - lastEvent > DELAY_LED_OFF ) {
    ledOff();
  }
  previousRead1 = currentRead1;
  previousRead2 = currentRead2;
#ifndef MULTI_TASK
  ArduinoOTA.handle();
#endif
  delay(10);
}
