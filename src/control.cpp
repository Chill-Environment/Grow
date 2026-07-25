#include <Arduino.h>
#include <time.h>
#include <Preferences.h>
#include "control.h"
#include "config.h"
#include "sensors.h"
#include "mqtt.h"
#include "telegram.h"

// ========== CONSTANTES DE CONTROL OPTIMIZADAS ==========
// Basadas en experiencia de cultivo indoor

// Temperaturas
const float TEMP_CRITICA_ALTA = 29.0;     // Activa extractor
const float TEMP_CRITICA_BAJA = 18.0;      // Desactiva extractor
const float TEMP_MAX_SEGURA = 30.0;        // Máxima segura
const float TEMP_MIN_SEGURA = 15.0;        // Mínima segura

// Humedades
const float HUM_CRITICA_ALTA = 80.0;       // Activa extractor
const float HUM_CRITICA_BAJA = 40.0;       // Activa intractor

// VPD - Umbrales de control con histéresis
const float VPD_UMBRAL_ALTO = 0.3;         // Activa extractor
const float VPD_UMBRAL_BAJO = 0.2;         // Apaga extractor
const float VPD_HISTERESIS = 0.15;         // Zona muerta

// ========== FUNCIONES AUXILIARES DE COOLDOWN ==========

bool canChangeState(unsigned long lastChangeTime, unsigned long cooldownMs, const char* logMessage) {
    unsigned long now = millis();
    unsigned long elapsed = now - lastChangeTime;
    
    if (elapsed < cooldownMs) {
        if (logMessage) {
            unsigned long remaining = (cooldownMs - elapsed) / 1000;
            Serial.printf("⏳ Cooldown para %s: %lu segundos restantes\n", 
                         logMessage, remaining);
        }
        return false;
    }
    return true;
}

void registerStateChange(const char* nombre, unsigned long& lastChangeTime, bool& lastState, bool newState) {
    lastChangeTime = millis();
    lastState = newState;
    Serial.printf("✅ Cambio de estado: %s -> %s\n", nombre, newState ? "ON" : "OFF");
}

// ========== CONTROL DE LUCES ==========

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
        Serial.println("⏰ Modo manual de luces expirado");
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
        
        // Horario optimizado según etapa
        if (modoFloracion) {
            // Floración: 12/12
            lucesOn = (hora >= 6 && hora < 18);
        } else {
            // Vegetativo: 18/6
            lucesOn = (hora >= 0 && hora < 18);
            
            // Plántulas (semanas 1-2): 20/4 para mejor desarrollo
            if (semanaCultivo <= 2) {
                lucesOn = (hora >= 0 && hora < 20);
            }
        }
    }
    
    if (lucesOn != luzEstado) {
        if (canChangeState(lastLucesChange, MIN_COOLDOWN_LUCES, "Luces")) {
            if (millis() - lastLuzPublish > PUBLISH_COOLDOWN) {
                lastLuzPublish = millis();
                controlarSonoff(SONOFF2_TOPIC, lucesOn);
                registerStateChange("Luces", lastLucesChange, lastLucesEstado, lucesOn);
                luzEstado = lucesOn;
            }
        }
    } else {
        lastLucesEstado = luzEstado;
    }
}

// ========== CONTROL DE EXTRACTOR OPTIMIZADO ==========

