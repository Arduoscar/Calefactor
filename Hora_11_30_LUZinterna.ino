// =====================================================================
// GRUPO: ENCABEZADO Y DESCRIPCIÓN GENERAL DEL SISTEMA
// =====================================================================
// Dispositivo: ESP32 Wemos D1 Mini
// Propósito: Sistema de control de calefactor inteligente
// Funcionalidades principales:
//  - Servidor Web NO BLOQUEANTE con IP fija
//  - DHT11 exterior (lectura temperatura/humedad ambiente exterior)
//  - DHT11 interior (lectura temperatura/humedad ambiente interior)
//  - Control por mando IR para cambiar setpoint y modo
//  - Relés con control automático/manual
//  - RELAY3: Control manual independiente desde la web
//  - RELAY4: Sigue a RELAY1/RELAY2 y mantiene retardo al apagarse
//  - EEPROM para guardar configuración persistente
//  - LCD I2C 16x2 con actualización optimizada
//  - Histéresis para evitar rebotes de encendido/apagado
// =====================================================================

// ------------------------------
// GRUPO: LIBRERÍAS
// ------------------------------
#include <WiFi.h>                // Manejo de conexión WiFi en ESP32
#include <DHTesp.h>              // Librería para sensores DHT en ESP32
#include <IRremote.hpp>          // Librería para recepción de mando IR
#include <EEPROM.h>              // Librería para memoria EEPROM emulada
#include <LiquidCrystal_I2C.h>   // Librería para LCD por I2C

// ------------------------------
// GRUPO: DEFINICIÓN DE PINES
// ------------------------------
#define DHT_EXTERIOR_PIN 17      // GPIO del sensor DHT11 exterior
#define DHT_INTERIOR_PIN 26      // GPIO del sensor DHT11 interior

// Objetos de los sensores DHT
DHTesp dhtExterior;              // Objeto para el sensor exterior
DHTesp dhtInterior;              // Objeto para el sensor interior

#define RELAY1_PIN 16            // GPIO del relé 1 (sistema automático 1)
#define RELAY2_PIN 18            // GPIO del relé 2 (sistema automático 2)
#define RELAY3_PIN 25            // GPIO del relé 3 (luz interna control web)
#define RELAY4_PIN 19            // GPIO del relé 4 (apagado automático con retardo)

#define IR_PIN 23                // GPIO donde está conectado el receptor IR

// Objeto del display LCD I2C dirección 0x27, 16 columnas x 2 filas
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ------------------------------
// GRUPO: VARIABLES DE CONTROL GENERAL
// ------------------------------
int setpoint = 23;               // Temperatura objetivo inicial
int modo = 1;                    // Modo inicial: 1=auto1, 2=auto2, 3=ambos
int estadoReles = 0;             // Variable reservada para estados persistentes
int estadoRele3Int = 0;          // Estado persistente del relé 3

// ------------------------------
// GRUPO: CONTROL DEL RELÉ 4 CON TEMPORIZADOR
// ------------------------------
bool relay4EnEspera = false;     // Indica si RELAY4 está en cuenta atrás para apagarse
unsigned long relay4Timer = 0;   // Guarda el instante en que empezó la espera
const unsigned long TIMER_5MIN =300000; // Tiempo de retardo para RELAY4 (60 s en pruebas)
bool relay1Relay2Activos = false; // Recuerda si RELAY1 o RELAY2 estuvieron activos

// ------------------------------
// GRUPO: DIRECCIONES EEPROM
// ------------------------------
int addrSetpoint    = 0;         // Dirección EEPROM para setpoint
int addrModo        = 4;         // Dirección EEPROM para modo
int addrEstadoReles = 8;         // Dirección EEPROM para estado general de relés
int addrRele3       = 12;        // Dirección EEPROM para estado de RELAY3

// ------------------------------
// GRUPO: CREDENCIALES WIFI Y RED FIJA
// ------------------------------
const char* ssid     = "Snoopy Troyano"; // Nombre de la red WiFi
const char* password = "PorAhora.123";      // Contraseña de la red WiFi

IPAddress local_IP(192,168,50,220);    // IP fija del ESP32
IPAddress gateway(192,168,50,1);       // Puerta de enlace
IPAddress subnet(255,255,255,0);        // Máscara de subred
IPAddress primaryDNS(8,8,8,8);          // DNS principal
IPAddress secondaryDNS(8,8,4,4);        // DNS secundario

