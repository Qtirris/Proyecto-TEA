/*
  ============================================================================
  WEARABLE DE ESTRÉS Y SUEÑO - ESP32-C3 Mini
  ============================================================================
  Sensores: MAX30105/MAX30102 (I2C) + MPU6050 (I2C)
  Salidas : LED_Verde, LED_Rojo, Motor_Vibrador (Pin 3)

  ARQUITECTURA (Máquina de Estados Finitos):
    Sys_Init          -> Arranque, carga y muestra datos basales en Flash
    Provisional_Calib -> Solo la 1ra vez: captura rápida de 2-3 min en reposo
    Stress_Monitor     -> Modo diurno: calcula BPM/HRV y compara contra basal
    Biofeedback        -> Se activa cuando hay estrés: vibración guiada
    Sleep_Detect        -> Modo nocturno: confirma sueño profundo por puntaje
    Basal_Calib         -> Captura y guarda el basal de la noche en curso

  DECISIONES DE DISEÑO QUE DEBE VALIDAR CON DATOS REALES (no son mágicas):
    - Todos los umbrales marcados "AJUSTAR" son estimaciones razonables de
      literatura de PPG/HRV en muñeca, NO mediciones calibradas a su piel,
      su MAX30102 específico ni su forma de uso. Regístrelos y ajústelos
      con al menos 3-5 días de datos reales antes de confiar en las alertas.
    - Este archivo no ha sido compilado (no tengo toolchain de Arduino/ESP32
      ni acceso a red en este entorno). Revise errores de compilación de
      firma de librerías (SparkFun MAX3010x vs versión que usted tenga) y
      compárteme el log si algo no compila; lo corrijo en la siguiente vuelta.
  ============================================================================
*/

#include <Wire.h>
#include <MAX30105.h>
#include <heartRate.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Preferences.h>
//----------------------------
#include <WiFi.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <ArduinoJson.h>
// ============================================================================
// 1.A CONFIGURACIÓN DE PINES
// ============================================================================
// ADVERTENCIA: en su código original usaba Wire.begin(1, 0), es decir
// SDA=GPIO1, SCL=GPIO0. En la ESP32-C3 Mini, GPIO0/GPIO1 son pines de
// "strapping" usados en el arranque (selección de modo de boot / UART0).
// Suelen funcionar para I2C después de bootear, pero si tiene bloqueos
// intermitentes en el arranque, esta es la primera sospechosa. Le dejo
// los mismos pines para no romper su cableado físico, pero valide esto.
const int Pin_I2c_Sda = 1;
const int Pin_I2c_Scl = 0;

const int Pin_Led_Verde   = 9;
const int Pin_Led_Rojo    = 7;
const int Pin_Motor       = 3;
// ============================================================================
// 2. CONFIGURACIÓN DE UUIDs BLE
// ============================================================================
#define WIFI_CRED_SERV_UUID "3373E991-457D-4656-9544-28DE1576896D" 
#define WIFI_CRED_CHAR_UUID "DFDE7591-38A4-4019-A6A1-C09B4D0FCE70" 
#define PASS_CRED_CHAR_UUID "FB4E9190-0D85-4810-A2A0-124BFD25A1AA" 
#define WIFI_START_SERV_UUID "897DC0D1-1C3A-4567-BF0E-1EDB5DD83855"
#define WIFI_START_CHAR_UUID "03FE09DA-15E3-43B4-9C1B-47A7DA1AC992"
#define USER_TOKEN_CHAR_UUID "2DA7E879-F0D5-4E74-8317-E40A5D87413C"
#define HORA_DORMIR_CHAR_UUID "7009B792-356A-4B4A-BAA1-FF4C1F5FF601"
// ============================================================================
// 3. CONFIGURACIÓN VARIABLES COMS
// ============================================================================
String wifiScan();                                     //Forward Declaration
void wifiConnect(String ssid,String pass);  //Forward Declaration
const char *authIP = "https://teapp.lat/auth/auth.php";
String wifi_pass = "";
String wifi_ssid = "";
String publicIP = "";
String UserToken = "";
String User = "";
String Token = "";
bool superficie = 0;
const char* Namespace_WiFi = "Redes";
bool BLEapagado=true;
// --- Infraestructura de red en segundo plano (optimización) ---
// El formato del POST (campos y separadores ";") NO CAMBIA. Lo que cambia
// es que ni el POST ni la conexión WiFi se ejecutan más en el loop()
// principal ni en el callback de BLE: antes, infoPOST() bloqueaba el loop
// (y con él, la lectura de sensores/motor/LEDs) varios segundos cada 5
// latidos si la red estaba lenta, y wifiConnect()/getTime() bloqueaban el
// callback de BLE hasta 10+ segundos con sus delay(). Ahora una tarea de
// FreeRTOS de baja prioridad hace ese trabajo aparte.
struct Datos_Post {
  float Bpm;
  float Hvr;
  bool Superficie;
  char Alertas[8];
  float Fuerza_Golpe;
};
struct WiFi_Credentials {
  String ssid;
  String pass;
};
QueueHandle_t Cola_Post = NULL;
TaskHandle_t Handle_Tarea_Red = NULL;

volatile bool Solicitud_Wifi_Pendiente = false;
String Solicitud_Wifi_Ssid = "";
String Solicitud_Wifi_Pass = "";
// ============================================================================
// 4.A OBJETOS DE HARDWARE
// ============================================================================
MAX30105 Sensor_Cardiaco;
Adafruit_MPU6050 Sensor_Mpu;
Preferences Memoria;
// ============================================================================
// 4.B OBJETOS DEL BLE
// ============================================================================
BLEServer *pServer = nullptr;

BLEService *pWifiCredService = nullptr;
BLECharacteristic *pWifiStartChar = nullptr;

BLEService *pWifiStartService = nullptr;
BLECharacteristic *pWifiCredChar = nullptr;
BLECharacteristic *pPassCredChar = nullptr;
BLECharacteristic *pUserTokenChar = nullptr;
BLECharacteristic *pHoraDormirChar = nullptr;
// ============================================================================
// 4.C CONFIGURACIÓN DEL MAX30102
// ============================================================================
// REVERTIDO a los valores originales (Brillo=100, Lecturas=1, ADC=8192) a
// pedido explícito, porque el intento de "optimizar para muñeca" (potencia
// más alta + promediado de 8 muestras) coincidió con una detección de
// latidos degradada: un latido y luego silencio. Mi lectura de por qué:
// promediar 8 muestras suaviza la señal, y eso puede aplanar la componente
// pulsátil (AC) que checkForBeat() necesita para detectar el latido, no
// solo quitar ruido — especialmente si además la potencia más alta empuja
// la señal hacia zonas de saturación del ADC en contacto cercano. No es
// una certeza absoluta, es la explicación más plausible con lo que vimos.
//
// Si más adelante quiere volver a intentar subir la potencia para mejorar
// el alcance en muñeca, hágalo de a un parámetro por vez (potencia sola,
// sin tocar el promediado a la vez), para poder aislar cuál de los dos
// causa cualquier efecto que vea — cambiar dos variables juntas fue lo que
// no permitió diagnosticar esto con claridad la primera vez.
const byte Config_Max_Potencia_Led = 100;       // valor original (equivalente a Brillo=100 del código base)
const byte Config_Max_Promedio_Muestras = 1;    // valor original (equivalente a Lecturas=1)
const byte Config_Max_Modo_Led = 2;             // Red + IR
const int  Config_Max_Velocidad_Muestreo = 400; // muestras/seg antes de promediar
const int  Config_Max_Ancho_Pulso = 411;        // 411 us = resolución máxima del ADC (18 bits)
const int  Config_Max_Rango_Adc = 8192;         // valor original

// ============================================================================
// 5. MÁQUINA DE ESTADOS PRINCIPAL
// ============================================================================
enum Estado_Sistema {
  Sys_Init,
  Provisional_Calib,
  Stress_Monitor,
  Biofeedback,
  Sleep_Detect,
  Basal_Calib
};
Estado_Sistema Estado_Actual = Sys_Init;

// --- Niveles de estrés (3 niveles en vez de 2) ---
// Declarado aquí, temprano, por la misma razón que Usuario_Estresado: tanto
// Procesar_Comando_Serial como Imprimir_Reporte_Latido lo necesitan antes
// en el archivo de lo que se calcula (sección 14).
enum Nivel_Estres {
  Nivel_Tranquilo,
  Nivel_Intranquilo,
  Nivel_Estresado
};
Nivel_Estres Nivel_Estres_Actual = Nivel_Tranquilo;

const char* Nombre_Nivel_Estres(Nivel_Estres Nivel) {
  switch (Nivel) {
    case Nivel_Tranquilo:   return "TRANQUILO";
    case Nivel_Intranquilo: return "INTRANQUILO";
    case Nivel_Estresado:   return "ESTRESADO";
  }
  return "?";
}

// Declarada aquí (junto al estado de la FSM) en vez de en la sección donde
// se calcula (14, Evaluar_Estres), porque tanto Procesar_Comando_Serial
// (sección 9) como Imprimir_Reporte_Latido (sección 11.b) la necesitan
// antes en el archivo. Ya me pasó una vez saltarme esto — no de nuevo.
// Se mantiene como booleano derivado de Nivel_Estres_Actual == Nivel_Estresado,
// para no tener que tocar el resto de la FSM (Biofeedback solo se dispara
// con el nivel más alto, no con "intranquilo").
bool Usuario_Estresado = false;

// --- Prueba forzada del motor (diagnóstico de hardware) ---
// Ver comando serial TEST_MOTOR más abajo (sección 9).
bool Prueba_Motor_Forzada = false;
unsigned long Prueba_Motor_Fin_Ms = 0;

