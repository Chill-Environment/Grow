#ifndef SENSORS_H
#define SENSORS_H

void initSensors();
bool readTempHumi(float &temp, float &hum);
float readPressure();
float readSoilMoisture();
float computeVPD(float temp, float hum);

#endif