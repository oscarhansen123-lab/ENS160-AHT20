#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <ScioSense_ENS16x.h>
#include <SparkFun_Qwiic_Humidity_AHT20.h>
#include "esp_wifi.h"
#include "esp_sleep.h"

#define I2C_ADDRESS_ENS160 0x53
AHT20 humiditySensor;
ENS160 ens16x;

const char* ssid = "RASPBERRYNET";
const char* password = "VerySecret";

const char* mqtt_server = "b512d33fcbc8401cb8504a21cce778a1.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "AQreader";
const char* mqtt_password = "Eksamen2026";

WiFiClientSecure espClient; // secure client for MQTT over TLS
PubSubClient client(espClient); // MQTT client using the secure client
#define MSG_BUFFER_SIZE 256 // buffer size for JSON message payload
char msg[MSG_BUFFER_SIZE]; // buffer to hold the JSON message payload

// -----Timing constants-----
const uint32_t SEND_SLACK_SEC = 2;
const uint32_t MEASURE_INTERVAL_SEC = 120;
const uint32_t SEND_INTERVAL_SEC    = 600;
const uint32_t ENS160_STAB_MS       = 30000;
const uint32_t BOOT_ACTIVE_MS       = 40000;
const uint32_t MQTT_HOLD_MS         = 20000; 

// ----- RTC data to retain across deep sleep cycles ----
RTC_DATA_ATTR uint32_t secondsAccumulated = 0;
RTC_DATA_ATTR bool firstBootDone = false;
RTC_DATA_ATTR uint32_t activeAccumulatedSec = 0;
RTC_DATA_ATTR uint32_t lastSleepSec = 0;



RTC_DATA_ATTR float lastT = 0;
RTC_DATA_ATTR float lastH = 0;
RTC_DATA_ATTR uint8_t lastAqi = 0;
RTC_DATA_ATTR uint16_t lastTvoc = 0;
RTC_DATA_ATTR uint16_t lastEco2 = 0;

// ----- WiFi and MQTT setup and helper functions -----
void wifi_setup() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.begin(ssid, password);

    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt < 15000)) {
        delay(500);
    }
}

void wifi_off() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
}

void mqtt_setup() {
    espClient.setInsecure();
    client.setServer(mqtt_server, mqtt_port);
}

bool mqtt_connect() {
    if (!client.connected()) {
        return client.connect("ESP32Client", mqtt_user, mqtt_password);
    }
    return true;
}

// ----- Deep sleep helper function -----
void goDeepSleep(uint32_t seconds) {
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL); 
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    uint32_t awakeStart = millis(); // track how long we've been awake in this cycle

    // ----- Boot delay to allow for serial monitor connection and code upload -----
    if (!firstBootDone) {
        Serial.println("Boot vindue 40 sek (upload kode nu)...");
        delay(BOOT_ACTIVE_MS);
        firstBootDone = true;
    }

    secondsAccumulated += lastSleepSec; // add the sleep time from the last cycle to the accumulated active time

    mqtt_setup();
    Wire.begin(8, 9);

    // ----- Sensor initialization and first reading -----
    humiditySensor.begin(); 
    ens16x.begin(&Wire, I2C_ADDRESS_ENS160); 
    while (ens16x.init() != true) delay(500);
    ens16x.startStandardMeasure();

    delay(ENS160_STAB_MS);

    lastT = humiditySensor.getTemperature();
    lastH = humiditySensor.getHumidity();
    if (ens16x.update() == RESULT_OK && ens16x.hasNewData()) {
        lastAqi = (uint8_t)ens16x.getAirQualityIndex_UBA();
        lastTvoc = ens16x.getTvoc();
        lastEco2 = ens16x.getEco2();
    }

    // Calculate total awake time so far in this cycle (including the current active time)
    uint32_t awakeSecSoFar = (millis() - awakeStart) / 1000;
    uint32_t totalElapsedSec = secondsAccumulated + activeAccumulatedSec + awakeSecSoFar;
   

    // ----- Check if it's time to send data, and if so, connect to WiFi and MQTT, publish the data, and then disconnect -----
    bool sendOk = false; // track if we successfully sent data, so we can reset the accumulated time if we did
    if (totalElapsedSec >= SEND_INTERVAL_SEC) {
        wifi_setup();

        // Only attempt to connect to MQTT and send data if we successfully connected to WiFi
        if (WiFi.status() == WL_CONNECTED) {
            if (mqtt_connect()) {
                JsonDocument doc;
                doc["temperature"] = lastT;
                doc["humidity"] = lastH;
                doc["aqi"] = lastAqi;
                doc["tvoc"] = lastTvoc;
                doc["eco2"] = lastEco2;

                serializeJson(doc, msg);
                bool pubOk = client.publish("sensor/data", msg); // publish the data to the MQTT topic
                sendOk = pubOk; // track if we successfully published the data

                
                unsigned long holdStart = millis();
                while (millis() - holdStart < MQTT_HOLD_MS) {
                    client.loop();
                    delay(50);
                }
            }
        }

        wifi_off(); // turn off WiFi to save power

        // If we successfully sent data, reset the accumulated active time and sleep time, since we're starting a new send interval
        if (sendOk) {
            secondsAccumulated = 0;
            activeAccumulatedSec = 0;
            lastSleepSec = 0;
        }
    }

    uint32_t awakeSecTotal = (millis() - awakeStart) / 1000; // total awake time for this cycle (including the current active time)
    activeAccumulatedSec += awakeSecTotal; // add the total awake time for this cycle to the accumulated active time

    // Recalculate total elapsed time with the updated active accumulated time
    totalElapsedSec = secondsAccumulated + activeAccumulatedSec;

   // Calculate how much time is left until we reach the next send interval, and sleep for the smaller of that time or the measure interval
    uint32_t remainingToSend;
if (totalElapsedSec >= SEND_INTERVAL_SEC) {
    remainingToSend = MEASURE_INTERVAL_SEC;
} else {
    remainingToSend = SEND_INTERVAL_SEC - totalElapsedSec;

    // If the remaining time until the next send interval is less than or equal to the measure interval, add a small slack to avoid timing issues in the next cycle
    if (remainingToSend <= MEASURE_INTERVAL_SEC) {
        remainingToSend += SEND_SLACK_SEC;   // add a small slack to avoid landing just under the next send interval, which could cause timing issues in the next cycle
    }
}
uint32_t sleepTime = min(MEASURE_INTERVAL_SEC, remainingToSend);

    lastSleepSec = sleepTime; // save the sleep time for the next cycle
    goDeepSleep(sleepTime);
}

void loop() {}