void controlExtractor() {
    if (modoExtractor == 0) {
        lastExtractorEstado = extractorEstado;
        return;
    }
    
    bool extractorOn = false;
    
    // ====== MODO INTERMITENTE (15 min/hora) ======
    if (modoExtractor == 1) {
        time_t now;
        time(&now);
        extractorOn = (localtime(&now)->tm_min < 15);
    }
    
    // ====== MODO AUTOMÁTICO CON UMBRALES OPTIMIZADOS ======
    else if (modoExtractor == 2) {
        float h = ultimaHumedadValida;
        float t = ultimaTempValida;
        float vpd = computeVPD(t, h);
        float vpdObjetivo = getVPDObjetivo();
        float diff = vpd - vpdObjetivo;
        
        // === REGLAS DE SEGURIDAD (PRIORIDAD MÁXIMA) ===
        if (t > TEMP_CRITICA_ALTA) {
            extractorOn = true;
            Serial.printf("🔥 Extractor ON: Temp crítica %.1f°C\n", t);
        } else if (h > HUM_CRITICA_ALTA) {
            extractorOn = true;
            Serial.printf("💧 Extractor ON: Humedad crítica %.1f%%\n", h);
        }
        // === REGLAS DE VPD CON HISTÉRESIS ===
        else if (diff > VPD_UMBRAL_ALTO) {
            extractorOn = true;
            Serial.printf("📊 Extractor ON: VPD %.2f (obj: %.2f)\n", vpd, vpdObjetivo);
        } else if (diff > VPD_UMBRAL_ALTO - VPD_HISTERESIS) {
            // Zona de transición: mantener estado actual
            extractorOn = extractorEstado;
        } else if (diff < -VPD_UMBRAL_BAJO) {
            extractorOn = false;
            Serial.printf("📊 Extractor OFF: VPD bajo %.2f (obj: %.2f)\n", vpd, vpdObjetivo);
        }
        // Zona muerta: mantener estado
        else {
            extractorOn = extractorEstado;
        }
        
        // === REGLAS ESPECÍFICAS POR ETAPA ===
        // Últimas semanas de floración: más extractor para prevenir moho
        if (modoFloracion && semanaCultivo >= 6) {
            if (h > 70) {
                extractorOn = true;
                Serial.printf("🍃 Extractor ON: Prevención moho (semana %d)\n", semanaCultivo);
            }
        }
        
        // Plántulas: menos extractor para mantener condiciones estables
        if (!modoFloracion && semanaCultivo <= 2) {
            if (t < 26 && h < 70) {
                extractorOn = false;
                Serial.printf("🌱 Extractor OFF: Plántulas (semana %d)\n", semanaCultivo);
            }
        }
    }
    
    // Verificar cooldown antes de cambiar
    if (extractorOn != extractorEstado) {
        if (canChangeState(lastExtractorChange, MIN_COOLDOWN_EXTRACTOR, "Extractor")) {
            if (millis() - lastExtractorPublish > PUBLISH_COOLDOWN) {
                lastExtractorPublish = millis();
                controlarSonoff(SONOFF3_TOPIC, extractorOn);
                registerStateChange("Extractor", lastExtractorChange, lastExtractorEstado, extractorOn);
                extractorEstado = extractorOn;
            }
        }
    } else {
        lastExtractorEstado = extractorEstado;
    }
}

// ========== CONTROL DE INTRACTOR OPTIMIZADO ==========

void controlIntractor() {
    if (modoIntractor == 0) {
        lastIntractorEstado = intractorEstado;
        return;
    }
    
    bool intractorOn = false;
    
    // MODO INTERMITENTE (15-30 minutos de cada hora)
    if (modoIntractor == 1) {
        time_t now;
        time(&now);
        int minuto = localtime(&now)->tm_min;
        intractorOn = (minuto >= 15 && minuto < 30);
    }
    
    // MODO AUTOMÁTICO OPTIMIZADO
    else if (modoIntractor == 2) {
        float h = ultimaHumedadValida;
        float t = ultimaTempValida;
        float vpd = computeVPD(t, h);
        float vpdObjetivo = getVPDObjetivo();
        float diff = vpd - vpdObjetivo;
        
        // === REGLAS DE SEGURIDAD ===
        if (t > TEMP_MAX_SEGURA || h > HUM_CRITICA_ALTA) {
            intractorOn = false;  // No meter aire caliente/húmedo
            Serial.printf("🌬️ Intractor OFF: Condiciones críticas\n");
        }
        // === REGLAS DE HUMEDAD ===
        else if (h < HUM_CRITICA_BAJA && t < 28) {
            intractorOn = true;   // Humedad baja → encender
            Serial.printf("🌬️ Intractor ON: Humedad baja %.1f%%\n", h);
        }
        // === REGLAS DE VPD ===
        else if (diff < -0.3) {
            intractorOn = true;   // VPD bajo → encender (aporta humedad)
            Serial.printf("🌬️ Intractor ON: VPD bajo %.2f\n", vpd);
        } else if (diff > 0.2) {
            intractorOn = false;  // VPD alto → apagar
        }
        // Zona muerta: mantener estado
        else {
            intractorOn = intractorEstado;
        }
        
        // === REGLAS ESPECÍFICAS POR ETAPA ===
        // Floración tardía: reducir intractor para evitar moho
        if (modoFloracion && semanaCultivo >= 7) {
            if (h > 65) {
                intractorOn = false;
                Serial.printf("🍃 Intractor OFF: Prevención moho (semana %d)\n", semanaCultivo);
            }
        }
    }
    
    // Verificar cooldown antes de cambiar
    if (intractorOn != intractorEstado) {
        if (canChangeState(lastIntractorChange, MIN_COOLDOWN_INTRACTOR, "Intractor")) {
            // TODO: Configurar tópico MQTT para intractor
            // controlarSonoff("cmnd/sonoff_intractor/power", intractorOn);
            registerStateChange("Intractor", lastIntractorChange, lastIntractorEstado, intractorOn);
            intractorEstado = intractorOn;
        }
    } else {
        lastIntractorEstado = intractorEstado;
    }
}

