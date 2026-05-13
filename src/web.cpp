#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncOTA.h>
#include <SPIFFS.h>
#include "web.h"
#include "config.h"
#include "sensors.h"
#include "mqtt.h"
#include "control.h"
#include "historial.h"

extern AsyncWebServer server;

void initWebServer() {
  if (adminUser.length() == 0) adminUser = "admin";
  if (adminPass.length() == 0) adminPass = "adminfumon";

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str()))
        return request->requestAuthentication();
    handleRoot(request);
  });

  // OTA - interfaz estática
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/ota.html", "text/html");
  });

  // Rutas que requieren autenticación y lógica C++
  server.on("/riego", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str()))
      return request->requestAuthentication();
    handleRiegoManual(request);
  });

  server.on("/luces", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str()))
      return request->requestAuthentication();
    handleModoLuces(request);
  });

  server.on("/lucesManual", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str()))
      return request->requestAuthentication();
    handleLucesManual(request);
  });

  server.on("/modoExtractor", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str()))
      return request->requestAuthentication();
    handleModoExtractor(request);
  });

  server.on("/manualExtractor", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str()))
      return request->requestAuthentication();
    handleManualExtractor(request);
  });

  server.on("/setSemana", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str()))
      return request->requestAuthentication();
    handleSetSemana(request);
  });

  server.on("/setVPD", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str()))
      return request->requestAuthentication();
    handleSetVPDTarget(request);
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str()))
      return request->requestAuthentication();
    handleConfig(request);
  });

  server.on("/saveconfig", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str()))
      return request->requestAuthentication();
    handleSaveConfig(request);
  });

  server.on("/resetconfig", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str()))
      return request->requestAuthentication();
    handleResetConfig(request);
  });

  AsyncOTA.begin(&server);
  server.begin();
}

void handleRiegoManual(AsyncWebServerRequest *request) {
  if (!riegoEnProgreso) {
    controlarSonoff(SONOFF1_TOPIC, true);
    riegoEnProgreso = true;
    lastRiegoStart = millis();
  }
  request->redirect("/");
}

void handleModoLuces(AsyncWebServerRequest *request) {
  if (request->hasArg("modo")) {
    modoFloracion = (request->arg("modo") == "flor");
    lucesManualMode = false;
    guardarEstado();
  }
  request->redirect("/");
}

void handleLucesManual(AsyncWebServerRequest *request) {
  if (request->hasArg("estado")) {
    lucesManualMode = true;
    lucesManualState = (request->arg("estado") == "on");
    lucesManualTimeout = millis();
    controlarSonoff(SONOFF2_TOPIC, lucesManualState);
  }
  request->redirect("/");
}

void handleModoExtractor(AsyncWebServerRequest *request) {
  if (request->hasArg("modo")) {
    modoExtractor = request->arg("modo").toInt();
    guardarEstado();
    // Forzar una actualización inmediata del estado del extractor
    controlExtractor();
  }
  request->redirect("/");
}

void handleManualExtractor(AsyncWebServerRequest *request) {
  if (request->hasArg("estado")) {
    bool encender = (request->arg("estado") == "on");
    Serial.printf("🔧 ManualExtractor: estado=%s\n", encender ? "ON" : "OFF");
    controlarSonoff(SONOFF3_TOPIC, encender);
    Serial.printf("   modoExtractor antes=%d\n", modoExtractor);
    modoExtractor = 0;
    Serial.printf("   modoExtractor después=%d\n", modoExtractor);
    guardarEstado();
  } else {
    Serial.println("⚠️ ManualExtractor: no hay argumento 'estado'");
  }
  request->redirect("/");
}

void handleSetSemana(AsyncWebServerRequest *request) {
  if (request->hasArg("semana")) {
    int nuevaSemana = request->arg("semana").toInt();
    if (nuevaSemana >= 1 && nuevaSemana <= 8) {
      semanaCultivo = nuevaSemana;
      guardarEstado();
    }
  }
  request->redirect("/");
}

void handleSetVPDTarget(AsyncWebServerRequest *request) {
  String html = "<h2>VPD Objetivo: " + String(getVPDObjetivo(),2) + " kPa</h2><p>Semana: " + String(semanaCultivo) + "</p><a href='/'>Volver</a>";
  request->send(200, "text/html", html);
}

