#ifndef HISTORIAL_H
#define HISTORIAL_H

#include "config.h"

void agregarHistorialTendencia(float temp, float hum, float vpd, float pres);
String getTendencia(const float datos[], int len);
void agregarAlHistorial(const char* tiempo, float temp, float humedad, float suelo, float vpd, float presion);
String generarHistorialHTML();
String interpretarPresion(float presion);
int getRecomendacionRiegoPresion(float presion);
void reiniciarContadoresDiarios();

#endif