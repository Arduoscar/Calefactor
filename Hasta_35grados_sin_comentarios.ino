
#include <WiFi.h>
#include <DHTesp.h>
#include <IRremote.hpp>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#define DHTPIN 17
DHTesp dht;

#define KY_PIN 36
#define RELAY1_PIN 16
#define RELAY2_PIN 18
#define IR_PIN 23
LiquidCrystal_I2C lcd(0x27, 16, 2);
int setpoint = 25;
int modo = 1;
int estadoReles = 0;
int addrSetpoint    = 0;
int addrModo        = 4;
int addrEstadoReles = 8;
const char* ssid     = "Samsung OSCAR";
const char* password = "14323717";
IPAddress local_IP(192,168,137,220);
IPAddress gateway(192,168,137,1);
IPAddress subnet(255,255,255,0);
IPAddress primaryDNS(8,8,8,8);
IPAddress secondaryDNS(8,8,4,4);
WiFiServer server(80);
float lastTemp = -100;
float lastTempKY = -100;
int lastModo = -1;
int lastSetpoint = -1;
const float HISTERESIS = 1.0;
unsigned long lastBlink = 0;
bool blinkState = false;
unsigned long lastKY = 0;   
float tempKY = 0;            
void setup() {
  Serial.begin(115200);
  EEPROM.begin(64);
  int eSetpoint = EEPROM.readInt(addrSetpoint);
  int eModo     = EEPROM.readInt(addrModo);
  int eEstado   = EEPROM.readInt(addrEstadoReles);
  if (eSetpoint >= 0 && eSetpoint <= 100) setpoint = eSetpoint;
  if (eModo >= 1 && eModo <= 3) modo = eModo;
  if (eEstado >= 0 && eEstado <= 3) estadoReles = eEstado;
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
  dht.setup(DHTPIN, DHTesp::DHT11);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);
  IrReceiver.begin(IR_PIN, ENABLE_LED_FEEDBACK);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("CREADO POR OSCAR");
  delay(5000);
}

