#include <Arduino.h>
#include <esp_task_wdt.h>
#include <UniversalTelegramBot.h>
#include "telegram.h"
#include "config.h"
#include "mqtt.h"
#include "sensors.h"
#include "control.h"
#include "historial.h"

extern WiFiClientSecure secured_client;
UniversalTelegramBot* bot = nullptr;

void initTelegram() {
  if (config_telegram_token.length() > 0) {
    bot = new UniversalTelegramBot(config_telegram_token, secured_client);
    secured_client.setInsecure();
  }
}

void enviarTelegram(const String& mensaje) {
  if (bot != nullptr && config_chat_id.length() > 0) {
    bot->sendMessage(config_chat_id, mensaje, "Markdown");
  }
}

void handleTelegramMessages() {
  if (bot == nullptr) return;
  static unsigned long lastTelegramCheck = 0;
  if (millis() - lastTelegramCheck < 4000) return;
  lastTelegramCheck = millis();

  int numNewMessages = bot->getUpdates(bot->last_message_received + 1);
  while (numNewMessages) {
    esp_task_wdt_reset();
    for (int i = 0; i < numNewMessages; i++) {
      String chat_id = String(bot->messages[i].chat_id);
      String text = bot->messages[i].text;
      text.toLowerCase();

      if (text == "/start") {
        bot->sendMessage(chat_id, "*GROW CONTROL*\n\nComandos:\n/datos - Ver datos\n/estado - Estado relés\n/riego - Riego manual\n/luces_on - Encender luces\n/luces_off - Apagar luces\n/floracion - Floración\n/vegetativo - Vegetativo\n/extractor_auto - Extractor auto\n/extractor_off - Extractor OFF\n/intractor_auto - Intractor auto\n/intractor_off - Intractor OFF\n/semana X - Set semana\n/vpd_target - Ver VPD objetivo", "Markdown");
      }
      else if (text == "/datos") {
        float temp, hum, pres, vpd, suelo;
        if (readTempHumi(temp, hum)) {
          pres = readPressure();
          vpd = computeVPD(temp, hum);
          suelo = readSoilMoisture();
          String msg = "🌡️ *DATOS*\nTemp: " + String(temp,1) + "°C\nHumedad: " + String(hum,1) + "%\nPresión: " + String(pres,1) + " hPa\nSuelo: " + String(suelo,1) + "%\nVPD: " + String(vpd,2) + " kPa\nSemana: " + String(semanaCultivo) + "\nModo: " + (modoFloracion ? "Floración" : "Vegetativo");
          bot->sendMessage(chat_id, msg, "Markdown");
        }
      }
      else if (text == "/estado") {
        String msg = "⚙️ *ESTADO RELÉS*\nBomba: " + String(bombaEstado ? "ON" : "OFF") + "\nLuces: " + String(luzEstado ? "ON" : "OFF") + "\nExtractor: " + String(extractorEstado ? "ON" : "OFF");
        bot->sendMessage(chat_id, msg, "Markdown");
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
        bot->sendMessage(chat_id, "☀️ *Vegetativo 18/6*", "Markdown");
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