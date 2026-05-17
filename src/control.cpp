#include <Arduino.h>
#include <time.h>
#include <Preferences.h>
#include "control.h"
#include "config.h"
#include "sensors.h"
#include "mqtt.h"
#include "telegram.h"

void controlLuces() {
  static unsigned long lastPreCheck = 0;
  if (millis() - lastPreCheck > 30000) {
    lastPreCheck = millis();
    if (mqttClient.connected()) {
      mqttClient.publish("cmnd/sonoff_luz/status", "11");
      delay(100);
      mqttClient.loop();
    }
  }
  if (lucesManualMode && millis() - lucesManualTimeout > lucesManualDuration) {
    lucesManualMode = false;
  }
  bool lucesOn;
  if (lucesManualMode) {
    lucesOn = lucesManualState;
  } else {
    static unsigned long lastLightsCheck = 0;
    if (millis() - lastLightsCheck < 60000) return;
    lastLightsCheck = millis();
    time_t now;
    time(&now);
    int hora = localtime(&now)->tm_hour;
    lucesOn = (modoFloracion) ? (hora >= 6 && hora < 18) : (hora >= 1 && hora < 18);
  }
  if (lucesOn != luzEstado) {
    if (millis() - lastLuzPublish > PUBLISH_COOLDOWN) {
      lastLuzPublish = millis();
      controlarSonoff(SONOFF2_TOPIC, lucesOn);
    }
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
    float vpd = computeVPD(t, h);
    float vpdObjetivo = getVPDObjetivo();
    if (vpd - vpdObjetivo > 0.3) extractorOn = true;
    else if (vpd - vpdObjetivo > 0.1) extractorOn = true;
    else if (vpd - vpdObjetivo < -0.2) extractorOn = false;
    if (t > 29.0) extractorOn = true;
    if (h > 80) extractorOn = true;
  }
  if (extractorOn != extractorEstado) {
    if (millis() - lastExtractorPublish > PUBLISH_COOLDOWN) {
      lastExtractorPublish = millis();
      controlarSonoff(SONOFF3_TOPIC, extractorOn);
    }
  }
}

void enviarReporteDiario() {
  time_t now;
  time(&now);
  struct tm *tm = localtime(&now);

  // Construir string con la fecha actual (año-mes-día)
  String today = String(tm->tm_year + 1900) + "-" + String(tm->tm_mon + 1) + "-" + String(tm->tm_mday);
  
  // Leer la última fecha de envío guardada en Preferences (sin generar error si no existe)
  Preferences prefs;
  prefs.begin("grow", true);
  String lastReportDate = "";
  if (prefs.isKey("lastReportDate")) {
    lastReportDate = prefs.getString("lastReportDate", "");
  }
  prefs.end();

  // Condición: son las 20:00, no se ha enviado hoy, hay suficientes lecturas, y no se ha enviado ya hoy
  if (tm->tm_hour == 20 && !dailyReportSent && readingsCount >= 10 && lastReportDate != today) {
    float avgTemp = sumTemp / readingsCount;
    float avgHA = sumHA / readingsCount;
    float avgSuelo = sumSuelo / readingsCount;
    float avgVPD = sumVPD / readingsCount;
    float avgPresion = sumPresion / readingsCount;
    String mensaje = "📊 *RESUMEN DIARIO*\n\n🌡️ Temp: " + String(avgTemp,1) + "°C\n💧 Humedad: " + String(avgHA,1) + "%\n🌍 Suelo: " + String(avgSuelo,1) + "%\n📊 VPD: " + String(avgVPD,2) + " kPa\n🌬️ Presión: " + String(avgPresion,1) + " hPa\n📆 Semana: " + String(semanaCultivo);
    enviarTelegram(mensaje);
    
    dailyReportSent = true;

    // Guardar la fecha de hoy en Preferences para evitar duplicados
    prefs.begin("grow", false);
    prefs.putString("lastReportDate", today);
    prefs.end();
    
    sumTemp = sumHA = sumSuelo = sumVPD = sumPresion = 0;
    readingsCount = 0;
    maxTemp = 0; minTemp = 100;
    maxHA = 0; minHA = 100;
    maxPresion = 0; minPresion = 1000;
  }
  
  // Reiniciar el flag si no son las 20:00
  if (tm->tm_hour != 20) dailyReportSent = false;
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