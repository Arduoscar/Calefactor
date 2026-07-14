// =====================================================================
// GRUPO: ENCABEZADO Y DESCRIPCIÓN GENERAL DEL SISTEMA
// =====================================================================
// Dispositivo: ESP32 Wemos D1 Mini
// Propósito: Sistema de control de calefactor inteligente
// Funcionalidades principales:
//  - Servidor Web NO BLOQUEANTE con IP fija
//  - DHT11 (lectura temperatura/humedad exterior)
//  - DHT11 (lectura temperatura/humedad interior) - MEJORADO
//  - Relés con control IR y modos avanzados
//  - RELAY3: Control manual ON/OFF independiente
//  - RELAY4 (GPIO 25): Sigue a RELAY1/2 con retraso al apagar
//  - EEPROM para persistencia de datos
//  - LCD I2C 16x2 optimizada sin parpadeo
//  - Control automático con HISTERESIS anti-rebote
// =====================================================================

// =====================================================================
// GRUPO: INCLUSIÓN DE LIBRERÍAS
// =====================================================================
#include <WiFi.h>                  // Librería para conectividad WiFi
#include <DHTesp.h>                // Librería para sensor DHT (temperatura/humedad)
#include <IRremote.hpp>            // Librería para control remoto infrarrojo
#include <EEPROM.h>                // Librería para almacenamiento permanente
#include <LiquidCrystal_I2C.h>     // Librería para pantalla LCD I2C

// =====================================================================
// GRUPO: CONFIGURACIÓN DE LOS SENSORES DHT11
// =====================================================================
#define DHT_EXTERIOR_PIN 17        // Pin GPIO 17 del ESP32 para el DHT11 exterior
#define DHT_INTERIOR_PIN 26        // Pin GPIO 26 del ESP32 para el DHT11 interior
DHTesp dhtExterior;                // Objeto para manejar el sensor DHT exterior
DHTesp dhtInterior;                // Objeto para manejar el sensor DHT interior

// =====================================================================
// GRUPO: CONFIGURACIÓN DE LOS RELÉS
// =====================================================================
#define RELAY1_PIN 16              // Pin GPIO 16 para el relé 1 (automático 1)
#define RELAY2_PIN 18              // Pin GPIO 18 para el relé 2 (automático 2)
#define RELAY3_PIN 19              // Pin GPIO 19 para el relé 3 (manual ON/OFF)
#define RELAY4_PIN 25              // Pin GPIO 25 para el relé 4 (sigue a RELAY1/2 con retraso al apagar)

// =====================================================================
// GRUPO: CONFIGURACIÓN DEL RECEPTOR INFRARROJO HX1838
// =====================================================================
#define IR_PIN 23                  // Pin GPIO 23 para el receptor IR

// =====================================================================
// GRUPO: CONFIGURACIÓN DE LA PANTALLA LCD I2C 16x2
// =====================================================================
// GPIO 21 (SDA) y GPIO 22 (SCL) para I2C - RESERVADOS
LiquidCrystal_I2C lcd(0x27, 16, 2); // Pantalla LCD con dirección I2C 0x27, 16 columnas, 2 filas

// =====================================================================
// GRUPO: VARIABLES DE CONTROL PRINCIPALES
// =====================================================================
int setpoint = 25;                 // Valor de temperatura objetivo (setpoint)
int modo = 1;                      // Modo de operación (1=AUTO1, 2=AUTO2, 3=PRECAUCIÓN)
int estadoReles = 0;               // Almacena estado anterior de relés para EEPROM
int estadoRele3Int = 0;            // Estado del relé manual (0=OFF, 1=ON)

// =====================================================================
// GRUPO: VARIABLES PARA RELAY4 (Sigue a RELAY1/2 con retraso al apagar)
// =====================================================================
bool relay4EnEspera = false;        // Flag: true si está en período de espera de 5 minutos
unsigned long relay4Timer = 0;      // Timestamp del inicio del temporizador de 5 minutos
const unsigned long TIMER_5MIN = 300000; // 5 minutos en milisegundos (300000 ms)
bool relay1Relay2Activos = false;   // Flag: true si RELAY1 O RELAY2 están activos (para detectar cambio a LOW)

