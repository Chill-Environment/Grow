#ifndef WEB_H
#define WEB_H

#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;

void initWebServer();
void handleRoot(AsyncWebServerRequest *request);
void handleRiegoManual(AsyncWebServerRequest *request);
void handleModoLuces(AsyncWebServerRequest *request);
void handleLucesManual(AsyncWebServerRequest *request);
void handleModoExtractor(AsyncWebServerRequest *request);
void handleManualExtractor(AsyncWebServerRequest *request);
void handleManualIntractor(AsyncWebServerRequest *request);
void handleModoIntractor(AsyncWebServerRequest *request);
void handleSetSemana(AsyncWebServerRequest *request);
void handleSetVPDTarget(AsyncWebServerRequest *request);
void handleConfig(AsyncWebServerRequest *request);
void handleSaveConfig(AsyncWebServerRequest *request);
void handleResetConfig(AsyncWebServerRequest *request);
void forzarLecturaInmediata();

#endif