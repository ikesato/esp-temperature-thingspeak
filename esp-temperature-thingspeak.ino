#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <Ticker.h>
#include <DHT.h>

extern "C" {
#include "user_interface.h"
}

#define LED_PIN 4
#define BUTTON_PIN 14
#define DHT_PIN 5
#define PUSH_INTERVAL (60 * 1000)
#define ENABLE_SLEEP 1
#define PUSH_COUNT_MAX 10

DHT dht(DHT_PIN, DHT11, 30);
Ticker ticker;

struct {
    uint32_t crc32;
    uint32_t counter;
    float temperature;
    float humidity;
} rtcData;

uint32_t calculateCRC32(const uint8_t *data, size_t length);

// thingspeak
const char* THING_SPEAK_HOST = "api.thingspeak.com";
const char* THING_SPEAK_KEY = "9NFQXTZK1KP3WS1U";


void deepSleep() {
    delay(1000);
    Serial.println("zzz...");
    ESP.deepSleep(60 * 1000 * 1000, WAKE_RF_DEFAULT);
}

void pushData() {
    float h = rtcData.humidity;
    float t = rtcData.temperature;
    float thindex = 0.81 * t + 0.01 * h * (0.99 * t - 14.3) + 46.3;

    WiFiClient client;
    const int port = 80;
    if (!client.connect(THING_SPEAK_HOST, port)) {
        Serial.println("Failed to connect server");
        return;
    }

    String url = "/update?key=";
    url += THING_SPEAK_KEY;
    url += "&field1=";
    url += String(t);
    url += "&field2=";
    url += String(h);
    url += "&field3=";
    url += String(thindex);

    Serial.print("Requesting URL: ");
    Serial.println(url);

    // This will send the request to the server
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + THING_SPEAK_HOST + "\r\n" +
                 "Connection: close\r\n\r\n");
    delay(10);

    // Read all the lines of the reply from server and print them to Serial
    while (client.available()) {
        String line = client.readStringUntil('\r');
        Serial.print(line);
    }

    Serial.println("Pushed to thingspeak!");
}


void blink() {
    static int state = 1;
    digitalWrite(LED_PIN, state);
    state = !state;
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT);
    dht.begin();

    digitalWrite(LED_PIN, 1);

    // Read struct from RTC memory
    if (ESP.rtcUserMemoryRead(0, (uint32_t*)&rtcData, sizeof(rtcData))) {
        Serial.println("Read: ");
        uint32_t crcOfData = calculateCRC32(((uint8_t*) &rtcData) + 4, sizeof(rtcData) - 4);
        Serial.print("CRC32 of data: ");
        Serial.println(crcOfData, HEX);
        Serial.print("CRC32 read from RTC: ");
        Serial.println(rtcData.crc32, HEX);
        if (crcOfData != rtcData.crc32) {
            Serial.println("CRC32 in RTC memory doesn't match CRC32 of data.");
            memset(&rtcData, 0, sizeof(rtcData));
        } else {
            Serial.println("CRC32 check ok, data is probably valid.");
            Serial.print("h:");
            Serial.print(rtcData.humidity);
            Serial.print(" t:");
            Serial.print(rtcData.temperature);
            Serial.print(" counter:");
            Serial.println(rtcData.counter);
        }
    }

    // check wakeup reason
    String resetReason = ESP.getResetReason();
    if ( resetReason == "Deep-Sleep Wake" ) {
        rtcData.counter++;
    } else {
        rtcData.counter = PUSH_COUNT_MAX;
    }

    // read sensor values
    float prevh = rtcData.humidity;
    float prevt = rtcData.temperature;
    float h,t;
    for (int i=0; i<10; i++) {
        h = dht.readHumidity();
        t = dht.readTemperature();
        if (isnan(h) || isnan(t)) {
            Serial.println("Failed to read from DHT sensor!");
            if (i==10-1) {
                Serial.println("I gave up to read sensor.");
                deepSleep();
                return;
            }
            continue;
        }
        break;
    }
    rtcData.humidity = h;
    rtcData.temperature = t;
    Serial.print("Humidity: ");
    Serial.print(h);
    Serial.print("%\tTemperature: ");
    Serial.print(t);
    Serial.println("C");

    // check to push or not
    if (prevh == rtcData.humidity &&
        prevt == rtcData.temperature &&
        rtcData.counter < PUSH_COUNT_MAX) {
        // write RTC memoery
        rtcData.crc32 = calculateCRC32(((uint8_t*) &rtcData) + 4, sizeof(rtcData) - 4);
        ESP.rtcUserMemoryWrite(0, (uint32_t*)&rtcData, sizeof(rtcData));
        deepSleep();
        return;
    }

    // write RTC memoery
    rtcData.counter = 0;
    rtcData.crc32 = calculateCRC32(((uint8_t*) &rtcData) + 4, sizeof(rtcData) - 4);
    ESP.rtcUserMemoryWrite(0, (uint32_t*)&rtcData, sizeof(rtcData));

    // setup WiFi
    WiFiManager wifiManager;
    Serial.println(digitalRead(BUTTON_PIN));
    if (digitalRead(BUTTON_PIN) == LOW) {
        Serial.println("reset settings!!!");
        wifiManager.resetSettings();
        Ticker t;
        t.attach_ms(100, blink);
        delay(3000);
    }
    ticker.attach_ms(500, blink);
    wifiManager.setTimeout(180);
    wifiManager.autoConnect("ESP-TEMPERATURE");
    Serial.println("connected...yeey :)");
    ticker.detach();

    // push
    pushData();
    deepSleep();
}

void loop() {
    if (digitalRead(BUTTON_PIN) == LOW) {
        delay(100);

        float h = dht.readHumidity();
        float t = dht.readTemperature();
        if (isnan(h) || isnan(t)) {
            Serial.println("Failed to read from DHT sensor!");
        } else {
            Serial.print("Humidity: ");
            Serial.print(h);
            Serial.print("%\tTemperature: ");
            Serial.print(t);
            Serial.println("C");
        }
        return;
    }
}


uint32_t calculateCRC32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xffffffff;
    while (length--) {
        uint8_t c = *data++;
        for (uint32_t i = 0x80; i > 0; i >>= 1) {
            bool bit = crc & 0x80000000;
            if (c & i) {
                bit = !bit;
            }
            crc <<= 1;
            if (bit) {
                crc ^= 0x04c11db7;
            }
        }
    }
    return crc;
}