// ============================================================================
// 6. DATOS BASALES (Preferences, ventana de 7 días)
// ============================================================================
const char* Namespace_Pref = "wearable";
int Contador_Dias_Basal = 0;
float Basal_Bpm_Por_Dia[7] = {0};
float Basal_Hvr_Por_Dia[7] = {0};
float Promedio_Basal_Bpm = 0;
float Promedio_Basal_Hvr = 0;
bool  Datos_Basales_Validos = false;
byte Contador_Envio = 0;
// Basal provisional (solo mientras no hay ninguna noche procesada)
float Basal_Bpm_Provisional = 0;
float Basal_Hvr_Provisional = 0;
bool  Usando_Basal_Provisional = false;
float Anterior_Golpe_Fuerza = 0;
// ============================================================================
// 7. RELOJ SIMPLIFICADO (contador HHMM, sin fecha ni RTC real)
// ============================================================================
// Formato HHMM continuo: 1200, 1201 ... 2359 -> 0000. Se configura la hora
// inicial por Serial ("H:HHMM") y a partir de ahí el propio firmware suma
// un minuto cada 60000 ms de forma no bloqueante.
//
// ADVERTENCIA que le debo señalar: al ser un contador basado en millis()
// y no en un RTC con cristal propio, el reloj SE REINICIA a su valor por
// defecto (1200) cada vez que el ESP32 pierde alimentación o se resetea,
// y además deriva unos segundos por hora respecto a la hora real (el
// oscilador interno de la ESP32-C3 no es de precisión de reloj). Para un
// registro de 7 días esa deriva es pequeña pero no cero. Si en algún punto
// necesita que el basal nocturno sea fiable a largo plazo, esto es lo
// primero que tendría que revisar — se lo dejo anotado para que lo decida
// usted con conocimiento del trade-off, no porque yo lo esté cuestionando
// de fondo.
int Hora_Actual_Hhmm = 1200; // valor por defecto al arrancar
unsigned long Marca_Ultimo_Minuto_Ms = 0;

int Hhmm_Inicio_Noche = 2300; // 23:00
const int Hhmm_Fin_Noche    = 600;  // 06:00

// ============================================================================
// 7. CONFIGURACIÓN BLE
// ============================================================================
//*************************
//Server Callback
//*************************
class Server_Callback : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    Serial.println("Cliente conectado.");
    BLEDevice::stopAdvertising();
  }
  void onDisconnect(BLEServer *pServer) {
    Serial.println("Cliente desconectado.");
    pServer->startAdvertising();
    Serial.println(WiFi.status());
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Apagando BLE");
      BLEDevice::deinit(true);
      BLEapagado=true;
    }
  }
};
//*************************
//Characteristics Callback
//*************************
class WifiStartChar_Callback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) {
    String valor = pChar->getValue();
    Serial.println(valor);
    if (valor == "1") {
      String redes = wifiScan();
      Serial.println(redes);
      pWifiCredChar->setValue(redes);
      pChar->setValue("");
    }
  }
  void onRead(BLECharacteristic *pChar) {
    String valor = pChar->getValue();
    Serial.println(valor);
  }
};
class WifiCredChar_Callback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) {
    String valor = pChar->getValue();
    Serial.println(valor.c_str());
    String uuid = pChar->getUUID().toString();
    uuid.toUpperCase();
    if (uuid == WIFI_CRED_CHAR_UUID) {
      wifi_ssid = valor;
    } else if (uuid == PASS_CRED_CHAR_UUID) {
      wifi_pass = valor;
      Serial.println(wifi_ssid);
      Serial.println(wifi_pass);
      // Antes: wifiConnect() se llamaba aquí mismo, bloqueando el callback
      // de BLE hasta 10+ segundos (WiFi.begin + reintentos + getIP + getTime,
      // todos con delay()). Ahora solo se deja la solicitud marcada; la
      // tarea de red la procesa fuera del hilo de BLE.
      Solicitud_Wifi_Ssid = wifi_ssid;
      Solicitud_Wifi_Pass = wifi_pass;
      Solicitud_Wifi_Pendiente = true;
    }
  }
  void onRead(BLECharacteristic *pChar) {
    String valor = pChar->getValue();
    Serial.print("Leyó: ");
    Serial.println(valor.c_str());
  }
};
class UserTokenChar_Callback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) {
    String valor = pChar->getValue();
    UserToken = valor.c_str();
    int separador = UserToken.indexOf(",");
    User = UserToken.substring(0, separador);
    Token = UserToken.substring(separador + 1);
    Token.trim();
    User.trim();
    Serial.println(User);
    Serial.println(Token);
  }
};
class HoraDormirChar_Callback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) {
    String valor = pChar->getValue();
    Hhmm_Inicio_Noche=valor.toInt();
    Serial.println(Hhmm_Inicio_Noche);
  }
};
//*************************
//Función principal BLE
//*************************
void BLE_Start() {
  bool server_status = BLEDevice::init("ESP32 TEA");
  BLEapagado=false;
  BLEDevice::setMTU(512);
  if (!server_status) {
    Serial.println("Error al establecer el servidor.");
    return;
  }
  Serial.println("BLE Correcto.");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new Server_Callback());

  pWifiStartService = pServer->createService(WIFI_START_SERV_UUID);
  pWifiStartChar = pWifiStartService->createCharacteristic(WIFI_START_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pWifiStartChar->setCallbacks(new WifiStartChar_Callback());
  pWifiStartChar->setValue("");
  pWifiStartService->start();

  pWifiCredService = pServer->createService(WIFI_CRED_SERV_UUID);
  pWifiCredChar = pWifiCredService->createCharacteristic(WIFI_CRED_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pPassCredChar = pWifiCredService->createCharacteristic(PASS_CRED_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pUserTokenChar = pWifiCredService->createCharacteristic(USER_TOKEN_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pHoraDormirChar = pWifiCredService->createCharacteristic(HORA_DORMIR_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pWifiCredChar->setCallbacks(new WifiCredChar_Callback());
  pPassCredChar->setCallbacks(new WifiCredChar_Callback());
  pUserTokenChar->setCallbacks(new UserTokenChar_Callback());
  pHoraDormirChar->setCallbacks(new HoraDormirChar_Callback());
  pWifiCredChar->setValue("");
  pPassCredChar->setValue("");
  pUserTokenChar->setValue("");
  pHoraDormirChar->setValue("");
  pWifiCredService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(WIFI_CRED_SERV_UUID);
  pAdvertising->addServiceUUID(WIFI_START_SERV_UUID);
  pAdvertising->setScanResponse(true);

  BLEDevice::startAdvertising();
}
// ============================================================================
// 7. CONFIGURACIÓN PEDIR HORA
// ============================================================================
String getIP() {
  HTTPClient http_ip;
  http_ip.begin("https://api.ipify.org");
  int httpCode = http_ip.GET();

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String respuesta = http_ip.getString();
      return respuesta;
    }
  } else {
    Serial.printf("HTTPClient Error: %s\n", http_ip.errorToString(httpCode).c_str());
  }
  http_ip.end();
  return "";
}

int getTime(String ip) {
  int intentos = 0;
  while (intentos < 3) {
    HTTPClient http_hora;
    String api_IP = "https://timeapi.io/api/v1/time/current/ip?ipAddress=" + ip;
    http_hora.begin(api_IP);
    int httpCode = http_hora.GET();

    if (httpCode == HTTP_CODE_OK) {
      String respuesta = http_hora.getString();
      StaticJsonDocument<256> doc;
      deserializeJson(doc, respuesta);
      String time_unformat = doc["time"];
      int time_format;
      time_unformat.replace(":", "");
      time_format=time_unformat.substring(0,4).toInt();
      http_hora.end();
      return time_format;
    }
    http_hora.end();
    intentos++;
    Serial.print(".");
    delay(200);
  }
  return 0;
}

void Actualizar_Reloj_Simple() {
  unsigned long Ahora = millis();
  if (Ahora - Marca_Ultimo_Minuto_Ms < 60000UL) return;
  Marca_Ultimo_Minuto_Ms = Ahora;

  int Horas = Hora_Actual_Hhmm / 100;
  int Minutos = Hora_Actual_Hhmm % 100;
  Minutos++;
  if (Minutos >= 60) {
    Minutos = 0;
    Horas++;
    if (Horas >= 24) Horas = 0;
  }
  Hora_Actual_Hhmm = Horas * 100 + Minutos;
}

// ============================================================================
// 7. FUNCIONES COMS
// ============================================================================
void Guardar_Credenciales_WiFi(String ssid,String pass){
  Memoria.begin(Namespace_WiFi,false);
  Memoria.putString("ssid",ssid);
  Memoria.putString("pass",pass);
  Serial.print("ssid: ");
  Serial.println(Memoria.getString("ssid",""));
  Serial.print("Pass: ");
  Serial.println(Memoria.getString("pass",""));
  Memoria.end();
}
WiFi_Credentials Leer_Credenciales_WiFi(){
  WiFi_Credentials credentials;
  Memoria.begin(Namespace_WiFi,false);
  credentials.ssid = Memoria.getString("ssid","");
  credentials.pass = Memoria.getString("pass","");
  Serial.print("ssid: ");
  Serial.println(credentials.ssid);
  Serial.print("Pass: ");
  Serial.println(credentials.pass);
  Memoria.end();
  return credentials;
}
String wifiScan() {
  Serial.println("Buscando Redes...");
  int min_rssi = -80;
  byte redes_totales = 0;
  byte redes = WiFi.scanNetworks();  //Cantidad de redes encontradas
  String wifi_list = "";

  if (redes == 0) {
    Serial.println("No se encontraron redes.");
    // Bugfix: antes esta rama no tenía return en una función que devuelve
    // String -> comportamiento indefinido (la función "caía" sin devolver
    // nada). Ahora devuelve explícitamente una cadena vacía.
    return wifi_list;
  }

  for (int i = 0; i < redes; i++) {  //Recorre las redes
    if (WiFi.RSSI(i) > min_rssi) {
      redes_totales += 1;
    }
  }
  Serial.print(redes_totales);
  Serial.println(" Redes encontradas");

  // Bugfix: el bucle original recorría los primeros `redes_totales` ÍNDICES
  // del escaneo sin volver a aplicar el filtro de RSSI, así que podía
  // incluir redes débiles que ya se habían descartado del conteo. Y el
  // separador "|" comparaba contra `redes` (el total sin filtrar) en vez
  // de `redes_totales`, dejando un separador de más o de menos al final.
  // El FORMATO de salida (SSID,RSSI separados por "|") es idéntico al de
  // antes — solo cambia qué redes entran y dónde va cada separador.
  int Redes_Agregadas = 0;
  for (int i = 0; i < redes; i++) {
    if (WiFi.RSSI(i) <= min_rssi) continue;
    if (Redes_Agregadas > 0) wifi_list += "|";
    wifi_list += WiFi.SSID(i) + "," + WiFi.RSSI(i);
    Redes_Agregadas++;
  }
  return wifi_list;
}
void wifiDisconnect() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Desconectando de la red: ");
    Serial.println(WiFi.SSID());
    WiFi.disconnect();
    delay(200);
  }
}
void wifiConnect( String ssid, String pass) {
  int intentos = 0;
  wifiDisconnect();

  Serial.print("Conectando a la red: ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    Serial.println(".");
    delay(500);
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conectado");
    Serial.println(WiFi.SSID());     //Imprime la red
    Serial.println(WiFi.localIP());  //Imprime la IP local de la ESP
    publicIP = getIP();
    Serial.println(publicIP);

    Hora_Actual_Hhmm = getTime(publicIP);
    Serial.println(Hora_Actual_Hhmm);

    Guardar_Credenciales_WiFi(ssid,pass);
  } else {
    Serial.println("Error al conectar.");
    Serial.println("La conexión tardo demasiado, vefifique la contraseña e intentelo nuevmente");
    WiFi.disconnect();
  }
}
void infoPOST(float BPMprom, float HVRprom, bool superficie, String alertas, float Fuerza_golpe) {
  HTTPClient http_info;

  Serial.println("Conectando al servidor...");
  http_info.begin(authIP);  //Conectar al servidor

  Serial.println("Haciendo POST");

  http_info.addHeader("USER", User);
  http_info.addHeader("TOKEN", Token);
  String info = String(BPMprom) + ";" + String(HVRprom) + ";" + String(superficie) + ";" + String(alertas)+ ";" + String(Fuerza_golpe);
  Serial.println(info);
  int httpCode = http_info.POST(info);

  if (httpCode > 0) {
    Serial.print("httpCode= ");
    Serial.println(httpCode);
    String respuesta = http_info.getString();
    Serial.println("Respuesta: ");
    Serial.println(respuesta);

  } else {
    Serial.printf("HTTPClient Error: %s\n", http_info.errorToString(httpCode).c_str());
  }
  http_info.end();
}

