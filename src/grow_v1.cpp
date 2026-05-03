#include <WiFi.h>                // Conexión a redes WiFi. Base de toda comunicación remota.
#include <WebServer.h>           // Servidor web para dashboard con gráficos y controles.
#include <time.h>                // Sincronización horaria NTP. Controla luces y reportes diarios.
#include <UniversalTelegramBot.h>// Control remoto por Telegram (comandos: /datos, /riego, /floracion...).
#include <WiFiClientSecure.h>    // Conexiones HTTPS seguras. Necesaria para Telegram.
#include <PubSubClient.h>        // Protocolo MQTT. Comunica ESP32 con Sonoffs (bomba, luces, extractor).

// SENSORES I2C (bus SDA=21, SCL=22)
#include <Wire.h>                // Bus I2C - conecta AHT20 y BMP280.
#include <Adafruit_Sensor.h>     // Framework unificado Adafruit para sensores.
#include <Adafruit_BMP280.h>     // Sensor de presión atmosférica (ajusta riego según clima).
#include <Adafruit_AHTX0.h>      // Sensor de temperatura y humedad ambiente (calcula VPD).

// ALMACENAMIENTO Y HERRAMIENTAS
#include <Preferences.h>         // Memoria NVS - guarda configuración entre reinicios.
#include <SPIFFS.h>              // Sistema de archivos - guarda config.txt e index.html.
#include <ArduinoOTA.h>          // Actualización remota por WiFi (subir firmware sin USB).
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

// OBJETOS GLOBALES
Preferences prefs;
WiFiClientSecure secured_client;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer server(80);
Adafruit_BMP280 bmp;
Adafruit_AHTX0 aht;

// Puntero al bot (se crea después de cargar la configuración)
UniversalTelegramBot* bot = nullptr;

// ================== VARIABLES DE CONFIGURACIÓN ==================
String config_ssid = "";
String config_password = "";
String config_mqtt_server = "";
int config_mqtt_port = 1883;
String config_mqtt_user = "";
String config_mqtt_password = "";
String config_telegram_token = "";
String config_chat_id = "";
int config_semana_inicial = 3;
bool config_modo_floracion = true;

// ================== VARIABLES GLOBALES ==================
int semanaCultivo = 3;
bool modoFloracion = true;
bool lucesManualMode = false;
bool lucesManualState = false;
unsigned long lastLightsCheck = 0;
unsigned long lucesManualTimeout = 0;

int modoExtractor = 0;
int modoIntractor = 0;
bool extractorEstado = false;
bool intractorEstado = false;
bool bombaEstado = false;
bool luzEstado = false;

unsigned long lastRiegoTime = 0;
unsigned long lastDailyReport = 0;
bool dailyReportSent = false;
unsigned long lastSensorRead = 0;
unsigned long lastTelegramCheck = 0;
unsigned long lastTelegramResponse = millis();

bool riegoEnProgreso = false;
unsigned long lastRiegoStart = 0;
bool primeraVez = true;
bool configuracionCompleta = false;

// Variables para tendencias
#define TENDENCIA_LECTURAS 24
float tempHistory[TENDENCIA_LECTURAS];
float humHistory[TENDENCIA_LECTURAS];
float vpdHistory[TENDENCIA_LECTURAS];
float presionHistory[TENDENCIA_LECTURAS];
int historyIndex = 0;
int historyCount = 0;

// Variables para promedios diarios
float sumTemp = 0, sumHA = 0, sumSuelo = 0, sumVPD = 0, sumPresion = 0;
int readingsCount = 0;
float maxTemp = 0, minTemp = 100;
float maxHA = 0, minHA = 100;
float maxPresion = 0, minPresion = 1000;

// Variables para fallos
int desconexionesMQTT = 0;
int fallosRiego = 0;
int fallosLuces = 0;
int fallosExtractor = 0;
int fallosSensorConsecutivos = 0;
bool hayDatosValidos = false;
float ultimaTempValida = 0;
float ultimaHumedadValida = 0;

// Variables de estado
bool lastLuzState = false;
bool lastExtractorState = false;
unsigned long lastLuzPublish = 0;
unsigned long lastExtractorPublish = 0;
const unsigned long PUBLISH_COOLDOWN = 2000;  // 2 segundos

// Historial web
#define MAX_HISTORIAL 15
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
Lectura historial[MAX_HISTORIAL];
int historialIndex = 0;
int historialCount = 0;

// Constantes fijas
#define I2C_SDA 21 //Pines temp/hum/pres
#define I2C_SCL 22 //Pines temp/hum/pres
const int soilPins[4] = {32, 33, 34, 35};
const int dryValue = 2700; //Calibracion hecha con sustrato seco en maceta de 11 litros
const int wetValue = 1200; //Calibracion hecha con sustrato humedo en maceta de 11 litros (riego al 90%)
const int sueloMinRiego = 35; //Valor que obtuve de observar la planta con signos de suelo seco a 30%
const int humAireMin = 60;
const int tempMin = 20;
// valor de humedad aire y temperatura minima los configure asi segun mi entorno para bajar la probabilidad de riego automatico en etapa de prueba. **Valores a recalibrar**
const int sueloMinAlerta = 35;
const unsigned long cooldownRiegoMs = 86400000;
const unsigned long riegoDuration = 10000; // 10 segundos de riego = 500ml agua.
const unsigned long lucesManualDuration = 3600000; // modos manual off/on luego de una hora vuelven a automatico

// Tópicos MQTT
#define SONOFF1_TOPIC "cmnd/sonoff_bomba/power"
#define SONOFF2_TOPIC "cmnd/sonoff_luz/power" //En tasmota era power1 por eso no recibia mensaje
#define SONOFF3_TOPIC "cmnd/sonoff-extractor/power"
#define SONOFF1_STAT "stat/sonoff_bomba/POWER"
#define SONOFF2_STAT "stat/sonoff_luz/POWER"
#define SONOFF3_STAT "stat/sonoff-extractor/POWER"

// ================== DECLARACIONES ==================
void guardarEstado();
void cargarEstado();
void agregarAlHistorial(const char* tiempo, float temp, float humedad, float suelo, float vpd, float presion);
String generarHistorialHTML();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publicarMQTT(const char* topic, const char* mensaje);
void controlarSonoff(const char* topic, bool encender);
void reconnectMQTT();
void leerSensoresYEnviarSiEsNecesario();
void controlLuces();
void controlExtractor();
void controlIntractor();
void inicializarEstados();
void enviarReporteDiario();
void handleRoot();
void handleRiegoManual();
void handleModoLuces();
void handleLucesManual();
void handleModoExtractor();
void handleModoIntractor();
void handleManualExtractor();
void handleManualIntractor();
void handleSetSemana();
void handleSetVPDTarget();
void handleConfig();
void handleSaveConfig();
void handleResetConfig();
float getVPDObjetivo();
String getTendencia(float datos[], int len);
String interpretarPresion(float presion);
int getRecomendacionRiegoPresion(float presion);
int getHumedadSueloOptima();
void agregarHistorialTendencia(float temp, float hum, float vpd, float presion);
void reiniciarContadoresDiarios();
void cargarConfiguracion();
void guardarConfiguracion();
void iniciarModoAP();
void setupConfigServer();
void enviarTelegram(const String& mensaje);
void verificarEstadosReales();

// ================== CONFIGURACIÓN SPIFFS ==================

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
    }
  }
  file.close();
  
  if (config_ssid.length() > 0 && config_telegram_token.length() > 0) {
    configuracionCompleta = true;
    semanaCultivo = config_semana_inicial;
    modoFloracion = config_modo_floracion;
    Serial.println(F("Configuración cargada correctamente"));
  } else {
    Serial.println(F("Configuración incompleta"));
  }
}

void guardarConfiguracion() {
  File file = SPIFFS.open("/config.txt", "w");
  if (!file) {
    Serial.println(F("Error al guardar configuración"));
    return;
  }
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
  Serial.println(F("Configuración guardada"));
}

// ================== MODO AP Y SERVIDOR DE CONFIGURACIÓN ==================

void iniciarModoAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("GrowControl-Setup", "dedodemomia");
  Serial.println(F("📡 Modo AP activado"));
  Serial.println(F("📡 Conéctate a wifi 'GrowControl-Setup' con contraseña 'dedodemomia'"));
  Serial.println(F("🌐 Abre http://192.168.4.1 en tu navegador"));
}