// Servidor web en puerto 80
WiFiServer server(80);

// ------------------------------
// GRUPO: VARIABLES DE REFRESCO LCD
// ------------------------------
float lastTemp = -100;           // Última temperatura exterior mostrada
float lastTempInterior = -100;   // Última temperatura interior mostrada
int lastModo = -1;               // Último modo mostrado
int lastSetpoint = -1;           // Último setpoint mostrado

// ------------------------------
// GRUPO: CONSTANTES DE CONTROL TÉRMICO
// ------------------------------
const float HISTERESIS = 1.0;             // Banda de histéresis para evitar rebotes
const float OFFSET_TEMP_INTERIOR = -2.0;  // Corrección aplicada a temperatura interior
unsigned long lastBlink = 0;              // Control del parpadeo del mensaje de alerta
bool blinkState = false;                  // Estado actual del parpadeo

// ------------------------------
// GRUPO: VARIABLES DE MEDICIÓN
// ------------------------------
float tempInterior = 0;          // Temperatura interior actual
float humInterior = 0;           // Humedad interior actual
float tempExterior = 0;          // Temperatura exterior actual
float humExterior = 0;           // Humedad exterior actual

// ------------------------------
// GRUPO: CONTROL DE TIEMPO DE LECTURA DHT
// ------------------------------
unsigned long lastDHTRead = 0;                 // Último instante de lectura DHT
const unsigned long DHT_READ_INTERVAL = 2000;  // Intervalo entre lecturas en milisegundos

// =====================================================================
// FUNCIÓN: guardarSetpoint
// Propósito: guardar el setpoint actual en EEPROM
// =====================================================================
void guardarSetpoint() {
  EEPROM.writeInt(addrSetpoint, setpoint);   // Escribe el setpoint en EEPROM
  EEPROM.commit();                           // Fuerza el guardado real en memoria
}

// =====================================================================
// FUNCIÓN: guardarModo
// Propósito: guardar el modo actual en EEPROM
// =====================================================================
void guardarModo() {
  EEPROM.writeInt(addrModo, modo);           // Escribe el modo en EEPROM
  EEPROM.commit();                           // Confirma la escritura
}

// =====================================================================
// FUNCIÓN: apagarSistemasAutomaticos
// Propósito: apagar RELAY1 y RELAY2 al cambiar de modo
// =====================================================================
void apagarSistemasAutomaticos() {
  digitalWrite(RELAY1_PIN, LOW);             // Apaga el relé 1
  digitalWrite(RELAY2_PIN, LOW);             // Apaga el relé 2
}

// =====================================================================
// FUNCIÓN: imprimirCodigoIR
// Propósito: mostrar por monitor serie el código IR recibido
// =====================================================================
void imprimirCodigoIR(unsigned long rawCode, uint8_t command) {
  Serial.print("IR RAW: 0x");               // Muestra texto previo del código RAW
  Serial.print(rawCode, HEX);                // Imprime código bruto en hexadecimal
  Serial.print(" | CMD: 0x");              // Separador entre RAW y comando
  Serial.println(command, HEX);              // Imprime el comando decodificado
}