// ============================================================================
// 7.b TAREA DE RED EN SEGUNDO PLANO (optimización de conectividad)
// ============================================================================
// Consume la cola de telemetría (Cola_Post) y procesa las solicitudes de
// conexión WiFi pendientes, todo FUERA del loop() principal y FUERA del
// hilo de callbacks de BLE. infoPOST() y wifiConnect() no cambiaron una
// sola línea de cómo arman el POST/GET — solo cambió QUIÉN los llama y
// CUÁNDO, para que nunca bloqueen la lectura de sensores ni el stack BLE.
void Tarea_Red(void *Parametro) {
  Datos_Post Dato;
  for (;;) {
    if (Solicitud_Wifi_Pendiente) {
      wifiConnect(Solicitud_Wifi_Ssid.c_str(), Solicitud_Wifi_Pass.c_str());
      Solicitud_Wifi_Pendiente = false;
    }
    // Espera hasta 200ms por un dato de telemetría; si no llega nada, vuelve
    // a revisar si hay una solicitud de WiFi pendiente. No consume CPU en
    // un bucle ocupado (xQueueReceive bloquea la tarea mientras espera).
    if (xQueueReceive(Cola_Post, &Dato, pdMS_TO_TICKS(200)) == pdTRUE) {
      infoPOST(Dato.Bpm, Dato.Hvr, Dato.Superficie, String(Dato.Alertas), Dato.Fuerza_Golpe);
    }
  }
}
// ============================================================================
// 8. ESTRUCTURA COMPARTIDA CON LA TAREA DEL MPU6050 (FreeRTOS)
// ============================================================================
struct Datos_Movimiento {
  float Magnitud_G;          // magnitud del vector de aceleración, en G
  float Delta_Magnitud_G;    // variación respecto a la lectura anterior
  float Fuerza_Pico_Impacto; // pico de G del último golpe detectado
  bool  Impacto_Detectado;   // bandera que el loop principal debe leer y limpiar
  bool  Movimiento_Alto;     // true si hay artefacto de movimiento activo
  unsigned long Marca_Tiempo_Ms;
};
volatile Datos_Movimiento Ultimo_Dato_Mpu = {1.0, 0, 0, false, false, 0};
SemaphoreHandle_t Mutex_Mpu;
// Mutex separado del anterior: protege el ACCESO FÍSICO al bus I2C en sí
// (no los datos). Sin esto, el loop() principal (leyendo el MAX30105) y
// esta tarea (leyendo el MPU6050) pueden hablarle al mismo bus I2C al
// mismo tiempo, lo que produce cuelgues silenciosos del sistema.
SemaphoreHandle_t Mutex_I2c;
TaskHandle_t Handle_Tarea_Mpu = NULL;

// --- Supresión de impacto durante la vibración del motor (bug reportado) ---
// El motor vibrador está montado en la misma estructura rígida que el
// MPU6050: cuando vibra, produce su PROPIA aceleración, que el acelerómetro
// no puede distinguir de un golpe externo real. Mientras el motor esté
// activo, se ignoran los picos de Delta_G para no confundir "me estoy
// vibrando a mí mismo" con "me golpearon". Declarada aquí (temprano) porque
// Tarea_Leer_Mpu, más abajo, la necesita antes en el archivo — mismo motivo
// que las otras variables que tuvieron que moverse por esto.
volatile bool Ignorar_Impactos_Por_Motor = false;

// AJUSTAR: umbral de delta-G para golpes contra superficies rígidas (mesa,
// pared) — un salto brusco entre dos muestras de 20ms.
const float Umbral_Impacto_G = 2.2;

// AJUSTAR: umbral de MAGNITUD ABSOLUTA (no delta) para autogolpes. Un golpe
// contra el propio cuerpo se amortigua con el tejido: el pico es más bajo
// y se reparte en más tiempo que contra una superficie rígida, así que el
// salto instantáneo (Delta_G) puede no alcanzar Umbral_Impacto_G aunque el
// golpe sea real. Este umbral, más bajo, dispara con solo alcanzar esta
// magnitud, sin exigir un salto brusco de una muestra a la siguiente.
//
// TRADE-OFF que le señalo: al ser más bajo, este umbral también puede
// dispararse con movimientos normales de brazo (gesticular, ejercicio) que
// no son ni golpes a superficies ni autogolpes. Pruébelo comparando
// autogolpes reales contra movimientos cotidianos y ajuste el valor según
// lo que separe mejor unos de otros en su caso — 1.8 es un punto de
// partida, no un valor validado.
const float Umbral_Impacto_Absoluto_G = 1.8;

// Período de "enfriamiento" tras registrar un impacto: un solo golpe físico
// abarca varias muestras de 20ms mientras la aceleración sube y baja: sin
// esto, un mismo golpe se contaría como varios impactos separados.
const unsigned long Refractario_Impacto_Ms = 150;

// AJUSTAR: magnitud por encima de la cual se considera artefacto de movimiento
// para descartar muestras de BPM/HRV (reposo normal ronda 1.0 G)
const float Umbral_Artefacto_Movimiento_G = 1.35;

// ============================================================================
// 9. RECUPERACIÓN DE BUS I2C
// ============================================================================
volatile int Contador_Fallos_I2c = 0;
const int Umbral_Fallos_I2c = 5;

