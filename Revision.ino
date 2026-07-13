// =====================================================================
// GRUPO: ENCABEZADO Y DESCRIPCIÓN GENERAL DEL SISTEMA
// =====================================================================
// Dispositivo: ESP32 Wemos D1 Mini
// Propósito: Sistema de control de calefactor inteligente
// Funcionalidades principales:
//  - Servidor Web NO BLOQUEANTE con IP fija
//  - DHT11 (lectura temperatura/humedad exterior)
//  - KY-013 (lectura temperatura interna) + ajuste +1°C
//  - Relés con control IR y modos avanzados
//  - RELAY3: Control manual ON/OFF independiente
//  - RELAY4 (GPIO 25): Sigue a RELAY1/2 con retraso al setpoint - NUEVO
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
// GRUPO: CONFIGURACIÓN DEL SENSOR DHT11
// =====================================================================
#define DHTPIN 17                  // Pin GPIO 17 del ESP32 para el DHT11
DHTesp dht;                        // Objeto para manejar el sensor DHT

// =====================================================================
// GRUPO: CONFIGURACIÓN DEL SENSOR KY-013 (temperatura analógica)
// =====================================================================
#define KY_PIN 36                  // Pin analógico GPIO 36 (ADC) para KY-013

// =====================================================================
// GRUPO: CONFIGURACIÓN DE LOS RELÉS
// =====================================================================
#define RELAY1_PIN 16              // Pin GPIO 16 para el relé 1 (automático 1)
#define RELAY2_PIN 18              // Pin GPIO 18 para el relé 2 (automático 2)
#define RELAY3_PIN 19              // Pin GPIO 19 para el relé 3 (manual ON/OFF)
#define RELAY4_PIN 25              // Pin GPIO 25 para el relé 4 (sigue a RELAY1/2 con retraso) - NUEVO

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
int estadoRele4Int = 0;            // Estado del relé GPIO 25 (0=OFF, 1=ON)

// =====================================================================
// GRUPO: VARIABLES PARA RELAY4 (Sigue a RELAY1/2 con retraso) - NUEVO
// =====================================================================
bool relay4ContadorActivo = false;  // Flag: true si el contador de 5 minutos está activo
unsigned long relay4Timer = 0;      // Timestamp del inicio del temporizador de 5 minutos
const unsigned long TIMER_5MIN = 300000; // 5 minutos en milisegundos (300000 ms)
bool setpointAlcanzado = false;    // Flag: true si tempKY alcanzó setpoint al menos una vez

// =====================================================================
// GRUPO: DIRECCIONES DE ALMACENAMIENTO EN EEPROM
// =====================================================================
int addrSetpoint    = 0;           // Dirección EEPROM para guardar setpoint
int addrModo        = 4;           // Dirección EEPROM para guardar modo
int addrEstadoReles = 8;           // Dirección EEPROM para guardar estado de relés
int addrRele3       = 12;          // Dirección EEPROM para guardar estado relay3
int addrRele4       = 16;          // Dirección EEPROM para guardar estado relay4 (GPIO 25)

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
float lastTemp = -100;                  // Última temperatura DHT mostrada
float lastTempKY = -100;                // Última temperatura KY-013 mostrada
int lastModo = -1;                      // Último modo mostrado
int lastSetpoint = -1;                  // Último setpoint mostrado
int lastEstadoRele3 = -1;               // Último estado relay3 mostrado

// =====================================================================
// GRUPO: HISTERESIS ANTI-REBOTE
// =====================================================================
// La hist éresis evita oscilaciones rápidas del relé cuando la temperatura está cerca del setpoint
const float HISTERESIS = 1.0;           // Rango de histéresis en grados Celsius

// =====================================================================
// GRUPO: VARIABLES PARA PARPADEO EN MODO 3
// =====================================================================
// Modo 3 es "PRECAUCIÓN" y muestra un mensaje parpadeante en LCD
unsigned long lastBlink = 0;            // Timestamp del último cambio de parpadeo
bool blinkState = false;                // Estado actual del parpadeo (true=visible, false=oculto)

// =====================================================================
// GRUPO: LECTURA ESTABLE DEL KY-013
// =====================================================================
// El KY-013 se lee cada 500ms promediando 10 muestras para evitar ruido
unsigned long lastKY = 0;               // Timestamp de la última lectura estable del KY-013
float tempKY = 0;                       // Temperatura interna estable (actualizada cada 500ms)