// =====================================================================
// FUNCIÓN: setup
// Propósito: inicialización general del sistema
// =====================================================================
void setup() {
  Serial.begin(115200);                      // Inicia comunicación serie para depuración
  EEPROM.begin(64);                          // Inicializa EEPROM emulada con 64 bytes

  // Lectura de los datos guardados previamente en EEPROM
  int eSetpoint = EEPROM.readInt(addrSetpoint);    // Lee setpoint guardado
  int eModo     = EEPROM.readInt(addrModo);        // Lee modo guardado
  int eEstado   = EEPROM.readInt(addrEstadoReles); // Lee estado general guardado
  int eRele3    = EEPROM.readInt(addrRele3);       // Lee estado guardado de RELAY3

  // Validación de valores antes de aplicarlos
  if (eSetpoint >= 0 && eSetpoint <= 100) setpoint = eSetpoint; // Solo si es válido
  if (eModo >= 1 && eModo <= 3) modo = eModo;                   // Solo si el modo es válido
  if (eEstado >= 0 && eEstado <= 3) estadoReles = eEstado;      // Solo si el rango es válido
  if (eRele3 >= 0 && eRele3 <= 1) estadoRele3Int = eRele3;      // Solo si es 0 o 1

  // Inicialización de los sensores DHT11
  dhtExterior.setup(DHT_EXTERIOR_PIN, DHTesp::DHT11); // Configura sensor exterior
  dhtInterior.setup(DHT_INTERIOR_PIN, DHTesp::DHT11); // Configura sensor interior

  // Configuración de pines de relés como salidas
  pinMode(RELAY1_PIN, OUTPUT);               // RELAY1 como salida
  pinMode(RELAY2_PIN, OUTPUT);               // RELAY2 como salida
  pinMode(RELAY3_PIN, OUTPUT);               // RELAY3 como salida
  pinMode(RELAY4_PIN, OUTPUT);               // RELAY4 como salida

  // Estado inicial de los relés al arrancar el equipo
  digitalWrite(RELAY1_PIN, LOW);             // Apaga RELAY1 al iniciar
  digitalWrite(RELAY2_PIN, LOW);             // Apaga RELAY2 al iniciar
  digitalWrite(RELAY3_PIN, LOW);             // Apaga RELAY3 al iniciar
  digitalWrite(RELAY4_PIN, LOW);             // Apaga RELAY4 al iniciar

  // Recupera el estado persistente de la luz interna
  if (estadoRele3Int == 1) {
    digitalWrite(RELAY3_PIN, HIGH);          // Si estaba encendido, lo vuelve a activar
  }

  IrReceiver.begin(IR_PIN, ENABLE_LED_FEEDBACK); // Inicializa el receptor infrarrojo

  // Inicialización del display LCD y mensaje de bienvenida
  lcd.init();                                // Inicia el LCD
  lcd.backlight();                           // Activa la luz de fondo
  lcd.clear();                               // Limpia contenido previo
  lcd.setCursor(0,0);                        // Sitúa cursor al inicio de la primera línea
  lcd.print("CREADO POR OSCAR");            // Muestra mensaje inicial
  delay(2000);                               // Mantiene el mensaje 2 segundos

  // Configuración de red e inicio de conexión WiFi
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS); // Configura IP fija
  WiFi.begin(ssid, password);                // Intenta conectar a la red WiFi

  // Espera de conexión limitada para no bloquear demasiado tiempo
  unsigned long inicio = millis();           // Guarda el tiempo inicial
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 5000) {
    delay(200);                              // Pequeña pausa entre reintentos
    Serial.print(".");                      // Muestra progreso en el monitor serie
  }

  // Si conecta, arranca el servidor web; si no, sigue en modo local
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado");     // Informa conexión correcta
    Serial.println(WiFi.localIP());          // Muestra la IP asignada
    server.begin();                          // Inicia el servidor web
  } else {
    Serial.println("\nWiFi NO disponible (control local activo)"); // Sigue funcionando sin web
  }

  Serial.println("Monitor IR listo. Presiona cualquier tecla del control para ver RAW y CMD.");
}