void Recuperar_Bus_I2c() {
  Serial.println("[I2C] Bus posiblemente bloqueado. Ejecutando recuperación...");

  // Espera indefinida (bloqueante) por el mutex: esta es una operación de
  // emergencia poco frecuente, y NO puede ejecutarse mientras la tarea del
  // MPU esté a mitad de una transacción I2C, o se corrompe el bus todavía más.
  xSemaphoreTake(Mutex_I2c, portMAX_DELAY);

  Wire.end();

  // Nota: aquí SÍ se usan delayMicroseconds() (no delay()) porque el
  // protocolo de recuperación de I2C exige pulsos de reloj de duración
  // controlada en microsegundos; no hay forma no-bloqueante de generarlos
  // y la rutina completa toma bloqueante durante corriendo pero menos de 1 ms.
  pinMode(Pin_I2c_Scl, OUTPUT);
  pinMode(Pin_I2c_Sda, INPUT_PULLUP);

  // Hasta 9 pulsos de reloj para forzar a cualquier esclavo atascado
  // a liberar la línea SDA
  for (int i = 0; i < 9; i++) {
    digitalWrite(Pin_I2c_Scl, HIGH);
    delayMicroseconds(5);
    digitalWrite(Pin_I2c_Scl, LOW);
    delayMicroseconds(5);
  }

  // Generar condición STOP manual (SDA sube mientras SCL está en alto)
  pinMode(Pin_I2c_Sda, OUTPUT);
  digitalWrite(Pin_I2c_Sda, LOW);
  delayMicroseconds(5);
  digitalWrite(Pin_I2c_Scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(Pin_I2c_Sda, HIGH);
  delayMicroseconds(5);

  Wire.begin(Pin_I2c_Sda, Pin_I2c_Scl);
  Wire.setClock(100000);
  Wire.setTimeOut(50); // ms; evita que un Wire.requestFrom cuelgue el sistema

  // El MPU6050 necesita un instante para estabilizarse después de que el
  // bus se reinicia, antes de aceptar su secuencia de inicialización
  // (reset interno + configuración de registros). Un escaneo crudo (SCAN)
  // no tiene este requisito porque es solo una transacción de bus, no el
  // protocolo completo de arranque del sensor — por eso el bus respondía
  // bien en el escaneo pero Sensor_Mpu.begin() fallaba aquí mismo.
  delay(20);

  // --- Reconfigurar ambos sensores, no solo el bus físico ---
  // Si la causa del bloqueo fue una caída de voltaje (no solo un glitch de
  // reloj I2C), el chip mismo pudo perder su configuración interna.
  // Reiniciar solo los pines SDA/SCL no alcanza en ese caso — el bus "se
  // recupera" pero el sensor sigue respondiendo con su config de fábrica.
  if (Sensor_Cardiaco.begin(Wire)) {
    Sensor_Cardiaco.setup(Config_Max_Potencia_Led, Config_Max_Promedio_Muestras, Config_Max_Modo_Led,
                           Config_Max_Velocidad_Muestreo, Config_Max_Ancho_Pulso, Config_Max_Rango_Adc);
  } else {
    Serial.println(F("[I2C] MAX30105 no respondio tras la recuperacion."));
  }

  if (Sensor_Mpu.begin(0x68, &Wire, 0)) {
    Sensor_Mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    Sensor_Mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  } else {
    Serial.println(F("[I2C] MPU6050 no respondio tras la recuperacion."));
  }

  Contador_Fallos_I2c = 0;
  Serial.println("[I2C] Bus reiniciado.");
  xSemaphoreGive(Mutex_I2c);
}

// ============================================================================
// 10. TAREA FREERTOS: LECTURA CONTINUA DEL MPU6050 (independiente del MAX)
// ============================================================================
// Justificación: la ESP32-C3 es single-core, así que "paralelo" aquí significa
// una tarea de FreeRTOS de alta prioridad con su propio período fijo, que no
// depende de cuánto tarde el procesamiento del MAX30105 en el loop principal.
// Esto resuelve el problema de que en su código original el movimiento solo
// se leía cuando llegaba un latido válido.
void Tarea_Leer_Mpu(void *Parametro) {
  sensors_event_t Evento_Accel, Evento_Giro, Evento_Temp;
  float Magnitud_Anterior_G = 1.0;
  // Persiste entre iteraciones del for(;;) porque está declarada fuera de
  // él (igual que Magnitud_Anterior_G) — no necesita ser 'static' porque
  // esta función nunca retorna, así que su stack vive todo el tiempo.
  unsigned long Marca_Ultimo_Impacto_Ms = 0;
  const TickType_t Periodo_Tarea = pdMS_TO_TICKS(20); // 50 Hz
  TickType_t Ultimo_Despertar = xTaskGetTickCount();

  for (;;) {
    bool Lectura_Ok = false;
    bool Se_Tomo_El_Mutex = (xSemaphoreTake(Mutex_I2c, pdMS_TO_TICKS(10)) == pdTRUE);
    if (Se_Tomo_El_Mutex) {
      Lectura_Ok = Sensor_Mpu.getEvent(&Evento_Accel, &Evento_Giro, &Evento_Temp);
      xSemaphoreGive(Mutex_I2c);
    }
    // Si no se pudo tomar el mutex a tiempo, es CONTENCIÓN NORMAL (el loop
    // principal estaba usando el bus en ese instante), no una falla de I2C.
    // Se descarta el ciclo sin penalizar el contador de fallos.

    if (Lectura_Ok) {
      float Ax_G = Evento_Accel.acceleration.x / 9.80665;
      float Ay_G = Evento_Accel.acceleration.y / 9.80665;
      float Az_G = Evento_Accel.acceleration.z / 9.80665;
      float Magnitud_G = sqrt(Ax_G * Ax_G + Ay_G * Ay_G + Az_G * Az_G);
      float Delta_G = fabs(Magnitud_G - Magnitud_Anterior_G);

      // --- Detección de impacto: DOS vías, no solo una ---
      // Vía 1 (golpe rígido, mesa/pared): salto brusco de una muestra a la
      // siguiente.
      // Vía 2 (autogolpe, amortiguado por tejido): magnitud absoluta alta,
      // sin exigir que el salto sea instantáneo.
      // El período refractario evita que un mismo golpe físico (que dura
      // varias muestras de 20ms mientras sube y baja la aceleración) se
      // cuente como impactos separados.
      bool Golpe_Rigido = (Delta_G > Umbral_Impacto_G);
      bool Golpe_Amortiguado = (Magnitud_G > Umbral_Impacto_Absoluto_G);
      unsigned long Ahora_Ms = millis();
      bool Fuera_De_Refractario = (Ahora_Ms - Marca_Ultimo_Impacto_Ms) >= Refractario_Impacto_Ms;
      bool Hay_Golpe_Nuevo = (Golpe_Rigido || Golpe_Amortiguado) &&
                              !Ignorar_Impactos_Por_Motor && Fuera_De_Refractario;

      if (Hay_Golpe_Nuevo) {
        // Se actualiza aquí, fuera del mutex, para que el refractario
        // funcione aunque el mutex esté ocupado ese ciclo puntual.
        Marca_Ultimo_Impacto_Ms = Ahora_Ms;
      }

      if (xSemaphoreTake(Mutex_Mpu, pdMS_TO_TICKS(5)) == pdTRUE) {
        Ultimo_Dato_Mpu.Magnitud_G = Magnitud_G;
        Ultimo_Dato_Mpu.Delta_Magnitud_G = Delta_G;
        Ultimo_Dato_Mpu.Movimiento_Alto = (Magnitud_G > Umbral_Artefacto_Movimiento_G);
        Ultimo_Dato_Mpu.Marca_Tiempo_Ms = Ahora_Ms;

        if (Hay_Golpe_Nuevo) {
          Ultimo_Dato_Mpu.Impacto_Detectado = true;
          Ultimo_Dato_Mpu.Fuerza_Pico_Impacto = Magnitud_G;
        }
        xSemaphoreGive(Mutex_Mpu);
      }
      Magnitud_Anterior_G = Magnitud_G;
      Contador_Fallos_I2c = 0;
    } else if (Se_Tomo_El_Mutex) {
      // Se tuvo el bus para uno mismo y aun así getEvent() falló: esto sí
      // es un fallo real de I2C, no contención con el loop principal.
      Contador_Fallos_I2c++;
    }

    vTaskDelayUntil(&Ultimo_Despertar, Periodo_Tarea);
  }
}

// ============================================================================
// 11. GESTIÓN DE HORA (comando serial simple, sin fecha)
// ============================================================================
// Envíe por Monitor Serial: H:HHMM
// Ejemplo: H:1430  (fija la hora en 14:30, y desde ahí el firmware cuenta solo)

// --- Escáner de bus I2C (diagnóstico) ---
// Comando serial: SCAN
// Recorre las 127 direcciones posibles y reporta cuáles responden. Útil
// para confirmar si el MPU6050 está en 0x68 (default) o 0x69 (si el pin
// AD0 está en alto), o si no aparece en absoluto (problema de cableado o
// alimentación, no de dirección).
void Escanear_Bus_I2c() {
  Serial.println(F("[SCAN] Escaneando bus I2C..."));
  if (xSemaphoreTake(Mutex_I2c, pdMS_TO_TICKS(200)) != pdTRUE) {
    Serial.println(F("[SCAN] No se pudo tomar el bus (ocupado). Intente de nuevo."));
    return;
  }

  int Dispositivos_Encontrados = 0;
  for (byte Direccion = 1; Direccion < 127; Direccion++) {
    Wire.beginTransmission(Direccion);
    byte Error = Wire.endTransmission();
    if (Error == 0) {
      Serial.print(F("[SCAN] Dispositivo encontrado en 0x"));
      if (Direccion < 16) Serial.print('0');
      Serial.println(Direccion, HEX);
      Dispositivos_Encontrados++;
    }
  }

  xSemaphoreGive(Mutex_I2c);

  if (Dispositivos_Encontrados == 0) {
    Serial.println(F("[SCAN] No respondio ningun dispositivo. Revise cableado/alimentacion."));
  } else {
    Serial.print(F("[SCAN] Total encontrados: "));
    Serial.println(Dispositivos_Encontrados);
    Serial.println(F("[SCAN] Referencia: MAX30105/30102 suele estar en 0x57."));
    Serial.println(F("[SCAN] Referencia: MPU6050 suele estar en 0x68 (AD0=GND) o 0x69 (AD0=VCC)."));
  }
}

void Procesar_Comando_Serial() {
  if (!Serial.available()) return;

  String Comando = Serial.readStringUntil('\n');
  Comando.trim();

  if (Comando.startsWith("H:")) {
    int Hhmm_Nuevo = Comando.substring(2).toInt();
    int Horas = Hhmm_Nuevo / 100;
    int Minutos = Hhmm_Nuevo % 100;
    if (Horas >= 0 && Horas <= 23 && Minutos >= 0 && Minutos <= 59) {
      Hora_Actual_Hhmm = Hhmm_Nuevo;
      Marca_Ultimo_Minuto_Ms = millis();
      Serial.print(F("[RELOJ] Hora fijada en: "));
      Serial.println(Hora_Actual_Hhmm);
    } else {
      Serial.println(F("[RELOJ] Formato invalido. Use H:HHMM, ej. H:1430"));
    }
  } else if (Comando == "SCAN") {
    Escanear_Bus_I2c();
  } else if (Comando == "TEST_MOTOR") {
    Serial.println(F("[TEST] Forzando Biofeedback por 15s para probar el motor (Pin 3)..."));
    Prueba_Motor_Forzada = true;
    Prueba_Motor_Fin_Ms = millis() + 15000;
    Usuario_Estresado = true;
    Nivel_Estres_Actual = Nivel_Estresado;
    Estado_Actual = Biofeedback;
    Iniciar_Biofeedback();
  } else if (Comando == "r") {
    Serial.println(F("[CMD] Reinicio de contador de latidos solicitado."));
  }
}

// Comparación numérica válida: los minutos nunca llegan a 60, así que el
// orden de los enteros HHMM coincide con el orden cronológico real dentro
// del mismo día.
bool Es_Modo_Nocturno() {
  return (Hora_Actual_Hhmm >= Hhmm_Inicio_Noche || Hora_Actual_Hhmm < Hhmm_Fin_Noche);
}

// ============================================================================
// 12. CARGA Y VISUALIZACIÓN OBLIGATORIA DE DATOS BASALES AL ARRANCAR
// ============================================================================
void Cargar_Y_Mostrar_Datos_Basales() {
  Memoria.begin(Namespace_Pref, true); // solo lectura
  Contador_Dias_Basal = Memoria.getInt("dias", 0);
  Memoria.getBytes("bpm_arr", Basal_Bpm_Por_Dia, sizeof(Basal_Bpm_Por_Dia));
  Memoria.getBytes("hvr_arr", Basal_Hvr_Por_Dia, sizeof(Basal_Hvr_Por_Dia));
  Memoria.end();

  Serial.println(F("=========================================================="));
  Serial.println(F(" DATOS BASALES ALMACENADOS EN FLASH (Preferences)"));
  Serial.println(F("=========================================================="));
  Serial.print(F("Dias de calibracion nocturna completados: "));
  Serial.println(Contador_Dias_Basal);

  if (Contador_Dias_Basal <= 0) {
    Serial.println(F("-> No hay datos basales previos. Se requiere calibracion."));
    Datos_Basales_Validos = false;
    Serial.println(F("=========================================================="));
    return;
  }

  // Bugfix respecto al original: bucle correcto (sin leer índice 7 en un
  // arreglo de tamaño 7) y sin dividir entre "Contador - 1" cuando puede
  // ser 0 o negativo.
  float Suma_Bpm = 0, Suma_Hvr = 0;
  int Dias_Validos = min(Contador_Dias_Basal, 7);
  for (int i = 0; i < Dias_Validos; i++) {
    Serial.print(F("  Dia ")); Serial.print(i + 1);
    Serial.print(F(" -> BPM basal: ")); Serial.print(Basal_Bpm_Por_Dia[i]);
    Serial.print(F(" | HRV basal (RMSSD ms): ")); Serial.println(Basal_Hvr_Por_Dia[i]);
    Suma_Bpm += Basal_Bpm_Por_Dia[i];
    Suma_Hvr += Basal_Hvr_Por_Dia[i];
  }
  Promedio_Basal_Bpm = Suma_Bpm / Dias_Validos;
  Promedio_Basal_Hvr = Suma_Hvr / Dias_Validos;
  Datos_Basales_Validos = true;

  Serial.print(F("PROMEDIO BASAL BPM: ")); Serial.println(Promedio_Basal_Bpm);
  Serial.print(F("PROMEDIO BASAL HRV: ")); Serial.println(Promedio_Basal_Hvr);
  Serial.println(F("=========================================================="));
}

void Guardar_Dato_Basal_Noche(float Bpm_Noche, float Hrv_Noche) {
  if (Contador_Dias_Basal >= 7) {
    // Ventana móvil de 7 días: se descarta el dato más antiguo.
    for (int i = 0; i < 6; i++) {
      Basal_Bpm_Por_Dia[i] = Basal_Bpm_Por_Dia[i + 1];
      Basal_Hvr_Por_Dia[i] = Basal_Hvr_Por_Dia[i + 1];
    }
    Basal_Bpm_Por_Dia[6] = Bpm_Noche;
    Basal_Hvr_Por_Dia[6] = Hrv_Noche;
  } else {
    Basal_Bpm_Por_Dia[Contador_Dias_Basal] = Bpm_Noche;
    Basal_Hvr_Por_Dia[Contador_Dias_Basal] = Hrv_Noche;
    Contador_Dias_Basal++;
  }

  Memoria.begin(Namespace_Pref, false);
  Memoria.putInt("dias", Contador_Dias_Basal);
  Memoria.putBytes("bpm_arr", Basal_Bpm_Por_Dia, sizeof(Basal_Bpm_Por_Dia));
  Memoria.putBytes("hvr_arr", Basal_Hvr_Por_Dia, sizeof(Basal_Hvr_Por_Dia));
  Memoria.end();

  float Suma_Bpm = 0, Suma_Hvr = 0;
  for (int i = 0; i < Contador_Dias_Basal; i++) {
    Suma_Bpm += Basal_Bpm_Por_Dia[i];
    Suma_Hvr += Basal_Hvr_Por_Dia[i];
  }
  Promedio_Basal_Bpm = Suma_Bpm / Contador_Dias_Basal;
  Promedio_Basal_Hvr = Suma_Hvr / Contador_Dias_Basal;
  Datos_Basales_Validos = true;
  Usando_Basal_Provisional = false;

  Serial.println(F("[BASAL] Noche registrada y promedios actualizados."));
  Serial.print(F("  BPM basal noche: ")); Serial.println(Bpm_Noche);
  Serial.print(F("  HRV basal noche: ")); Serial.println(Hrv_Noche);
}

// ============================================================================
// 12. LECTURA DE PULSO / HRV (RMSSD real, no la aproximación del original)
// ============================================================================
// AJUSTAR: se redujo de 30 a 15 latidos. Con 30, un puñado de intervalos
// contaminados al ponerse el dispositivo seguían pesando en el promedio
// durante los siguientes ~30 latidos (RMSSD "pegado" y de reacción lenta,
// como usted observó). Con 15 reacciona más rápido a cambios reales, a
// costa de un RMSSD algo más ruidoso latido a latido. Es un trade-off, no
// una solución perfecta; si lo ve demasiado nervioso, súbalo de nuevo.
const int Tamano_Buffer_Rr = 15;
long Buffer_Intervalos_Rr[Tamano_Buffer_Rr];
int Indice_Buffer_Rr = 0;
int Cantidad_Rr_Validos = 0;

unsigned long Marca_Ultimo_Latido_Ms = 0;
float Bpm_Actual = 0;
float Hrv_Rmssd_Actual = 0;
unsigned long Numero_Latido = 0;

// --- Filtro fisiológico de outliers, independiente del acelerómetro ---
// El filtro de movimiento (MPU) no siempre alcanza a marcar "movimiento
// alto" a tiempo: un gesto de muñeca puede durar menos que el período de
// muestreo del MPU (20 ms). Este segundo filtro rechaza un intervalo R-R
// que se aparte demasiado del promedio móvil reciente, sin importar lo
// que haya dicho el acelerómetro en ese instante.
float Rr_Promedio_Movil = 0;
bool Rr_Promedio_Inicializado = false;
const float Umbral_Variacion_Rr = 0.25; // AJUSTAR: 25% de salto máximo aceptado
const float Alpha_Ema_Rr = 0.2;         // qué tan rápido se adapta el promedio móvil

// Calcula RMSSD real: raíz de la media de las diferencias sucesivas al cuadrado
void Actualizar_Rmssd() {
  if (Cantidad_Rr_Validos < 2) return;
  double Suma_Cuadrados = 0;
  int Pares = 0;
  for (int i = 1; i < Cantidad_Rr_Validos; i++) {
    int Idx_Actual = (Indice_Buffer_Rr - Cantidad_Rr_Validos + i + Tamano_Buffer_Rr) % Tamano_Buffer_Rr;
    int Idx_Previo  = (Idx_Actual - 1 + Tamano_Buffer_Rr) % Tamano_Buffer_Rr;
    double Diferencia = Buffer_Intervalos_Rr[Idx_Actual] - Buffer_Intervalos_Rr[Idx_Previo];
    Suma_Cuadrados += Diferencia * Diferencia;
    Pares++;
  }
  if (Pares > 0) Hrv_Rmssd_Actual = sqrt(Suma_Cuadrados / Pares);
}

// Devuelve true si se registró un latido válido en esta llamada
bool Procesar_Lectura_Cardiaca(bool Hay_Movimiento_Alto) {
  long Valor_Ir = Sensor_Cardiaco.getIR();

  if (Valor_Ir < 50000) {
    digitalWrite(Pin_Led_Rojo, HIGH);
    superficie = 0;
    //infoPOST(-1,-1,0,"00",0.0);//Revisar pq no agarra Dato.Superficie
    return false; // sin contacto con la piel
  }
  if (!checkForBeat(Valor_Ir)) return false;

  unsigned long Tiempo_Actual = millis();
  long Intervalo_Rr = Tiempo_Actual - Marca_Ultimo_Latido_Ms;
  Marca_Ultimo_Latido_Ms = Tiempo_Actual;

  // Rango fisiológico válido de intervalo R-R (46-150 BPM aprox.)
  if (Intervalo_Rr < 400 || Intervalo_Rr > 1300) return false;

  // --- Filtro de artefactos de movimiento (requisito #3), vía acelerómetro ---
  if (Hay_Movimiento_Alto) {
    Sensor_Cardiaco.clearFIFO();
    return false;
  }

  // --- Filtro fisiológico de outliers (segunda línea de defensa) ---
  // Se aplica ANTES de aceptar el latido, para que un salto brusco no
  // contamine ni el BPM mostrado ni el buffer de RMSSD, aunque el MPU no
  // haya detectado movimiento en ese instante.
  if (Rr_Promedio_Inicializado) {
    float Variacion = fabs((float)Intervalo_Rr - Rr_Promedio_Movil) / Rr_Promedio_Movil;
    if (Variacion > Umbral_Variacion_Rr) {
      return false; // se descarta como artefacto fisiológicamente implausible
    }
  }

  Bpm_Actual = 60000.0 / Intervalo_Rr;
  Numero_Latido++;

  // Actualiza el promedio móvil (EMA) SOLO con latidos ya aceptados
  if (!Rr_Promedio_Inicializado) {
    Rr_Promedio_Movil = Intervalo_Rr;
    Rr_Promedio_Inicializado = true;
  } else {
    Rr_Promedio_Movil = Rr_Promedio_Movil * (1.0 - Alpha_Ema_Rr) + Intervalo_Rr * Alpha_Ema_Rr;
  }

  Buffer_Intervalos_Rr[Indice_Buffer_Rr] = Intervalo_Rr;
  Indice_Buffer_Rr = (Indice_Buffer_Rr + 1) % Tamano_Buffer_Rr;
  if (Cantidad_Rr_Validos < Tamano_Buffer_Rr) Cantidad_Rr_Validos++;
  Actualizar_Rmssd();

  digitalWrite(Pin_Led_Rojo, LOW);
  superficie = 1;
  return true;
}

// Usuario_Estresado ya está declarada más arriba, junto a Estado_Actual
// (sección 3), porque Procesar_Comando_Serial también la necesita antes.

// ============================================================================
// 11.b TELEMETRÍA EN TIEMPO REAL POR LATIDO (requisito estricto)
// ============================================================================
// Se llama una única vez por cada latido válido detectado, inmediatamente
// después de calcular BPM/HRV y evaluar el nivel de estrés de ese instante.
// Nota que le señalo: durante la noche este reporte también se imprime,
// pero el "nivel de estrés" que muestra está calculado contra el basal
// DIURNO (Promedio_Basal_Bpm/Hrv), porque así se lo pidió: "cada vez que
// se detecte una pulsación válida". Si de noche ese campo no le sirve o le
// ensucia el log de sueño, dígamelo y lo condiciono a Es_Modo_Nocturno().
void Imprimir_Reporte_Latido(bool Es_Noche) {
Datos_Movimiento Actual_Envio_Golpe;
  Serial.print(F("[LATIDO #"));
  Serial.print(Numero_Latido);
  Serial.print(F("] BPM: "));
  Serial.print(Bpm_Actual, 1);
  Serial.print(F(" | HRV(RMSSD): "));
  Serial.print(Hrv_Rmssd_Actual, 1);
  Serial.print(F(" ms | Estado: "));
  Serial.print(Nombre_Nivel_Estres(Nivel_Estres_Actual));
  Serial.print(F(" | Modo: "));
  Serial.print(Es_Noche ? F("NOCHE") : F("DIA"));
  Serial.print(F(" | Hora: "));
  Serial.println(Hora_Actual_Hhmm);
  Contador_Envio++;
  if(Contador_Envio >= 5) {
    byte Numero_Estres = 0;
    switch (Nivel_Estres_Actual){
        case Nivel_Tranquilo:
            Numero_Estres = 0;
            break;
        case Nivel_Intranquilo:
            Numero_Estres = 1;
            break;
        case Nivel_Estresado:
            Numero_Estres = 2;
            break;
    }
    String AlertasPOST="";
    Actual_Envio_Golpe.Fuerza_Pico_Impacto = Ultimo_Dato_Mpu.Fuerza_Pico_Impacto;
    AlertasPOST+= Numero_Estres;
    if ( Actual_Envio_Golpe.Fuerza_Pico_Impacto != Anterior_Golpe_Fuerza){
        Anterior_Golpe_Fuerza = Actual_Envio_Golpe.Fuerza_Pico_Impacto = Ultimo_Dato_Mpu.Fuerza_Pico_Impacto;
        AlertasPOST+="1";
    }
    else{
        AlertasPOST+="0";
    }
    // Antes: infoPOST(...) se llamaba aquí mismo, bloqueando el loop
    // principal (sensores, motor, LEDs) durante todo el round-trip HTTP.
    // Ahora se encola el dato y la Tarea_Red lo envía aparte, con el mismo
    // contenido y el mismo formato de siempre.
    if (WiFi.status() != WL_CONNECTED) {
      // Sin WiFi, encolar solo gastaría un espacio de la cola (capacidad 5)
      // en un envío que va a fallar seguro. Se descarta aquí, más barato.
      Serial.println(F("[POST] Sin WiFi conectado; se omite este envio."));
    } else {
      Datos_Post Dato_A_Enviar;
      Dato_A_Enviar.Bpm = Bpm_Actual;
      Dato_A_Enviar.Hvr = Hrv_Rmssd_Actual;
      Dato_A_Enviar.Superficie = superficie;
      AlertasPOST.toCharArray(Dato_A_Enviar.Alertas, sizeof(Dato_A_Enviar.Alertas));
      Dato_A_Enviar.Fuerza_Golpe = Actual_Envio_Golpe.Fuerza_Pico_Impacto;
      if (Cola_Post == NULL || xQueueSend(Cola_Post, &Dato_A_Enviar, 0) != pdTRUE) {
        // No bloqueante a propósito: si la cola está llena (Tarea_Red va
        // atrasada procesando envíos anteriores), se descarta este envío
        // puntual en vez de congelar el loop esperando espacio.
        Serial.println(F("[POST] Cola llena; se descarta este envio (posible retraso de red)."));
      }
    }
    Contador_Envio = 0;
  }
}

// ============================================================================
// 12. LEDs NO BLOQUEANTES
// ============================================================================
unsigned long Marca_Ultimo_Parpadeo_Led = 0;
bool Estado_Led_Verde_On = false;

// --- Parpadeo del verde durante la toma de datos primitivos (Provisional_Calib) ---
// Variables propias, separadas de las de arriba, para no interferir con el
// parpadeo del rojo que usa Actualizar_Led_Estado() en los otros estados.
unsigned long Marca_Ultimo_Parpadeo_Provisional = 0;
bool Estado_Led_Verde_Provisional = false;
const unsigned long Intervalo_Parpadeo_Provisional = 400; // ms

void Actualizar_Led_Provisional() {
  unsigned long Ahora = millis();
  if (Ahora - Marca_Ultimo_Parpadeo_Provisional >= Intervalo_Parpadeo_Provisional) {
    Marca_Ultimo_Parpadeo_Provisional = Ahora;
    Estado_Led_Verde_Provisional = !Estado_Led_Verde_Provisional;
    digitalWrite(Pin_Led_Verde, Estado_Led_Verde_Provisional ? HIGH : LOW);
  }
}

// Solo hay 2 LEDs físicos (verde y rojo), así que los 3 niveles se
// distinguen por el PATRÓN del rojo, no por un tercer color que no existe:
//   Tranquilo    -> verde fijo, rojo apagado
//   Intranquilo  -> rojo parpadeando LENTO (800 ms), verde apagado
//   Estresado    -> rojo parpadeando RÁPIDO (300 ms), verde apagado
void Actualizar_Led_Estado(bool Hay_Contacto, Nivel_Estres Nivel) {
  unsigned long Ahora = millis();

  if (!Hay_Contacto) {
    digitalWrite(Pin_Led_Verde, LOW);
    digitalWrite(Pin_Led_Rojo, LOW);
    return;
  }

  if (Nivel == Nivel_Tranquilo) {
    digitalWrite(Pin_Led_Rojo, LOW);
    digitalWrite(Pin_Led_Verde, HIGH);
    return;
  }

  unsigned long Intervalo_Parpadeo = (Nivel == Nivel_Estresado) ? 300 : 800;
  if (Ahora - Marca_Ultimo_Parpadeo_Led >= Intervalo_Parpadeo) {
    Marca_Ultimo_Parpadeo_Led = Ahora;
    Estado_Led_Verde_On = !Estado_Led_Verde_On; // reutilizado como flip-flop del rojo
    digitalWrite(Pin_Led_Rojo, Estado_Led_Verde_On ? HIGH : LOW);
    digitalWrite(Pin_Led_Verde, LOW);
  }
}

// ============================================================================
// 13. BIOFEEDBACK: VIBRACIÓN GUIADA DE RESPIRACIÓN (Pin 3, no bloqueante)
// ============================================================================
// Arranca imitando el ritmo cardíaco acelerado real del usuario, y cada
// ciclo alarga el período gradualmente hasta alcanzar un ritmo de
// respiración objetivo (por defecto 10 s/ciclo = 6 respiraciones/min,
// un valor clásico de respiración guiada para bajar el ritmo cardíaco).
bool Motor_Activo = false;
bool Motor_Pulso_En_Alto = false;
unsigned long Motor_Marca_Cambio_Ms = 0;
unsigned long Motor_Periodo_Actual_Ms = 800;
const unsigned long Motor_Periodo_Objetivo_Ms = 10000;
const float Motor_Factor_Alargamiento = 1.02; // AJUSTAR: qué tan rápido se relaja

void Iniciar_Biofeedback() {
  Motor_Activo = true;
  Ignorar_Impactos_Por_Motor = true;
  // Arranca con el intervalo real del latido actual (imita la aceleración)
  Motor_Periodo_Actual_Ms = (Bpm_Actual > 0) ? (unsigned long)(60000.0 / Bpm_Actual) : 800;
  Motor_Pulso_En_Alto = false;
  Motor_Marca_Cambio_Ms = millis();
}

void Detener_Biofeedback() {
  Motor_Activo = false;
  digitalWrite(Pin_Motor, LOW);
  Ignorar_Impactos_Por_Motor = false;
}

void Actualizar_Biofeedback() {
  if (!Motor_Activo) return;

  unsigned long Ahora = millis();
  // Duración del pulso ON: 40% del período total, resto en OFF
  unsigned long Duracion_On = Motor_Periodo_Actual_Ms * 0.4;
  unsigned long Duracion_Off = Motor_Periodo_Actual_Ms - Duracion_On;

  if (Motor_Pulso_En_Alto) {
    if (Ahora - Motor_Marca_Cambio_Ms >= Duracion_On) {
      digitalWrite(Pin_Motor, LOW);
      Motor_Pulso_En_Alto = false;
      Motor_Marca_Cambio_Ms = Ahora;
    }
  } else {
    if (Ahora - Motor_Marca_Cambio_Ms >= Duracion_Off) {
      digitalWrite(Pin_Motor, HIGH);
      Motor_Pulso_En_Alto = true;
      Motor_Marca_Cambio_Ms = Ahora;

      // Al iniciar cada nuevo pulso, alargamos gradualmente el período
      // (guía de respiración: cada ciclo es un poco más lento que el anterior)
      if (Motor_Periodo_Actual_Ms < Motor_Periodo_Objetivo_Ms) {
        Motor_Periodo_Actual_Ms = (unsigned long)(Motor_Periodo_Actual_Ms * Motor_Factor_Alargamiento);
        if (Motor_Periodo_Actual_Ms > Motor_Periodo_Objetivo_Ms) {
          Motor_Periodo_Actual_Ms = Motor_Periodo_Objetivo_Ms;
        }
      }
    }
  }
}

// ============================================================================
// 14. DETECCIÓN DE ESTRÉS, 3 NIVELES (modo diurno)
// ============================================================================
// AJUSTAR: estos umbrales son punto de partida, no verdad calibrada.
//
// Diseño: "Estresado" exige AMBAS condiciones a la vez (BPM muy elevado Y
// HRV muy bajo) — igual que antes, para evitar falsos positivos del nivel
// más alto (el que dispara el motor). "Intranquilo" es más permisivo:
// exige UNA sola condición moderada, porque es una categoría intermedia de
// aviso, no la que activa el biofeedback.
const float Umbral_Bpm_Intranquilo = 6;    // BPM por encima del basal
const float Umbral_Bpm_Estresado = 12;     // BPM por encima del basal
const float Factor_Hrv_Intranquilo = 0.85; // RMSSD por debajo del 85% del basal
const float Factor_Hrv_Estresado = 0.70;   // RMSSD por debajo del 70% del basal
const int Muestras_Consecutivas_Para_Cambio = 5; // debounce, igual que antes

Nivel_Estres Nivel_Candidato_Anterior = Nivel_Tranquilo;
int Contador_Muestras_Nivel = 0;

void Evaluar_Estres() {
  float Basal_Bpm_Referencia = Usando_Basal_Provisional ? Basal_Bpm_Provisional : Promedio_Basal_Bpm;
  float Basal_Hvr_Referencia = Usando_Basal_Provisional ? Basal_Hvr_Provisional : Promedio_Basal_Hvr;

  if (Basal_Bpm_Referencia <= 0 || Basal_Hvr_Referencia <= 0) {
    Nivel_Estres_Actual = Nivel_Tranquilo; // sin basal confiable, no alertamos (evita falsos positivos)
    Usuario_Estresado = false;
    return;
  }

  bool Bpm_Muy_Elevado = Bpm_Actual > (Basal_Bpm_Referencia + Umbral_Bpm_Estresado);
  bool Hrv_Muy_Bajo = Hrv_Rmssd_Actual < (Basal_Hvr_Referencia * Factor_Hrv_Estresado);
  bool Bpm_Elevado = Bpm_Actual > (Basal_Bpm_Referencia + Umbral_Bpm_Intranquilo);
  bool Hrv_Bajo = Hrv_Rmssd_Actual < (Basal_Hvr_Referencia * Factor_Hrv_Intranquilo);

  Nivel_Estres Nivel_Candidato;
  if (Bpm_Muy_Elevado && Hrv_Muy_Bajo) {
    Nivel_Candidato = Nivel_Estresado;
  } else if (Bpm_Elevado || Hrv_Bajo) {
    Nivel_Candidato = Nivel_Intranquilo;
  } else {
    Nivel_Candidato = Nivel_Tranquilo;
  }

  // Debounce: exige varios latidos consecutivos con el MISMO nivel
  // candidato antes de adoptarlo, para no saltar entre niveles latido a
  // latido por ruido puntual.
  if (Nivel_Candidato == Nivel_Candidato_Anterior) {
    Contador_Muestras_Nivel++;
  } else {
    Contador_Muestras_Nivel = 1;
    Nivel_Candidato_Anterior = Nivel_Candidato;
  }

  if (Contador_Muestras_Nivel >= Muestras_Consecutivas_Para_Cambio) {
    Nivel_Estres_Actual = Nivel_Candidato;
  }

  Usuario_Estresado = (Nivel_Estres_Actual == Nivel_Estresado);
}

// ============================================================================
// 15. DETECCIÓN DE SUEÑO POR PUNTAJE (modo nocturno)
// ============================================================================
// Requisito: quieto (baja variabilidad de movimiento) durante 5-10 min
// continuos Y BPM estable y bajo, sin picos.
const unsigned long Tiempo_Requerido_Quietud_Ms = 8UL * 60UL * 1000UL; // 8 min
const float Umbral_Desviacion_Movimiento = 0.05; // AJUSTAR: std-dev en G
const float Umbral_Bpm_Reposo_Extra = 5.0;        // margen sobre basal BPM nocturno

unsigned long Marca_Inicio_Quietud_Ms = 0;
bool En_Ventana_Quietud = false;
bool Esta_Dormido = false;

// Estadística incremental simple (Welford) para desviación estándar del movimiento
double Welford_Media = 0, Welford_M2 = 0;
long Welford_Contador = 0;

void Reiniciar_Welford() {
  Welford_Media = 0; Welford_M2 = 0; Welford_Contador = 0;
}

void Actualizar_Welford(float Muestra) {
  Welford_Contador++;
  double Delta = Muestra - Welford_Media;
  Welford_Media += Delta / Welford_Contador;
  double Delta2 = Muestra - Welford_Media;
  Welford_M2 += Delta * Delta2;
}

float Obtener_Desviacion_Welford() {
  if (Welford_Contador < 2) return 999;
  return sqrt(Welford_M2 / Welford_Contador);
}

void Evaluar_Sueno(float Magnitud_Movimiento_G) {
  Actualizar_Welford(Magnitud_Movimiento_G);
  float Desviacion_Actual = Obtener_Desviacion_Welford();

  bool Bpm_De_Reposo = (Promedio_Basal_Bpm > 0) &&
                        (Bpm_Actual > 0) &&
                        (Bpm_Actual <= Promedio_Basal_Bpm + Umbral_Bpm_Reposo_Extra);

  bool Condicion_Quietud = Desviacion_Actual < Umbral_Desviacion_Movimiento;

  if (Condicion_Quietud && Bpm_De_Reposo) {
    if (!En_Ventana_Quietud) {
      En_Ventana_Quietud = true;
      Marca_Inicio_Quietud_Ms = millis();
    }
    if (millis() - Marca_Inicio_Quietud_Ms >= Tiempo_Requerido_Quietud_Ms) {
      Esta_Dormido = true;
    }
  } else {
    En_Ventana_Quietud = false;
    Esta_Dormido = false;
    Reiniciar_Welford();
  }
}

// ============================================================================
// 16. ACUMULACIÓN DE DATOS BASALES DURANTE LA NOCHE (Basal_Calib)
// ============================================================================
double Acumulador_Bpm_Noche = 0;
double Acumulador_Hvr_Noche = 0;
long Muestras_Noche = 0;

void Acumular_Muestra_Noche() {
  if (Bpm_Actual <= 0) return;
  Acumulador_Bpm_Noche += Bpm_Actual;
  Acumulador_Hvr_Noche += Hrv_Rmssd_Actual;
  Muestras_Noche++;
}

void Cerrar_Calibracion_Noche() {
  if (Muestras_Noche < 30) {
    Serial.println(F("[BASAL] Muestras insuficientes esta noche, no se guarda."));
  } else {
    float Bpm_Prom = Acumulador_Bpm_Noche / Muestras_Noche;
    float Hvr_Prom = Acumulador_Hvr_Noche / Muestras_Noche;
    Guardar_Dato_Basal_Noche(Bpm_Prom, Hvr_Prom);
  }
  Acumulador_Bpm_Noche = 0;
  Acumulador_Hvr_Noche = 0;
  Muestras_Noche = 0;
}

// ============================================================================
// 17. CALIBRACIÓN PROVISIONAL (primera vez, dispositivo sin datos)
// ============================================================================
unsigned long Marca_Inicio_Provisional_Ms = 0;
const unsigned long Duracion_Provisional_Ms = 2UL * 60UL * 1000UL; // 2 min (AJUSTAR hasta 3 min)
double Acumulador_Bpm_Provisional = 0;
double Acumulador_Hvr_Provisional = 0;
long Muestras_Provisional = 0;

// ============================================================================
// 18. BLE - DESACTIVADO POR AHORA
// ============================================================================
// Se retiró temporalmente porque no es uno de sus 5 requisitos técnicos y
// estaba bloqueando la compilación (BLEDevice.h no se encontraba en su
// instalación del core). Si más adelante confirma que sí lo necesita,
// verifiquemos primero que la carpeta
//   ...\Arduino15\packages\esp32\hardware\esp32\<version>\libraries\BLE
// exista en su instalación antes de reintegrarlo, para no repetir el mismo
// error a ciegas.

// ============================================================================
// 19. SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(50); // única espera aceptable: dar tiempo al monitor serial en el arranque

  pinMode(Pin_Led_Verde, OUTPUT);
  pinMode(Pin_Led_Rojo, OUTPUT);
  pinMode(Pin_Motor, OUTPUT);
  digitalWrite(Pin_Led_Verde, LOW);
  digitalWrite(Pin_Led_Rojo, LOW);
  digitalWrite(Pin_Motor, LOW);

  WiFi.mode(WIFI_STA); //Se inicia el modo Station del Wifi
  BLE_Start();  //Se llama la función que inicia el BLE
  Serial.println(F("\n--- Iniciando Wearable Estres/Sueno (FSM) ---"));

  // --- Mutex de FreeRTOS: se crean ANTES de tocar I2C o los sensores ---
  // Bug corregido: antes se creaban después de inicializar los sensores.
  // Si un sensor fallaba en el arranque, Recuperar_Bus_I2c() intentaba usar
  // Mutex_I2c cuando todavía no existía (puntero nulo) -> el RTOS abortaba
  // con "assert failed: xQueueSemaphoreTake ... pxQueue" al primer fallo
  // de sensor durante el arranque. Por eso solo se veía en el caso real:
  // solo se manifestaba si el MPU o el MAX fallaban en setup(), no en uso normal.
  Mutex_Mpu = xSemaphoreCreateMutex();
  Mutex_I2c = xSemaphoreCreateMutex();

  // --- I2C ---
  Wire.begin(Pin_I2c_Sda, Pin_I2c_Scl);
  Wire.setClock(100000);
  Wire.setTimeOut(50);

  // --- Sensor cardíaco ---
  if (!Sensor_Cardiaco.begin(Wire)) {
    Serial.println(F("ERROR: MAX30105 no encontrado. Intentando recuperar bus I2C..."));
    Recuperar_Bus_I2c();
    if (!Sensor_Cardiaco.begin(Wire)) {
      Serial.println(F("ERROR CRITICO: MAX30105 sigue sin responder. Reiniciando en 3s."));
      // Parpadeo de error acotado en el tiempo (no infinito) antes de reiniciar
      for (int i = 0; i < 15; i++) {
        digitalWrite(Pin_Led_Rojo, !digitalRead(Pin_Led_Rojo));
        delay(200);
      }
      ESP.restart();
    }
  }
  Serial.println(F("-> MAX30105 detectado."));
  Sensor_Cardiaco.setup(Config_Max_Potencia_Led, Config_Max_Promedio_Muestras, Config_Max_Modo_Led,
                         Config_Max_Velocidad_Muestreo, Config_Max_Ancho_Pulso, Config_Max_Rango_Adc);
  Serial.print(F("-> MAX30105 configurado para muneca. Potencia LED: 0x"));
  Serial.println(Config_Max_Potencia_Led, HEX);

  // --- Acelerómetro ---
  if (!Sensor_Mpu.begin(0x68, &Wire, 0)) {
    Serial.println(F("ERROR: MPU6050 no encontrado. Intentando recuperar bus I2C..."));
    Recuperar_Bus_I2c();
    if (!Sensor_Mpu.begin(0x68, &Wire, 0)) {
      Serial.println(F("ERROR: MPU6050 sigue sin responder tras recuperar el bus."));
      Serial.println(F("       Revise cableado SDA/SCL y alimentacion del sensor."));
      Serial.println(F("       El sistema continua, pero sin datos de movimiento."));
    }
  }
  Sensor_Mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  Sensor_Mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println(F("-> MPU6050 enlazado."));

  // --- Tarea FreeRTOS del MPU (independiente del loop principal) ---
  xTaskCreate(Tarea_Leer_Mpu, "Tarea_Mpu", 4096, NULL, 3, &Handle_Tarea_Mpu);

  // --- Tarea FreeRTOS de red (POST/GET en segundo plano) ---
  // Prioridad baja (1): nunca debe competir con la lectura de sensores.
  // Stack de 8192 bytes: las operaciones HTTPS consumen bastante pila
  // (manejo de certificados/TLS), 4096 se queda corto y puede crashear.
  Cola_Post = xQueueCreate(5, sizeof(Datos_Post));
  xTaskCreate(Tarea_Red, "Tarea_Red", 8192, NULL, 1, &Handle_Tarea_Red);

  // --- BLE ---
  // (ya se inicializó arriba, vía BLE_Start())

  // --- Datos basales ---
  Cargar_Y_Mostrar_Datos_Basales();

  Serial.println(F("Envie por Serial 'H:HHMM' para fijar la hora, ej. H:1430"));
  Serial.println(F("Envie por Serial 'SCAN' para escanear el bus I2C (diagnostico)."));
  Serial.println(F("Envie por Serial 'TEST_MOTOR' para probar el motor 15s (diagnostico)."));

  if (!Datos_Basales_Validos) {
    Estado_Actual = Provisional_Calib;
    Marca_Inicio_Provisional_Ms = millis();
    Serial.println(F("[FSM] -> Provisional_Calib (sin datos basales previos)"));
  } else {
    Estado_Actual = Sys_Init;
  }
  
  WiFi_Credentials credenciales=Leer_Credenciales_WiFi();
  Serial.println(credenciales.ssid);
  Serial.println(credenciales.pass);
  if (credenciales.ssid !="" && credenciales.pass !=""){
    wifiConnect(credenciales.ssid.c_str(),credenciales.pass.c_str());
  }
}

