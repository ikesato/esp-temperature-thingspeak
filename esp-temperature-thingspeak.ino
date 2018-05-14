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


DHT dht(DHT_PIN, DHT11, 30);
Ticker ticker;
unsigned long lastTime = 0;


// thingspeak
const char* THING_SPEAK_HOST = "api.thingspeak.com";
const char* THING_SPEAK_KEY = "9NFQXTZK1KP3WS1U";


void deepSleep() {
    Serial.println("zzz...");
    ESP.deepSleep(60 * 1000 * 1000, WAKE_RF_DEFAULT);
}

void pushData() {
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
        Serial.println("Failed to read from DHT sensor!");
        return;
    }

    WiFiClient client;
    const int port = 80;
    if (!client.connect(THING_SPEAK_HOST, port)) {
        Serial.println("Failed to connect server");
        return;
    }

    String temp = String(t);
    String humidity = String(h);

    String url = "/update?key=";
    url += THING_SPEAK_KEY;
    url += "&field1=";
    url += temp;
    url += "&field2=";
    url += humidity;

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

    Serial.print("Pushed to thingspeak!");
}


void blink() {
    static int state = 1;
    digitalWrite(LED_PIN, state);
    state = !state;
}

void setup() {
    Serial.begin(115200);

    dht.begin();
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT);

    // setup WiFi
    WiFiManager wifiManager;
    Serial.println(digitalRead(BUTTON_PIN));
    if (digitalRead(BUTTON_PIN) == LOW) {
        Serial.println("reset settings!!!");
        wifiManager.resetSettings();
        Ticker t;
        t.attach_ms(100, blink);
        delay(3000);
        ESP.restart();
        return;
    }
    ticker.attach_ms(500, blink);
    wifiManager.autoConnect("ESP-TEMPERATURE");
    Serial.println("connected...yeey :)");
    ticker.detach();
    digitalWrite(LED_PIN, 1);
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

#if ENABLE_SLEEP == 1
    pushData();
    delay(1000);
    deepSleep();
#else
    unsigned long now = millis();
    if (now - lastTime >= PUSH_INTERVAL) {
        pushData();
        lastTime = now;
    }
#endif
}
