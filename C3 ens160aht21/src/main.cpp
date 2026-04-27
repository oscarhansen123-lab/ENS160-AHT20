#include <Arduino.h>
#include <Wire.h>
#include <ScioSense_ENS16x.h>
#include <SparkFun_Qwiic_Humidity_AHT20.h> 


#define I2C_ADDRESS_ENS160 0x53
AHT20 humiditySensor;
ENS160 ens16x;

void setup() {
    
    delay(2000);
    Serial.begin(115200); 
    
    
    Wire.begin(8, 9); 

    Serial.println("\n--- Initialiserer sensorer ---");

    
    if (humiditySensor.begin() == false) {
        Serial.println("AHT20 ikke fundet!");
    } else {
        Serial.println("AHT20 OK");
    }

    
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
    float t = 25.0;
    float h = 50.0;

    
    if (humiditySensor.available()) {
        t = humiditySensor.getTemperature();
        h = humiditySensor.getHumidity();

        
        
        Serial.print("T: "); Serial.print(t, 1);
        Serial.print("C | H: "); Serial.print(h, 1);
        Serial.print("% | ");
    }

    
    if (ens16x.update() == RESULT_OK) {
        if (ens16x.hasNewData()) {
            Serial.print("AQI: "); Serial.print((uint8_t)ens16x.getAirQualityIndex_UBA());
            Serial.print("\tTVOC: "); Serial.print(ens16x.getTvoc()); Serial.print("ppb");
            Serial.print("\tECO2: "); Serial.print(ens16x.getEco2()); Serial.println("ppm");
        }
    }

    delay(2000);
}
