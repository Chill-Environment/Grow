#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include "sensors.h"
#include "config.h"

void initSensors() {
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!aht.begin()) Serial.println(F("❌ Error: No se encontró AHT20"));
  else Serial.println(F("✅ AHT20 inicializado"));
  if (!bmp.begin(0x76)) {
    if (!bmp.begin(0x77)) Serial.println(F("❌ Error: No se encontró BMP280"));
    else Serial.println(F("✅ BMP280 inicializado (0x77)"));
  } else {
    Serial.println(F("✅ BMP280 inicializado (0x76)"));
  }
}

bool readTempHumi(float &temp, float &hum) {
  sensors_event_t humidity, temperature;
  aht.getEvent(&humidity, &temperature);
  temp = temperature.temperature;
  hum = humidity.relative_humidity;
  return (!isnan(temp) && !isnan(hum) && temp > -10 && temp < 60);
}

float readPressure() {
  return bmp.readPressure() / 100.0F;
}

float readSoilMoisture() {
  float suma = 0;
  for (int i = 0; i < 4; i++) {
    long sumaRaw = 0;
    for (int k = 0; k < 20; k++) sumaRaw += analogRead(soilPins[i]);
    float porc = map(sumaRaw / 20, dryValue, wetValue, 0, 100);
    suma += constrain(porc, 0, 100);
  }
  return suma / 4.0;
}

float computeVPD(float temp, float hum) {
  float es = 0.6112 * exp((17.67 * temp) / (temp + 243.5));
  float ea = (hum / 100.0) * es;
  return es - ea;
}