// =====================================================================
//  FUNCIÓN: SETUP (Inicialización del sistema)
// =====================================================================
void setup() {
  // ---- SECCIÓN: Inicialización de comunicación serial ----
  Serial.begin(115200);                 // Iniciar puerto serial a 115200 baud para debug

  // ---- SECCIÓN: Inicialización de EEPROM ----
  EEPROM.begin(64);                     // Reservar 64 bytes de EEPROM

  // ---- SECCIÓN: Lectura de valores almacenados en EEPROM ----
  int eSetpoint = EEPROM.readInt(addrSetpoint);   // Leer setpoint guardado
  int eModo     = EEPROM.readInt(addrModo);       // Leer modo guardado
  int eEstado   = EEPROM.readInt(addrEstadoReles);// Leer estado relés guardado
  int eRele3    = EEPROM.readInt(addrRele3);      // Leer estado relay3 guardado
  int eRele4    = EEPROM.readInt(addrRele4);      // Leer estado relay4 guardado

  // ---- SECCIÓN: Validación e importación de valores EEPROM ----
  // Solo se importan valores si están dentro de los rangos válidos
  if (eSetpoint >= 0 && eSetpoint <= 100) setpoint = eSetpoint;  // Validar setpoint (0-100°C)
  if (eModo >= 1 && eModo <= 3) modo = eModo;                    // Validar modo (1, 2 o 3)
  if (eEstado >= 0 && eEstado <= 3) estadoReles = eEstado;       // Validar estado relés
  if (eRele3 >= 0 && eRele3 <= 1) estadoRele3Int = eRele3;       // Validar relay3 (0 o 1)
  if (eRele4 >= 0 && eRele4 <= 1) estadoRele4Int = eRele4;       // Validar relay4 (0 o 1)

  // ---- SECCIÓN: Configuración de red WiFi ----
  // Asignar IP fija antes de conectar
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  // Iniciar conexión WiFi con SSID y contraseña
  WiFi.begin(ssid, password);

  // ---- SECCIÓN: Espera de conexión WiFi (máximo 5 segundos) ----
  unsigned long inicio = millis();      // Guardar timestamp de inicio
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 5000) {
    delay(200);                         // Esperar 200ms entre intentos
    Serial.print(".");                  // Mostrar punto de progreso
  }

  // ---- SECCIÓN: Verificación de conexión WiFi ----
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado");           // Mensaje de éxito
    Serial.println(WiFi.localIP());              // Mostrar IP asignada
    server.begin();                               // Iniciar servidor web
  } else {
    Serial.println("\nWiFi NO disponible");       // Mensaje de error
  }

  // ---- SECCIÓN: Configuración del sensor DHT11 ----
  dht.setup(DHTPIN, DHTesp::DHT11);               // Inicializar DHT11 en pin DHTPIN

  // ---- SECCIÓN: Configuración de pines de relés como salidas ----
  pinMode(RELAY1_PIN, OUTPUT);                   // Configurar RELAY1 como salida digital
  pinMode(RELAY2_PIN, OUTPUT);                   // Configurar RELAY2 como salida digital
  pinMode(RELAY3_PIN, OUTPUT);                   // Configurar RELAY3 como salida digital
  pinMode(RELAY4_PIN, OUTPUT);                   // Configurar RELAY4 como salida digital - NUEVO

  // ---- SECCIÓN: Inicialización de relés en estado OFF (LOW) ----
  digitalWrite(RELAY1_PIN, LOW);                 // Apagar relé 1
  digitalWrite(RELAY2_PIN, LOW);                 // Apagar relé 2
  digitalWrite(RELAY3_PIN, LOW);                 // Apagar relé 3
  digitalWrite(RELAY4_PIN, estadoRele4Int ? HIGH : LOW); // Restaurar relé 4 guardado

  // ---- SECCIÓN: Configuración del receptor infrarrojo ----
  IrReceiver.begin(IR_PIN, ENABLE_LED_FEEDBACK); // Inicializar IR con feedback LED

  // ---- SECCIÓN: Configuración de la pantalla LCD ----
  lcd.init();                                    // Inicializar LCD
  lcd.backlight();                               // Encender luz de fondo
  lcd.clear();                                   // Limpiar pantalla
  lcd.setCursor(0,0);                            // Posicionar cursor en fila 0, columna 0
  lcd.print("CREADO POR OSCAR");                 // Mostrar texto de bienvenida
  delay(5000);                                   // Esperar 5 segundos para ver el mensaje
}