// =====================================================================
// GRUPO: DIRECCIONES DE ALMACENAMIENTO EN EEPROM
// =====================================================================
int addrSetpoint    = 0;           // Dirección EEPROM para guardar setpoint
int addrModo        = 4;           // Dirección EEPROM para guardar modo
int addrEstadoReles = 8;           // Dirección EEPROM para guardar estado de relés
int addrRele3       = 12;          // Dirección EEPROM para guardar estado relay3

// =====================================================================
// GRUPO: CONFIGURACIÓN WIFI
// =====================================================================
const char* ssid     = "Samsung OSCAR";  // Nombre de la red WiFi
const char* password = "14323717";       // Contraseña de la red WiFi

// =====================================================================
// GRUPO: CONFIGURACIÓN DE IP FIJA
// =====================================================================
IPAddress local_IP(192,168,137,220);    // IP fija del ESP32
IPAddress gateway(192,168,137,1);       // Puerta de enlace (router)
IPAddress subnet(255,255,255,0);        // Máscara de subred
IPAddress primaryDNS(8,8,8,8);          // DNS primario (Google)
IPAddress secondaryDNS(8,8,4,4);        // DNS secundario (Google)

// =====================================================================
// GRUPO: SERVIDOR WEB
// =====================================================================
WiFiServer server(80);                  // Servidor web escuchando en puerto 80 (HTTP)

// =====================================================================
// GRUPO: VARIABLES DE CONTROL LCD (ANTI-PARPADEO)
// =====================================================================
// Estas variables almacenan el último valor mostrado para evitar redibujado innecesario
float lastTemp = -100;                  // Última temperatura DHT exterior mostrada
float lastTempInterior = -100;          // Última temperatura DHT interior mostrada
int lastModo = -1;                      // Último modo mostrado
int lastSetpoint = -1;                  // Último setpoint mostrado
int lastEstadoRele3 = -1;               // Último estado relay3 mostrado

// =====================================================================
// GRUPO: HISTERESIS ANTI-REBOTE
// =====================================================================
// La histéresis evita oscilaciones rápidas del relé cuando la temperatura está cerca del setpoint
const float HISTERESIS = 1.0;           // Rango de histéresis en grados Celsius

// =====================================================================
// GRUPO: VARIABLES PARA PARPADEO EN MODO 3
// =====================================================================
// Modo 3 es "PRECAUCIÓN" y muestra un mensaje parpadeante en LCD
unsigned long lastBlink = 0;            // Timestamp del último cambio de parpadeo
bool blinkState = false;                // Estado actual del parpadeo (true=visible, false=oculto)

// =====================================================================
// GRUPO: LECTURA DE SENSORES
// =====================================================================
float tempInterior = 0;                 // Temperatura interior (DHT11 interior)
float humInterior = 0;                  // Humedad interior (DHT11 interior)
float tempExterior = 0;                 // Temperatura exterior (DHT11 exterior)
float humExterior = 0;                  // Humedad exterior (DHT11 exterior)

// =====================================================================
// GRUPO: CONTROL DE INTERVALO DE LECTURA DHT11 (ESTABILIDAD)
// =====================================================================
unsigned long lastDHTRead = 0;                 // Timestamp de la última lectura válida de DHT
const unsigned long DHT_READ_INTERVAL = 2000;  // DHT11 requiere ~2s entre lecturas

