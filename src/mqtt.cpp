#include <Arduino.h>
#include <esp_task_wdt.h>
#include <time.h>
#include <WiFi.h>
#include "mqtt.h"
#include "config.h"
#include "telegram.h"
#include "historial.h"

// Declaración extern de la función logToWeb (definida en web.cpp)
extern void logToWeb(const char* format, ...);

void initMQTT() {
  mqttClient.setServer(config_mqtt_server.c_str(), config_mqtt_port);
  mqttClient.setCallback(mqttCallback);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  String topicStr = String(topic);
  Serial.printf("📨 MQTT Recibido: %s -> %s\n", topic, message.c_str());
  logToWeb("📨 MQTT Recibido: %s -> %s\n", topic, message.c_str());

  if (topicStr == SONOFF1_STAT) {
    bombaEstado = (message == "ON");
  }
  else if (topicStr == SONOFF2_STAT) {
    luzEstado = (message == "ON");
    if (historialCount > 0) {
      int lastIdx = (historialIndex - 1 + MAX_HISTORIAL) % MAX_HISTORIAL;
      historial[lastIdx].luces = luzEstado;
    }
  }
  else if (topicStr == SONOFF3_STAT) {
    extractorEstado = (message == "ON");
  }
  else if (topicStr == "tele/sonoff_luz/STATE") {
    if (message.indexOf("\"POWER\":\"ON\"") > 0) luzEstado = true;
    else if (message.indexOf("\"POWER\":\"OFF\"") > 0) luzEstado = false;
  }
  else if (topicStr == "tele/sonoff_bomba/STATE") {
    bombaEstado = (message.indexOf("\"POWER\":\"ON\"") > 0);
  }
  else if (topicStr == "tele/sonoff-extractor/STATE") {
    extractorEstado = (message.indexOf("\"POWER\":\"ON\"") > 0);
  }
}

void reconnectMQTT() {
  int intentos = 0;
  while (!mqttClient.connected() && intentos < 5) {
    esp_task_wdt_reset();
    String clientId = "ESP32Grow-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str(), config_mqtt_user.c_str(), config_mqtt_password.c_str())) {
      mqttClient.subscribe(SONOFF1_STAT);
      mqttClient.subscribe(SONOFF2_STAT);
      mqttClient.subscribe(SONOFF3_STAT);
    } else {
      intentos++;
      if (intentos >= 5) desconexionesMQTT++;
      delay(3000);
    }
  }
}

void publicarMQTT(const char* topic, const char* mensaje) {
  if (mqttClient.connected()) {
    String payload = String(mensaje);
    payload.toUpperCase();
    if (payload != "ON" && payload != "OFF") return;
    if (!mqttClient.publish(topic, payload.c_str())) {
      if (topic == SONOFF1_TOPIC) fallosRiego++;
      else if (topic == SONOFF2_TOPIC) fallosLuces++;
      else if (topic == SONOFF3_TOPIC) fallosExtractor++;
    }
  }
}

void controlarSonoff(const char* topic, bool encender) {
  publicarMQTT(topic, encender ? "ON" : "OFF");
}

void verificarEstadosReales() {
  if (mqttClient.connected()) {
    mqttClient.publish("cmnd/sonoff_luz/status", "11");
    mqttClient.publish("cmnd/sonoff_bomba/status", "11");
    mqttClient.publish("cmnd/sonoff-extractor/status", "11");
  }
}

void inicializarEstados() {
  controlarSonoff(SONOFF1_TOPIC, false);
  bombaEstado = false;
  controlarSonoff(SONOFF3_TOPIC, false);
  extractorEstado = false;
  time_t now;
  time(&now);
  int hora = localtime(&now)->tm_hour;
  bool lucesOn = (modoFloracion) ? (hora >= 6 && hora < 18) : (hora >= 1 && hora < 18);
  controlarSonoff(SONOFF2_TOPIC, lucesOn);
  luzEstado = lucesOn;
}