// =====================================================================
//  FUNCIÓN: LOOP (Bucle principal)
// =====================================================================
void loop() {

  // =====================================================================
  // GRUPO: LECTURA ESTABLE DEL SENSOR KY-013 CADA 500ms
  // =====================================================================
  // Se realiza cada 500ms para no saturar el bucle principal
  if (millis() - lastKY >= 500) {
    // ---- SECCIÓN: Recolección de 10 muestras del ADC ----
    int suma = 0;                                // Variable acumuladora para promediar
    for(int i=0; i<10; i++){                     // Bucle para tomar 10 muestras
      suma += analogRead(KY_PIN);                // Leer valor analógico y sumar
      delay(1);                                  // Esperar 1ms entre muestras
    }

    // ---- SECCIÓN: Cálculo del promedio y conversión a temperatura ----
    int rawKY = suma / 10;                       // Dividir por 10 para obtener promedio
    // Mapear rango analógico (0-5950) a rango de temperatura (0-50°C) y sumar +1°C de ajuste
    tempKY = map(rawKY, 0, 5950, 0, 50) + 1;

    // ---- SECCIÓN: Actualización del timestamp de lectura ----
    lastKY = millis();                           // Guardar tiempo actual
  }

  // =====================================================================
  // GRUPO: LECTURA DEL CONTROL REMOTO INFRARROJO
  // =====================================================================
  if (IrReceiver.decode()) {
    // ---- SECCIÓN: Extracción de códigos IR recibidos ----
    unsigned long rawCode = IrReceiver.decodedIRData.decodedRawData;  // Código IR completo
    uint8_t command = IrReceiver.decodedIRData.command;               // Comando IR

    // ---- SECCIÓN: Envío de códigos por serial para debugging ----
    Serial.print("RAW: "); Serial.println(rawCode, HEX);   // Mostrar código RAW en hexadecimal
    Serial.print("CMD: "); Serial.println(command, HEX);   // Mostrar comando en hexadecimal

    // ---- SECCIÓN: Botón IR para aumentar setpoint ----
    if (rawCode == 0xE718FF00) {
      setpoint++;                                          // Incrementar setpoint
      EEPROM.writeInt(addrSetpoint, setpoint);             // Guardar en EEPROM
      EEPROM.commit();                                     // Confirmar escritura
    }

    // ---- SECCIÓN: Botón IR para disminuir setpoint ----
    if (rawCode == 0xAD52FF00) {
      setpoint--;                                          // Decrementar setpoint
      EEPROM.writeInt(addrSetpoint, setpoint);             // Guardar en EEPROM
      EEPROM.commit();                                     // Confirmar escritura
    }

    // ---- SECCIÓN: Botón IR para activar MODO 1 (AUTO1) ----
    if (command == 0x45) {
      modo = 1;                                            // Cambiar a modo 1
      EEPROM.writeInt(addrModo, modo);                     // Guardar modo en EEPROM
      EEPROM.commit();                                     // Confirmar
      digitalWrite(RELAY1_PIN, LOW);                       // Apagar relé 1
      digitalWrite(RELAY2_PIN, LOW);                       // Apagar relé 2
      estadoReles = 1;                                     // Marcar estado como 1
      EEPROM.writeInt(addrEstadoReles, estadoReles);       // Guardar estado en EEPROM
      EEPROM.commit();                                     // Confirmar
    }

    // ---- SECCIÓN: Botón IR para activar MODO 2 (AUTO2) ----
    if (rawCode == 0xB946FF00) {
      modo = 2;                                            // Cambiar a modo 2
      EEPROM.writeInt(addrModo, modo);                     // Guardar modo en EEPROM
      EEPROM.commit();                                     // Confirmar
      digitalWrite(RELAY1_PIN, LOW);                       // Apagar relé 1
      digitalWrite(RELAY2_PIN, LOW);                       // Apagar relé 2
      estadoReles = 2;                                     // Marcar estado como 2
      EEPROM.writeInt(addrEstadoReles, estadoReles);       // Guardar estado en EEPROM
      EEPROM.commit();                                     // Confirmar
    }

    // ---- SECCIÓN: Botón IR para activar MODO 3 (PRECAUCIÓN) ----
    if (rawCode == 0xB847FF00) {
      modo = 3;                                            // Cambiar a modo 3
      EEPROM.writeInt(addrModo, modo);                     // Guardar modo en EEPROM
      EEPROM.commit();                                     // Confirmar
      digitalWrite(RELAY1_PIN, LOW);                       // Apagar relé 1
      digitalWrite(RELAY2_PIN, LOW);                       // Apagar relé 2
      estadoReles = 3;                                     // Marcar estado como 3
      EEPROM.writeInt(addrEstadoReles, estadoReles);       // Guardar estado en EEPROM
      EEPROM.commit();                                     // Confirmar
    }

    // ---- SECCIÓN: Botón IR para forzar GPIO 25 a ON (manual) ----
    if (command == 0x44) {
      digitalWrite(RELAY4_PIN, HIGH);                      // Encender GPIO 25 inmediatamente
      estadoRele4Int = 1;                                  // Actualizar estado entero para EEPROM
      relay4ContadorActivo = false;                        // Reiniciar contador al forzar encendido
      EEPROM.writeInt(addrRele4, estadoRele4Int);          // Guardar estado manual de GPIO 25
      EEPROM.commit();                                     // Confirmar escritura en EEPROM
    }

    // ---- SECCIÓN: Botón IR para forzar GPIO 25 a OFF (manual) ----
    if (command == 0x43) {
      digitalWrite(RELAY4_PIN, LOW);                       // Apagar GPIO 25 inmediatamente
      estadoRele4Int = 0;                                  // Actualizar estado entero para EEPROM
      relay4ContadorActivo = false;                        // Reiniciar contador al forzar apagado
      EEPROM.writeInt(addrRele4, estadoRele4Int);          // Guardar estado manual de GPIO 25
      EEPROM.commit();                                     // Confirmar escritura en EEPROM
    }

    // ---- SECCIÓN: Reanudación del receptor IR para próximas lecturas ----
    IrReceiver.resume();                                   // Preparar IR para siguiente código
  }

  // =====================================================================
  // GRUPO: LECTURA DEL SENSOR DHT11
  // =====================================================================
  float temperatura = dht.getTemperature();               // Leer temperatura exterior en °C
  float humedad = dht.getHumidity();                      // Leer humedad relativa en %

  // =====================================================================
  // GRUPO: CONTROL AUTOMÁTICO DE RELÉS SEGÚN MODO
  // =====================================================================
  // La lógica usa histéresis para evitar oscilaciones del relé

  // ---- SECCIÓN: MODO 1 (Control RELAY1 con histéresis) ----
  if (modo == 1) {
    // Si el relé 1 está encendido (HIGH), verificar si debe apagarse
    if (digitalRead(RELAY1_PIN)) {
      // Si temperatura interna >= setpoint + histéresis, apagar relé
      if (tempKY >= setpoint + HISTERESIS) digitalWrite(RELAY1_PIN, LOW);
    } else {
      // Si el relé 1 está apagado, verificar si debe encenderse
      // Si temperatura interna <= setpoint - histéresis, encender relé
      if (tempKY <= setpoint - HISTERESIS) digitalWrite(RELAY1_PIN, HIGH);
    }
    digitalWrite(RELAY2_PIN, LOW);                        // Asegurar que RELAY2 está OFF
  }

  // ---- SECCIÓN: MODO 2 (Control RELAY2 con histéresis) ----
  if (modo == 2) {
    // Si el relé 2 está encendido (HIGH), verificar si debe apagarse
    if (digitalRead(RELAY2_PIN)) {
      // Si temperatura interna >= setpoint + histéresis, apagar relé
      if (tempKY >= setpoint + HISTERESIS) digitalWrite(RELAY2_PIN, LOW);
    } else {
      // Si el relé 2 está apagado, verificar si debe encenderse
      // Si temperatura interna <= setpoint - histéresis, encender relé
      if (tempKY <= setpoint - HISTERESIS) digitalWrite(RELAY2_PIN, HIGH);
    }
    digitalWrite(RELAY1_PIN, LOW);                        // Asegurar que RELAY1 está OFF
  }

  // ---- SECCIÓN: MODO 3 (Ambos relés con histéresis) ----
  // Modo de precaución: ambos relés trabajan juntos
  if (modo == 3) {
    // Si alguno de los relés está encendido
    if (digitalRead(RELAY1_PIN) || digitalRead(RELAY2_PIN)) {
      // Si temperatura interna >= setpoint + histéresis, apagar ambos
      if (tempKY >= setpoint + HISTERESIS) {
        digitalWrite(RELAY1_PIN, LOW);
        digitalWrite(RELAY2_PIN, LOW);
      }
    } else {
      // Si ambos relés están apagados, verificar si deben encenderse
      // Si temperatura interna <= setpoint - histéresis, encender ambos
      if (tempKY <= setpoint - HISTERESIS) {
        digitalWrite(RELAY1_PIN, HIGH);
        digitalWrite(RELAY2_PIN, HIGH);
      }
    }
  }

  // =====================================================================
  // GRUPO: CONTROL DE RELAY4 (Sigue a RELAY1/2 con retraso de 5 min) - NUEVO
  // =====================================================================
  // Lógica solicitada:
  // 1) Si RELAY1 o RELAY2 están HIGH => RELAY4 HIGH inmediato.
  // 2) Si RELAY1 y RELAY2 están LOW => iniciar conteo de 5 minutos, solo si ya se alcanzó setpoint.
  // 3) Si durante conteo vuelve HIGH en RELAY1/2 => cancelar/reiniciar conteo y RELAY4 vuelve HIGH.
  // 4) Solo después de 5 minutos continuos en LOW => RELAY4 LOW.

  // ---- SECCIÓN: Detección estable del setpoint con histéresis ----
  if (!setpointAlcanzado && tempKY >= setpoint) {
    setpointAlcanzado = true;                             // Latchear evento de setpoint alcanzado
  }
  if (setpointAlcanzado && tempKY <= setpoint - HISTERESIS) {
    setpointAlcanzado = false;                            // Liberar latch cuando cae por histéresis
    relay4ContadorActivo = false;                         // Cancelar contador hasta nuevo setpoint
  }

  // ---- SECCIÓN: Lectura de condición maestra (RELAY1/2) ----
  bool rele1o2High = digitalRead(RELAY1_PIN) || digitalRead(RELAY2_PIN); // true si alguno está activo

  // ---- SECCIÓN: Control inteligente principal de RELAY4 ----
  if (rele1o2High) {
    digitalWrite(RELAY4_PIN, HIGH);                       // Activación inmediata por demanda de RELAY1/2
    relay4ContadorActivo = false;                         // Detener/reiniciar contador al volver a HIGH
  } else {
    if (setpointAlcanzado) {
      if (!relay4ContadorActivo) {
        relay4ContadorActivo = true;                      // Iniciar conteo solo cuando ambos pasan a LOW
        relay4Timer = millis();                           // Guardar instante de inicio del conteo
      }

      if (millis() - relay4Timer >= TIMER_5MIN) {
        digitalWrite(RELAY4_PIN, LOW);                    // Apagar tras 5 minutos LOW ininterrumpidos
      } else {
        digitalWrite(RELAY4_PIN, HIGH);                   // Mantener HIGH mientras corre el temporizador
      }
    } else {
      relay4ContadorActivo = false;                       // Sin setpoint alcanzado no hay conteo activo
      digitalWrite(RELAY4_PIN, HIGH);                     // Mantener HIGH hasta alcanzar setpoint
    }
  }

  // ---- SECCIÓN: Lectura del estado actual de los relés para mostrar en web ----
  String estadoRele1 = digitalRead(RELAY1_PIN) ? "ON" : "OFF";  // RELAY1: ON o OFF
  String estadoRele2 = digitalRead(RELAY2_PIN) ? "ON" : "OFF";  // RELAY2: ON o OFF
  String estadoRele3String = digitalRead(RELAY3_PIN) ? "ON" : "OFF";  // RELAY3: ON o OFF
  String estadoRele4 = digitalRead(RELAY4_PIN) ? "ON" : "OFF";  // RELAY4: ON o OFF - NUEVO

  // ---- SECCIÓN: Persistencia del estado real de GPIO 25 en EEPROM ----
  int estadoRele4ActualInt = digitalRead(RELAY4_PIN) ? 1 : 0;   // Convertir estado digital a entero
  if (estadoRele4ActualInt != estadoRele4Int) {
    estadoRele4Int = estadoRele4ActualInt;                       // Actualizar estado interno
    EEPROM.writeInt(addrRele4, estadoRele4Int);                  // Persistir cambio en EEPROM
    EEPROM.commit();                                             // Confirmar escritura solo cuando cambió
  }

  // ---- SECCIÓN: Cálculo del tiempo restante del temporizador para mostrar en web - NUEVO ----
  unsigned long tiempoRestante = 0;                       // Tiempo restante en segundos
  if (relay4ContadorActivo) {
    // Si estamos dentro del temporizador, calcular tiempo restante
    unsigned long tiempoTranscurrido = millis() - relay4Timer;
    if (tiempoTranscurrido < TIMER_5MIN) {
      tiempoRestante = (TIMER_5MIN - tiempoTranscurrido) / 1000; // Convertir a segundos
    } else {
      tiempoRestante = 0;                                 // El timer ya expiró
    }
  }

  // =====================================================================
  // GRUPO: ACTUALIZACIÓN DE PANTALLA LCD CON ANTI-PARPADEO
  // =====================================================================
  // Solo se redibuja si hay cambios para evitar parpadeos innecesarios

  // ---- SECCIÓN: Mostrar temperatura exterior (DHT11) ----
  if (temperatura != lastTemp) {
    lcd.setCursor(0,0);                                   // Posicionar cursor fila 0, columna 0
    lcd.print("EXT:");                                    // Etiqueta "EXT"
    lcd.print(temperatura,1);                             // Mostrar temperatura con 1 decimal
    lcd.print("   ");                                     // Espacios para limpiar caracteres anteriores
    lastTemp = temperatura;                               // Guardar valor mostrado
  }

  // ---- SECCIÓN: Mostrar temperatura interior (KY-013) ----
  if (tempKY != lastTempKY) {
    lcd.setCursor(10,0);                                  // Posicionar cursor fila 0, columna 10
    lcd.print("INT:");                                    // Etiqueta "INT"
    lcd.print(tempKY,0);                                  // Mostrar temperatura sin decimales
    lcd.print("   ");                                     // Espacios para limpiar caracteres anteriores
    lastTempKY = tempKY;                                  // Guardar valor mostrado
  }

  // ---- SECCIÓN: Mostrar modo de operación actual ----
  if (modo != lastModo) {
    lcd.setCursor(0,1);                                   // Posicionar cursor fila 1, columna 0
    lcd.print("M:");                                      // Etiqueta "M" (Modo)
    if (modo == 1) lcd.print("AUTO1");                    // Mostrar "AUTO1" para modo 1
    else if (modo == 2) lcd.print("AUTO2");               // Mostrar "AUTO2" para modo 2
    else lcd.print("      ");                             // Espacios en blanco para modo 3 (parpadea por separado)
    lastModo = modo;                                      // Guardar valor mostrado
  }

  // ---- SECCIÓN: Mostrar setpoint (valor objetivo de temperatura) ----
  if (setpoint != lastSetpoint) {
    lcd.setCursor(9,1);                                   // Posicionar cursor fila 1, columna 9
    lcd.print("SETP:");                                   // Etiqueta "SETP"
    lcd.print(setpoint);                                  // Mostrar valor del setpoint
    lcd.print("   ");                                     // Espacios para limpiar caracteres anteriores
    lastSetpoint = setpoint;                              // Guardar valor mostrado
  }

  // ---- SECCIÓN: Mostrar alerta parpadeante en MODO 3 ----
  if (modo == 3) {
    // Cambiar estado de parpadeo cada 500ms
    if (millis() - lastBlink >= 500) {
      blinkState = !blinkState;                           // Invertir estado (true/false)
      lastBlink = millis();                               // Guardar timestamp del cambio
    }
    lcd.setCursor(0,1);                                   // Posicionar cursor fila 1, columna 0
    if (blinkState) {
      lcd.print("PRECAUCION!");                           // Mostrar advertencia cuando está visible
    } else {
      lcd.print("            ");                          // Ocultar con espacios cuando está oculto
    }
  }

  // =====================================================================
  // GRUPO: SERVIDOR WEB AJAX (SIN RECARGA DE PÁGINA)
  // =====================================================================
  // Verificar si hay conexión WiFi activa
  if (WiFi.status() == WL_CONNECTED) {

    // ---- SECCIÓN: Aceptar conexión de cliente ----
    WiFiClient client = server.available();               // Verificar si hay cliente conectado

    // ---- SECCIÓN: Procesar solicitud HTTP del cliente ----
    if (client) {

      // ---- SUBSECCIÓN: Leer línea de solicitud HTTP ----
      String req = client.readStringUntil('\r');          // Leer hasta carriage return

      // =====================================================================
      // ENDPOINT: /data (Retorna JSON con todos los datos)
      // =====================================================================
      if (req.indexOf("GET /data") != -1) {

        // ---- Construcción del JSON con los datos actuales ----
        String json = "{";                                // Abrir objeto JSON
        json += "\"temperatura\":" + String(temperatura) + ",";  // Temperatura DHT11
        json += "\"humedad\":" + String(humedad) + ",";           // Humedad DHT11
        json += "\"tempKY\":" + String(tempKY) + ",";             // Temperatura KY-013
        json += "\"setpoint\":" + String(setpoint) + ",";         // Setpoint actual
        json += "\"modo\":" + String(modo) + ",";                 // Modo actual
        json += "\"rele1\":\"" + estadoRele1 + "\",";             // Estado RELAY1
        json += "\"rele2\":\"" + estadoRele2 + "\",";             // Estado RELAY2
        json += "\"rele3\":\"" + estadoRele3String + "\",";       // Estado RELAY3
        json += "\"rele4\":\"" + estadoRele4 + "\",";             // Estado RELAY4 - NUEVO
        json += "\"tiempoRestante\":" + String(tiempoRestante);   // Tiempo restante del timer - NUEVO
        json += "}";                                      // Cerrar objeto JSON

        // ---- Envío de respuesta HTTP con JSON ----
        client.println("HTTP/1.1 200 OK");                // Código de éxito
        client.println("Content-Type: application/json"); // Tipo de contenido JSON
        client.println();                                 // Línea en blanco (separador)
        client.println(json);                             // Enviar datos JSON
        client.stop();                                    // Cerrar conexión
        return;                                           // Salir de la función
      }

      // =====================================================================
      // ENDPOINT: /up (Aumentar setpoint)
      // =====================================================================
      if (req.indexOf("GET /up") != -1) {
        setpoint++;                                       // Incrementar setpoint en 1°C
        EEPROM.writeInt(addrSetpoint, setpoint);          // Guardar nuevo valor en EEPROM
        EEPROM.commit();                                  // Confirmar escritura
        client.println("HTTP/1.1 200 OK");                // Código de éxito
        client.println("Content-Type: text/plain");       // Tipo de contenido texto
        client.println();                                 // Línea en blanco
        client.println("OK");                             // Confirmación
        client.stop();                                    // Cerrar conexión
        return;                                           // Salir
      }

      // =====================================================================
      // ENDPOINT: /down (Disminuir setpoint)
      // =====================================================================
      if (req.indexOf("GET /down") != -1) {
        setpoint--;                                       // Decrementar setpoint en 1°C
        EEPROM.writeInt(addrSetpoint, setpoint);          // Guardar nuevo valor en EEPROM
        EEPROM.commit();                                  // Confirmar escritura
        client.println("HTTP/1.1 200 OK");                // Código de éxito
        client.println("Content-Type: text/plain");       // Tipo de contenido texto
        client.println();                                 // Línea en blanco
        client.println("OK");                             // Confirmación
        client.stop();                                    // Cerrar conexión
        return;                                           // Salir
      }

      // =====================================================================
      // ENDPOINT: /rele3on (Encender GPIO 25 manualmente)
      // =====================================================================
      if (req.indexOf("GET /rele3on") != -1) {
        digitalWrite(RELAY4_PIN, HIGH);                   // Establecer GPIO 25 a HIGH (ON)
        estadoRele4Int = 1;                               // Guardar estado en variable INT
        relay4ContadorActivo = false;                     // Detener contador por activación manual
        EEPROM.writeInt(addrRele4, estadoRele4Int);       // Guardar en EEPROM
        EEPROM.commit();                                  // Confirmar escritura
        client.println("HTTP/1.1 200 OK");                // Código de éxito
        client.println("Content-Type: text/plain");       // Tipo de contenido texto
        client.println();                                 // Línea en blanco
        client.println("OK");                             // Confirmación
        client.stop();                                    // Cerrar conexión
        return;                                           // Salir
      }

      // =====================================================================
      // ENDPOINT: /rele3off (Apagar GPIO 25 manualmente)
      // =====================================================================
      if (req.indexOf("GET /rele3off") != -1) {
        digitalWrite(RELAY4_PIN, LOW);                    // Establecer GPIO 25 a LOW (OFF)
        estadoRele4Int = 0;                               // Guardar estado en variable INT
        relay4ContadorActivo = false;                     // Detener contador por apagado manual
        EEPROM.writeInt(addrRele4, estadoRele4Int);       // Guardar en EEPROM
        EEPROM.commit();                                  // Confirmar escritura
        client.println("HTTP/1.1 200 OK");                // Código de éxito
        client.println("Content-Type: text/plain");       // Tipo de contenido texto
        client.println();                                 // Línea en blanco
        client.println("OK");                             // Confirmación
        client.stop();                                    // Cerrar conexión
        return;                                           // Salir
      }

      // =====================================================================
      // PÁGINA HTML PRINCIPAL (INTERFAZ WEB)
      // =====================================================================
      String pagina = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
      pagina += "<title>Calefactor</title>";

      // ---- SECCIÓN: JAVASCRIPT (AJAX y funciones de control) ----
      pagina += "<script>";

      // Función para actualizar datos en tiempo real (cada 2 segundos)
      pagina += "function actualizar(){";
      pagina += "fetch('/data').then(r=>r.json()).then(d=>{";  // Solicitar JSON al ESP32

      // Actualizar elementos HTML con los datos recibidos
      pagina += "document.getElementById('temp').innerHTML = d.temperatura;";     // Temperatura exterior
      pagina += "document.getElementById('hum').innerHTML = d.humedad;";           // Humedad
      pagina += "document.getElementById('ky').innerHTML = d.tempKY;";             // Temperatura interior
      pagina += "document.getElementById('setp').innerHTML = d.setpoint;";         // Setpoint
      pagina += "document.getElementById('modo').innerHTML = d.modo;";             // Modo
      pagina += "document.getElementById('r1').innerHTML = d.rele1;";              // Estado RELAY1
      pagina += "document.getElementById('r2').innerHTML = d.rele2;";              // Estado RELAY2
      pagina += "document.getElementById('r4').innerHTML = d.rele4;";              // Estado RELAY4 - NUEVO
      pagina += "document.getElementById('r4manual').innerHTML = d.rele4;";        // Estado GPIO 25 en control manual
      pagina += "document.getElementById('timer').innerHTML = d.tiempoRestante + ' seg';"; // Timer - NUEVO

      // Mostrar alerta si está en MODO 3 (PRECAUCIÓN)
      pagina += "if(d.modo == 3){document.getElementById('alerta').style.display='block';}";
      pagina += "else{document.getElementById('alerta').style.display='none';}";

      // Cambiar color de botones GPIO 25 según su estado actual
      pagina += "if(d.rele4 == 'ON'){";
      pagina += "document.getElementById('btnRele25On').style.background='#27ae60';"; // Verde si está ON
      pagina += "document.getElementById('btnRele25Off').style.background='#3498db';}"; // Azul si está OFF
      pagina += "else{";
      pagina += "document.getElementById('btnRele25On').style.background='#3498db';"; // Azul si está OFF
      pagina += "document.getElementById('btnRele25Off').style.background='#e74c3c';}"; // Rojo si está ON

      // Cambiar color del indicador RELAY4 según su estado - NUEVO
      pagina += "if(d.rele4 == 'ON'){";
      pagina += "document.getElementById('r4').style.background='#27ae60';";       // Verde si está ON
      pagina += "document.getElementById('r4').style.color='white';}";
      pagina += "else{";
      pagina += "document.getElementById('r4').style.background='#e74c3c';";       // Rojo si está OFF
      pagina += "document.getElementById('r4').style.color='white';}";

      pagina += "});}";

      // Configurar actualización automática cada 2 segundos
      pagina += "setInterval(actualizar,2000);";

      // Funciones para controlar el setpoint
      pagina += "function setUp(){fetch('/up');}";
      pagina += "function setDown(){fetch('/down');}";

      // Funciones para controlar GPIO 25 manualmente
      pagina += "function rele3On(){fetch('/rele3on');}";
      pagina += "function rele3Off(){fetch('/rele3off');}";

      pagina += "</script>";

      // ---- SECCIÓN: ESTILOS CSS ----
      pagina += "<style>";
      pagina += "body{font-family:Segoe UI;text-align:center;background:#f7f9fc;color:#333;}";
      pagina += ".card{background:#e3f2fd;padding:20px;margin:20px;box-shadow:0 5px 10px rgba(0,0,0,0.1);border-radius:10px;}";
      pagina += ".alerta{width:100%;color:#ff0000;font-size:2em;font-weight:bold;background:#ffe6e6;padding:20px;border-radius:10px;box-shadow:0 0 10px red;margin-bottom:20px;display:none;}";
      pagina += "button{font-size:20px;padding:10px 20px;margin:10px;border:none;border-radius:10px;background:#3498db;color:white;cursor:pointer;transition:background 0.3s;}";
      pagina += "button:hover{opacity:0.8;}";
      pagina += ".button-group{display:flex;justify-content:center;gap:10px;}";  // Para botones lado a lado
      pagina += ".state-box{font-size:24px;font-weight:bold;padding:15px;border-radius:10px;margin:10px 0;min-height:30px;}"; // Para estados
      pagina += "</style></head><body>";

      // ---- SECCIÓN: ALERTA DE PRECAUCIÓN ----
      pagina += "<div id='alerta' class='alerta'>⚠ PRECAUCION EXCESO DE POTENCIA ⚠</div>";

      // ---- SECCIÓN: TÍTULO PRINCIPAL ----
      pagina += "<h1>SISTEMA CALEFACTOR</h1>";

      // ---- SECCIÓN: TEMPERATURA EXTERIOR (DHT11) ----
      pagina += "<div class='card'><h2>TEMPERATURA EXTERIOR</h2>";
      pagina += "<div>TEMPERATURA: <span id='temp'></span> °C</div>";
      pagina += "<div>HUMEDAD: <span id='hum'></span> %</div></div>";

      // ---- SECCIÓN: TEMPERATURA INTERIOR (KY-013) ----
      pagina += "<div class='card'><h2>TEMPERATURA INTERIOR</h2>";
      pagina += "<div>Temperatura: <span id='ky'></span> °C</div></div>";

      // ---- SECCIÓN: CONTROL DE SETPOINT ----
      pagina += "<div class='card'><h2>SETPOINT</h2>";
      pagina += "<div>Valor: <span id='setp'></span> °C</div>";
      pagina += "<div class='button-group'>";
      pagina += "<button onclick='setUp()'>▲ AUMENTAR</button>";
      pagina += "<button onclick='setDown()'>▼ DISMINUIR</button>";
      pagina += "</div></div>";

      // ---- SECCIÓN: ESTADO DEL MODO ----
      pagina += "<div class='card'><h2>OPERANDO</h2>";
      pagina += "<div>Modo actual: <span id='modo'></span></div></div>";

      // ---- SECCIÓN: ESTADO SISTEMA 1 (RELAY1) ----
      pagina += "<div class='card'><h2>SISTEMA 1 (AUTOMÁTICO)</h2>";
      pagina += "<div>Estado: <span id='r1'></span></div></div>";

      // ---- SECCIÓN: ESTADO SISTEMA 2 (RELAY2) ----
      pagina += "<div class='card'><h2>SISTEMA 2 (AUTOMÁTICO)</h2>";
      pagina += "<div>Estado: <span id='r2'></span></div></div>";

      // ---- SECCIÓN: CONTROL MANUAL GPIO 25 ----
      pagina += "<div class='card'><h2>RELÉ GPIO 25 (CONTROL MANUAL ON/OFF)</h2>";
      pagina += "<div>Estado actual GPIO 25: <span id='r4manual'></span></div>";
      pagina += "<div class='button-group'>";
      pagina += "<button id='btnRele25On' onclick='rele3On()' style='background:#27ae60;'>🟢 ENCENDER</button>";
      pagina += "<button id='btnRele25Off' onclick='rele3Off()' style='background:#e74c3c;'>🔴 APAGAR</button>";
      pagina += "</div></div>";

      // ---- SECCIÓN: ESTADO RELAY4 (AUTOMÁTICO CON TEMPORIZADOR) - NUEVO ----
      pagina += "<div class='card'><h2>RELÉ AUTOMÁTICO CON RETRASO (5 MINUTOS)</h2>";
      pagina += "<div>Estado: <span id='r4' class='state-box'></span></div>";
      pagina += "<div>Tiempo restante: <span id='timer' class='state-box'>0 seg</span></div>";
      pagina += "<p style='font-size:12px;color:#666;'><strong>Funcionamiento:</strong> Sigue a RELAY1/2. Al alcanzar setpoint, espera 5 min antes de apagar.</p>";
      pagina += "</div>";

      pagina += "</body></html>";

      // ---- SECCIÓN: Envío de la página HTML al cliente ----
      client.println("HTTP/1.1 200 OK");                  // Código de éxito
      client.println("Content-type:text/html");           // Tipo de contenido HTML
      client.println();                                   // Línea en blanco
      client.println(pagina);                             // Enviar página HTML
      client.println();                                   // Línea final
      client.stop();                                      // Cerrar conexión
    }
  }
}