void setupConfigServer() {
  server.on("/", HTTP_GET, []() {
    File file = SPIFFS.open("/index.html", "r");
    if (!file) {
      server.send(500, "text/plain", "Error al cargar la página");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });
  
  server.on("/save", HTTP_POST, []() {
    if (server.hasArg("ssid")) config_ssid = server.arg("ssid");
    if (server.hasArg("pass")) config_password = server.arg("pass");
    if (server.hasArg("mqtt_server")) config_mqtt_server = server.arg("mqtt_server");
    if (server.hasArg("mqtt_port")) config_mqtt_port = server.arg("mqtt_port").toInt();
    if (server.hasArg("mqtt_user")) config_mqtt_user = server.arg("mqtt_user");
    if (server.hasArg("mqtt_password")) config_mqtt_password = server.arg("mqtt_password");
    if (server.hasArg("telegram_token")) config_telegram_token = server.arg("telegram_token");
    if (server.hasArg("chat_id")) config_chat_id = server.arg("chat_id");
    if (server.hasArg("semana_inicial")) config_semana_inicial = server.arg("semana_inicial").toInt();
    if (server.hasArg("modo_inicial")) config_modo_floracion = (server.arg("modo_inicial") == "floracion");
    
    guardarConfiguracion();
    server.send(200, "text/plain", "Configuración guardada. Reiniciando...");
    delay(1000);
    ESP.restart();
  });
  
  server.begin();
}

// ================== CONEXIÓN WIFI ==================

bool conectarWiFi() {
  if (config_ssid.length() == 0) return false;
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(config_ssid.c_str(), config_password.c_str());
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi conectado! IP: " + WiFi.localIP().toString());
    return true;
  }
  
  Serial.println(F("\n❌ No se pudo conectar a WiFi"));
  return false;
}

// ================== FUNCIÓN PARA ENVIAR TELEGRAM ==================

void enviarTelegram(const String& mensaje) {
  if (bot != nullptr && config_chat_id.length() > 0) {
    bot->sendMessage(config_chat_id, mensaje, "Markdown");
  }
}

// ================== SETUP ==================

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n🌱 Grow Control v2.0 - Configurable por Web"));
  
  cargarConfiguracion();
  
  if (configuracionCompleta && conectarWiFi()) {
    bot = new UniversalTelegramBot(config_telegram_token, secured_client);
    secured_client.setInsecure();
    
    Wire.begin(I2C_SDA, I2C_SCL);
    
    if (!aht.begin()) Serial.println(F("❌ Error: No se encontró AHT20"));
    else Serial.println(F("✅ AHT20 inicializado"));
    
    if (!bmp.begin(0x76)) {
      if (!bmp.begin(0x77)) Serial.println(F("❌ Error: No se encontró BMP280"));
      else Serial.println(F("✅ BMP280 inicializado (0x77)"));
    } else {
      Serial.println(F("✅ BMP280 inicializado (0x76)"));
    }
    
    mqttClient.setServer(config_mqtt_server.c_str(), config_mqtt_port);
    mqttClient.setCallback(mqttCallback);
    
    configTime(-10800, 0, "pool.ntp.org", "time.nist.gov");
    delay(2000);
    
    enviarTelegram("🌱 *GROW CONTROL INICIADO*\n✅ Semana: " + String(semanaCultivo));
    
    // Rutas del dashboard
    server.on("/", handleRoot);
    server.on("/riego", handleRiegoManual);
    server.on("/luces", handleModoLuces);
    server.on("/lucesManual", handleLucesManual);
    server.on("/modoExtractor", handleModoExtractor);
    server.on("/modoIntractor", handleModoIntractor);
    server.on("/manualExtractor", handleManualExtractor);
    server.on("/manualIntractor", handleManualIntractor);
    server.on("/setSemana", handleSetSemana);
    server.on("/setVPD", handleSetVPDTarget);
    
    // Rutas de configuración avanzada
    server.on("/config", handleConfig);
    server.on("/saveconfig", handleSaveConfig);
    server.on("/resetconfig", handleResetConfig);
    
    server.begin();
    Serial.println(F("🌐 Web server iniciado"));

    // Iniciar ElegantOTA en el puerto 81
    ElegantOTA.begin(&server);  // ← Usa tu WebServer del puerto 80
    Serial.println(F("✨ ElegantOTA iniciado en puerto 81"));
    Serial.println(F("   - Para actualizar: http://[IP]:81/update"));
    
    lastDailyReport = millis();
    
    for (int i = 0; i < MAX_HISTORIAL; i++) strcpy(historial[i].tiempo, "");
    for (int i = 0; i < TENDENCIA_LECTURAS; i++) {
      tempHistory[i] = 0; humHistory[i] = 0; vpdHistory[i] = 0; presionHistory[i] = 0;
    }
    
    cargarEstado();

  // Configuración de OTA nativo
ArduinoOTA.setHostname("GrowControl");
//ArduinoOTA.setPassword("12345678");  // Cambialo por una contraseña segura

ArduinoOTA.onStart([]() {
    Serial.println("Iniciando actualización OTA...");
});
ArduinoOTA.onEnd([]() {
    Serial.println("\nActualización completada.");
});
ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progreso: %u%%\r", (progress / (total / 100)));
});
ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
});

ArduinoOTA.begin();
Serial.println("✅ OTA nativo iniciado");
Serial.println("   - Para actualizar: PlatformIO → Upload → Network");

  } else {
    iniciarModoAP();
    setupConfigServer();
  }
}

// ================== LOOP ==================

