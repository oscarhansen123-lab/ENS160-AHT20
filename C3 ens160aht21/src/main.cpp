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

const char* ssid = "xx";
const char* password = "";

const char* mqtt_server = "b512d33fcbc8401cb8504a21cce778a1.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "AQreader";
const char* mqtt_password = "Eksamen2026";

WiFiClientSecure espClient;
PubSubClient client(espClient);
#define MSG_BUFFER_SIZE 256
char msg[MSG_BUFFER_SIZE];

// ===== TID =====
const uint32_t MEASURE_INTERVAL_SEC = 120;
const uint32_t SEND_INTERVAL_SEC    = 600;
const uint32_t ENS160_STAB_MS       = 30000;
const uint32_t BOOT_ACTIVE_MS       = 40000;
const uint32_t MQTT_HOLD_MS         = 20000; // NEW: hold 20 sek efter publish

// ===== RTC =====
RTC_DATA_ATTR uint32_t secondsAccumulated = 0;
RTC_DATA_ATTR bool firstBootDone = false;
RTC_DATA_ATTR uint32_t activeAccumulatedSec = 0;
RTC_DATA_ATTR uint32_t lastSleepSec = 0;


RTC_DATA_ATTR float lastT = 0;
RTC_DATA_ATTR float lastH = 0;
RTC_DATA_ATTR uint8_t lastAqi = 0;
RTC_DATA_ATTR uint16_t lastTvoc = 0;
RTC_DATA_ATTR uint16_t lastEco2 = 0;

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

void goDeepSleep(uint32_t seconds) {
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    uint32_t awakeStart = millis();

    if (!firstBootDone) {
        Serial.println("Boot vindue 40 sek (upload kode nu)...");
        delay(BOOT_ACTIVE_MS);
        firstBootDone = true;
    }

    // === mål hver wake ===
    secondsAccumulated += lastSleepSec;

    mqtt_setup();
    Wire.begin(8, 9);

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

        // >>> START: beregn samlet forløbet tid (sleep + aktiv)
    uint32_t awakeSecSoFar = (millis() - awakeStart) / 1000;
    uint32_t totalElapsedSec = secondsAccumulated + activeAccumulatedSec + awakeSecSoFar;
    // <<< END: beregn samlet forløbet tid

    // ===== SEND når der er gået 10 min =====
    bool sendOk = false;
    if (secondsAccumulated >= SEND_INTERVAL_SEC) {
        wifi_setup();

        if (WiFi.status() == WL_CONNECTED) {
            if (mqtt_connect()) {
                JsonDocument doc;
                doc["temperature"] = lastT;
                doc["humidity"] = lastH;
                doc["aqi"] = lastAqi;
                doc["tvoc"] = lastTvoc;
                doc["eco2"] = lastEco2;

                serializeJson(doc, msg);
                client.publish("sensor/data", msg);
                sendOk = true;

                // NEW: hold forbindelsen åben 20 sek for leverance
                unsigned long holdStart = millis();
                while (millis() - holdStart < MQTT_HOLD_MS) {
                    client.loop();
                    delay(50);
                }
            }
        }

        wifi_off();

        if (sendOk) {
            secondsAccumulated = 0;
        }
    }

        // >>> START: opdater aktiv tid for hele wake
    uint32_t awakeSecTotal = (millis() - awakeStart) / 1000;
    activeAccumulatedSec += awakeSecTotal;
    // <<< END: opdater aktiv tid

    uint32_t remainingToSend = (secondsAccumulated >= SEND_INTERVAL_SEC)
        ? MEASURE_INTERVAL_SEC
        : (SEND_INTERVAL_SEC - secondsAccumulated);

    uint32_t sleepTime = min(MEASURE_INTERVAL_SEC, remainingToSend);

    lastSleepSec = sleepTime;

    goDeepSleep(sleepTime);
}

void loop() {}