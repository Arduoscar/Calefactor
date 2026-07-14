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
//  - RELAY4 (GPIO 19): Sigue a RELAY1/2 con retraso al apagar
//  - EEPROM para persistencia de datos
//  - LCD I2C 16x2 optimizada sin parpadeo
//  - Control automático con HISTERESIS anti-rebote
// =====================================================================

#include <WiFi.h>
#include <DHTesp.h>
#include <IRremote.hpp>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>

#define DHT_EXTERIOR_PIN 17
#define DHT_INTERIOR_PIN 26
DHTesp dhtExterior;
DHTesp dhtInterior;

#define RELAY1_PIN 16
#define RELAY2_PIN 18
#define RELAY3_PIN 25              // Relay manual movido a GPIO25
#define RELAY4_PIN 19              // Relay4 ahora en GPIO19

#define IR_PIN 23
LiquidCrystal_I2C lcd(0x27, 16, 2);

int setpoint = 25;
int modo = 1;
int estadoReles = 0;
int estadoRele3Int = 0;

bool relay4EnEspera = false;
unsigned long relay4Timer = 0;
const unsigned long TIMER_5MIN = 300000;
bool relay1Relay2Activos = false;

int addrSetpoint    = 0;
int addrModo        = 4;
int addrEstadoReles = 8;
int addrRele3       = 12;

const char* ssid     = "Samsung OSCAR";
const char* password = "14323717";

IPAddress local_IP(192,168,137,220);
IPAddress gateway(192,168,137,1);
IPAddress subnet(255,255,255,0);
IPAddress primaryDNS(8,8,8,8);
IPAddress secondaryDNS(8,8,4,4);

WiFiServer server(80);

float lastTemp = -100;
float lastTempInterior = -100;
int lastModo = -1;
int lastSetpoint = -1;

const float HISTERESIS = 1.0;
unsigned long lastBlink = 0;
bool blinkState = false;

float tempInterior = 0;
float humInterior = 0;
float tempExterior = 0;
float humExterior = 0;

unsigned long lastDHTRead = 0;
const unsigned long DHT_READ_INTERVAL = 2000;

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
  delay(2000);

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
    Serial.println("\nWiFi NO disponible (control local activo)");
  }
}

