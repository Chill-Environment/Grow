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

void handleRoot(AsyncWebServerRequest *request) {
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

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title> Grow Control - Dashboard</title>"
                "<style>"
                "*{margin:0;padding:0;box-sizing:border-box;}"
                "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,'Helvetica Neue',Arial,sans-serif;background:#F4F6F9;padding:24px;color:#1A1A2E;}"
                ".container{max-width:1280px;margin:0 auto;}"
                ".header{display:flex;justify-content:space-between;align-items:center;margin-bottom:32px;flex-wrap:wrap;gap:16px;}"
                ".header h1{font-size:24px;font-weight:600;color:#1A1A2E;display:flex;align-items:center;gap:8px;}"
                ".badge{background:#E8ECF4;padding:6px 14px;border-radius:20px;font-size:12px;color:#0052CC;font-family:monospace;}"
                ".dashboard{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:24px;margin-bottom:32px;}"
                ".card{background:#FFFFFF;border-radius:16px;padding:24px;box-shadow:0 2px 8px rgba(0,0,0,0.04);border:1px solid #E8ECF4;}"
                ".card h3{font-size:13px;text-transform:uppercase;letter-spacing:0.5px;color:#6C757D;margin-bottom:12px;}"
                ".card .value{font-size:36px;font-weight:700;color:#1A1A2E;}"
                ".card .unit{font-size:14px;font-weight:400;color:#6C757D;margin-left:4px;}"
                ".card .trend{font-size:12px;margin-top:8px;color:#6C757D;}"
                ".panel-controls{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:24px;margin-bottom:32px;}"
                ".control-group{background:#FFFFFF;border-radius:16px;padding:20px;border:1px solid #E8ECF4;}"
                ".control-group h3{font-size:16px;font-weight:600;margin-bottom:16px;display:flex;align-items:center;gap:8px;}"
                ".button-group{display:grid;grid-template-columns:repeat(auto-fit,minmax(100px,1fr));gap:10px;margin-bottom:12px;}"
                ".btn{display:inline-block;padding:10px 16px;border-radius:8px;text-decoration:none;text-align:center;font-weight:500;font-size:13px;transition:all 0.2s;border:none;cursor:pointer;}"
                ".btn-primary{background:#0052CC;color:white;}"
                ".btn-danger{background:#E53935;color:white;}"
                ".btn-secondary{background:#E8ECF4;color:#1A1A2E;}"
                ".btn-active{background:#00A86B;color:white;}"
                ".badge-status{display:inline-block;padding:4px 12px;border-radius:20px;font-size:12px;font-weight:500;background:#F4F6F9;color:#1A1A2E;margin-top:12px;}"
                ".badge-status.on{background:#00A86B;color:white;}"
                ".badge-status.off{background:#E53935;color:white;}"
                ".info-section{background:#FFFFFF;border-radius:16px;padding:20px;margin-top:24px;border:1px solid #E8ECF4;}"
                ".historial{background:#FFFFFF;border-radius:16px;margin-top:32px;border:1px solid #E8ECF4;overflow:auto;}"
                "table{width:100%;border-collapse:collapse;font-size:13px;}"
                "th{text-align:left;padding:16px 20px;background:#F8FAFE;}"
                "td{padding:12px 20px;border-bottom:1px solid #F0F2F5;}"
                ".footer{text-align:center;margin-top:32px;font-size:12px;color:#6C757D;}"
                "</style>"
                "</head><body>"
                "<div class='container'>"
                "<div class='header'><h1>🌱 Grow Control</h1><div class='badge'>📡 " + WiFi.localIP().toString() + "</div></div>"
                "<div class='dashboard'>"
                "<div class='card'><h3>🌡️ Temperatura</h3><div class='value'>" + String(temp,1) + "<span class='unit'>°C</span></div><div class='trend'>" + tendenciaTemp + "</div></div>"
                "<div class='card'><h3>💧 Humedad</h3><div class='value'>" + String(hum,1) + "<span class='unit'>%</span></div><div class='trend'>Óptimo: " + String(getHumedadSueloOptima()) + "%</div></div>"
                "<div class='card'><h3>🌍 Suelo</h3><div class='value'>" + String(humSuelo,1) + "<span class='unit'>%</span></div><div class='trend'>Riego &lt; " + String(sueloMinRiego) + "%</div></div>"
                "<div class='card'><h3>📊 VPD</h3><div class='value'>" + String(vpd,2) + "<span class='unit'>kPa</span></div><div class='trend'>Objetivo: " + String(getVPDObjetivo(),2) + " kPa</div></div>"
                "</div>"
                "<div class='panel-controls'>"
                "<div class='control-group'><h3>💧 Riego</h3><div class='button-group'><a href='/riego' class='btn btn-primary'>🚿 Riego Manual</a></div></div>"
                "<div class='control-group'><h3>💡 Luces</h3><div class='button-group'><a href='/lucesManual?estado=on' class='btn btn-primary'>💡 Encender (1h)</a><a href='/lucesManual?estado=off' class='btn btn-danger'>🌙 Apagar (1h)</a></div><div class='button-group'><a href='/luces?modo=flor' class='btn " + String(modoFloracion ? "btn-active" : "btn-secondary") + "'>🌙 Floración</a><a href='/luces?modo=veg' class='btn " + String(!modoFloracion ? "btn-active" : "btn-secondary") + "'>☀️ Vegetativo</a></div></div>"
                "<div class='control-group'><h3>🌀 Extractor</h3><div class='button-group'><a href='/manualExtractor?estado=on' class='btn btn-primary'>🔧 ON</a><a href='/manualExtractor?estado=off' class='btn btn-secondary'>🔧 OFF</a><a href='/modoExtractor?modo=1' class='btn btn-secondary'>⏱️ Intermitente</a><a href='/modoExtractor?modo=2' class='btn btn-secondary'>🤖 Automático</a></div><div class='badge-status " + String(extractorEstado ? "on" : "off") + "'>Estado: " + String(extractorEstado ? "ON" : "OFF") + " | Modo: " + modoExtractorStr + "</div></div>"
                "<div class='control-group'><h3>🌬️ Intractor</h3><div class='button-group'><a href='/manualIntractor?estado=on' class='btn btn-primary'>🔧 ON</a><a href='/manualIntractor?estado=off' class='btn btn-secondary'>🔧 OFF</a><a href='/modoIntractor?modo=1' class='btn btn-secondary'>⏱️ Intermitente</a><a href='/modoIntractor?modo=2' class='btn btn-secondary'>🤖 Automático</a></div><div class='badge-status " + String(intractorEstado ? "on" : "off") + "'>Estado: " + String(intractorEstado ? "ON" : "OFF") + " | Modo: " + modoIntractorStr + "</div></div>"
                "</div>"
                "<div class='info-section'><h3>📊 Resumen del Cultivo</h3><div class='config-row'><span>Presión atmosférica:</span><strong>" + String(pres,1) + " hPa | " + interpretacion + "</strong></div><div class='config-row'><span>Semana de cultivo:</span><strong>" + String(semanaCultivo) + " (" + cicloLucesStr + ")</strong></div><div class='config-row'><span>VPD objetivo:</span><strong>" + String(getVPDObjetivo(),2) + " kPa</strong></div><div class='separator'></div>"
                "<form action='/setSemana' method='get' style='display:flex;align-items:center;gap:12px;flex-wrap:wrap;'><label>Cambiar semana:</label><input type='number' name='semana' min='1' max='8' value='" + String(semanaCultivo) + "' class='semana-input'><input type='submit' value='Actualizar' class='btn btn-primary'><a href='/config' class='btn btn-secondary'>⚙️ Configuración avanzada</a><a href='/' class='btn btn-secondary'>🔄 Refrescar</a></form></div>"
                "<div class='historial'><h3>📜 Historial de lecturas</h3>" + generarHistorialHTML() + "</div>"
                "<div class='footer'> Grow Control v2.0 | " + WiFi.localIP().toString() + "</div>"
                "</div><script>setTimeout(()=>{location.reload()},30000);</script></body></html>";
  request->send(200, "text/html", html);
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
  }
  request->redirect("/");
}

