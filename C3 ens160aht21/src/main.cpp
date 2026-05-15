#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <ScioSense_ENS16x.h>
#include <SparkFun_Qwiic_Humidity_AHT20.h> 


#define I2C_ADDRESS_ENS160 0x53
AHT20 humiditySensor; // AHT20 sensor instance
ENS160 ens16x; // ENS160 sensor instance

const char* ssid = "RASPBERRYNET";
const char* password = "VerySecret";

const char* mqtt_server = "b512d33fcbc8401cb8504a21cce778a1.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "AQreader";
const char* mqtt_password = "Eksamen2026";

WiFiClientSecure espClient; // WiFi client for MQTT (Secure)
PubSubClient client(espClient); // MQTT client
unsigned long lastMsg = 0; // Timestamp for the last MQTT message sent
#define MSG_BUFFER_SIZE 256 // Buffer size for MQTT messages (increased for JSON)
char msg[MSG_BUFFER_SIZE]; // Buffer for MQTT messages

void wifi_setup() {
    delay(10);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void reconnect() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
            Serial.println("connected");
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}




void setup() {
    
    delay(2000);
    Serial.begin(115200);
    wifi_setup(); // Connect to WiFi 
    
    // For HiveMQ Cloud, skip certificate verification (or add your CA certificate)
    espClient.setInsecure(); // Skip certificate verification
    
    client.setServer(mqtt_server, mqtt_port); // Set MQTT server and port

    // Initialize I2C communication on pins 8 (SDA) and 9 (SCL)
    Wire.begin(8, 9); 
    
    Serial.println("\n--- Initialiserer sensorer ---");

   // Initialize AHT20 sensor 
    if (humiditySensor.begin() == false) {
        Serial.println("AHT20 ikke fundet!");
    } else {
        Serial.println("AHT20 OK");
    }

    // Initialize ENS160 sensor
    ens16x.enableDebugging(Serial);
    ens16x.begin(&Wire, I2C_ADDRESS_ENS160);

    Serial.print("Venter på ENS160...");
    while (ens16x.init() != true) {
        Serial.print(".");
        delay(1000);
    }
    Serial.println("\nENS160 success!");

    ens16x.startStandardMeasure();
}

void loop() {
    float t = 25.0; // Default temperature value in case sensor reading fails
    float h = 50.0; // Default humidity value in case sensor reading fails
    if (!client.connected()) {
        reconnect(); // Reconnect to MQTT if connection is lost
    }
    client.loop(); // Handle MQTT communication

    // Read temperature and humidity from AHT20 sensor
    t = humiditySensor.getTemperature();
    h = humiditySensor.getHumidity();

    // Print temperature and humidity values to Serial
    Serial.print("T: "); Serial.print(t, 1);
    Serial.print("C | H: "); Serial.print(h, 1);
    Serial.print("% | ");


    // Update ENS160 sensor and print air quality data if new data is available
    if (ens16x.update() == RESULT_OK) {
        if (ens16x.hasNewData()) {
            Serial.print("AQI: "); Serial.print((uint8_t)ens16x.getAirQualityIndex_UBA());
            Serial.print("\tTVOC: "); Serial.print(ens16x.getTvoc()); Serial.print("ppb");
            Serial.print("\tECO2: "); Serial.print(ens16x.getEco2()); Serial.println("ppm");
        }
    }

    unsigned long now = millis();
    if (now - lastMsg > 300000) { // Send MQTT message every 60 seconds
        lastMsg = now;
        // Create JSON object for MQTT message
        JsonDocument doc; 
        doc["temperature"] = t = humiditySensor.getTemperature();
        doc["humidity"] = h = humiditySensor.getHumidity();
        doc["aqi"] = (uint8_t)ens16x.getAirQualityIndex_UBA();
        doc["tvoc"] = ens16x.getTvoc();
        doc["eco2"] = ens16x.getEco2();

        serializeJson(doc, msg); // Serialize JSON object to string

        Serial.print("Publishing message: ");
        Serial.println(msg);
        client.publish("sensor/data", msg); // Publish temperature and humidity data to MQTT
    }

    delay(60000);
}