// ============================================================================
// 20. LOOP PRINCIPAL (FSM)
// ============================================================================
void loop() {
  Procesar_Comando_Serial();
  Actualizar_Reloj_Simple();
  if (WiFi.status()!= WL_CONNECTED && BLEapagado == true){
    WiFi_Credentials credenciales=Leer_Credenciales_WiFi();
    Serial.println(credenciales.ssid);
    Serial.println(credenciales.pass);
    if (credenciales.ssid !="" && credenciales.pass !=""){
      wifiConnect(credenciales.ssid.c_str(),credenciales.pass.c_str());
    }
  }
  if (xSemaphoreTake(Mutex_I2c, pdMS_TO_TICKS(20)) == pdTRUE) {
    Sensor_Cardiaco.check();
    xSemaphoreGive(Mutex_I2c);
  }

  // Copia local (thread-safe) del último dato de movimiento
  Datos_Movimiento Dato_Mpu_Local;
  if (xSemaphoreTake(Mutex_Mpu, pdMS_TO_TICKS(5)) == pdTRUE) {
    Dato_Mpu_Local.Magnitud_G = Ultimo_Dato_Mpu.Magnitud_G;
    Dato_Mpu_Local.Movimiento_Alto = Ultimo_Dato_Mpu.Movimiento_Alto;
    Dato_Mpu_Local.Impacto_Detectado = Ultimo_Dato_Mpu.Impacto_Detectado;
    Dato_Mpu_Local.Fuerza_Pico_Impacto = Ultimo_Dato_Mpu.Fuerza_Pico_Impacto;
    if (Ultimo_Dato_Mpu.Impacto_Detectado) {
      Ultimo_Dato_Mpu.Impacto_Detectado = false; // se consume la bandera
    }
    xSemaphoreGive(Mutex_Mpu);
  }

  // --- Vigilancia de fallos de I2C acumulados ---
  if (Contador_Fallos_I2c >= Umbral_Fallos_I2c) {
    Recuperar_Bus_I2c();
  }

  // --- Reporte de impacto (requisito #4), válido en cualquier estado ---
  if (Dato_Mpu_Local.Impacto_Detectado) {
    Serial.print(F("[IMPACTO] Fuerza pico: "));
    Serial.print(Dato_Mpu_Local.Fuerza_Pico_Impacto, 2);
    Serial.println(F(" G"));
  }

  // Sección crítica de I2C: lectura del MAX30105 (latido + contacto).
  // Si no se logra tomar el mutex a tiempo, se descarta este ciclo entero
  // en vez de leer el sensor sin protección.
  bool Hay_Latido_Nuevo = false;
  bool Hay_Contacto = false;
  if (xSemaphoreTake(Mutex_I2c, pdMS_TO_TICKS(20)) == pdTRUE) {
    Hay_Latido_Nuevo = Procesar_Lectura_Cardiaca(Dato_Mpu_Local.Movimiento_Alto);
    Hay_Contacto = (Sensor_Cardiaco.getIR() >= 50000);
    xSemaphoreGive(Mutex_I2c);
  }
  bool Es_Noche = Es_Modo_Nocturno();

  // --- Prueba forzada del motor (comando TEST_MOTOR) ---
  if (Prueba_Motor_Forzada) {
    if (millis() >= Prueba_Motor_Fin_Ms) {
      Prueba_Motor_Forzada = false;
      Detener_Biofeedback();
      Estado_Actual = Es_Noche ? Sleep_Detect : Stress_Monitor;
      Serial.println(F("[TEST] Prueba de motor finalizada."));
    } else {
      Usuario_Estresado = true; // se mantiene forzado durante la ventana de prueba
      Nivel_Estres_Actual = Nivel_Estresado;
    }
  }

  // --- Telemetría en tiempo real por latido (requisito estricto) ---
  // Se evalúa el estrés y se imprime el reporte apenas hay un latido válido,
  // sin esperar a que la FSM procese nada más. Esto hace que Usuario_Estresado
  // esté siempre actualizado ANTES de entrar al switch de abajo.
  if (Hay_Latido_Nuevo) {
    if (!Prueba_Motor_Forzada) {
      Evaluar_Estres(); // no se recalcula con datos reales mientras la prueba está activa
    }
    Imprimir_Reporte_Latido(Es_Noche);
  }

  switch (Estado_Actual) {

    // ---------------------------------------------------------------
    case Sys_Init: {
      Estado_Actual = Es_Noche ? Sleep_Detect : Stress_Monitor;
      break;
    }

    // ---------------------------------------------------------------
    case Provisional_Calib: {
      Actualizar_Led_Provisional();
      if (Hay_Latido_Nuevo) {
        Acumulador_Bpm_Provisional += Bpm_Actual;
        Acumulador_Hvr_Provisional += Hrv_Rmssd_Actual;
        Muestras_Provisional++;
      }
      if (millis() - Marca_Inicio_Provisional_Ms >= Duracion_Provisional_Ms) {
        if (Muestras_Provisional > 10) {
          Basal_Bpm_Provisional = Acumulador_Bpm_Provisional / Muestras_Provisional;
          Basal_Hvr_Provisional = Acumulador_Hvr_Provisional / Muestras_Provisional;
          Usando_Basal_Provisional = true;
          Serial.print(F("[PROVISIONAL] Basal temporal BPM: "));
          Serial.print(Basal_Bpm_Provisional);
          Serial.print(F(" | HRV: "));
          Serial.println(Basal_Hvr_Provisional);
        } else {
          Serial.println(F("[PROVISIONAL] Datos insuficientes; se reintentara con el basal por defecto."));
        }
        // Ya no se apaga a la fuerza aquí: se deja que Actualizar_Led_Estado()
        // tome el control del LED en el siguiente ciclo, dentro de
        // Stress_Monitor, sin un apagón intermedio.
        Estado_Actual = Es_Noche ? Sleep_Detect : Stress_Monitor;
      }
      break;
    }

    // ---------------------------------------------------------------
    case Stress_Monitor: {
      if (Es_Noche) {
        Estado_Actual = Sleep_Detect;
        break;
      }
      Actualizar_Led_Estado(Hay_Contacto, Nivel_Estres_Actual);

      if (Usuario_Estresado) {
        Estado_Actual = Biofeedback;
        Iniciar_Biofeedback();
        Serial.println(F("[FSM] -> Biofeedback (estres detectado)"));
      }
      break;
    }

    // ---------------------------------------------------------------
    case Biofeedback: {
      if (Es_Noche) {
        Detener_Biofeedback();
        Estado_Actual = Sleep_Detect;
        break;
      }
      Actualizar_Led_Estado(Hay_Contacto, Nivel_Estresado);
      Actualizar_Biofeedback();

      // Sale de biofeedback cuando el estrés se resuelve o el motor ya
      // alcanzó el ritmo de respiración objetivo y el usuario se estabilizó
      if (!Usuario_Estresado) {
        Detener_Biofeedback();
        Estado_Actual = Stress_Monitor;
        Serial.println(F("[FSM] -> Stress_Monitor (estres resuelto)"));
      }
      break;
    }

    // ---------------------------------------------------------------
    case Sleep_Detect: {
      if (!Es_Noche) {
        Esta_Dormido = false;
        En_Ventana_Quietud = false;
        Estado_Actual = Stress_Monitor;
        Serial.println(F("[FSM] -> Stress_Monitor (fin de ventana nocturna)"));
        break;
      }
      digitalWrite(Pin_Led_Verde, LOW);
      digitalWrite(Pin_Led_Rojo, LOW);

      if (Hay_Latido_Nuevo) {
        Evaluar_Sueno(Dato_Mpu_Local.Magnitud_G);
      }

      if (Esta_Dormido) {
        Estado_Actual = Basal_Calib;
        Muestras_Noche = 0;
        Acumulador_Bpm_Noche = 0;
        Acumulador_Hvr_Noche = 0;
        Serial.println(F("[FSM] -> Basal_Calib (sueno profundo confirmado)"));
      }
      break;
    }

    // ---------------------------------------------------------------
    case Basal_Calib: {
      if (!Es_Noche) {
        Cerrar_Calibracion_Noche();
        Estado_Actual = Stress_Monitor;
        Serial.println(F("[FSM] -> Stress_Monitor (fin de ventana nocturna)"));
        break;
      }
      // Si el movimiento vuelve a subir de forma sostenida, el usuario se
      // despertó: cerramos la calibración de esta noche y regresamos a
      // Sleep_Detect en vez de seguir acumulando datos de vigilia.
      if (Dato_Mpu_Local.Movimiento_Alto) {
        Cerrar_Calibracion_Noche();
        Esta_Dormido = false;
        En_Ventana_Quietud = false;
        Estado_Actual = Sleep_Detect;
        Serial.println(F("[FSM] -> Sleep_Detect (usuario se movio / desperto)"));
        break;
      }
      if (Hay_Latido_Nuevo) {
        Acumular_Muestra_Noche();
      }
      break;
    }
  }

  // Nota: la telemetría en tiempo real por latido ya se envía por Serial en
  // Imprimir_Reporte_Latido(), más arriba, apenas se detecta el latido.
  // Este bloque BLE se retiró junto con el resto de la sección 18.
}