void handleManualExtractor(AsyncWebServerRequest *request) {
  if (request->hasArg("estado")) {
    controlarSonoff(SONOFF3_TOPIC, request->arg("estado") == "on");
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
    <title> Grow Control - Configuración</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { margin:0; padding:0; box-sizing:border-box; }
        body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif; background:#F4F6F9; display:flex; justify-content:center; align-items:center; min-height:100vh; padding:24px; }
        .container { background:#FFFFFF; border-radius:16px; padding:32px; max-width:560px; width:100%; box-shadow:0 4px 20px rgba(0,0,0,0.08); border:1px solid #E8ECF4; }
        h1 { font-size:24px; font-weight:600; text-align:center; margin-bottom:8px; display:flex; align-items:center; justify-content:center; gap:8px; }
        .subtitle { text-align:center; color:#6C757D; margin-bottom:32px; font-size:14px; }
        .section { background:#F8FAFE; padding:20px; border-radius:12px; margin-bottom:24px; border:1px solid #E8ECF4; }
        .section h3 { margin-top:0; margin-bottom:16px; color:#1A1A2E; font-size:16px; font-weight:600; display:flex; align-items:center; gap:8px; }
        .form-group { margin-bottom:16px; }
        .form-group label { display:block; margin-bottom:8px; font-weight:500; font-size:13px; color:#1A1A2E; }
        input, select { width:100%; padding:10px 12px; border:1px solid #E8ECF4; border-radius:8px; font-size:14px; background:#FFFFFF; }
        input:focus, select:focus { outline:none; border-color:#0052CC; box-shadow:0 0 0 2px rgba(0,82,204,0.1); }
        button { width:100%; padding:12px; background:#0052CC; color:white; border:none; border-radius:8px; font-size:14px; font-weight:600; cursor:pointer; margin-top:8px; }
        button.danger { background:#E53935; }
        .current-values { background:#F8FAFE; padding:12px; border-radius:8px; font-size:12px; margin-bottom:20px; border:1px solid #E8ECF4; }
        hr { margin:24px 0 16px; border-top:1px solid #E8ECF4; }
        .footer-link { text-align:center; margin-top:20px; }
        .footer-link a { color:#0052CC; text-decoration:none; font-size:13px; }
    </style>
</head>
<body>
<div class="container">
    <h1> Grow Control</h1>
    <div class="subtitle">Configuración avanzada</div>
    <div class="current-values">📡 Configuración actual:<br>WiFi: <strong>)rawliteral" + current_ssid + R"rawliteral(</strong><br>MQTT: <strong>)rawliteral" + current_mqtt_server + R"rawliteral(</strong><br>Telegram: <strong>)rawliteral" + (current_telegram_token.length() > 0 ? "Configurado ✓" : "No configurado") + R"rawliteral(</strong></div>
    <form id="configForm">
        <div class="section"><h3>📡 WiFi</h3>
            <div class="form-group"><label>SSID</label><input type="text" name="ssid" value=")rawliteral" + current_ssid + R"rawliteral(" required></div>
            <div class="form-group"><label>Contraseña</label><input type="password" name="pass" placeholder="Contraseña WiFi"><small>Dejar en blanco para mantener la actual</small></div>
        </div>
        <div class="section"><h3>📨 MQTT</h3>
            <div class="form-group"><label>Servidor</label><input type="text" name="mqtt_server" value=")rawliteral" + current_mqtt_server + R"rawliteral(" required></div>
            <div class="form-group"><label>Puerto</label><input type="text" name="mqtt_port" value="1883"></div>
            <div class="form-group"><label>Usuario</label><input type="text" name="mqtt_user" value=")rawliteral" + current_mqtt_user + R"rawliteral("></div>
            <div class="form-group"><label>Contraseña</label><input type="password" name="mqtt_password" placeholder="Password MQTT"><small>Dejar en blanco para mantener la actual</small></div>
        </div>
        <div class="section"><h3>🤖 Telegram</h3>
            <div class="form-group"><label>Bot Token</label><input type="text" name="telegram_token" value=")rawliteral" + current_telegram_token + R"rawliteral(" required></div>
            <div class="form-group"><label>Chat ID</label><input type="text" name="chat_id" value=")rawliteral" + current_chat_id + R"rawliteral(" required></div>
        </div>
        <div class="section"><h3>🌱 Cultivo</h3>
            <div class="form-group"><label>Semana actual</label><select name="semana_inicial">
                <option value="1" )rawliteral" + String(current_semana==1?"selected":"") + R"rawliteral(>Semana 1</option>
                <option value="2" )rawliteral" + String(current_semana==2?"selected":"") + R"rawliteral(>Semana 2</option>
                <option value="3" )rawliteral" + String(current_semana==3?"selected":"") + R"rawliteral(>Semana 3</option>
                <option value="4" )rawliteral" + String(current_semana==4?"selected":"") + R"rawliteral(>Semana 4</option>
                <option value="5" )rawliteral" + String(current_semana==5?"selected":"") + R"rawliteral(>Semana 5</option>
                <option value="6" )rawliteral" + String(current_semana==6?"selected":"") + R"rawliteral(>Semana 6</option>
                <option value="7" )rawliteral" + String(current_semana==7?"selected":"") + R"rawliteral(>Semana 7</option>
                <option value="8" )rawliteral" + String(current_semana==8?"selected":"") + R"rawliteral(>Semana 8</option>
            </select></div>
            <div class="form-group"><label>Modo</label><select name="modo_inicial">
                <option value="floracion" )rawliteral" + String(current_modo=="floracion"?"selected":"") + R"rawliteral(>Floración (12/12)</option>
                <option value="vegetativo" )rawliteral" + String(current_modo=="vegetativo"?"selected":"") + R"rawliteral(>Vegetativo (17/7)</option>
            </select></div>
        </div>
        <button type="submit">💾 Guardar y Reiniciar</button>
    </form>
    <hr>
    <button id="resetBtn" class="danger">⚠️ Reset de Fábrica</button>
    <div class="footer-link"><a href="/">← Volver al Dashboard</a></div>
</div>
<script>
    document.getElementById('configForm').addEventListener('submit', async (e) => {
        e.preventDefault();
        const formData = new FormData(e.target);
        const response = await fetch('/saveconfig', { method: 'POST', body: formData });
        const result = await response.text();
        alert(result);
        if(response.ok) setTimeout(()=>{ window.location.href='/'; }, 2000);
    });
    document.getElementById('resetBtn').addEventListener('click', async () => {
        if(confirm('¿Borrar toda la configuración?')) {
            const response = await fetch('/resetconfig', { method: 'POST' });
            alert(await response.text());
            if(response.ok) setTimeout(()=>{ window.location.href='/'; }, 2000);
        }
    });
</script>
</body>
</html>
)rawliteral";
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