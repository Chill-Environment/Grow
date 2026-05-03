# Grow Control ESP32

**Sistema de automatización para cultivo indoor**

[![Estado](https://img.shields.io/badge/Estado-En%20producción%20(fase%20de%20pruebas)-yellow)]()

## Características

### Monitoreo
- Temperatura y humedad ambiente (AHT20)
- Presión atmosférica (BMP280)
- Humedad de suelo (4 sensores)

### Control
- Riego automático por umbral
- Luces: Floración 12/12 o Vegetativo 17/7
- Extractor automático por VPD, temperatura o humedad
- Intractor automático por humedad

### Interfaz
- Dashboard web con gráficos en tiempo real
- Control remoto por Telegram (14 comandos)
- Actualizaciones OTA por WiFi

## Hardware Requerido

| Componente | Cantidad |
|------------|----------|
| ESP32 Dev Board | 1 |
| Sensor AHT20 | 1 |
| Sensor BMP280 | 1 |
| Sensores humedad suelo | 4 |
| Sonoff (Tasmota) | 4 |

## Diagrama de Conexiones

| Componente | Pin ESP32 |
|------------|-----------|
| AHT20 SDA | GPIO21 |
| AHT20 SCL | GPIO22 |
| BMP280 SDA | GPIO21 |
| BMP280 SCL | GPIO22 |
| Sensor Suelo 1 | GPIO32 |
| Sensor Suelo 2 | GPIO33 |
| Sensor Suelo 3 | GPIO34 |
| Sensor Suelo 4 | GPIO35 |

## Instalación - Primera configuración (modo AP)

1. Sube el código al ESP32 por USB
2. Conéctate desde tu teléfono a la red WiFi `GrowControl-Setup`
   - Contraseña: `dedodemomia`
3. Abre `http://192.168.4.1`
4. Completa los datos de WiFi, MQTT y Telegram

## Configuración avanzada y documentación completa

📄 [Descargar documentación técnica (PDF)](docs/GrowControl.pdf)

- **Datos históricos:** Las lecturas se publican por MQTT en formato InfluxDB y se almacenan en una base de datos de series temporales, visualizables con Chronograf o Grafana.

## Archivo `config.txt` (opcional)

```ini
ssid=TuWiFi
password=TuClave
mqtt_server=192.168.1.100
mqtt_user=usuario
mqtt_password=clave
telegram_token=1234567890:ABCdefGHIjkl
chat_id=123456789
semana_inicial=1
modo_inicial=vegetativo
```

☕ Apoya el desarrollo
Si este proyecto te ha sido útil y quieres ayudarme a seguir mejorándolo, puedes invitarme un café en Ko-fi:

[![Ko-fi](https://img.shields.io/badge/Apóyame%20en-Ko--fi-FF5E5B?logo=ko-fi)](https://ko-fi.com/chillenvironment)

Cualquier aportación es bienvenida y me motiva a seguir desarrollando.