void loop() {
  server.handleClient();
  ElegantOTA.loop();
  ArduinoOTA.handle();
  
  if (WiFi.getMode() == WIFI_AP) {
    delay(100);
    return;
  }
  
  if (bot == nullptr) return;
  
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();

  static unsigned long lastStateCheck = 0;
if (millis() - lastStateCheck > 30000) {
  lastStateCheck = millis();
  verificarEstadosReales();
}
  
  if (primeraVez && mqttClient.connected()) {
    inicializarEstados();
    primeraVez = false;
  }
  
  if (millis() - lastSensorRead >= 30000) {
    lastSensorRead = millis();
    time_t now;
    time(&now);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%d/%m/%Y %H:%M", localtime(&now));
    leerSensoresYEnviarSiEsNecesario();
  }
  
  controlLuces();
  controlExtractor();
  controlIntractor();
  enviarReporteDiario();
  
  if (riegoEnProgreso && (millis() - lastRiegoStart >= riegoDuration)) {
    controlarSonoff(SONOFF1_TOPIC, false);
    riegoEnProgreso = false;
    Serial.println(F("✅ Riego completado"));
  }
  
  // Telegram - recibir mensajes
  if (millis() - lastTelegramCheck > 4000) {
    lastTelegramCheck = millis();
    
    int numNewMessages = bot->getUpdates(bot->last_message_received + 1);
    if (numNewMessages > 0) lastTelegramResponse = millis();
    
    while (numNewMessages) {
      for (int i = 0; i < numNewMessages; i++) {
        String chat_id = String(bot->messages[i].chat_id);
        String text = bot->messages[i].text;
        text.toLowerCase();
        
        Serial.print(F("📱 Telegram: "));
        Serial.println(text);
        
        if (text == "/start") {
          bot->sendMessage(chat_id, "🌱 *GROW CONTROL*\n\nComandos:\n/datos - Ver datos\n/estado - Estado relés\n/riego - Riego manual\n/luces_on - Encender luces\n/luces_off - Apagar luces\n/floracion - Floración\n/vegetativo - Vegetativo\n/extractor_auto - Extractor auto\n/extractor_off - Extractor OFF\n/intractor_auto - Intractor auto\n/intractor_off - Intractor OFF\n/semana X - Set semana\n/vpd_target - Ver VPD objetivo", "Markdown");
        }
        else if (text == "/datos") {
          sensors_event_t humidity, temp;
          aht.getEvent(&humidity, &temp);
          float temperatura = temp.temperature;
          float humedad = humidity.relative_humidity;
          float presion = bmp.readPressure() / 100.0F;
          float es = 0.6112 * exp((17.67 * temperatura) / (temperatura + 243.5));
          float ea = (humedad / 100.0) * es;
          float vpd = es - ea;
          float suma = 0;
          for (int j = 0; j < 4; j++) {
            long sumaRaw = 0;
            for (int k = 0; k < 20; k++) sumaRaw += analogRead(soilPins[j]);
            float porc = map(sumaRaw / 20, dryValue, wetValue, 0, 100);
            suma += constrain(porc, 0, 100);
          }
          float humSueloProm = suma / 4.0;
          String mensaje = "🌡️ *DATOS*\nTemp: " + String(temperatura,1) + "°C\nHumedad: " + String(humedad,1) + "%\nPresión: " + String(presion,1) + " hPa\nSuelo: " + String(humSueloProm,1) + "%\nVPD: " + String(vpd,2) + " kPa\nSemana: " + String(semanaCultivo) + "\nModo: " + (modoFloracion ? "Floración" : "Vegetativo");
          bot->sendMessage(chat_id, mensaje, "Markdown");
        }
        else if (text == "/estado") {
          String mensaje = "⚙️ *ESTADO RELÉS*\nBomba: " + String(bombaEstado ? "ON" : "OFF") + "\nLuces: " + String(luzEstado ? "ON" : "OFF") + "\nExtractor: " + String(extractorEstado ? "ON" : "OFF");
          bot->sendMessage(chat_id, mensaje, "Markdown");
        }
        else if (text == "/riego") {
          if (!riegoEnProgreso) {
            controlarSonoff(SONOFF1_TOPIC, true);
            riegoEnProgreso = true;
            lastRiegoStart = millis();
            bot->sendMessage(chat_id, "💧 *Iniciando riego manual*", "Markdown");
          } else {
            bot->sendMessage(chat_id, "⏳ *Riego ya en progreso*", "Markdown");
          }
        }
        else if (text == "/luces_on") {
          controlarSonoff(SONOFF2_TOPIC, true);
          bot->sendMessage(chat_id, "💡 *Luces encendidas*", "Markdown");
        }
        else if (text == "/luces_off") {
          controlarSonoff(SONOFF2_TOPIC, false);
          bot->sendMessage(chat_id, "🌙 *Luces apagadas*", "Markdown");
        }
        else if (text == "/floracion") {
          modoFloracion = true;
          guardarEstado();
          bot->sendMessage(chat_id, "🌙 *Floración 12/12*", "Markdown");
        }
        else if (text == "/vegetativo") {
          modoFloracion = false;
          guardarEstado();
          bot->sendMessage(chat_id, "☀️ *Vegetativo 17/7*", "Markdown");
        }
        else if (text == "/extractor_auto") {
          modoExtractor = 2;
          guardarEstado();
          bot->sendMessage(chat_id, "🌀 *Extractor Automático*", "Markdown");
        }
        else if (text == "/extractor_off") {
          modoExtractor = 0;
          controlarSonoff(SONOFF3_TOPIC, false);
          guardarEstado();
          bot->sendMessage(chat_id, "🌀 *Extractor APAGADO*", "Markdown");
        }
        else if (text == "/intractor_auto") {
          modoIntractor = 2;
          guardarEstado();
          bot->sendMessage(chat_id, "🌬️ *Intractor Automático*", "Markdown");
        }
        else if (text == "/intractor_off") {
          modoIntractor = 0;
          guardarEstado();
          bot->sendMessage(chat_id, "🌬️ *Intractor APAGADO*", "Markdown");
        }
        else if (text.startsWith("/semana")) {
          int nuevaSemana = text.substring(7).toInt();
          if (nuevaSemana >= 1 && nuevaSemana <= 8) {
            semanaCultivo = nuevaSemana;
            guardarEstado();
            bot->sendMessage(chat_id, "📅 *Semana actualizada a " + String(semanaCultivo) + "*", "Markdown");
          } else {
            bot->sendMessage(chat_id, "❌ Semana inválida. Usar 1-8", "Markdown");
          }
        }
        else if (text == "/vpd_target") {
          bot->sendMessage(chat_id, "🎯 *VPD Objetivo:* " + String(getVPDObjetivo(),2) + " kPa\nSemana: " + String(semanaCultivo), "Markdown");
        }
      }
      numNewMessages = bot->getUpdates(bot->last_message_received + 1);
    }
  }
  
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000 && WiFi.status() != WL_CONNECTED) {
    lastWiFiCheck = millis();
    WiFi.reconnect();
  }
}

// ================== FUNCIONES PRINCIPALES ==================

void guardarEstado() {
  prefs.begin("grow", false);
  prefs.putInt("semanaCultivo", semanaCultivo);
  prefs.putInt("modoExtractor", modoExtractor);
  prefs.putInt("modoIntractor", modoIntractor);
  prefs.putBool("modoFloracion", modoFloracion);
  prefs.end();
}

void cargarEstado() {
  prefs.begin("grow", true);
  semanaCultivo = prefs.getInt("semanaCultivo", config_semana_inicial);
  modoExtractor = prefs.getInt("modoExtractor", 0);
  modoIntractor = prefs.getInt("modoIntractor", 0);
  modoFloracion = prefs.getBool("modoFloracion", config_modo_floracion);
  prefs.end();
}

float getVPDObjetivo() {
  if (modoFloracion) {
    switch(semanaCultivo) {
      case 1: return 1.25; case 2: return 1.35; case 3: return 1.45;
      case 4: return 1.55; case 5: return 1.55; case 6: return 1.45;
      case 7: return 1.35; case 8: return 1.25; default: return 1.40;
    }
  } else {
    switch(semanaCultivo) {
      case 1: return 0.85; case 2: return 1.05; case 3: return 1.15;
      case 4: return 1.25; default: return 1.00;
    }
  }
}

int getHumedadSueloOptima() {
  if (modoFloracion) {
    switch(semanaCultivo) {
      case 1: return 65; case 2: return 60; case 3: return 55; case 4: return 50;
      case 5: return 45; case 6: return 40; case 7: return 35; case 8: return 30;
      default: return 50;
    }
  } else {
    switch(semanaCultivo) {
      case 1: return 72; case 2: return 67; case 3: return 62; case 4: return 57;
      default: return 65;
    }
  }
}

String interpretarPresion(float presion) {
  if (presion > 1025) return "Alta presión (clima estable) ✅";
  else if (presion >= 1013) return "Presión normal ✅";
  else if (presion >= 1000) return "⚠️ Baja presión - posible lluvia";
  else return "🚨 Tormenta - REDUCIR RIEGO";
}

int getRecomendacionRiegoPresion(float presion) {
  if (presion >= 1013) return 0;
  else if (presion >= 1000) return -10;
  else return -30;
}

void agregarHistorialTendencia(float temp, float hum, float vpd, float presion) {
  tempHistory[historyIndex] = temp;
  humHistory[historyIndex] = hum;
  vpdHistory[historyIndex] = vpd;
  presionHistory[historyIndex] = presion;
  historyIndex = (historyIndex + 1) % TENDENCIA_LECTURAS;
  if (historyCount < TENDENCIA_LECTURAS) historyCount++;
}

String getTendencia(float datos[], int len) {
  if (len < 3) return "Insuficiente";
  float suma = 0;
  for (int i = 0; i < len; i++) suma += datos[i];
  float promedio = suma / len;
  float ultimo = datos[(historyIndex - 1 + TENDENCIA_LECTURAS) % TENDENCIA_LECTURAS];
  if (ultimo > promedio + 0.5) return "⬆️ Subiendo";
  else if (ultimo < promedio - 0.5) return "⬇️ Bajando";
  else return "➡️ Estable";
}