// =====================================================================
//  FUNCIÓN: SETUP (Inicialización del sistema)
// =====================================================================
void setup() {
  Serial.begin(115200);
  EEPROM.begin(64);

  int eSetpoint = EEPROM.readInt(addrSetpoint);
  int eModo     = EEPROM.readInt(addrModo);
  int eEstado   = EEPROM.readInt(addrEstadoReles);
  int eRele3    = EEPROM.readInt(addrRele3);

  if (eSetpoint >= 0 && eSetpoint <= 100) setpoint = eSetpoint;
  if (eModo >= 1 && eModo <= 3) modo = eModo;
  if (eEstado >= 0 && eEstado <= 3) estadoReles = eEstado;
  if (eRele3 >= 0 && eRele3 <= 1) estadoRele3Int = eRele3;

  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  WiFi.begin(ssid, password);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 5000) {
    delay(200);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado");
    Serial.println(WiFi.localIP());
    server.begin();
  } else {
    Serial.println("\nWiFi NO disponible");
  }

  dhtExterior.setup(DHT_EXTERIOR_PIN, DHTesp::DHT11);
  dhtInterior.setup(DHT_INTERIOR_PIN, DHTesp::DHT11);

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(RELAY3_PIN, OUTPUT);
  pinMode(RELAY4_PIN, OUTPUT);

  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);
  digitalWrite(RELAY3_PIN, LOW);
  digitalWrite(RELAY4_PIN, LOW);

  IrReceiver.begin(IR_PIN, ENABLE_LED_FEEDBACK);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("CREADO POR OSCAR");
  delay(5000);
}

