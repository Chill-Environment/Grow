#include <Arduino.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include "config.h"

// ========== CONSTANTES ==========
const int soilPins[4] = {32, 33, 34, 35};
const int dryValue = 2700;
const int wetValue = 1200;
const int sueloMinRiego = 35;
const int humAireMin = 60;
const int tempMin = 20;
const int sueloMinAlerta = 35;
const unsigned long cooldownRiegoMs = 86400000;
const unsigned long riegoDuration = 10000;
const unsigned long lucesManualDuration = 3600000;

// ========== VARIABLES DE CONFIGURACIÓN ==========
String config_ssid = "";
String config_password = "";
String config_mqtt_server = "";
int config_mqtt_port = 1883;
String config_mqtt_user = "";
String config_mqtt_password = "";
String config_telegram_token = "";
String config_chat_id = "";
int config_semana_inicial = 1;
bool config_modo_floracion = false;
String adminUser = "";
String adminPass = "";

// ========== VARIABLES GLOBALES ==========
int semanaCultivo = 1;
bool modoFloracion = true;
bool lucesManualMode = false;
bool lucesManualState = false;
unsigned long lucesManualTimeout = 0;
int modoExtractor = 0;
int modoIntractor = 0;
bool extractorEstado = false;
bool intractorEstado = false;
bool bombaEstado = false;
bool luzEstado = false;
unsigned long lastRiegoTime = 0;
bool riegoEnProgreso = false;
unsigned long lastRiegoStart = 0;

unsigned long lastSensorRead = 0;
unsigned long lastTelegramCheck = 0;
unsigned long lastDailyReport = 0;
bool dailyReportSent = false;
unsigned long lastLightsCheck = 0;
unsigned long lastLuzPublish = 0;
unsigned long lastExtractorPublish = 0;
const unsigned long PUBLISH_COOLDOWN = 2000;

float ultimaTempValida = 0, ultimaHumedadValida = 0;
bool hayDatosValidos = false;
int desconexionesMQTT = 0;
int fallosRiego = 0, fallosLuces = 0, fallosExtractor = 0;

const int TENDENCIA_LECTURAS = 24;
float tempHistory[24];
float humHistory[24];
float vpdHistory[24];
float presionHistory[24];
int historyIndex = 0, historyCount = 0;

float sumTemp = 0, sumHA = 0, sumSuelo = 0, sumVPD = 0, sumPresion = 0;
int readingsCount = 0;
float maxTemp = 0, minTemp = 100, maxHA = 0, minHA = 100, maxPresion = 0, minPresion = 1000;

Lectura historial[MAX_HISTORIAL];
int historialIndex = 0, historialCount = 0;

// ========== FUNCIONES SPIFFS / CONFIGURACIÓN ==========
void cargarConfiguracion() {
  if (!SPIFFS.begin(true)) {
    Serial.println(F("Error al montar SPIFFS"));
    return;
  }
  File file = SPIFFS.open("/config.txt", "r");
  if (!file) {
    Serial.println(F("No hay configuración guardada. Usando modo AP"));
    return;
  }
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    int sep = line.indexOf('=');
    if (sep > 0) {
      String key = line.substring(0, sep);
      String value = line.substring(sep + 1);
      if (key == "ssid") config_ssid = value;
      else if (key == "password") config_password = value;
      else if (key == "mqtt_server") config_mqtt_server = value;
      else if (key == "mqtt_port") config_mqtt_port = value.toInt();
      else if (key == "mqtt_user") config_mqtt_user = value;
      else if (key == "mqtt_password") config_mqtt_password = value;
      else if (key == "telegram_token") config_telegram_token = value;
      else if (key == "chat_id") config_chat_id = value;
      else if (key == "semana_inicial") config_semana_inicial = value.toInt();
      else if (key == "modo_inicial") config_modo_floracion = (value == "floracion");
      else if (key == "admin_user") adminUser = value;
      else if (key == "admin_pass") adminPass = value;
    }
  }
  file.close();
  if (config_ssid.length() > 0 && config_telegram_token.length() > 0) {
    semanaCultivo = config_semana_inicial;
    modoFloracion = config_modo_floracion;
  }
}

void guardarConfiguracion() {
  File file = SPIFFS.open("/config.txt", "w");
  if (!file) return;
  file.println("ssid=" + config_ssid);
  file.println("password=" + config_password);
  file.println("mqtt_server=" + config_mqtt_server);
  file.println("mqtt_port=" + String(config_mqtt_port));
  file.println("mqtt_user=" + config_mqtt_user);
  file.println("mqtt_password=" + config_mqtt_password);
  file.println("telegram_token=" + config_telegram_token);
  file.println("chat_id=" + config_chat_id);
  file.println("semana_inicial=" + String(config_semana_inicial));
  file.println("modo_inicial=" + String(config_modo_floracion ? "floracion" : "vegetativo"));
  file.close();
}

void guardarEstado() {
  Preferences prefs;
  prefs.begin("grow", false);
  prefs.putInt("semanaCultivo", semanaCultivo);
  prefs.putInt("modoExtractor", modoExtractor);
  prefs.putInt("modoIntractor", modoIntractor);
  prefs.putBool("modoFloracion", modoFloracion);
  prefs.end();
}

void cargarEstado() {
  Preferences prefs;
  prefs.begin("grow", true);
  semanaCultivo = prefs.getInt("semanaCultivo", config_semana_inicial);
  modoExtractor = prefs.getInt("modoExtractor", 0);
  modoIntractor = prefs.getInt("modoIntractor", 0);
  modoFloracion = prefs.getBool("modoFloracion", config_modo_floracion);
  prefs.end();
}