// =====================================================================
// FUNCIÓN: loop
// Propósito: bucle principal de ejecución continua
// =====================================================================
void loop() {
  // -----------------------------------------------------------------
  // GRUPO: LECTURA PERIÓDICA DE LOS SENSORES DHT11
  // -----------------------------------------------------------------
  if (millis() - lastDHTRead >= DHT_READ_INTERVAL) {
    float tExt = dhtExterior.getTemperature(); // Lee temperatura exterior
    float hExt = dhtExterior.getHumidity();    // Lee humedad exterior
    float tInt = dhtInterior.getTemperature(); // Lee temperatura interior
    float hInt = dhtInterior.getHumidity();    // Lee humedad interior

    if (!isnan(tInt)) tInt += OFFSET_TEMP_INTERIOR; // Aplica corrección de calibración

    if (!isnan(tExt)) tempExterior = tExt;     // Guarda temperatura exterior válida
    if (!isnan(hExt)) humExterior = hExt;      // Guarda humedad exterior válida
    if (!isnan(tInt)) tempInterior = tInt;     // Guarda temperatura interior válida
    if (!isnan(hInt)) humInterior = hInt;      // Guarda humedad interior válida

    if (isnan(tInt) || isnan(hInt)) {
      Serial.println("DHT11 interior (GPIO26) lectura inválida (NaN)"); // Informa error de lectura
    }

    lastDHTRead = millis();                    // Actualiza el instante de última lectura
  }

  // -----------------------------------------------------------------
  // GRUPO: RECEPCIÓN Y GESTIÓN DEL MANDO INFRARROJO
  // -----------------------------------------------------------------
  if (IrReceiver.decode()) {
    unsigned long rawCode = IrReceiver.decodedIRData.decodedRawData; // Obtiene código RAW
    uint8_t command = IrReceiver.decodedIRData.command;              // Obtiene comando decodificado

    imprimirCodigoIR(rawCode, command);       // Muestra por serie el código recibido

    if (rawCode == 0xE718FF00) {
      setpoint++;                             // Aumenta setpoint
      guardarSetpoint();                      // Guarda nuevo valor
      Serial.print("SETPOINT aumentado a: ");
      Serial.println(setpoint);
    }
    else if (rawCode == 0xAD52FF00) {
      setpoint--;                             // Disminuye setpoint
      guardarSetpoint();                      // Guarda nuevo valor
      Serial.print("SETPOINT disminuido a: ");
      Serial.println(setpoint);
    }
    else if (rawCode == 0xBA45FF00) {
      modo = 1;                               // Selecciona modo 1
      guardarModo();                          // Guarda modo en EEPROM
      apagarSistemasAutomaticos();            // Reinicia relés automáticos
      Serial.println("MODO 1 activado");
    }
    else if (rawCode == 0xB946FF00) {
      modo = 2;                               // Selecciona modo 2
      guardarModo();                          // Guarda modo en EEPROM
      apagarSistemasAutomaticos();            // Reinicia relés automáticos
      Serial.println("MODO 2 activado");
    }
    else if (rawCode == 0xB847FF00) {
      modo = 3;                               // Selecciona modo 3
      guardarModo();                          // Guarda modo en EEPROM
      apagarSistemasAutomaticos();            // Reinicia relés automáticos
      Serial.println("MODO 3 activado");
    }
    // Nueva tecla IR para encender/apagar la luz interna desde el mando
    else if (rawCode == 0xE31CFF00) {
      estadoRele3Int = !estadoRele3Int;       // Invierte el estado actual de RELAY3
      digitalWrite(RELAY3_PIN, estadoRele3Int ? HIGH : LOW); // Aplica el nuevo estado al relé
      EEPROM.writeInt(addrRele3, estadoRele3Int); // Guarda el nuevo estado en EEPROM
      EEPROM.commit();                        // Confirma la escritura persistente
      Serial.print("LUZ INTERNA RELAY3: ");
      Serial.println(estadoRele3Int ? "ON" : "OFF"); // Informa si quedó encendida o apagada
    }
    else if (command == 0x16) {
      setpoint = 0;                           // Establece setpoint en 0
      guardarSetpoint();                      // Guarda en EEPROM
      Serial.println("SETPOINT fijado a 0");
    }
    else if (command == 0xD) {
      setpoint = 23;                          // Establece setpoint en 23
      guardarSetpoint();                      // Guarda en EEPROM
      Serial.println("SETPOINT fijado a 23");
    }

    IrReceiver.resume();                      // Prepara receptor para la siguiente señal
  }

  // -----------------------------------------------------------------
  // GRUPO: CONTROL AUTOMÁTICO DE RELÉS SEGÚN EL MODO
  // -----------------------------------------------------------------
  if (modo == 1) {
    if (digitalRead(RELAY1_PIN)) {
      if (tempInterior >= setpoint + HISTERESIS) digitalWrite(RELAY1_PIN, LOW); // Apaga al superar el umbral alto
    } else {
      if (tempInterior <= setpoint - HISTERESIS) digitalWrite(RELAY1_PIN, HIGH); // Enciende al bajar del umbral bajo
    }
    digitalWrite(RELAY2_PIN, LOW);            // En modo 1, RELAY2 siempre apagado
  }

  if (modo == 2) {
    if (digitalRead(RELAY2_PIN)) {
      if (tempInterior >= setpoint + HISTERESIS) digitalWrite(RELAY2_PIN, LOW); // Apaga al superar el umbral alto
    } else {
      if (tempInterior <= setpoint - HISTERESIS) digitalWrite(RELAY2_PIN, HIGH); // Enciende al bajar del umbral bajo
    }
    digitalWrite(RELAY1_PIN, LOW);            // En modo 2, RELAY1 siempre apagado
  }

  if (modo == 3) {
    if (digitalRead(RELAY1_PIN) || digitalRead(RELAY2_PIN)) {
      if (tempInterior >= setpoint + HISTERESIS) {
        digitalWrite(RELAY1_PIN, LOW);        // Apaga RELAY1
        digitalWrite(RELAY2_PIN, LOW);        // Apaga RELAY2
      }
    } else {
      if (tempInterior <= setpoint - HISTERESIS) {
        digitalWrite(RELAY1_PIN, HIGH);       // Enciende RELAY1
        digitalWrite(RELAY2_PIN, HIGH);       // Enciende RELAY2
      }
    }
  }

  // -----------------------------------------------------------------
  // GRUPO: CONTROL DE RELAY4 CON RETARDO DE APAGADO
  // -----------------------------------------------------------------
  bool relay1O2Activos = (digitalRead(RELAY1_PIN) || digitalRead(RELAY2_PIN)); // Comprueba si RELAY1 o RELAY2 están activos

  if (relay1O2Activos) {
    digitalWrite(RELAY4_PIN, HIGH);           // Si hay actividad, enciende RELAY4
    relay4EnEspera = false;                   // Cancela cualquier temporizador previo
    relay1Relay2Activos = true;               // Marca que hubo actividad de los relés automáticos
  } else {
    if (relay1Relay2Activos) {
      relay4EnEspera = true;                  // Inicia fase de espera antes de apagar
      relay4Timer = millis();                 // Guarda el instante de arranque del temporizador
      relay1Relay2Activos = false;            // Borra la marca de actividad previa
    }

    if (relay4EnEspera) {
      if (millis() - relay4Timer >= TIMER_5MIN) {
        digitalWrite(RELAY4_PIN, LOW);        // Apaga RELAY4 al terminar la espera
        relay4EnEspera = false;               // Finaliza el estado de espera
      } else {
        digitalWrite(RELAY4_PIN, HIGH);       // Mantiene RELAY4 encendido durante la cuenta atrás
      }
    } else {
      digitalWrite(RELAY4_PIN, LOW);          // Si no hay actividad ni espera, RELAY4 apagado
    }
  }

  // -----------------------------------------------------------------
  // GRUPO: PREPARACIÓN DE ESTADOS EN TEXTO PARA LA WEB
  // -----------------------------------------------------------------
  String estadoRele1 = digitalRead(RELAY1_PIN) ? "ON" : "OFF";       // Estado textual de RELAY1
  String estadoRele2 = digitalRead(RELAY2_PIN) ? "ON" : "OFF";       // Estado textual de RELAY2
  String estadoRele3String = digitalRead(RELAY3_PIN) ? "ON" : "OFF"; // Estado textual de RELAY3
  String estadoRele4 = digitalRead(RELAY4_PIN) ? "ON" : "OFF";       // Estado textual de RELAY4

  // -----------------------------------------------------------------
  // GRUPO: CÁLCULO DEL TIEMPO RESTANTE DE RELAY4
  // -----------------------------------------------------------------
  unsigned long tiempoRestante = 0;           // Inicializa contador en 0
  if (relay4EnEspera) {
    unsigned long tiempoTranscurrido = millis() - relay4Timer; // Tiempo ya consumido
    if (tiempoTranscurrido < TIMER_5MIN) {
      tiempoRestante = (TIMER_5MIN - tiempoTranscurrido) / 1000; // Convierte a segundos restantes
    }
  }

  // -----------------------------------------------------------------
  // GRUPO: ACTUALIZACIÓN OPTIMIZADA DEL DISPLAY LCD
  // -----------------------------------------------------------------
  if (tempExterior != lastTemp) {
    lcd.setCursor(0,0);                       // Coloca cursor para temperatura exterior
    lcd.print("EXT:");                       // Etiqueta exterior
    lcd.print(tempExterior,1);                // Muestra valor con 1 decimal
    lcd.print("   ");                        // Borra restos de caracteres antiguos
    lastTemp = tempExterior;                  // Guarda el valor mostrado
  }

  if (tempInterior != lastTempInterior) {
    lcd.setCursor(10,0);                      // Coloca cursor para temperatura interior
    lcd.print("INT:");                       // Etiqueta interior
    lcd.print(tempInterior,0);                // Muestra valor sin decimales
    lcd.print("   ");                        // Borra restos de caracteres antiguos
    lastTempInterior = tempInterior;          // Guarda el valor mostrado
  }

  if (modo != lastModo) {
    lcd.setCursor(0,1);                       // Posición del modo
    lcd.print("M:");                         // Etiqueta del modo
    if (modo == 1) lcd.print("AUTO1");      // Texto para modo 1
    else if (modo == 2) lcd.print("AUTO2"); // Texto para modo 2
    else lcd.print("      ");                // Limpia la zona si entra en modo 3
    lastModo = modo;                          // Guarda el último modo mostrado
  }

  if (setpoint != lastSetpoint) {
    lcd.setCursor(9,1);                       // Posición del setpoint
    lcd.print("SETP:");                      // Etiqueta del setpoint
    lcd.print(setpoint);                      // Muestra el valor actual
    lcd.print("   ");                        // Limpia posibles restos
    lastSetpoint = setpoint;                  // Guarda el último setpoint mostrado
  }

  // Mensaje parpadeante de advertencia cuando el modo 3 está activo
  if (modo == 3) {
    if (millis() - lastBlink >= 500) {
      blinkState = !blinkState;               // Alterna estado para parpadeo
      lastBlink = millis();                   // Reinicia temporizador de parpadeo
    }
    lcd.setCursor(0,1);                       // Coloca cursor en segunda línea
    if (blinkState) lcd.print("PRECAUCION!");
    else lcd.print("            ");          // Borra texto para crear efecto parpadeo
  }

  // -----------------------------------------------------------------
  // GRUPO: ATENCIÓN DEL SERVIDOR WEB Y PETICIONES HTTP
  // -----------------------------------------------------------------
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client = server.available();   // Comprueba si hay un cliente conectado
    if (client && client.connected()) {
      client.setTimeout(5);                   // Tiempo máximo de espera para leer petición
      String req = client.readStringUntil('\r'); // Lee la primera línea de la petición HTTP

      // -------------------------------------------------------------
      // SUBGRUPO: ENDPOINT /data PARA ENVIAR JSON CON EL ESTADO
      // -------------------------------------------------------------
      if (req.indexOf("GET /data") != -1) {
        String json = "{";                   // Inicio del JSON
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
        json += "}";                         // Fin del JSON

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println();
        client.println(json);                 // Envía el JSON al navegador
        client.stop();                        // Cierra conexión
        return;                               // Sale del loop para no seguir procesando
      }

      // -------------------------------------------------------------
      // SUBGRUPO: ENDPOINTS PARA MODIFICAR EL SETPOINT
      // -------------------------------------------------------------
      if (req.indexOf("GET /up") != -1) {
        setpoint++;                           // Incrementa el setpoint
        guardarSetpoint();                    // Lo guarda en EEPROM
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK");
        client.stop();
        return;
      }

      if (req.indexOf("GET /down") != -1) {
        setpoint--;                           // Decrementa el setpoint
        guardarSetpoint();                    // Lo guarda en EEPROM
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK");
        client.stop();
        return;
      }

      if (req.indexOf("GET /sp0") != -1) {
        setpoint = 0;                         // Fija setpoint a 0
        guardarSetpoint();                    // Lo guarda en EEPROM
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK");
        client.stop();
        return;
      }

      if (req.indexOf("GET /sp23") != -1) {
        setpoint = 23;                        // Fija setpoint a 23
        guardarSetpoint();                    // Lo guarda en EEPROM
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK");
        client.stop();
        return;
      }

      // -------------------------------------------------------------
      // SUBGRUPO: ENDPOINTS DE LA LUZ INTERNA (RELAY3)
      // -------------------------------------------------------------
      if (req.indexOf("GET /rele3on") != -1) {
        digitalWrite(RELAY3_PIN, HIGH);       // Enciende RELAY3
        estadoRele3Int = 1;                   // Actualiza variable persistente
        EEPROM.writeInt(addrRele3, estadoRele3Int); // Guarda el estado en EEPROM
        EEPROM.commit();                      // Confirma la escritura
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK");
        client.stop();
        return;
      }

      if (req.indexOf("GET /rele3off") != -1) {
        digitalWrite(RELAY3_PIN, LOW);        // Apaga RELAY3
        estadoRele3Int = 0;                   // Actualiza variable persistente
        EEPROM.writeInt(addrRele3, estadoRele3Int); // Guarda el estado en EEPROM
        EEPROM.commit();                      // Confirma la escritura
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK");
        client.stop();
        return;
      }

      // -------------------------------------------------------------
      // SUBGRUPO: CONSTRUCCIÓN DE LA PÁGINA WEB HTML + CSS + JS
      // -------------------------------------------------------------
      String pagina = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"; // Cabecera HTML
      pagina += "<title>Calefactor</title>";                                // Título de la pestaña
      pagina += "<script>";                                                  // Inicio del bloque JavaScript
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
      pagina += "});}";                                                      // Fin de función actualizar
      pagina += "setInterval(actualizar,2000);";                             // Refresco automático cada 2 segundos
      pagina += "function setUp(){fetch('/up');} function setDown(){fetch('/down');}";   // Funciones subir/bajar
      pagina += "function setSp0(){fetch('/sp0');} function setSp23(){fetch('/sp23');}"; // Funciones directas de setpoint
      pagina += "function rele3On(){fetch('/rele3on');} function rele3Off(){fetch('/rele3off');}"; // Funciones de luz interna
      pagina += "</script>";                                                 // Fin del JavaScript
      pagina += "<style>body{font-family:Segoe UI;text-align:center;background:#f7f9fc;color:#333;}"; // Estilo general
      pagina += ".card{background:#e3f2fd;padding:20px;margin:20px;box-shadow:0 5px 10px rgba(0,0,0,0.1);border-radius:10px;}"; // Tarjetas
      pagina += ".alerta{width:100%;color:#ff0000;font-size:2em;font-weight:bold;background:#ffe6e6;padding:20px;border-radius:10px;box-shadow:0 0 10px red;margin-bottom:20px;display:none;}"; // Alerta visual
      pagina += ".setpoint-valor{font-size:3.2em;font-weight:bold;color:#0d47a1;margin:15px 0;}"; // Setpoint más grande y visible
      pagina += "button{font-size:20px;padding:10px 20px;margin:10px;border:none;border-radius:10px;background:#3498db;color:white;cursor:pointer;}"; // Botones
      pagina += "</style></head><body>";                                     // Fin de head e inicio body
      pagina += "<div id='alerta' class='alerta'>⚠ PRECAUCION EXCESO DE POTENCIA ⚠</div>"; // Banner de advertencia
      pagina += "<h1>SISTEMA CALEFACTOR</h1>";                               // Título principal
      pagina += "<div class='card'><h2>SETPOINT</h2><div class='setpoint-valor'><span id='setp'></span> °C</div><button onclick='setUp()'>▲ AUMENTAR</button><button onclick='setDown()'>▼ DISMINUIR</button><button onclick='setSp0()'>0°C</button><button onclick='setSp23()'>23°C</button></div>"; // Tarjeta de control del setpoint
      pagina += "<div class='card'><h2>TEMPERATURA EXTERIOR</h2><div>TEMPERATURA: <span id='temp'></span> °C</div><div>HUMEDAD: <span id='hum'></span> %</div></div>"; // Tarjeta exterior
      pagina += "<div class='card'><h2>TEMPERATURA INTERIOR</h2><div>TEMPERATURA: <span id='tempInt'></span> °C</div><div>HUMEDAD: <span id='humInt'></span> %</div></div>"; // Tarjeta interior
      pagina += "<div class='card'><h2>OPERANDO</h2><div>MODO: <span id='modo'></span></div></div>"; // Tarjeta modo
      pagina += "<div class='card'><h2>SISTEMA 1 (AUTOMÁTICO)</h2><div>Estado: <span id='r1'></span></div></div>"; // Tarjeta RELAY1
      pagina += "<div class='card'><h2>SISTEMA 2 (AUTOMÁTICO)</h2><div>Estado: <span id='r2'></span></div></div>"; // Tarjeta RELAY2
      pagina += "<div class='card'><h2>APAGADO AUTOMATICO</h2><div>Estado: <span id='r4'></span></div><div>Tiempo restante: <span id='timer'>0 seg</span></div></div>"; // Tarjeta RELAY4
      pagina += "<div class='card'><h2>LUZ INTERNA</h2><div>Estado: <span id='r3'></span></div><button onclick='rele3On()'>ENCENDER</button><button onclick='rele3Off()'>APAGAR</button></div>"; // Tarjeta RELAY3
      pagina += "</body></html>";                                            // Cierre del documento HTML

      // Respuesta HTTP con la página principal al navegador
      client.println("HTTP/1.1 200 OK");
      client.println("Content-type:text/html");
      client.println("Connection: close");
      client.println();
      client.println(pagina);                   // Envía el HTML generado
      client.stop();                            // Cierra la conexión con el cliente
    }
  }
}