void reiniciarContadoresDiarios() {
  desconexionesMQTT = 0; fallosRiego = 0; fallosLuces = 0; fallosExtractor = 0;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  String topicStr = String(topic);

  Serial.printf("📨 MQTT Recibido: %s -> %s\n", topic, message.c_str());
  
  if (topicStr == SONOFF1_STAT) {
    bombaEstado = (message == "ON");
    Serial.printf("📡 Bomba real: %s\n", bombaEstado ? "ON" : "OFF");
  }
  else if (topicStr == SONOFF2_STAT) {
    luzEstado = (message == "ON");
    lastLuzState = luzEstado;
    Serial.printf("📡 Luz real: %s\n", luzEstado ? "ON" : "OFF");

    // 🔥 NUEVO: Actualizar el historial cuando cambia el estado de las luces
    time_t now;
    time(&now);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%d/%m/%Y %H:%M:%S", localtime(&now));
    
    // Obtener la última lectura del historial y actualizar su estado de luces
    if (historialCount > 0) {
      int lastIdx = (historialIndex - 1 + MAX_HISTORIAL) % MAX_HISTORIAL;
      historial[lastIdx].luces = luzEstado;
      Serial.printf("📝 Historial actualizado: luces = %s\n", luzEstado ? "ON" : "OFF");
    }
  }
  else if (topicStr == SONOFF3_STAT) {
    extractorEstado = (message == "ON");
    lastExtractorState = extractorEstado;
    Serial.printf("📡 Extractor real: %s\n", extractorEstado ? "ON" : "OFF");
  }
  
  // NUEVO: Capturar respuestas de STATUS (cuando consultas con STATUS 11)
  else if (topicStr == "tele/sonoff_luz/STATE") {
    // El JSON de Tasmota contiene "POWER": "ON" o "OFF"
    if (message.indexOf("\"POWER\":\"ON\"") > 0) {
      luzEstado = true;
      lastLuzState = true;
      Serial.println("📡 STATUS: Luz REAL = ON");
    } else if (message.indexOf("\"POWER\":\"OFF\"") > 0) {
      luzEstado = false;
      lastLuzState = false;
      Serial.println("📡 STATUS: Luz REAL = OFF");
    }
  }
  else if (topicStr == "tele/sonoff_bomba/STATE") {
    if (message.indexOf("\"POWER\":\"ON\"") > 0) {
      bombaEstado = true;
      Serial.println("📡 STATUS: Bomba REAL = ON");
    } else if (message.indexOf("\"POWER\":\"OFF\"") > 0) {
      bombaEstado = false;
      Serial.println("📡 STATUS: Bomba REAL = OFF");
    }
  }
  else if (topicStr == "tele/sonoff-extractor/STATE") {
    if (message.indexOf("\"POWER\":\"ON\"") > 0) {
      extractorEstado = true;
      lastExtractorState = true;
      Serial.println("📡 STATUS: Extractor REAL = ON");
    } else if (message.indexOf("\"POWER\":\"OFF\"") > 0) {
      extractorEstado = false;
      lastExtractorState = false;
      Serial.println("📡 STATUS: Extractor REAL = OFF");
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

void reconnectMQTT() {
  int intentos = 0;
  while (!mqttClient.connected() && intentos < 5) {
    String clientId = "ESP32Grow-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str(), config_mqtt_user.c_str(), config_mqtt_password.c_str())) {
      mqttClient.subscribe(SONOFF1_STAT);
      mqttClient.subscribe(SONOFF2_STAT);
      mqttClient.subscribe(SONOFF3_STAT);
    } else {
      intentos++;
      if (intentos >= 5) desconexionesMQTT++;
      delay(5000);
    }
  }
}

void verificarEstadosReales() {
  if (mqttClient.connected()) {
    // Publicar comandos de consulta a los tópicos STAT
    // Tasmota responde automáticamente al suscribirse, pero podemos forzar
    mqttClient.publish("cmnd/sonoff_luz/STATUS", "11");  // Consulta estado completo
    mqttClient.publish("cmnd/sonoff_bomba/STATUS", "11");
    mqttClient.publish("cmnd/sonoff-extractor/STATUS", "11");
  }
}

void inicializarEstados() {
  controlarSonoff(SONOFF1_TOPIC, false);
  bombaEstado = false;
  controlarSonoff(SONOFF3_TOPIC, false);
  extractorEstado = false;
  lastExtractorState = false;  // Inicializar
  
  time_t now;
  time(&now);
  int hora = localtime(&now)->tm_hour;
  bool lucesOn = (modoFloracion) ? (hora >= 6 && hora < 18) : (hora >= 1 && hora < 18);
  controlarSonoff(SONOFF2_TOPIC, lucesOn);
  luzEstado = lucesOn;
  lastLuzState = lucesOn;  // 🔥 Inicializar variable de sincronización
  Serial.printf("✅ Inicializado: luzEstado = %s, lastLuzState = %s\n", luzEstado ? "ON" : "OFF", lastLuzState ? "ON" : "OFF");
}

void agregarAlHistorial(const char* tiempo, float temp, float humedad, float suelo, float vpd, float presion) {
  strcpy(historial[historialIndex].tiempo, tiempo);
  historial[historialIndex].temp = temp;
  historial[historialIndex].humedad = humedad;
  historial[historialIndex].suelo = suelo;
  historial[historialIndex].vpd = vpd;
  historial[historialIndex].presion = presion;
  historial[historialIndex].bomba = bombaEstado;
  historial[historialIndex].luces = luzEstado;
  historial[historialIndex].extractor = extractorEstado;
  historial[historialIndex].intractor = intractorEstado;
  
  historialIndex = (historialIndex + 1) % MAX_HISTORIAL;
  if (historialCount < MAX_HISTORIAL) historialCount++;
}

String generarHistorialHTML() {
  String html = "<table border='0' cellpadding='5' cellspacing='0' width='100%'><tr bgcolor='#2d6a4f'><th>Fecha</th><th>T°C</th><th>HA%</th><th>Suelo%</th><th>VPD</th><th>Bomba</th><th>Luces</th><th>Extractor</th></tr>";
  for (int i = 0; i < historialCount; i++) {
    int idx = (historialIndex - 1 - i + MAX_HISTORIAL) % MAX_HISTORIAL;
    if (strlen(historial[idx].tiempo) > 0) {
      html += "<tr>";
      html += "<td>" + String(historial[idx].tiempo) + "</td>";
      html += "<td>" + String(historial[idx].temp,1) + "</td>";
      html += "<td>" + String(historial[idx].humedad,1) + "</td>";
      html += "<td>" + String(historial[idx].suelo,1) + "</td>";
      html += "<td>" + String(historial[idx].vpd,2) + "</td>";
      html += "<td>" + String(historial[idx].bomba ? "ON" : "OFF") + "</td>";
      html += "<td>" + String(historial[idx].luces ? "ON" : "OFF") + "</td>";
      html += "<td>" + String(historial[idx].extractor ? "ON" : "OFF") + "</td>";
      html += "</tr>";
    }
  }
  html += "</table>";
  return html;
}

void leerSensoresYEnviarSiEsNecesario() {

  if (mqttClient.connected()) {
    Serial.println("🔄 Sincronizando estado REAL de luces antes de lectura...");
    mqttClient.publish("cmnd/sonoff_luz/STATUS", "11");
    delay(100);  // Pequeña pausa para permitir respuesta MQTT
    mqttClient.loop();  // Procesar respuesta inmediatamente
  }

  float temperatura = 0, humedad = 0;
  bool lecturaExitosa = false;
  
  for (int intento = 0; intento < 3; intento++) {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    temperatura = temp.temperature;
    humedad = humidity.relative_humidity;
    
    if (!isnan(temperatura) && !isnan(humedad) && temperatura > -10 && temperatura < 60) {
      lecturaExitosa = true;
      break;
    }
    delay(100);
  }
  
  if (!lecturaExitosa) {
    if (hayDatosValidos) {
      temperatura = ultimaTempValida;
      humedad = ultimaHumedadValida;
    } else {
      return;
    }
  } else {
    ultimaTempValida = temperatura;
    ultimaHumedadValida = humedad;
    hayDatosValidos = true;
    fallosSensorConsecutivos = 0;
  }
  
  float presion = bmp.readPressure() / 100.0F;
  float es = 0.6112 * exp((17.67 * temperatura) / (temperatura + 243.5));
  float ea = (humedad / 100.0) * es;
  float vpd = es - ea;
  
  float suma = 0;
  for (int i = 0; i < 4; i++) {
    long sumaRaw = 0;
    for (int k = 0; k < 20; k++) sumaRaw += analogRead(soilPins[i]);
    float porc = map(sumaRaw / 20, dryValue, wetValue, 0, 100);
    suma += constrain(porc, 0, 100);
  }
  float humSueloProm = suma / 4.0;

  agregarHistorialTendencia(temperatura, humedad, vpd, presion);
  
  sumTemp += temperatura; sumHA += humedad; sumSuelo += humSueloProm;
  sumVPD += vpd; sumPresion += presion;
  readingsCount++;
  if (temperatura > maxTemp) maxTemp = temperatura;
  if (temperatura < minTemp) minTemp = temperatura;
  if (humedad > maxHA) maxHA = humedad;
  if (humedad < minHA) minHA = humedad;
  if (presion > maxPresion) maxPresion = presion;
  if (presion < minPresion) minPresion = presion;

  time_t now;
  time(&now);
  char timeStr[20];
  strftime(timeStr, sizeof(timeStr), "%d/%m/%Y %H:%M", localtime(&now));

  agregarAlHistorial(timeStr, temperatura, humedad, humSueloProm, vpd, presion);
  
  if (mqttClient.connected()) {
    String payload = "grow_sensors,host=ESP32_grow temp=" + String(temperatura,1) + 
                     ",humedad=" + String(humedad,1) + ",presion=" + String(presion,1) + 
                     ",vpd=" + String(vpd,2) + ",suelo=" + String(humSueloProm,1);
    mqttClient.publish("grow/sensor", payload.c_str());
  }

  // Riego automático
  int ajusteRiego = getRecomendacionRiegoPresion(presion);
  int umbralRiegoAjustado = sueloMinRiego + ajusteRiego;
  if (umbralRiegoAjustado < 20) umbralRiegoAjustado = 20;
  
  bool condicionesRiego = (humSueloProm < umbralRiegoAjustado) && (humedad < humAireMin) && (temperatura > tempMin);
  if (condicionesRiego && !riegoEnProgreso && (millis() - lastRiegoTime > cooldownRiegoMs || lastRiegoTime == 0)) {
    controlarSonoff(SONOFF1_TOPIC, true);
    riegoEnProgreso = true;
    lastRiegoStart = millis();
    lastRiegoTime = millis();
    enviarTelegram("💧 *Riego automático*");
  }
  
  if (humSueloProm < sueloMinAlerta) {
    enviarTelegram("🚨 *ALERTA: Suelo seco!* " + String(humSueloProm,1) + "%");
  }
}

void controlLuces() {
  // ============================================================
  // PASO 1: Verificar estado REAL desde MQTT antes de decidir
  // ============================================================
  static unsigned long lastPreCheck = 0;
  if (millis() - lastPreCheck > 30000) {  // Cada 30 segundos
    lastPreCheck = millis();
    if (mqttClient.connected()) {
      Serial.println("🔍 Pre-verificación: consultando estado real de luces...");
      mqttClient.publish("cmnd/sonoff_luz/STATUS", "11");
      delay(100);
      mqttClient.loop();
    }
  }
  
  // ============================================================
  // PASO 2: Verificar modo manual (expirado?)
  // ============================================================
  if (lucesManualMode && millis() - lucesManualTimeout > lucesManualDuration) {
    lucesManualMode = false;
    Serial.println("⏰ Modo manual de luces expirado, volviendo a automático");
  }
  
  // ============================================================
  // PASO 3: Calcular estado DESEADO
  // ============================================================
  bool lucesOn;
  if (lucesManualMode) {
    lucesOn = lucesManualState;
    Serial.printf("🎮 Modo MANUAL: luces = %s\n", lucesOn ? "ON" : "OFF");
  } else {
    if (millis() - lastLightsCheck < 60000) return;
    lastLightsCheck = millis();
    time_t now;
    time(&now);
    int hora = localtime(&now)->tm_hour;
    lucesOn = (modoFloracion) ? (hora >= 6 && hora < 18) : (hora >= 1 && hora < 18);
    Serial.printf("⏰ Modo AUTO: hora=%d, luces deseadas=%s\n", hora, lucesOn ? "ON" : "OFF");
  }
  
  // ============================================================
  // PASO 4: Comparar con estado REAL y publicar si diferente
  // ============================================================
  if (lucesOn != luzEstado) {
    if (millis() - lastLuzPublish > PUBLISH_COOLDOWN) {
      lastLuzPublish = millis();
      Serial.printf("💡 Cambiando luces: REAL=%s -> DESEADO=%s\n", 
                    luzEstado ? "ON" : "OFF", 
                    lucesOn ? "ON" : "OFF");
      controlarSonoff(SONOFF2_TOPIC, lucesOn);
    } else {
      Serial.println("⏳ Cooldown luces - publicación ignorada");
    }
  } else {
    // Opcional: descomentar para debug
    // Serial.printf("✅ Luces sincronizadas: REAL=%s == DESEADO=%s\n", 
    //               luzEstado ? "ON" : "OFF", lucesOn ? "ON" : "OFF");
  }
}

void controlExtractor() {
  if (modoExtractor == 0) return;
  bool extractorOn = false;
    
  if (modoExtractor == 1) {
    time_t now;
    time(&now);
    extractorOn = (localtime(&now)->tm_min < 15);
  } else {
    float h = ultimaHumedadValida;
    float t = ultimaTempValida;
    float es = 0.6112 * exp((17.67 * t) / (t + 243.5));
    float ea = (h / 100.0) * es;
    float vpd = es - ea;
    
    float vpdObjetivo = getVPDObjetivo();
    if (vpd - vpdObjetivo > 0.3) extractorOn = true;
    else if (vpd - vpdObjetivo > 0.1) extractorOn = true;
    else if (vpd - vpdObjetivo < -0.2) extractorOn = false;
    
    if (t > 29.0) extractorOn = true;
    if (h > 80) extractorOn = true;
  }
  
  // Usar extractorEstado del callback, no variable static local
  if (extractorOn != extractorEstado) {
    if (millis() - lastExtractorPublish > PUBLISH_COOLDOWN) {
      lastExtractorPublish = millis();
      Serial.printf("🌀 Cambiando extractor: %s -> %s\n", extractorEstado ? "ON" : "OFF", extractorOn ? "ON" : "OFF");
      controlarSonoff(SONOFF3_TOPIC, extractorOn);
    }
  }
}

void controlIntractor() {
  if (modoIntractor == 0) return;
  bool intractorOn = false;
  
  if (modoIntractor == 1) {
    time_t now;
    time(&now);
    intractorOn = (localtime(&now)->tm_min < 15);
  } else {
    float h = ultimaHumedadValida;
    if (h > 74) intractorOn = true;
  }
  intractorEstado = intractorOn;
}

void enviarReporteDiario() {
  time_t now;
  time(&now);
  struct tm *tm = localtime(&now);

  if (tm->tm_hour == 20 && !dailyReportSent && readingsCount > 0) {
    float avgTemp = sumTemp / readingsCount;
    float avgHA = sumHA / readingsCount;
    float avgSuelo = sumSuelo / readingsCount;
    float avgVPD = sumVPD / readingsCount;
    float avgPresion = sumPresion / readingsCount;

    String mensaje = "📊 *RESUMEN DIARIO*\n\n🌡️ Temp: " + String(avgTemp,1) + "°C\n💧 Humedad: " + String(avgHA,1) + "%\n🌍 Suelo: " + String(avgSuelo,1) + "%\n📊 VPD: " + String(avgVPD,2) + " kPa\n🌬️ Presión: " + String(avgPresion,1) + " hPa\n📆 Semana: " + String(semanaCultivo);
    
    enviarTelegram(mensaje);

    dailyReportSent = true;
    sumTemp = sumHA = sumSuelo = sumVPD = sumPresion = 0;
    readingsCount = 0;
    maxTemp = 0; minTemp = 100;
    maxHA = 0; minHA = 100;
    maxPresion = 0; minPresion = 1000;
    reiniciarContadoresDiarios();
  }
  
  if (tm->tm_hour != 20) dailyReportSent = false;
}

// ================== DASHBOARD COMPLETO ==================

void handleRoot() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  float temperaturaActual = temp.temperature;
  float humedadActual = humidity.relative_humidity;
  float presionActual = bmp.readPressure() / 100.0F;
  
  float es = 0.6112 * exp((17.67 * temperaturaActual) / (temperaturaActual + 243.5));
  float ea = (humedadActual / 100.0) * es;
  float vpdActual = es - ea;
  
  float suma = 0;
  for (int j = 0; j < 4; j++) {
    long sumaRaw = 0;
    for (int k = 0; k < 20; k++) sumaRaw += analogRead(soilPins[j]);
    float porc = map(sumaRaw / 20, dryValue, wetValue, 0, 100);
    suma += constrain(porc, 0, 100);
  }
  float humSueloProm = suma / 4.0;
  
  String tendenciaTemp = getTendencia(tempHistory, historyCount);
  String tendenciaVPD = getTendencia(vpdHistory, historyCount);
  String interpretacion = interpretarPresion(presionActual);
  
  String modoExtractorStr = (modoExtractor == 0) ? "Manual" : ((modoExtractor == 1) ? "Intermitente" : "Automático");
  String modoIntractorStr = (modoIntractor == 0) ? "Manual" : ((modoIntractor == 1) ? "Intermitente" : "Automático");
  String modoLucesStr = lucesManualMode ? "Manual" : "Automático";
  String cicloLucesStr = modoFloracion ? "Floración 12/12" : "Vegetativo 17/7";
  
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>🌱 Grow Control - Dashboard</title>"
                "<style>"
                "*{margin:0;padding:0;box-sizing:border-box;}"
                "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,'Helvetica Neue',Arial,sans-serif;background:#F4F6F9;padding:24px;color:#1A1A2E;}"
                ".container{max-width:1280px;margin:0 auto;}"
                
                /* HEADER */
                ".header{display:flex;justify-content:space-between;align-items:center;margin-bottom:32px;flex-wrap:wrap;gap:16px;}"
                ".header h1{font-size:24px;font-weight:600;color:#1A1A2E;display:flex;align-items:center;gap:8px;}"
                ".badge{background:#E8ECF4;padding:6px 14px;border-radius:20px;font-size:12px;color:#0052CC;font-family:monospace;}"
                
                /* TARJETAS */
                ".dashboard{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:24px;margin-bottom:32px;}"
                ".card{background:#FFFFFF;border-radius:16px;padding:24px;box-shadow:0 2px 8px rgba(0,0,0,0.04);border:1px solid #E8ECF4;transition:box-shadow 0.2s;}"
                ".card:hover{box-shadow:0 8px 24px rgba(0,0,0,0.08);}"
                ".card h3{font-size:13px;text-transform:uppercase;letter-spacing:0.5px;color:#6C757D;margin-bottom:12px;}"
                ".card .value{font-size:36px;font-weight:700;color:#1A1A2E;}"
                ".card .unit{font-size:14px;font-weight:400;color:#6C757D;margin-left:4px;}"
                ".card .trend{font-size:12px;margin-top:8px;color:#6C757D;}"
                
                /* PANEL DE CONTROL */
                ".panel-controls{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:24px;margin-bottom:32px;}"
                ".control-group{background:#FFFFFF;border-radius:16px;padding:20px;border:1px solid #E8ECF4;}"
                ".control-group h3{font-size:16px;font-weight:600;margin-bottom:16px;color:#1A1A2E;display:flex;align-items:center;gap:8px;}"
                ".button-group{display:grid;grid-template-columns:repeat(auto-fit,minmax(100px,1fr));gap:10px;margin-bottom:12px;}"
                ".btn{display:inline-block;padding:10px 16px;border-radius:8px;text-decoration:none;text-align:center;font-weight:500;font-size:13px;transition:all 0.2s;border:none;cursor:pointer;}"
                ".btn:hover{opacity:0.85;transform:translateY(-1px);}"
                ".btn-primary{background:#0052CC;color:white;}"
                ".btn-danger{background:#E53935;color:white;}"
                ".btn-secondary{background:#E8ECF4;color:#1A1A2E;}"
                ".btn-active{background:#00A86B;color:white;box-shadow:0 0 0 2px #00A86B33;}"
                ".badge-status{display:inline-block;padding:4px 12px;border-radius:20px;font-size:12px;font-weight:500;background:#F4F6F9;color:#1A1A2E;margin-top:12px;}"
                ".badge-status.on{background:#00A86B;color:white;}"
                ".badge-status.off{background:#E53935;color:white;}"
                
                /* INFO SECTION */
                ".info-section{background:#FFFFFF;border-radius:16px;padding:20px;margin-top:24px;border:1px solid #E8ECF4;}"
                ".info-section h3{font-size:16px;font-weight:600;margin-bottom:16px;display:flex;align-items:center;gap:8px;}"
                ".config-row{display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:12px;margin-bottom:16px;}"
                ".semana-input{width:80px;padding:10px;border:1px solid #E8ECF4;border-radius:8px;font-size:14px;text-align:center;}"
                ".separator{height:1px;background:#E8ECF4;margin:20px 0;}"
                
                /* HISTORIAL */
                ".historial{background:#FFFFFF;border-radius:16px;margin-top:32px;border:1px solid #E8ECF4;overflow:auto;}"
                ".historial h3{padding:20px 20px 0 20px;font-size:16px;font-weight:600;display:flex;align-items:center;gap:8px;}"
                "table{width:100%;border-collapse:collapse;font-size:13px;}"
                "th{text-align:left;padding:16px 20px;background:#F8FAFE;font-weight:600;color:#1A1A2E;border-bottom:1px solid #E8ECF4;}"
                "td{padding:12px 20px;border-bottom:1px solid #F0F2F5;}"
                "tr:hover td{background:#F8FAFE;}"
                
                /* FOOTER */
                ".footer{text-align:center;margin-top:32px;font-size:12px;color:#6C757D;}"
                "@media(max-width:640px){body{padding:16px;}.card .value{font-size:28px;}}"
                "</style>"
                "</head><body>"
                "<div class='container'>"
                
                "<!-- HEADER -->"
                "<div class='header'>"
                "<h1>🌱 Grow Control</h1>"
                "<div class='badge'>📡 " + WiFi.localIP().toString() + "</div>"
                "</div>"
                
                "<!-- TARJETAS -->"
                "<div class='dashboard'>"
                "<div class='card'><h3>🌡️ Temperatura</h3><div class='value'>" + String(temperaturaActual,1) + "<span class='unit'>°C</span></div><div class='trend'>" + tendenciaTemp + "</div></div>"
                "<div class='card'><h3>💧 Humedad</h3><div class='value'>" + String(humedadActual,1) + "<span class='unit'>%</span></div><div class='trend'>Óptimo: " + String(getHumedadSueloOptima()) + "%</div></div>"
                "<div class='card'><h3>🌍 Suelo</h3><div class='value'>" + String(humSueloProm,1) + "<span class='unit'>%</span></div><div class='trend'>Riego &lt; " + String(sueloMinRiego) + "%</div></div>"
                "<div class='card'><h3>📊 VPD</h3><div class='value'>" + String(vpdActual,2) + "<span class='unit'>kPa</span></div><div class='trend'>Objetivo: " + String(getVPDObjetivo(),2) + " kPa</div></div>"
                "</div>"
                
                "<!-- PANEL DE CONTROL -->"
                "<div class='panel-controls'>"
                
                "<!-- RIEGO -->"
                "<div class='control-group'>"
                "<h3>💧 Riego</h3>"
                "<div class='button-group'><a href='/riego' class='btn btn-primary'>🚿 Riego Manual</a></div>"
                "</div>"
                
                "<!-- LUCES -->"
                "<div class='control-group'>"
                "<h3>💡 Luces</h3>"
                "<div class='button-group'>"
                "<a href='/lucesManual?estado=on' class='btn btn-primary'>💡 Encender (1h)</a>"
                "<a href='/lucesManual?estado=off' class='btn btn-danger'>🌙 Apagar (1h)</a>"
                "</div>"
                "<div class='button-group'>"
                "<a href='/luces?modo=flor' class='btn " + String(modoFloracion ? "btn-active" : "btn-secondary") + "'>🌙 Floración</a>"
                "<a href='/luces?modo=veg' class='btn " + String(!modoFloracion ? "btn-active" : "btn-secondary") + "'>☀️ Vegetativo</a>"
                "</div>"
                "</div>"
                
                "<!-- EXTRACTOR -->"
                "<div class='control-group'>"
                "<h3>🌀 Extractor</h3>"
                "<div class='button-group'>"
                "<a href='/manualExtractor?estado=on' class='btn btn-primary'>🔧 ON</a>"
                "<a href='/manualExtractor?estado=off' class='btn btn-secondary'>🔧 OFF</a>"
                "<a href='/modoExtractor?modo=1' class='btn btn-secondary'>⏱️ Intermitente</a>"
                "<a href='/modoExtractor?modo=2' class='btn btn-secondary'>🤖 Automático</a>"
                "</div>"
                "<div class='badge-status " + String(extractorEstado ? "on" : "off") + "'>Estado: " + String(extractorEstado ? "ON" : "OFF") + " | Modo: " + modoExtractorStr + "</div>"
                "</div>"
                
                "<!-- INTRACTOR -->"
                "<div class='control-group'>"
                "<h3>🌬️ Intractor</h3>"
                "<div class='button-group'>"
                "<a href='/manualIntractor?estado=on' class='btn btn-primary'>🔧 ON</a>"
                "<a href='/manualIntractor?estado=off' class='btn btn-secondary'>🔧 OFF</a>"
                "<a href='/modoIntractor?modo=1' class='btn btn-secondary'>⏱️ Intermitente</a>"
                "<a href='/modoIntractor?modo=2' class='btn btn-secondary'>🤖 Automático</a>"
                "</div>"
                "<div class='badge-status " + String(intractorEstado ? "on" : "off") + "'>Estado: " + String(intractorEstado ? "ON" : "OFF") + " | Modo: " + modoIntractorStr + "</div>"
                "</div>"
                "</div>"
                
                "<!-- CONFIGURACIÓN -->"
                "<div class='info-section'>"
                "<h3>📊 Resumen del Cultivo</h3>"
                "<div class='config-row'><span>Presión atmosférica:</span><strong>" + String(presionActual,1) + " hPa | " + interpretacion + "</strong></div>"
                "<div class='config-row'><span>Semana de cultivo:</span><strong>" + String(semanaCultivo) + " (" + cicloLucesStr + ")</strong></div>"
                "<div class='config-row'><span>VPD objetivo:</span><strong>" + String(getVPDObjetivo(),2) + " kPa</strong></div>"
                "<div class='separator'></div>"
                "<form action='/setSemana' method='get' style='display:flex;align-items:center;gap:12px;flex-wrap:wrap;'>"
                "<label style='font-weight:500;'>Cambiar semana:</label>"
                "<input type='number' name='semana' min='1' max='8' value='" + String(semanaCultivo) + "' class='semana-input'>"
                "<input type='submit' value='Actualizar' class='btn btn-primary' style='padding:10px 20px;'>"
                "<a href='/config' class='btn btn-secondary'>⚙️ Configuración avanzada</a>"
                "<a href='/' class='btn btn-secondary'>🔄 Refrescar</a>"
                "</form>"
                "</div>"
                
                "<!-- HISTORIAL -->"
                "<div class='historial'>"
                "<h3>📜 Historial de lecturas</h3>"
                + generarHistorialHTML() +
                "</div>"
                
                "<div class='footer'>🌱 Grow Control v2.0 | " + WiFi.localIP().toString() + "</div>"
                "</div>"
                "<script>setTimeout(()=>{location.reload()},30000);</script>"
                "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleRiegoManual() {
  if (!riegoEnProgreso) {
    controlarSonoff(SONOFF1_TOPIC, true);
    riegoEnProgreso = true;
    lastRiegoStart = millis();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleModoLuces() {
  if (server.hasArg("modo")) {
    modoFloracion = (server.arg("modo") == "flor");
    lucesManualMode = false;
    guardarEstado();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleLucesManual() {
  if (server.hasArg("estado")) {
    lucesManualMode = true;
    lucesManualState = (server.arg("estado") == "on");
    lucesManualTimeout = millis();
    controlarSonoff(SONOFF2_TOPIC, lucesManualState);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleModoExtractor() {
  if (server.hasArg("modo")) {
    modoExtractor = server.arg("modo").toInt();
    guardarEstado();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleModoIntractor() {
  if (server.hasArg("modo")) {
    modoIntractor = server.arg("modo").toInt();
    guardarEstado();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleManualExtractor() {
  if (server.hasArg("estado")) {
    controlarSonoff(SONOFF3_TOPIC, server.arg("estado") == "on");
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleManualIntractor() {
  if (server.hasArg("estado")) {
    intractorEstado = (server.arg("estado") == "on");
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetSemana() {
  if (server.hasArg("semana")) {
    int nuevaSemana = server.arg("semana").toInt();
    if (nuevaSemana >= 1 && nuevaSemana <= 8) {
      semanaCultivo = nuevaSemana;
      guardarEstado();
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetVPDTarget() {
  String html = "<h2>VPD Objetivo: " + String(getVPDObjetivo(),2) + " kPa</h2><p>Semana: " + String(semanaCultivo) + "</p><a href='/'>Volver</a>";
  server.send(200, "text/html", html);
}

// ================== CONFIGURACIÓN AVANZADA ==================

void handleConfig() {
  // Leer configuración actual desde SPIFFS
  String current_ssid = "";
  String current_mqtt_server = "";
  String current_mqtt_user = "";
  String current_telegram_token = "";
  String current_chat_id = "";
  int current_semana = semanaCultivo;
  String current_modo = modoFloracion ? "floracion" : "vegetativo";
  
  File file = SPIFFS.open("/config.txt", "r");
  if (file) {
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      int sep = line.indexOf('=');
      if (sep > 0) {
        String key = line.substring(0, sep);
        String value = line.substring(sep + 1);
        if (key == "ssid") current_ssid = value;
        else if (key == "mqtt_server") current_mqtt_server = value;
        else if (key == "mqtt_user") current_mqtt_user = value;
        else if (key == "telegram_token") current_telegram_token = value;
        else if (key == "chat_id") current_chat_id = value;
      }
    }
    file.close();
  }
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>🌱 Grow Control - Configuración</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
            background: #F4F6F9;
            margin: 0;
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 24px;
        }

        .container {
            background: #FFFFFF;
            border-radius: 16px;
            padding: 32px;
            max-width: 560px;
            width: 100%;
            box-shadow: 0 4px 20px rgba(0, 0, 0, 0.08);
            border: 1px solid #E8ECF4;
        }

        h1 {
            font-size: 24px;
            font-weight: 600;
            color: #1A1A2E;
            text-align: center;
            margin-bottom: 8px;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
        }

        .subtitle {
            text-align: center;
            color: #6C757D;
            margin-bottom: 32px;
            font-size: 14px;
        }

        .section {
            background: #F8FAFE;
            padding: 20px;
            border-radius: 12px;
            margin-bottom: 24px;
            border: 1px solid #E8ECF4;
        }

        .section h3 {
            margin-top: 0;
            margin-bottom: 16px;
            color: #1A1A2E;
            font-size: 16px;
            font-weight: 600;
            display: flex;
            align-items: center;
            gap: 8px;
        }

        .form-group {
            margin-bottom: 16px;
        }

        .form-group:last-child {
            margin-bottom: 0;
        }

        label {
            display: block;
            margin-bottom: 8px;
            font-weight: 500;
            font-size: 13px;
            color: #1A1A2E;
        }

        input, select {
            width: 100%;
            padding: 10px 12px;
            border: 1px solid #E8ECF4;
            border-radius: 8px;
            font-size: 14px;
            transition: all 0.2s;
            background: #FFFFFF;
        }

        input:focus, select:focus {
            outline: none;
            border-color: #0052CC;
            box-shadow: 0 0 0 2px rgba(0, 82, 204, 0.1);
        }

        small {
            font-size: 11px;
            color: #6C757D;
            display: block;
            margin-top: 4px;
        }

        button {
            width: 100%;
            padding: 12px;
            background: #0052CC;
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 14px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
            margin-top: 8px;
        }

        button:hover {
            background: #0041a8;
            transform: translateY(-1px);
        }

        button.danger {
            background: #E53935;
        }

        button.danger:hover {
            background: #c62828;
        }

        .current-values {
            background: #F8FAFE;
            padding: 12px;
            border-radius: 8px;
            font-size: 12px;
            margin-bottom: 20px;
            border: 1px solid #E8ECF4;
            color: #1A1A2E;
        }

        .status {
            text-align: center;
            padding: 12px;
            margin-top: 16px;
            border-radius: 8px;
            font-size: 13px;
            font-weight: 500;
            display: none;
        }

        .status.success {
            background: #00A86B;
            color: white;
            display: block;
        }

        .status.error {
            background: #E53935;
            color: white;
            display: block;
        }

        hr {
            margin: 24px 0 16px 0;
            border: none;
            border-top: 1px solid #E8ECF4;
        }

        .footer-link {
            text-align: center;
            margin-top: 20px;
        }

        .footer-link a {
            color: #0052CC;
            text-decoration: none;
            font-size: 13px;
        }

        .footer-link a:hover {
            text-decoration: underline;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌱 Grow Control</h1>
        <div class="subtitle">Configuración avanzada</div>
        
        <div class="current-values">
            📡 Configuración actual:<br>
            WiFi: <strong>)rawliteral" + current_ssid + R"rawliteral(</strong><br>
            MQTT: <strong>)rawliteral" + current_mqtt_server + R"rawliteral(</strong><br>
            Telegram: <strong>)rawliteral" + (current_telegram_token.length() > 0 ? "Configurado ✓" : "No configurado") + R"rawliteral(</strong>
        </div>
        
        <form id="configForm">
            <div class="section">
                <h3>📡 WiFi</h3>
                <div class="form-group">
                    <label>SSID</label>
                    <input type="text" name="ssid" placeholder="Nombre de tu red WiFi" value=")rawliteral" + current_ssid + R"rawliteral(" required>
                </div>
                <div class="form-group">
                    <label>Contraseña</label>
                    <input type="password" name="pass" placeholder="Contraseña WiFi">
                    <small>Dejar en blanco para mantener la actual</small>
                </div>
            </div>
            
            <div class="section">
                <h3>📨 MQTT (Mosquitto)</h3>
                <div class="form-group">
                    <label>Servidor</label>
                    <input type="text" name="mqtt_server" placeholder="192.168.1.x" value=")rawliteral" + current_mqtt_server + R"rawliteral(" required>
                </div>
                <div class="form-group">
                    <label>Puerto</label>
                    <input type="text" name="mqtt_port" value="1883">
                </div>
                <div class="form-group">
                    <label>Usuario</label>
                    <input type="text" name="mqtt_user" placeholder="Usuario MQTT" value=")rawliteral" + current_mqtt_user + R"rawliteral(">
                </div>
                <div class="form-group">
                    <label>Contraseña</label>
                    <input type="password" name="mqtt_password" placeholder="Password MQTT">
                    <small>Dejar en blanco para mantener la actual</small>
                </div>
            </div>
            
            <div class="section">
                <h3>🤖 Telegram</h3>
                <div class="form-group">
                    <label>Bot Token</label>
                    <input type="text" name="telegram_token" placeholder="1234567890:ABCdefGHIjkl" value=")rawliteral" + current_telegram_token + R"rawliteral(" required>
                </div>
                <div class="form-group">
                    <label>Chat ID</label>
                    <input type="text" name="chat_id" placeholder="123456789" value=")rawliteral" + current_chat_id + R"rawliteral(" required>
                </div>
            </div>
            
            <div class="section">
                <h3>🌱 Configuración de cultivo</h3>
                <div class="form-group">
                    <label>Semana actual</label>
                    <select name="semana_inicial">
                        <option value="1" )rawliteral" + String(current_semana == 1 ? "selected" : "") + R"rawliteral(>Semana 1</option>
                        <option value="2" )rawliteral" + String(current_semana == 2 ? "selected" : "") + R"rawliteral(>Semana 2</option>
                        <option value="3" )rawliteral" + String(current_semana == 3 ? "selected" : "") + R"rawliteral(>Semana 3</option>
                        <option value="4" )rawliteral" + String(current_semana == 4 ? "selected" : "") + R"rawliteral(>Semana 4</option>
                        <option value="5" )rawliteral" + String(current_semana == 5 ? "selected" : "") + R"rawliteral(>Semana 5</option>
                        <option value="6" )rawliteral" + String(current_semana == 6 ? "selected" : "") + R"rawliteral(>Semana 6</option>
                        <option value="7" )rawliteral" + String(current_semana == 7 ? "selected" : "") + R"rawliteral(>Semana 7</option>
                        <option value="8" )rawliteral" + String(current_semana == 8 ? "selected" : "") + R"rawliteral(>Semana 8</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>Modo</label>
                    <select name="modo_inicial">
                        <option value="floracion" )rawliteral" + String(current_modo == "floracion" ? "selected" : "") + R"rawliteral(>Floración (12/12)</option>
                        <option value="vegetativo" )rawliteral" + String(current_modo == "vegetativo" ? "selected" : "") + R"rawliteral(>Vegetativo (17/7)</option>
                    </select>
                </div>
            </div>
            
            <button type="submit">💾 Guardar y Reiniciar</button>
        </form>
        
        <hr>
        
        <button id="resetBtn" class="danger">⚠️ Reset de Fábrica (borrar configuración)</button>
        
        <div id="status" class="status"></div>
        
        <div class="footer-link">
            <a href="/">← Volver al Dashboard</a>
        </div>
    </div>
    
    <script>
        document.getElementById('configForm').addEventListener('submit', async (e) => {
            e.preventDefault();
            const formData = new FormData(e.target);
            const statusDiv = document.getElementById('status');
            
            statusDiv.className = 'status';
            statusDiv.textContent = '⏳ Guardando configuración...';
            
            try {
                const response = await fetch('/saveconfig', {
                    method: 'POST',
                    body: formData
                });
                const result = await response.text();
                if (response.ok) {
                    statusDiv.className = 'status success';
                    statusDiv.textContent = '✅ ' + result;
                    setTimeout(() => { window.location.href = '/'; }, 3000);
                } else {
                    statusDiv.className = 'status error';
                    statusDiv.textContent = '❌ Error: ' + result;
                }
            } catch (error) {
                statusDiv.className = 'status error';
                statusDiv.textContent = '❌ Error de conexión';
            }
        });
        
        document.getElementById('resetBtn').addEventListener('click', async () => {
            if (confirm('⚠️ ¿Estás seguro? Esto borrará TODA la configuración y reiniciará el ESP32.')) {
                const statusDiv = document.getElementById('status');
                statusDiv.className = 'status';
                statusDiv.textContent = '⏳ Borrando configuración...';
                
                try {
                    const response = await fetch('/resetconfig', { method: 'POST' });
                    const result = await response.text();
                    statusDiv.className = 'status success';
                    statusDiv.textContent = '✅ ' + result;
                    setTimeout(() => { window.location.href = '/'; }, 3000);
                } catch (error) {
                    statusDiv.className = 'status error';
                    statusDiv.textContent = '❌ Error de conexión';
                }
            }
        });
    </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleSaveConfig() {
  if (server.hasArg("ssid")) config_ssid = server.arg("ssid");
  if (server.hasArg("pass") && server.arg("pass").length() > 0) config_password = server.arg("pass");
  if (server.hasArg("mqtt_server")) config_mqtt_server = server.arg("mqtt_server");
  if (server.hasArg("mqtt_port")) config_mqtt_port = server.arg("mqtt_port").toInt();
  if (server.hasArg("mqtt_user")) config_mqtt_user = server.arg("mqtt_user");
  if (server.hasArg("mqtt_password") && server.arg("mqtt_password").length() > 0) config_mqtt_password = server.arg("mqtt_password");
  if (server.hasArg("telegram_token")) config_telegram_token = server.arg("telegram_token");
  if (server.hasArg("chat_id")) config_chat_id = server.arg("chat_id");
  if (server.hasArg("semana_inicial")) {
    config_semana_inicial = server.arg("semana_inicial").toInt();
    semanaCultivo = config_semana_inicial;
    guardarEstado();
  }
  if (server.hasArg("modo_inicial")) {
    config_modo_floracion = (server.arg("modo_inicial") == "floracion");
    modoFloracion = config_modo_floracion;
    guardarEstado();
  }
  
  guardarConfiguracion();
  server.send(200, "text/plain", "Configuración guardada. Reiniciando...");
  delay(1000);
  ESP.restart();
}

void handleResetConfig() {
  SPIFFS.remove("/config.txt");
  server.send(200, "text/plain", "Configuración borrada. Reiniciando...");
  delay(1000);
  ESP.restart();
}
