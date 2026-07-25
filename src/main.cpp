#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include <time.h>
#include <AsyncOTA.h>

#include "config.h"
#include "sensors.h"
#include "mqtt.h"
#include "telegram.h"
#include "web.h"
#include "control.h"
#include "historial.h"
#include "utils.h"

// ========== OBJETOS GLOBALES (definiciones) ==========
AsyncWebServer server(80);
Adafruit_BMP280 bmp;
Adafruit_AHTX0 aht;
WiFiClientSecure secured_client;
Preferences prefs;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ========== VARIABLES DE ESTADO ==========
bool configuracionCompleta = false;
bool primeraVez = true;

void setup() {
  Serial.begin(115200);

   // ========== DIAGNÓSTICO DE REINICIOS ==========
  
esp_reset_reason_t reason = esp_reset_reason();
Serial.printf("Motivo del último reinicio: %d\n", reason);
if (reason == ESP_RST_WDT) {
  Serial.println("→ Reinicio por watchdog");  
}
else if (reason == ESP_RST_BROWNOUT) {
  Serial.println("→ Reinicio por brownout (baja tensión)");  
}
else if (reason == ESP_RST_POWERON) {
  Serial.println("→ Reinicio por encendido normal");
}
else if (reason == ESP_RST_SW) {
  Serial.println("→ Reinicio por software (ESP.restart())"); 
}
  // ============================================================

  esp_task_wdt_init(300, true);
  esp_task_wdt_add(NULL);
  Serial.println(F("✅ Watchdog Timer iniciado (300 segundos)"));

  cargarConfiguracion();
  if (adminUser.length() == 0) adminUser = "admin";
  if (adminPass.length() == 0) adminPass = "adminfumon";

  if (config_ssid.length() > 0 && config_telegram_token.length() > 0) {
    configuracionCompleta = true;
    if (conectarWiFi()) {
      initSensors();
      initMQTT();
      initTelegram();

      configTime(-10800, 0, "pool.ntp.org", "time.nist.gov");
      delay(2000);
      esp_task_wdt_reset();

      enviarTelegram("🌱 *GROW CONTROL INICIADO*\n✅ Semana: " + String(semanaCultivo));

      initWebServer();

      for (int i = 0; i < MAX_HISTORIAL; i++) strcpy(historial[i].tiempo, "");
      for (int i = 0; i < TENDENCIA_LECTURAS; i++) {
        tempHistory[i] = 0; humHistory[i] = 0; vpdHistory[i] = 0; presionHistory[i] = 0;
      }
      cargarEstado();

      ArduinoOTA.setHostname("GrowControl");
      ArduinoOTA.begin();
      Serial.println("✅ OTA nativo iniciado");

      lastDailyReport = millis();
    } else {
      iniciarModoAP();
      setupConfigServer();
    }
  } else {
    iniciarModoAP();
    setupConfigServer();
  }
}

void loop() {

  ArduinoOTA.handle();

  if (WiFi.getMode() == WIFI_AP) {
    delay(100);
    return;
  }

  if (!configuracionCompleta) return;

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

  if (millis() - lastSensorRead >= 3600000) {
    lastSensorRead = millis();
    time_t now;
    time(&now);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%d/%m/%Y %H:%M", localtime(&now));

    float temp, hum, pres, vpd, suelo;
    if (readTempHumi(temp, hum)) {
      ultimaTempValida = temp;
      ultimaHumedadValida = hum;
      hayDatosValidos = true;
    } else if (hayDatosValidos) {
      temp = ultimaTempValida;
      hum = ultimaHumedadValida;
    } else {
      return;
    }
    pres = readPressure();
    vpd = computeVPD(temp, hum);
    suelo = readSoilMoisture();

    agregarHistorialTendencia(temp, hum, vpd, pres);
    sumTemp += temp; sumHA += hum; sumSuelo += suelo; sumVPD += vpd; sumPresion += pres;
    readingsCount++;

    agregarAlHistorial(timeStr, temp, hum, suelo, vpd, pres);

    if (mqttClient.connected()) {
      String payload = "grow_sensors,host=ESP32_grow temp=" + String(temp,1) +
                       ",humedad=" + String(hum,1) + ",presion=" + String(pres,1) +
                       ",vpd=" + String(vpd,2) + ",suelo=" + String(suelo,1);
      mqttClient.publish("grow/sensor", payload.c_str());
    }

    // Riego automático
    int ajuste = getRecomendacionRiegoPresion(pres);
    int umbral = sueloMinRiego + ajuste;
    if (umbral < 20) umbral = 20;
    if (suelo < umbral && hum < humAireMin && temp > tempMin && !riegoEnProgreso &&
        (millis() - lastRiegoTime > cooldownRiegoMs || lastRiegoTime == 0)) {
      controlarSonoff(SONOFF1_TOPIC, true);
      riegoEnProgreso = true;
      lastRiegoStart = millis();
      lastRiegoTime = millis();
      enviarTelegram("💧 *Riego automático*");
    }
    if (suelo < sueloMinAlerta) {
      enviarTelegram("🚨 *ALERTA: Suelo seco!* " + String(suelo,1) + "%");
    }
  }

  controlLuces();
  controlExtractor();
  enviarReporteDiario();
  handleTelegramMessages();

  if (riegoEnProgreso && (millis() - lastRiegoStart >= riegoDuration)) {
    controlarSonoff(SONOFF1_TOPIC, false);
    riegoEnProgreso = false;
  }

  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000 && WiFi.status() != WL_CONNECTED) {
    lastWiFiCheck = millis();
    WiFi.reconnect();
  }

  esp_task_wdt_reset();
}