void loop() {
  if (millis() - lastKY >= 500) {
    int suma = 0;
    for(int i=0; i<10; i++){
      suma += analogRead(KY_PIN);
      delay(1);
    }
    int rawKY = suma / 10;
    tempKY = map(rawKY, 0, 5950, 0, 50) + 1;
    lastKY = millis();
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

  float temperatura = dht.getTemperature();
  float humedad = dht.getHumidity();

  if (modo == 1) {
    if (digitalRead(RELAY1_PIN)) {
      if (tempKY >= setpoint + HISTERESIS) digitalWrite(RELAY1_PIN, LOW);
    } else {
      if (tempKY <= setpoint - HISTERESIS) digitalWrite(RELAY1_PIN, HIGH);
    }
    digitalWrite(RELAY2_PIN, LOW);
  }
  if (modo == 2) {
    if (digitalRead(RELAY2_PIN)) {
      if (tempKY >= setpoint + HISTERESIS) digitalWrite(RELAY2_PIN, LOW);
    } else {
      if (tempKY <= setpoint - HISTERESIS) digitalWrite(RELAY2_PIN, HIGH);
    }
    digitalWrite(RELAY1_PIN, LOW);
  }
  if (modo == 3) {
    if (digitalRead(RELAY1_PIN) || digitalRead(RELAY2_PIN)) {
      if (tempKY >= setpoint + HISTERESIS) {
        digitalWrite(RELAY1_PIN, LOW);
        digitalWrite(RELAY2_PIN, LOW);
      }
    } else {
      if (tempKY <= setpoint - HISTERESIS) {
        digitalWrite(RELAY1_PIN, HIGH);
        digitalWrite(RELAY2_PIN, HIGH);
      }
    }
  }
  String estadoRele1 = digitalRead(RELAY1_PIN) ? "ON" : "OFF";
  String estadoRele2 = digitalRead(RELAY2_PIN) ? "ON" : "OFF";
  if (temperatura != lastTemp) {
    lcd.setCursor(0,0);
    lcd.print("EXT:");
    lcd.print(temperatura,1);
    lcd.print("   ");
    lastTemp = temperatura;
  }
  if (tempKY != lastTempKY) {
    lcd.setCursor(10,0);
    lcd.print("INT:");
    lcd.print(tempKY,0);
    lcd.print("   ");
    lastTempKY = tempKY;
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
        json += "\"temperatura\":" + String(temperatura) + ",";
        json += "\"humedad\":" + String(humedad) + ",";
        json += "\"tempKY\":" + String(tempKY) + ",";
        json += "\"setpoint\":" + String(setpoint) + ",";
        json += "\"modo\":" + String(modo) + ",";
        json += "\"rele1\":\"" + estadoRele1 + "\",";
        json += "\"rele2\":\"" + estadoRele2 + "\"";
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
      String pagina = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
      pagina += "<title>Calefactor</title>";
      pagina += "<script>";
      pagina += "function actualizar(){";
      pagina += "fetch('/data').then(r=>r.json()).then(d=>{";
      pagina += "document.getElementById('temp').innerHTML = d.temperatura;";
      pagina += "document.getElementById('hum').innerHTML = d.humedad;";
      pagina += "document.getElementById('ky').innerHTML = d.tempKY;";
      pagina += "document.getElementById('setp').innerHTML = d.setpoint;";
      pagina += "document.getElementById('modo').innerHTML = d.modo;";
      pagina += "document.getElementById('r1').innerHTML = d.rele1;";
      pagina += "document.getElementById('r2').innerHTML = d.rele2;";
      pagina += "if(d.modo == 3){document.getElementById('alerta').style.display='block';}";
      pagina += "else{document.getElementById('alerta').style.display='none';}";
      pagina += "});}";
      pagina += "setInterval(actualizar,2000);";  // ⭐ AJAX cada 2 segundos (estable)
      pagina += "function setUp(){fetch('/up');}";
      pagina += "function setDown(){fetch('/down');}";
      pagina += "</script>";
      pagina += "<style>";
      pagina += "body{font-family:Segoe UI;text-align:center;background:#f7f9fc;color:#333;}";
      pagina += ".card{background:#e3f2fd;padding:20px;margin:20px;"
                "box-shadow:0 5px 10px rgba(0,0,0,0.1);border-radius:10px;}";
      pagina += ".alerta{width:100%;color:#ff0000;font-size:2em;font-weight:bold;"
                "background:#ffe6e6;padding:20px;border-radius:10px;"
                "box-shadow:0 0 10px red;margin-bottom:20px;display:none;}";
      pagina += "button{font-size:20px;padding:10px 20px;margin:10px;border:none;"
                "border-radius:10px;background:#3498db;color:white;cursor:pointer;}";
      pagina += "button:hover{background:#2980b9;}";
      pagina += "</style></head><body>";
      pagina += "<div id='alerta' class='alerta'>⚠ PRECAUCION EXCESO DE POTENCIA ⚠</div>";
      pagina += "<h1>SISTEMA CALEFACTOR</h1>";
      pagina += "<div class='card'><h2>TEMPERATURA EXTERIOR</h2>";
      pagina += "<div>TEMPERATURA: <span id='temp'></span> °C</div>";
      pagina += "<div>HUMEDAD: <span id='hum'></span> %</div></div>";
      pagina += "<div class='card'><h2>TEMPERATURA INTERIOR</h2>";
      pagina += "<div>Temperatura: <span id='ky'></span> °C</div></div>";
      pagina += "<div class='card'><h2>SETPOINT</h2>";
      pagina += "<div>Valor: <span id='setp'></span> °C</div>";
      pagina += "<button onclick='setUp()'>▲</button>";
      pagina += "<button onclick='setDown()'>▼</button>";
      pagina += "</div>";
      pagina += "<div class='card'><h2>OPERANDO</h2>";
      pagina += "<div>Modo actual: <span id='modo'></span></div></div>";
      pagina += "<div class='card'><h2>SISTEMA 1</h2>";
      pagina += "<div>Estado: <span id='r1'></span></div></div>";
      pagina += "<div class='card'><h2>SISTEMA 2</h2>";
      pagina += "<div>Estado: <span id='r2'></span></div></div>";
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
