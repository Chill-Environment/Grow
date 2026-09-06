#include <Arduino.h>
#include <time.h>
#include "historial.h"
#include "config.h"

void agregarHistorialTendencia(float temp, float hum, float vpd, float pres) {
  tempHistory[historyIndex] = temp;
  humHistory[historyIndex] = hum;
  vpdHistory[historyIndex] = vpd;
  presionHistory[historyIndex] = pres;
  historyIndex = (historyIndex + 1) % TENDENCIA_LECTURAS;
  if (historyCount < TENDENCIA_LECTURAS) historyCount++;
}

String getTendencia(const float datos[], int len) {
  if (len < 3) return "Insuficiente";
  float suma = 0;
  for (int i = 0; i < len; i++) suma += datos[i];
  float promedio = suma / len;
  float ultimo = datos[(historyIndex - 1 + TENDENCIA_LECTURAS) % TENDENCIA_LECTURAS];
  if (ultimo > promedio + 0.5) return "⬆️ Subiendo";
  else if (ultimo < promedio - 0.5) return "⬇️ Bajando";
  else return "➡️ Estable";
}

void agregarAlHistorial(const char* tiempo, float temp, float humedad, float suelo, float vpd, float presion) {
  snprintf(historial[historialIndex].tiempo, sizeof(historial[historialIndex].tiempo), "%s", tiempo);
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
  String htmlString generarHistorialHTML() {
  // Encabezado con 9 columnas (agregamos Intractor)
  String html = "<table border='0' cellpadding='5' cellspacing='0' width='100%'><tr bgcolor='#2d6a4f'><th>Fecha</th><th>T°C</th><th>HA%</th><th>Suelo%</th><th>VPD</th><th>Bomba</th><th>Luces</th><th>Extractor</th><th>Intractor</th></tr>"; = "<table border='0' cellpadding='5' cellspacing='0' width='100%'><tr bgcolor='#2d6a4f'><th>Fecha</th><th>T°C</th><th>HA%</th><th>Suelo%</th><th>VPD</th><th>Bomba</th><th>Luces</th><th>Extractor</th></tr>";
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
      html += "<td>" + String(historial[idx].intractor ? "ON" : "OFF") + "</td>";  // NUEVO
      html += "</tr>";
    }
  }
  html += "</table>";
  return html;
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

void reiniciarContadoresDiarios() {
  desconexionesMQTT = 0; fallosRiego = 0; fallosLuces = 0; fallosExtractor = 0;
}