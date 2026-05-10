#ifndef MQTT_H
#define MQTT_H

#include <PubSubClient.h>

extern PubSubClient mqttClient;

void initMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void reconnectMQTT();
void publicarMQTT(const char* topic, const char* mensaje);
void controlarSonoff(const char* topic, bool encender);
void verificarEstadosReales();
void inicializarEstados();

#endif