void loop() {
  // 1) Lectura sensores independiente de WiFi
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

  // 2) IR independiente de WiFi
  if (IrReceiver.decode()) {
    unsigned long rawCode = IrReceiver.decodedIRData.decodedRawData;
    uint8_t command = IrReceiver.decodedIRData.command;

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
    }
    if (rawCode == 0xB946FF00) {
      modo = 2;
      EEPROM.writeInt(addrModo, modo);
      EEPROM.commit();
      digitalWrite(RELAY1_PIN, LOW);
      digitalWrite(RELAY2_PIN, LOW);
    }
    if (rawCode == 0xB847FF00) {
      modo = 3;
      EEPROM.writeInt(addrModo, modo);
      EEPROM.commit();
      digitalWrite(RELAY1_PIN, LOW);
      digitalWrite(RELAY2_PIN, LOW);
    }
    IrReceiver.resume();
  }

  // 3) Control relés 1/2 SIEMPRE activo
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

  // 4) Control RELAY4 robusto y totalmente independiente de web
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
        digitalWrite(RELAY4_PIN, LOW);   // apagado definitivo al terminar el conteo
        relay4EnEspera = false;
      } else {
        digitalWrite(RELAY4_PIN, HIGH);
      }
    } else {
      digitalWrite(RELAY4_PIN, LOW);
    }
  }

  // Estados para LCD/Web
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

  // LCD
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

  // 5) Web al final, sin afectar control
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client = server.available();
    if (client && client.connected()) {
      client.setTimeout(5);
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
        client.println("Connection: close");
        client.println();
        client.println(json);
        client.stop();
        return;
      }

      if (req.indexOf("GET /up") != -1) {
        setpoint++;
        EEPROM.writeInt(addrSetpoint, setpoint);
        EEPROM.commit();
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK");
        client.stop();
        return;
      }

      if (req.indexOf("GET /down") != -1) {
        setpoint--;
        EEPROM.writeInt(addrSetpoint, setpoint);
        EEPROM.commit();
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK");
        client.stop();
        return;
      }

      if (req.indexOf("GET /rele3on") != -1) {
        digitalWrite(RELAY3_PIN, HIGH);
        estadoRele3Int = 1;
        EEPROM.writeInt(addrRele3, estadoRele3Int);
        EEPROM.commit();
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK");
        client.stop();
        return;
      }

      if (req.indexOf("GET /rele3off") != -1) {
        digitalWrite(RELAY3_PIN, LOW);
        estadoRele3Int = 0;
        EEPROM.writeInt(addrRele3, estadoRele3Int);
        EEPROM.commit();
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK");
        client.stop();
        return;
      }

      String pagina = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
      pagina += "<title>Calefactor</title>";
      pagina += "<script>";
      pagina += "function actualizar(){fetch('/data').then(r=>r.json()).then(d=>{";
      pagina += "document.getElementById('temp').innerHTML=d.temperatura;";
      pagina += "document.getElementById('hum').innerHTML=d.humedad;";
      pagina += "document.getElementById('tempInt').innerHTML=d.tempInterior;";
      pagina += "document.getElementById('humInt').innerHTML=d.humedadInterior;";
      pagina += "document.getElementById('setp').innerHTML=d.setpoint;";
      pagina += "document.getElementById('modo').innerHTML=d.modo;";
      pagina += "document.getElementById('r1').innerHTML=d.rele1;";
      pagina += "document.getElementById('r2').innerHTML=d.rele2;";
      pagina += "document.getElementById('r3').innerHTML=d.rele3;";
      pagina += "document.getElementById('r4').innerHTML=d.rele4;";
      pagina += "document.getElementById('timer').innerHTML=d.tiempoRestante+' seg';";
      pagina += "if(d.modo==3){document.getElementById('alerta').style.display='block';}else{document.getElementById('alerta').style.display='none';}";
      pagina += "});}";
      pagina += "setInterval(actualizar,2000);";
      pagina += "function setUp(){fetch('/up');} function setDown(){fetch('/down');}";
      pagina += "function rele3On(){fetch('/rele3on');} function rele3Off(){fetch('/rele3off');}";
      pagina += "</script>";
      pagina += "<style>body{font-family:Segoe UI;text-align:center;background:#f7f9fc;color:#333;}";
      pagina += ".card{background:#e3f2fd;padding:20px;margin:20px;box-shadow:0 5px 10px rgba(0,0,0,0.1);border-radius:10px;}";
      pagina += ".alerta{width:100%;color:#ff0000;font-size:2em;font-weight:bold;background:#ffe6e6;padding:20px;border-radius:10px;box-shadow:0 0 10px red;margin-bottom:20px;display:none;}";
      pagina += "button{font-size:20px;padding:10px 20px;margin:10px;border:none;border-radius:10px;background:#3498db;color:white;cursor:pointer;}";
      pagina += "</style></head><body>";
      pagina += "<div id='alerta' class='alerta'>⚠ PRECAUCION EXCESO DE POTENCIA ⚠</div>";
      pagina += "<h1>SISTEMA CALEFACTOR</h1>";
      pagina += "<div class='card'><h2>TEMPERATURA EXTERIOR</h2><div>TEMPERATURA: <span id='temp'></span> °C</div><div>HUMEDAD: <span id='hum'></span> %</div></div>";
      pagina += "<div class='card'><h2>TEMPERATURA INTERIOR</h2><div>TEMPERATURA: <span id='tempInt'></span> °C</div><div>HUMEDAD: <span id='humInt'></span> %</div></div>";
      pagina += "<div class='card'><h2>SETPOINT</h2><div>Valor: <span id='setp'></span> °C</div><button onclick='setUp()'>▲ AUMENTAR</button><button onclick='setDown()'>▼ DISMINUIR</button></div>";
      pagina += "<div class='card'><h2>OPERANDO</h2><div>Modo actual: <span id='modo'></span></div></div>";
      pagina += "<div class='card'><h2>SISTEMA 1 (AUTOMÁTICO)</h2><div>Estado: <span id='r1'></span></div></div>";
      pagina += "<div class='card'><h2>SISTEMA 2 (AUTOMÁTICO)</h2><div>Estado: <span id='r2'></span></div></div>";
      pagina += "<div class='card'><h2>RELÉ MANUAL</h2><div>Estado: <span id='r3'></span></div><button onclick='rele3On()'>ENCENDER</button><button onclick='rele3Off()'>APAGAR</button></div>";
      pagina += "<div class='card'><h2>RELÉ AUTOMÁTICO CON RETRASO (5 MIN)</h2><div>Estado: <span id='r4'></span></div><div>Tiempo restante: <span id='timer'>0 seg</span></div></div>";
      pagina += "</body></html>";

      client.println("HTTP/1.1 200 OK");
      client.println("Content-type:text/html");
      client.println("Connection: close");
      client.println();
      client.println(pagina);
      client.stop();
    }
  }
}