// ========== REPORTE DIARIO ==========

void enviarReporteDiario() {
    time_t now;
    time(&now);
    struct tm *tm = localtime(&now);
    
    String today = String(tm->tm_year + 1900) + "-" + String(tm->tm_mon + 1) + "-" + String(tm->tm_mday);
    
    Preferences prefs;
    prefs.begin("grow", true);
    String lastReportDate = "";
    if (prefs.isKey("lastReportDate")) {
        lastReportDate = prefs.getString("lastReportDate", "");
    }
    prefs.end();
    
    if (tm->tm_hour == 20 && !dailyReportSent && readingsCount >= 10 && lastReportDate != today) {
        float avgTemp = sumTemp / readingsCount;
        float avgHA = sumHA / readingsCount;
        float avgSuelo = sumSuelo / readingsCount;
        float avgVPD = sumVPD / readingsCount;
        float avgPresion = sumPresion / readingsCount;
        
        String mensaje = "📊 *RESUMEN DIARIO*\n\n";
        mensaje += "🌡️ Temp: " + String(avgTemp,1) + "°C\n";
        mensaje += "💧 Humedad: " + String(avgHA,1) + "%\n";
        mensaje += "🌍 Suelo: " + String(avgSuelo,1) + "%\n";
        mensaje += "📊 VPD: " + String(avgVPD,2) + " kPa\n";
        mensaje += "🌬️ Presión: " + String(avgPresion,1) + " hPa\n";
        mensaje += "📆 Semana: " + String(semanaCultivo) + "\n";
        mensaje += "💡 Luces: " + String(modoFloracion ? "Floración" : "Vegetativo") + "\n";
        mensaje += "🌀 Extractor: " + String(extractorEstado ? "ON" : "OFF");
        
        enviarTelegram(mensaje);
        Serial.println("✅ Resumen diario enviado");
        
        dailyReportSent = true;
        
        prefs.begin("grow", false);
        prefs.putString("lastReportDate", today);
        prefs.end();
        
        sumTemp = sumHA = sumSuelo = sumVPD = sumPresion = 0;
        readingsCount = 0;
        maxTemp = 0; minTemp = 100;
        maxHA = 0; minHA = 100;
        maxPresion = 0; minPresion = 1000;
    }
    
    if (tm->tm_hour != 20) dailyReportSent = false;
}

// ========== FUNCIONES DE CÁLCULO CON UMBRALES OPTIMIZADOS ==========

float getVPDObjetivo() {
    if (modoFloracion) {
        switch(semanaCultivo) {
            case 1: return 1.25;   // Transición
            case 2: return 1.35;   // Estiramiento
            case 3: return 1.45;   // Formación
            case 4: return 1.55;   // Engorde máximo
            case 5: return 1.55;   // Engorde máximo
            case 6: return 1.45;   // Maduración
            case 7: return 1.35;   // Engorde final
            case 8: return 1.25;   // Última semana
            default: return 1.40;
        }
    } else {
        switch(semanaCultivo) {
            case 1: return 0.85;   // Plántula
            case 2: return 1.05;   // Crecimiento
            case 3: return 1.15;   // Crecimiento activo
            case 4: return 1.25;   // Pre-floración
            default: return 1.05;
        }
    }
}

int getHumedadSueloOptima() {
    if (modoFloracion) {
        switch(semanaCultivo) {
            case 1: return 65;     // Transición
            case 2: return 60;     // Estiramiento
            case 3: return 55;     // Formación
            case 4: return 50;     // Engorde
            case 5: return 45;     // Engorde activo
            case 6: return 40;     // Maduración
            case 7: return 35;     // Engorde final
            case 8: return 30;     // Última semana
            default: return 50;
        }
    } else {
        switch(semanaCultivo) {
            case 1: return 72;     // Plántula
            case 2: return 67;     // Crecimiento
            case 3: return 62;     // Crecimiento activo
            case 4: return 57;     // Pre-floración
            default: return 65;
        }
    }
}