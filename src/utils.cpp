#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <SPIFFS.h>
#include "utils.h"
#include "config.h"

extern AsyncWebServer server;

void iniciarModoAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("GrowControl-Setup", "dedodemomia");
  Serial.println(F("📡 Modo AP activado"));
  Serial.println(F("📡 Conéctate a wifi 'GrowControl-Setup' con contraseña 'dedodemomia'"));
  Serial.println(F("🌐 Abre http://192.168.4.1 en tu navegador"));
}

bool conectarWiFi() {
  if (config_ssid.length() == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(config_ssid.c_str(), config_password.c_str());
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    esp_task_wdt_reset();
    delay(500);
    Serial.print(".");
    intentos++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("\n✅ WiFi conectado! IP: "));
    Serial.println(WiFi.localIP().toString());
    return true;
  }
  Serial.println(F("\n❌ No se pudo conectar a WiFi"));
  return false;
}

void setupConfigServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    File file = SPIFFS.open("/index.html", "r");
    if (!file) {
      request->send(500, "text/plain", "Error al cargar la página");
      return;
    }
    request->send(SPIFFS, "/index.html", "text/html");
    file.close();
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasArg("ssid")) config_ssid = request->arg("ssid");
    if (request->hasArg("pass")) config_password = request->arg("pass");
    if (request->hasArg("mqtt_server")) config_mqtt_server = request->arg("mqtt_server");
    if (request->hasArg("mqtt_port")) config_mqtt_port = request->arg("mqtt_port").toInt();
    if (request->hasArg("mqtt_user")) config_mqtt_user = request->arg("mqtt_user");
    if (request->hasArg("mqtt_password")) config_mqtt_password = request->arg("mqtt_password");
    if (request->hasArg("telegram_token")) config_telegram_token = request->arg("telegram_token");
    if (request->hasArg("chat_id")) config_chat_id = request->arg("chat_id");
    if (request->hasArg("semana_inicial")) config_semana_inicial = request->arg("semana_inicial").toInt();
    if (request->hasArg("modo_inicial")) config_modo_floracion = (request->arg("modo_inicial") == "floracion");
    guardarConfiguracion();
    request->send(200, "text/plain", "Configuración guardada. Reiniciando...");
    delay(1000);
    ESP.restart();
  });
  server.begin();
}