void loop() {
  // =====================================================================
  // LECTURA DHT11 CADA 2 SEGUNDOS + VALIDACIÓN NaN
  // =====================================================================
  if (millis() - lastDHTRead >= DHT_READ_INTERVAL) {
    float tExt = dhtExterior.getTemperature();
    float hExt = dhtExterior.getHumidity();
    float tInt = dhtInterior.getTemperature();
    float hInt = dhtInterior.getHumidity();

    if (!isnan(tExt)) tempExterior = tExt;
    if (!isnan(hExt)) humExterior = hExt;
    if (!isnan(tInt)) tempInterior = tInt;
    if (!isnan(hInt)) humInterior = hInt;

    if (isnan(tInt) || isnan(hInt)) {
      Serial.println("DHT11 interior (GPIO26) lectura inválida (NaN)");
    }

    lastDHTRead = millis();
  }

  if (IrReceiver.decode()) {
    unsigned long rawCode = IrReceiver.decodedIRData.decodedRawData;
    uint8_t command = IrReceiver.decodedIRData.command;

    Serial.print("RAW: "); Serial.println(rawCode, HEX);
    Serial.print("CMD: "); Serial.println(command, HEX);

    if (rawCode == 0xE718FF00) {
      setpoint++;
      EEPROM.writeInt(addrSetpoint, setpoint);
      EEPROM.commit();
    }

    if (rawCode == 0xAD52FF00) {
      setpoint--;
      EEPROM.writeInt(addrSetpoint, setpoint);
      EEPROM.commit();
    }

    if (command == 0x45) {
      modo = 1;
      EEPROM.writeInt(addrModo, modo);
      EEPROM.commit();
      digitalWrite(RELAY1_PIN, LOW);
      digitalWrite(RELAY2_PIN, LOW);
      estadoReles = 1;
      EEPROM.writeInt(addrEstadoReles, estadoReles);
      EEPROM.commit();
    }

    if (rawCode == 0xB946FF00) {
      modo = 2;
      EEPROM.writeInt(addrModo, modo);
      EEPROM.commit();
      digitalWrite(RELAY1_PIN, LOW);
      digitalWrite(RELAY2_PIN, LOW);
      estadoReles = 2;
      EEPROM.writeInt(addrEstadoReles, estadoReles);
      EEPROM.commit();
    }

    if (rawCode == 0xB847FF00) {
      modo = 3;
      EEPROM.writeInt(addrModo, modo);
      EEPROM.commit();
      digitalWrite(RELAY1_PIN, LOW);
      digitalWrite(RELAY2_PIN, LOW);
      estadoReles = 3;
      EEPROM.writeInt(addrEstadoReles, estadoReles);
      EEPROM.commit();
    }

    IrReceiver.resume();
  }

  if (modo == 1) {
    if (digitalRead(RELAY1_PIN)) {
      if (tempInterior >= setpoint + HISTERESIS) digitalWrite(RELAY1_PIN, LOW);
    } else {
      if (tempInterior <= setpoint - HISTERESIS) digitalWrite(RELAY1_PIN, HIGH);
    }
    digitalWrite(RELAY2_PIN, LOW);
  }

  if (modo == 2) {
    if (digitalRead(RELAY2_PIN)) {
      if (tempInterior >= setpoint + HISTERESIS) digitalWrite(RELAY2_PIN, LOW);
    } else {
      if (tempInterior <= setpoint - HISTERESIS) digitalWrite(RELAY2_PIN, HIGH);
    }
    digitalWrite(RELAY1_PIN, LOW);
  }

  if (modo == 3) {
    if (digitalRead(RELAY1_PIN) || digitalRead(RELAY2_PIN)) {
      if (tempInterior >= setpoint + HISTERESIS) {
        digitalWrite(RELAY1_PIN, LOW);
        digitalWrite(RELAY2_PIN, LOW);
      }
    } else {
      if (tempInterior <= setpoint - HISTERESIS) {
        digitalWrite(RELAY1_PIN, HIGH);
        digitalWrite(RELAY2_PIN, HIGH);
      }
    }
  }

  bool relay1O2Activos = (digitalRead(RELAY1_PIN) || digitalRead(RELAY2_PIN));

  if (relay1O2Activos) {
    digitalWrite(RELAY4_PIN, HIGH);
    relay4EnEspera = false;
    relay1Relay2Activos = true;
  } else {
    if (relay1Relay2Activos) {
      relay4EnEspera = true;
      relay4Timer = millis();
      relay1Relay2Activos = false;
    }

    if (relay4EnEspera) {
      if (millis() - relay4Timer >= TIMER_5MIN) {
        digitalWrite(RELAY4_PIN, LOW);
        relay4EnEspera = false;
      } else {
        digitalWrite(RELAY4_PIN, HIGH);
      }
    } else {
      digitalWrite(RELAY4_PIN, LOW);
    }
  }

  String estadoRele1 = digitalRead(RELAY1_PIN) ? "ON" : "OFF";
  String estadoRele2 = digitalRead(RELAY2_PIN) ? "ON" : "OFF";
  String estadoRele3String = digitalRead(RELAY3_PIN) ? "ON" : "OFF";
  String estadoRele4 = digitalRead(RELAY4_PIN) ? "ON" : "OFF";

  unsigned long tiempoRestante = 0;
  if (relay4EnEspera) {
    unsigned long tiempoTranscurrido = millis() - relay4Timer;
    if (tiempoTranscurrido < TIMER_5MIN) {
      tiempoRestante = (TIMER_5MIN - tiempoTranscurrido) / 1000;
    }
  }

  if (tempExterior != lastTemp) {
    lcd.setCursor(0,0);
    lcd.print("EXT:");
    lcd.print(tempExterior,1);
    lcd.print("   ");
    lastTemp = tempExterior;
  }

  if (tempInterior != lastTempInterior) {
    lcd.setCursor(10,0);
    lcd.print("INT:");
    lcd.print(tempInterior,0);
    lcd.print("   ");
    lastTempInterior = tempInterior;
  }

  if (modo != lastModo) {
    lcd.setCursor(0,1);
    lcd.print("M:");
    if (modo == 1) lcd.print("AUTO1");
    else if (modo == 2) lcd.print("AUTO2");
    else lcd.print("      ");
    lastModo = modo;
  }

  if (setpoint != lastSetpoint) {
    lcd.setCursor(9,1);
    lcd.print("SETP:");
    lcd.print(setpoint);
    lcd.print("   ");
    lastSetpoint = setpoint;
  }

  if (modo == 3) {
    if (millis() - lastBlink >= 500) {
      blinkState = !blinkState;
      lastBlink = millis();
    }
    lcd.setCursor(0,1);
    if (blinkState) lcd.print("PRECAUCION!");
    else lcd.print("            ");
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client = server.available();
    if (client) {
      String req = client.readStringUntil('\r');

      if (req.indexOf("GET /data") != -1) {
        String json = "{";
        json += "\"temperatura\":" + String(tempExterior) + ",";
        json += "\"humedad\":" + String(humExterior) + ",";
        json += "\"tempInterior\":" + String(tempInterior) + ",";
        json += "\"humedadInterior\":" + String(humInterior) + ",";
        json += "\"setpoint\":" + String(setpoint) + ",";
        json += "\"modo\":" + String(modo) + ",";
        json += "\"rele1\":\"" + estadoRele1 + "\",";
        json += "\"rele2\":\"" + estadoRele2 + "\",";
        json += "\"rele3\":\"" + estadoRele3String + "\",";
        json += "\"rele4\":\"" + estadoRele4 + "\",";
        json += "\"tiempoRestante\":" + String(tiempoRestante);
        json += "}";

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println();
        client.println(json);
        client.stop();
        return;
      }

      if (req.indexOf("GET /up") != -1) {
        setpoint++;
        EEPROM.writeInt(addrSetpoint, setpoint);
        EEPROM.commit();
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/plain");
        client.println();
        client.println("OK");
        client.stop();
        return;
      }

      if (req.indexOf("GET /down") != -1) {
        setpoint--;
        EEPROM.writeInt(addrSetpoint, setpoint);
        EEPROM.commit();
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/plain");
        client.println();
        client.println("OK");
        client.stop();
        return;
      }

      if (req.indexOf("GET /rele3on") != -1) {
        digitalWrite(RELAY3_PIN, HIGH);
        estadoRele3Int = 1;
        EEPROM.writeInt(addrRele3, estadoRele3Int);
        EEPROM.commit();
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/plain");
        client.println();
        client.println("OK");
        client.stop();
        return;
      }

      if (req.indexOf("GET /rele3off") != -1) {
        digitalWrite(RELAY3_PIN, LOW);
        estadoRele3Int = 0;
        EEPROM.writeInt(addrRele3, estadoRele3Int);
        EEPROM.commit();
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/plain");
        client.println();
        client.println("OK");
        client.stop();
        return;
      }

      String pagina = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
      pagina += "<title>Calefactor</title>";
      pagina += "<script>";
      pagina += "function actualizar(){";
      pagina += "fetch('/data').then(r=>r.json()).then(d=>{";
      pagina += "document.getElementById('temp').innerHTML = d.temperatura;";
      pagina += "document.getElementById('hum').innerHTML = d.humedad;";
      pagina += "document.getElementById('tempInt').innerHTML = d.tempInterior;";
      pagina += "document.getElementById('humInt').innerHTML = d.humedadInterior;";
      pagina += "document.getElementById('setp').innerHTML = d.setpoint;";
      pagina += "document.getElementById('modo').innerHTML = d.modo;";
      pagina += "document.getElementById('r1').innerHTML = d.rele1;";
      pagina += "document.getElementById('r2').innerHTML = d.rele2;";
      pagina += "document.getElementById('r3').innerHTML = d.rele3;";
      pagina += "document.getElementById('r4').innerHTML = d.rele4;";
      pagina += "document.getElementById('timer').innerHTML = d.tiempoRestante + ' seg';";
      pagina += "if(d.modo == 3){document.getElementById('alerta').style.display='block';}";
      pagina += "else{document.getElementById('alerta').style.display='none';}";
      pagina += "if(d.rele3 == 'ON'){";
      pagina += "document.getElementById('btnRele3On').style.background='#27ae60';";
      pagina += "document.getElementById('btnRele3Off').style.background='#3498db';}";
      pagina += "else{";
      pagina += "document.getElementById('btnRele3On').style.background='#3498db';";
      pagina += "document.getElementById('btnRele3Off').style.background='#e74c3c';}";
      pagina += "if(d.rele4 == 'ON'){";
      pagina += "document.getElementById('r4').style.background='#27ae60';";
      pagina += "document.getElementById('r4').style.color='white';}";
      pagina += "else{";
      pagina += "document.getElementById('r4').style.background='#e74c3c';";
      pagina += "document.getElementById('r4').style.color='white';}";
      pagina += "});}";
      pagina += "setInterval(actualizar,2000);";
      pagina += "function setUp(){fetch('/up');}";
      pagina += "function setDown(){fetch('/down');}";
      pagina += "function rele3On(){fetch('/rele3on');}";
      pagina += "function rele3Off(){fetch('/rele3off');}";
      pagina += "</script>";
      pagina += "<style>";
      pagina += "body{font-family:Segoe UI;text-align:center;background:#f7f9fc;color:#333;}";
      pagina += ".card{background:#e3f2fd;padding:20px;margin:20px;box-shadow:0 5px 10px rgba(0,0,0,0.1);border-radius:10px;}";
      pagina += ".alerta{width:100%;color:#ff0000;font-size:2em;font-weight:bold;background:#ffe6e6;padding:20px;border-radius:10px;box-shadow:0 0 10px red;margin-bottom:20px;display:none;}";
      pagina += "button{font-size:20px;padding:10px 20px;margin:10px;border:none;border-radius:10px;background:#3498db;color:white;cursor:pointer;transition:background 0.3s;}";
      pagina += "button:hover{opacity:0.8;}";
      pagina += ".button-group{display:flex;justify-content:center;gap:10px;}";
      pagina += ".state-box{font-size:24px;font-weight:bold;padding:15px;border-radius:10px;margin:10px 0;min-height:30px;}";
      pagina += "</style></head><body>";
      pagina += "<div id='alerta' class='alerta'>⚠ PRECAUCION EXCESO DE POTENCIA ⚠</div>";
      pagina += "<h1>SISTEMA CALEFACTOR</h1>";
      pagina += "<div class='card'><h2>TEMPERATURA EXTERIOR</h2>";
      pagina += "<div>TEMPERATURA: <span id='temp'></span> °C</div>";
      pagina += "<div>HUMEDAD: <span id='hum'></span> %</div></div>";
      pagina += "<div class='card'><h2>TEMPERATURA INTERIOR</h2>";
      pagina += "<div>TEMPERATURA: <span id='tempInt'></span> °C</div>";
      pagina += "<div>HUMEDAD: <span id='humInt'></span> %</div></div>";
      pagina += "<div class='card'><h2>SETPOINT</h2>";
      pagina += "<div>Valor: <span id='setp'></span> °C</div>";
      pagina += "<div class='button-group'>";
      pagina += "<button onclick='setUp()'>▲ AUMENTAR</button>";
      pagina += "<button onclick='setDown()'>▼ DISMINUIR</button>";
      pagina += "</div></div>";
      pagina += "<div class='card'><h2>OPERANDO</h2>";
      pagina += "<div>Modo actual: <span id='modo'></span></div></div>";
      pagina += "<div class='card'><h2>SISTEMA 1 (AUTOMÁTICO)</h2>";
      pagina += "<div>Estado: <span id='r1'></span></div></div>";
      pagina += "<div class='card'><h2>SISTEMA 2 (AUTOMÁTICO)</h2>";
      pagina += "<div>Estado: <span id='r2'></span></div></div>";
      pagina += "<div class='card'><h2>RELÉ MANUAL (CONTROL ON/OFF)</h2>";
      pagina += "<div>Estado: <span id='r3'></span></div>";
      pagina += "<div class='button-group'>";
      pagina += "<button id='btnRele3On' onclick='rele3On()' style='background:#27ae60;'>🟢 ENCENDER</button>";
      pagina += "<button id='btnRele3Off' onclick='rele3Off()' style='background:#e74c3c;'>🔴 APAGAR</button>";
      pagina += "</div></div>";
      pagina += "<div class='card'><h2>RELÉ AUTOMÁTICO CON RETRASO (5 MINUTOS)</h2>";
      pagina += "<div>Estado: <span id='r4' class='state-box'></span></div>";
      pagina += "<div>Tiempo restante: <span id='timer' class='state-box'>0 seg</span></div>";
      pagina += "<p style='font-size:12px;color:#666;'><strong>Funcionamiento:</strong> Sigue a RELAY1/2 (HIGH inmediatamente). Al apagar RELAY1/2, espera 5 min antes de apagar.</p>";
      pagina += "</div>";
      pagina += "</body></html>";

      client.println("HTTP/1.1 200 OK");
      client.println("Content-type:text/html");
      client.println();
      client.println(pagina);
      client.println();
      client.stop();
    }
  }
}
