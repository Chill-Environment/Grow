#ifndef CONTROL_H
#define CONTROL_H

// ========== FUNCIONES DE COOLDOWN ==========
// IMPORTANTE: El valor por defecto se define SOLO en el .cpp
bool canChangeState(unsigned long lastChangeTime, unsigned long cooldownMs, const char* logMessage);
void registerStateChange(const char* nombre, unsigned long& lastChangeTime, bool& lastState, bool newState);

// ========== FUNCIONES DE CONTROL ==========
void controlLuces();
void controlExtractor();
void controlIntractor();
void enviarReporteDiario();
float getVPDObjetivo();
int getHumedadSueloOptima();

#endif