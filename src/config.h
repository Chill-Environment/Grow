#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <PubSubClient.h> 
#include <WiFi.h>  

// ========== PINES I2C ==========
#define I2C_SDA 21
#define I2C_SCL 22

// ========== SENSORES DE SUELO ==========
extern const int soilPins[4];
extern const int dryValue;
extern const int wetValue;
extern const int sueloMinRiego;
extern const int humAireMin;
extern const int tempMin;
extern const int sueloMinAlerta;
extern const unsigned long cooldownRiegoMs;
extern const unsigned long riegoDuration;
extern const unsigned long lucesManualDuration;

// ========== TÓPICOS MQTT ==========
#define SONOFF1_TOPIC "cmnd/sonoff_bomba/power"
#define SONOFF2_TOPIC "cmnd/sonoff_luz/power"
#define SONOFF3_TOPIC "cmnd/sonoff-extractor/power"
#define SONOFF4_TOPIC "cmnd/sonoff_intractor/power"
#define SONOFF1_STAT "stat/sonoff_bomba/POWER"
#define SONOFF2_STAT "stat/sonoff_luz/POWER"
#define SONOFF3_STAT "stat/sonoff-extractor/POWER"
#define SONOFF4_STAT "stat/sonoff_intractor/POWER" 

// ========== VARIABLES DE CONFIGURACIÓN ==========
extern String config_ssid;
extern String config_password;
extern String config_mqtt_server;
extern int config_mqtt_port;
extern String config_mqtt_user;
extern String config_mqtt_password;
extern String config_telegram_token;
extern String config_chat_id;
extern int config_semana_inicial;
extern bool config_modo_floracion;
extern String adminUser;
extern String adminPass;

// ========== VARIABLES GLOBALES DE ESTADO ==========
extern int semanaCultivo;
extern bool modoFloracion;
extern bool lucesManualMode;
extern bool lucesManualState;
extern unsigned long lucesManualTimeout;
extern int modoExtractor;
extern int modoIntractor;
extern bool extractorEstado;
extern bool intractorEstado;
extern bool bombaEstado;
extern bool luzEstado;
extern unsigned long lastRiegoTime;
extern bool riegoEnProgreso;
extern unsigned long lastRiegoStart;

// ========== VARIABLES DE TEMPORIZACIÓN ==========
extern unsigned long lastSensorRead;
extern unsigned long lastTelegramCheck;
extern unsigned long lastDailyReport;
extern bool dailyReportSent;
extern unsigned long lastLightsCheck;
extern unsigned long lastLuzPublish;
extern unsigned long lastExtractorPublish;
extern unsigned long lastIntractorPublish;
extern const unsigned long PUBLISH_COOLDOWN;

// ========== VARIABLES PARA FALLOS Y TENDENCIAS ==========
extern float ultimaTempValida;
extern float ultimaHumedadValida;
extern bool hayDatosValidos;
extern int desconexionesMQTT;
extern int fallosRiego, fallosLuces, fallosExtractor;
extern const int TENDENCIA_LECTURAS;
extern float tempHistory[];
extern float humHistory[];
extern float vpdHistory[];
extern float presionHistory[];
extern int historyIndex;
extern int historyCount;

// ========== VARIABLES PARA PROMEDIOS DIARIOS ==========
extern float sumTemp, sumHA, sumSuelo, sumVPD, sumPresion;
extern int readingsCount;
extern float maxTemp, minTemp, maxHA, minHA, maxPresion, minPresion;

// ========== CONSTANTES DE COOLDOWN ==========
extern const unsigned long MIN_COOLDOWN_EXTRACTOR;
extern const unsigned long MIN_COOLDOWN_LUCES;
extern const unsigned long MIN_COOLDOWN_BOMBA;
extern const unsigned long MIN_COOLDOWN_INTRACTOR;

// ========== TIMERS PARA COOLDOWN ==========
extern unsigned long lastExtractorChange;
extern unsigned long lastLucesChange;
extern unsigned long lastBombaChange;
extern unsigned long lastIntractorChange;

// ========== ESTADOS ANTERIORES ==========
extern bool lastExtractorEstado;
extern bool lastLucesEstado;
extern bool lastBombaEstado;
extern bool lastIntractorEstado;

// ========== HISTORIAL WEB ==========
#define MAX_HISTORIAL 24
struct Lectura {
  char tiempo[20];
  float temp;
  float humedad;
  float suelo;
  float vpd;
  float presion;
  bool bomba;
  bool luces;
  bool extractor;
  bool intractor;
};
extern Lectura historial[MAX_HISTORIAL];
extern int historialIndex;
extern int historialCount;

// ========== OBJETOS GLOBALES (extern) ==========
extern AsyncWebServer server; //
extern Adafruit_BMP280 bmp;
extern Adafruit_AHTX0 aht;
extern WiFiClientSecure secured_client;
extern Preferences prefs;
extern WiFiClient espClient;
extern PubSubClient mqttClient;

// ========== DECLARACIÓN DE FUNCIONES ==========
void cargarConfiguracion();
void guardarConfiguracion();
void guardarEstado();
void cargarEstado();
void enviarTelegram(const String& mensaje);
void controlarSonoff(const char* topic, bool encender);
void publicarMQTT(const char* topic, const char* mensaje);
void agregarAlHistorial(const char* tiempo, float temp, float humedad, float suelo, float vpd, float presion);
void verificarEstadosReales();

#endif