void handleConfig(AsyncWebServerRequest *request) {
    File file = SPIFFS.open("/config.html", "r");
    if (!file) {
        request->send(500, "text/plain", "config.html not found");
        return;
    }
    String html = file.readString();
    file.close();

    // Leer valores actuales del archivo config.txt
    String current_ssid = "";
    String current_mqtt_server = "";
    String current_mqtt_user = "";
    String current_telegram_token = "";
    String current_chat_id = "";
    int current_semana = semanaCultivo;
    String current_modo = modoFloracion ? "floracion" : "vegetativo";

    File configFile = SPIFFS.open("/config.txt", "r");
    if (configFile) {
        while (configFile.available()) {
            String line = configFile.readStringUntil('\n');
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
        configFile.close();
    }

    // Reemplazar marcadores
    html.replace("%SSID%", current_ssid);
    html.replace("%MQTT_SERVER%", current_mqtt_server);
    html.replace("%MQTT_USER%", current_mqtt_user);
    html.replace("%TELEGRAM_TOKEN%", current_telegram_token);
    html.replace("%CHAT_ID%", current_chat_id);
    html.replace("%TELEGRAM_STATUS%", current_telegram_token.length() > 0 ? "Configurado ✓" : "No configurado");
    
    // Reemplazar selección de semana
    for (int i = 1; i <= 8; i++) {
        String marker = "%SEL_SEMANA" + String(i) + "%";
        html.replace(marker, (current_semana == i) ? "selected" : "");
    }
    
    // Reemplazar selección de modo
    html.replace("%SEL_FLOR%", (current_modo == "floracion") ? "selected" : "");
    html.replace("%SEL_VEG%", (current_modo == "vegetativo") ? "selected" : "");

    request->send(200, "text/html", html);
}

void handleRoot(AsyncWebServerRequest *request) {
    File file = SPIFFS.open("/dashboard.html", "r");
    if (!file) {
        request->send(500, "text/plain", "dashboard.html not found");
        return;
    }
    String html = file.readString();
    file.close();

    // Obtener valores actuales (igual que antes)
    float temp, hum;
    if (!readTempHumi(temp, hum)) {
        temp = ultimaTempValida;
        hum = ultimaHumedadValida;
    }
    float pres = readPressure();
    float vpd = computeVPD(temp, hum);
    float humSuelo = readSoilMoisture();
    String tendenciaTemp = getTendencia(tempHistory, historyCount);
    String interpretacion = interpretarPresion(pres);
    String modoExtractorStr = (modoExtractor == 0) ? "Manual" : ((modoExtractor == 1) ? "Intermitente" : "Automático");
    String modoIntractorStr = (modoIntractor == 0) ? "Manual" : ((modoIntractor == 1) ? "Intermitente" : "Automático");
    String cicloLucesStr = modoFloracion ? "Floración 12/12" : "Vegetativo 17/7";

    // Reemplazar marcadores
    html.replace("%IP%", WiFi.localIP().toString());
    html.replace("%TEMP%", String(temp, 1));
    html.replace("%HUM%", String(hum, 1));
    html.replace("%SUELO%", String(humSuelo, 1));
    html.replace("%VPD%", String(vpd, 2));
    html.replace("%PRES%", String(pres, 1));
    html.replace("%TENDENCIA_TEMP%", tendenciaTemp);
    html.replace("%INTERPRETACION%", interpretacion);
    html.replace("%OPTIMO_HUM%", String(getHumedadSueloOptima()));
    html.replace("%RIEGO%", String(sueloMinRiego));
    html.replace("%VPD_OBJ%", String(getVPDObjetivo(), 2));
    html.replace("%SEMANA%", String(semanaCultivo));
    html.replace("%CICLO_LUCES%", cicloLucesStr);
    html.replace("%MODO_EXTRACTOR%", modoExtractorStr);
    html.replace("%MODO_INTRACTOR%", modoIntractorStr);
    html.replace("%EXTRACTOR_ESTADO%", extractorEstado ? "ON" : "OFF");
    html.replace("%EXTRACTOR_CLASE%", extractorEstado ? "on" : "off");
    html.replace("%INTRACTOR_ESTADO%", intractorEstado ? "ON" : "OFF");
    html.replace("%INTRACTOR_CLASE%", intractorEstado ? "on" : "off");
    html.replace("%FLOR_ACTIVE%", modoFloracion ? "btn-active" : "btn-secondary");
    html.replace("%VEG_ACTIVE%", modoFloracion ? "btn-secondary" : "btn-active");
    html.replace("%HISTORIAL%", generarHistorialHTML());

    request->send(200, "text/html", html);
}

void handleSaveConfig(AsyncWebServerRequest *request) {
  if (request->hasArg("ssid")) config_ssid = request->arg("ssid");
  if (request->hasArg("pass") && request->arg("pass").length() > 0) config_password = request->arg("pass");
  if (request->hasArg("mqtt_server")) config_mqtt_server = request->arg("mqtt_server");
  if (request->hasArg("mqtt_port")) config_mqtt_port = request->arg("mqtt_port").toInt();
  if (request->hasArg("mqtt_user")) config_mqtt_user = request->arg("mqtt_user");
  if (request->hasArg("mqtt_password") && request->arg("mqtt_password").length() > 0) config_mqtt_password = request->arg("mqtt_password");
  if (request->hasArg("telegram_token")) config_telegram_token = request->arg("telegram_token");
  if (request->hasArg("chat_id")) config_chat_id = request->arg("chat_id");
  if (request->hasArg("semana_inicial")) {
    config_semana_inicial = request->arg("semana_inicial").toInt();
    semanaCultivo = config_semana_inicial;
    guardarEstado();
  }
  if (request->hasArg("modo_inicial")) {
    config_modo_floracion = (request->arg("modo_inicial") == "floracion");
    modoFloracion = config_modo_floracion;
    guardarEstado();
  }
  guardarConfiguracion();
  request->send(200, "text/plain", "Configuración guardada. Reiniciando...");
  delay(1000);
  ESP.restart();
}

void handleResetConfig(AsyncWebServerRequest *request) {
  SPIFFS.remove("/config.txt");
  request->send(200, "text/plain", "Configuración borrada. Reiniciando...");
  delay(1000);
  ESP.restart();
}