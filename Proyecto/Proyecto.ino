//**************
//Librerias coms
//**************
#include <WiFi.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <ArduinoJson.h>
//**********************
//Configuaricón BLE coms
//**********************
#define WIFI_CRED_SERV_UUID "3373E991-457D-4656-9544-28DE1576896D"   //UUID para el BLE
#define WIFI_CRED_CHAR_UUID "DFDE7591-38A4-4019-A6A1-C09B4D0FCE70"   //UUID para el BLE
#define PASS_CRED_CHAR_UUID "FB4E9190-0D85-4810-A2A0-124BFD25A1AA"   //UUID para el BLE
#define WIFI_START_SERV_UUID "897DC0D1-1C3A-4567-BF0E-1EDB5DD83855"  //UUID para el BLE
#define WIFI_START_CHAR_UUID "03FE09DA-15E3-43B4-9C1B-47A7DA1AC992"  //UUID para el BLE
#define USER_TOKEN_CHAR_UUID "2DA7E879-F0D5-4E74-8317-E40A5D87413C"

String wifiScan();                                     //Forward Declaration
void wifiConnect(const char *ssid, const char *pass);  //Forward Declaration
unsigned long Tiempo_Anterior = 0;
const char *authIP = "https://teapp.lat/auth/auth.php";
String wifi_pass = "";
String wifi_ssid = "";
String publicIP = "";
String hora = "";
String UserToken = "";
String UserToken_Buffer="";
String User = "";
String Token = "";
int cota=0;
//***************
//Objetos del BLE
//***************
BLEServer *pServer = nullptr;
BLEService *pWifiCredService = nullptr;
BLEService *pWifiStartService = nullptr;
BLECharacteristic *pWifiCredChar = nullptr;
BLECharacteristic *pPassCredChar = nullptr;
BLECharacteristic *pWifiStartChar = nullptr;
BLECharacteristic *pUserTokenChar = nullptr;
//*********************
//Callback del servidor
//*********************
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
    }
  }
};
//************************
//Callback Caracteristicas
//************************
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
      wifiConnect(wifi_ssid.c_str(), wifi_pass.c_str());
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
//*********************
//Función principal BLE
//*********************
void wifi_start_credentials() {
  bool server_status = BLEDevice::init("ESP32 TEA");
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
  pWifiCredChar->setCallbacks(new WifiCredChar_Callback());
  pPassCredChar->setCallbacks(new WifiCredChar_Callback());
  pUserTokenChar->setCallbacks(new UserTokenChar_Callback());
  pWifiCredChar->setValue("");
  pPassCredChar->setValue("");
  pUserTokenChar->setValue("");
  pWifiCredService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(WIFI_CRED_SERV_UUID);
  pAdvertising->addServiceUUID(WIFI_START_SERV_UUID);
  pAdvertising->setScanResponse(true);

  BLEDevice::startAdvertising();
}
//*****************
//Funciones de COMS
//*****************
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

String getTime(String ip) {
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
      time_unformat.replace(":", "");
      http_hora.end();
      return time_unformat.substring(0, 4);
    }
    http_hora.end();
    intentos++;
    Serial.print(".");
    delay(1000);
  }
  return "";
}
String wifiScan() {
  Serial.println("Buscando Redes...");
  int min_rssi = -80;
  byte redes_totales = 0;
  byte redes = WiFi.scanNetworks();  //Cantidad de redes encontradas
  if (redes == 0) {
    Serial.println("No se encontraron redes.");
  } else {
    for (int i = 0; i < redes; i++) {  //Recorre las redes
      if (WiFi.RSSI(i) > min_rssi) {
        redes_totales += 1;
      }
    }
    Serial.print(redes_totales);
    Serial.println(" Redes encontradas");
    String wifi_list = "";
    for (int i = 0; i < redes_totales; i++) {
      wifi_list += WiFi.SSID(i) + "," + WiFi.RSSI(i);
      if (i < redes - 2) wifi_list += "|";
    }
    return wifi_list;
  }
}
void wifiDisconnect() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Desconectando de la red: ");
    Serial.println(WiFi.SSID());
    WiFi.disconnect();
    delay(200);
  }
}
void wifiConnect(const char *ssid, const char *pass) {  //Toma como parametros el nombre y la contraseña de la red
  int intentos = 0;
  wifiDisconnect();  //Se desconecta si ya esta conectado

  Serial.print("Conectando a la red: ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);  //Conectarse

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

    hora = getTime(publicIP);
    Serial.println(hora);
  } else {
    Serial.println("Error al conectar.");
    Serial.println("La conexión tardo demasiado, vefifique la contraseña e intentelo nuevmente");
    WiFi.disconnect();
  }
}

void infoPOST(float BPMprom, float HVRprom, bool superficie, String status) {  //§Recibe la IP y el valor de la alerta
  HTTPClient http_info;                                                      //Iniciamos el objeto

  Serial.println("Conectando al servidor...");
  http_info.begin(authIP);  //Conectar al servidor

  Serial.println("Haciendo POST");

  http_info.addHeader("USER", User);
  http_info.addHeader("TOKEN", Token);
  String info = String(BPMprom) + ";" + String(HVRprom) + ";" + String(superficie) + ";" + String(status);
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
//********************************
//Librerias Frecuencia & Variables
//********************************
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <vector>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Preferences.h>
//********
//Sensores
//********
MAX30105 Sensor_Cardiaco;
Adafruit_MPU6050 Mpu_Sensor;
Preferences preferences;
//***********************
// Configuración de Pines
//***********************
#define Led_Verde 9
#define Led_Rojo 7
#define Motor 3
//********
// Horario
//********
int Hora_Dormir = 2000;
int Hora_Actual = 1200;
int Hora_Activacion = 1200;
//*******************
// Variables de datos
//*******************
bool Datos_De_Hoy = false;
bool Dia_1 = true;
bool Tocando_Piel = false;
bool Tocando_Piel_Aneterior = false;
int Contador_Promedios = 0;
//*****************
// Variables de HVR
//*****************
long HVR = 0;
int Recopilador_HVR[180];
float Diferencia_HVR[179];
int Contador_HVR = 0;
float Promedio_HVR = 0;
float Promedio_Basal_HVR = 0;
std::vector<int> Capturar_HVR;
int Nivel_Superior_HVR = 20;
bool HVR_Primitivo = true;
float Guardar_Promedios_HVR[7];
float Guardar_Recuperar_HVR[7];
//*********************
// Variables del estres
//*********************
float Nivel_Estres = 0;
int Contador_Estres = 0;
byte Reset_Contador_Estres = 0;
bool Alerta_Estres = false;
//*******************
// Variables de pulso
//*******************
int Recopilador_BPM[180];
unsigned long Tempo_Ultimo_Latido = 0;
float BPM = 0;
unsigned long Numero_Latido = 0;
float Promedio_BPM = 0;
float Promedio_Basal_BPM = 0;
int Contador_BPM = 0;
std::vector<int> Capturar_BPM;
bool Alerta_Cardiaca = false;
int Contador_BMP_Alerta = 0;
int Reset_Contador_BMP_Alerta = 0;
bool BMP_Primitivo = true;
float Guardar_Promedios_BPM[7];
float Guardar_Recuperar_BPM[7];
//*********************************
// Variables de deteccion del sueño
//*********************************
int Picos_De_Frecuencia = 0;
std::vector<int> Recopilador_Cardiaco;
const unsigned long Tiempo_Esperando_Sueño = 15000;
const unsigned long Tiempo_Capturando_Sueño = 15000;
const unsigned long Tiempo_De_Espera_Datos = 15000;
unsigned long Tiempo_Comparacion_Sueño = 0;
bool Esta_Dormido = false;
enum Estado_Datos_Dormido {
  Esperando_15_Min,
  Capturando_5_Min
};
Estado_Datos_Dormido Estado_Dato_Sueño = Capturando_5_Min;
bool Desactivar_Promedio_Sueño = true;
float Promedio_Cardiaco_Sueño = 0;
//************************
// Variables de movimiento
//************************
float Accel_X = 0, Accel_Y = 0, Accel_Z = 0;
float Magnitud_Movimiento = 0;
float Promedio_Anterior_Movimiento = 0;
int Esta_Quieto = 0;
std::vector<float> Capturar_Movimiento;
//-------------------------------------------------------------------------------------------

//****************
//Variables alerta
//****************
String AlertaVar="";
float Promedio_HVR_Estres_Global=0;

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);       // Poner en modo station a la ESP
  wifi_start_credentials();  //Llama al BLE
  //***********************
  //Inicializacion de pines
  //***********************
  pinMode(Led_Verde, OUTPUT);
  pinMode(Led_Rojo, OUTPUT);
  digitalWrite(Led_Verde, LOW);
  digitalWrite(Led_Rojo, LOW);
  //****************
  //Comunicacion I2C
  //****************
  Wire.begin(1, 0);
  Wire.setClock(100000);
  //********
  //MAX30102
  //********
  if (!Sensor_Cardiaco.begin(Wire)) {
    Serial.println("ERROR: No se encontró el MAX30102.");
    /*while (1) {
      digitalWrite(Led_Rojo, HIGH); delay(100);
      digitalWrite(Led_Rojo, LOW);  delay(100);
      ESP.restart();
    }*/
  }
  Serial.println("-> Sensor MAX30102 detectado");
  byte Brillo = 100;
  byte Lecturas = 1;
  byte Modo = 2;
  int Velocidad_Muestreo = 400;
  int Ancho_Pulso = 411;
  int Rango_ADC = 8192;
  Sensor_Cardiaco.setup(Brillo, Lecturas, Modo, Velocidad_Muestreo, Ancho_Pulso, Rango_ADC);
  //********
  //MPU-6050
  //********
  Mpu_Sensor.begin(0x68, &Wire, 0);
  Mpu_Sensor.setAccelerometerRange(MPU6050_RANGE_2_G);
  Mpu_Sensor.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("-> Sensor MPU-6050 detectado");
  //!FUNCIONES HORA DE ACTIVACION Y ACTUAL Y DORMIR
  //Acceder a datos guardados
  Contador_Promedios = preferences.getInt("contador", 0);
  preferences.getBytes("hvr", Guardar_Recuperar_HVR, sizeof(Guardar_Recuperar_HVR));
  preferences.getBytes("bpm", Guardar_Recuperar_BPM, sizeof(Guardar_Recuperar_BPM));
  for (int i = 0; i < Contador_Promedios; i++) {
    Promedio_Basal_HVR += Guardar_Recuperar_HVR[i];
    Promedio_Basal_BPM += Guardar_Recuperar_BPM[i];
    Guardar_Promedios_HVR[i] = Guardar_Recuperar_HVR[i];
    Guardar_Promedios_BPM[i] = Guardar_Recuperar_BPM[i];
  }
  Promedio_Basal_HVR /= Contador_Promedios - 1;
  Promedio_Basal_BPM /= Contador_Promedios - 1;
  if (Contador_Promedios > 0) {
    Dia_1 = preferences.getBool("dia1", 0);
  }
  if (Contador_Promedios > 7) {
    preferences.end();
  }
  for (int i = 0; i <= 7; i++) {
    Serial.print(Guardar_Recuperar_HVR[i]);
    Serial.print("|");
  }
  Serial.println("bpm");
  for (int i = 0; i <= 7; i++) {
    Serial.print(Guardar_Recuperar_BPM[i]);
    Serial.print("|");
  }
  Serial.println(Contador_Promedios);
  Serial.println(Dia_1);
}
//-------------------------------------------------------------------------------------------
void loop() {
  //***************
  //Obtiene la Hora
  //***************
  unsigned long Tiempo = millis();
  if (Tiempo - Tiempo_Anterior >= 300000 && publicIP != "") {
    Tiempo_Anterior = Tiempo;
    hora = getTime(publicIP);
    Serial.println(hora);
  }
  //******************************************
  //Deteccion de presencia sobre el sensor MAX
  //******************************************
  Sensor_Cardiaco.check();
  long Valor_Presencia = Sensor_Cardiaco.getIR();
  if (Tocando_Piel != Tocando_Piel_Aneterior) {
    Serial.println("Piel!");
    Serial.println(User);
    Serial.println(Token);
    infoPOST(-1, -1, Tocando_Piel,"-1");
  }
  if (Valor_Presencia < 50000) {
    digitalWrite(Led_Rojo, HIGH);
    Tocando_Piel_Aneterior = Tocando_Piel;
    Tocando_Piel = false;
    return;
  } else {
    digitalWrite(Led_Rojo, LOW);
    Tocando_Piel_Aneterior = Tocando_Piel;
    Tocando_Piel = true;
  }
  //FUCION DE ACTUALIZAR HORA ACTUAL
  if (Dia_1 == true) {
    if (Hora_Activacion >= Hora_Dormir - 30) {
      Hora_Dormir = Hora_Activacion + 40;
    } else if (Hora_Activacion - Hora_Dormir - 30 > 0 && Hora_Activacion - Hora_Dormir - 30 < 30) {
      Hora_Dormir = Hora_Activacion + 40;
    }
  }
  //*******************************
  //Leer Variables del acelerometro
  //*******************************
  sensors_event_t a, g, temp;
  Mpu_Sensor.getEvent(&a, &g, &temp);
  Accel_X = a.acceleration.x;
  Accel_Y = a.acceleration.y;
  Accel_Z = a.acceleration.z;
  Magnitud_Movimiento = sqrt(Accel_X * Accel_X + Accel_Y * Accel_Y + Accel_Z * Accel_Z);
  //*********************************
  //Cuando detecta un pulso que hacer
  //*********************************
  if (checkForBeat(Valor_Presencia) == true) {
    digitalWrite(Led_Verde, HIGH);
    digitalWrite(Led_Rojo, LOW);
    //*****************
    //Configurar Timers
    //*****************
    unsigned long Tiempo_Actual = millis();
    HVR = Tiempo_Actual - Tempo_Ultimo_Latido;
    Tempo_Ultimo_Latido = Tiempo_Actual;
    unsigned long Tiempo_Actual_Sueño = millis();
    //*******************************************
    //Filtro de valores erroneos e inicializacion
    //*******************************************
    if (HVR > 400 && HVR < 1300) {
      BPM = 60000.0 / HVR;
      Numero_Latido++;

      Serial.print("<3 Latido #");
      Serial.print(Numero_Latido);
      Serial.print(" | HVR: ");
      Serial.print(HVR);
      Serial.print("ms");
      Serial.print(" | BPM: ");
      Serial.print(BPM, 1);
      Serial.print(" | Mov: ");
      Serial.print(Magnitud_Movimiento, 2);
      Serial.print(" | Quieto: ");
      Serial.print(Esta_Quieto);
      Serial.print(" | Dormido: ");
      Serial.println(Esta_Dormido ? "SI" : "NO");

      if (Contador_HVR < 180 && Esta_Dormido == true) {
        Recopilador_HVR[Contador_HVR] = HVR;
        Contador_HVR++;
      }
      if (Contador_BPM < 180 && Esta_Dormido == true) {
        Recopilador_BPM[Contador_BPM] = BPM;
        Contador_BPM++;
      }
      //********************************************
      // Procesamiento de HRV y algoritmos de estrés
      //********************************************
      if (Promedio_Basal_HVR != 0 || HVR_Primitivo == true) {
        if (Capturar_HVR.size() < 11) {
          Capturar_HVR.push_back(HVR);
        } else {
          float Promedio_HVR_Estres = 0;
          for (int i = 0; i < Capturar_HVR.size() - 1; i++) {
            Capturar_HVR[i] = fabs(Capturar_HVR[i] - Capturar_HVR[i + 1]);
            if (Capturar_HVR[i] < Promedio_Basal_HVR + Nivel_Superior_HVR) {
              Promedio_HVR_Estres += Capturar_HVR[i];
            } else if (HVR_Primitivo == true) {
              Promedio_HVR_Estres += Capturar_HVR[i];
            }
          }
          Promedio_HVR_Estres /= Capturar_HVR.size();
          if (HVR_Primitivo == true) {
            Promedio_Basal_HVR = Promedio_HVR_Estres;
            Promedio_HVR_Estres_Global=Promedio_HVR_Estres;
            HVR_Primitivo = false;
          }
          if (Dia_1 == true && Promedio_Basal_HVR < Promedio_HVR_Estres && Hora_Actual < Hora_Dormir - 30) {
            Promedio_Basal_HVR = Promedio_HVR_Estres;
            Promedio_HVR_Estres_Global=Promedio_HVR_Estres;
          }
          Capturar_HVR.clear();
          int Promediar_Estres = fabs(Promedio_HVR_Estres - Promedio_Basal_HVR);
          float Desviacion_HVR = (fabs(Promediar_Estres - Promedio_Basal_HVR) / Promedio_Basal_HVR) * 100;
          if (Desviacion_HVR <= 15) {
            Serial.println("<---- Estado HVR Tranquilo");
            Reset_Contador_Estres++;
            AlertaVar+="0";
          } else if (Desviacion_HVR <= 30) {
            Serial.println("<---- Estado HVR Intranquilo");
            AlertaVar+="1";
          } else if (Desviacion_HVR > 30) {
            Serial.println("<---- Estado HVR Alerta ");
            Contador_Estres++;
            AlertaVar+="2";
          }
          Serial.println(Desviacion_HVR);
          Serial.println(Promediar_Estres);
        }
      }
      //********************************************
      // Procesamiento de variaciones rápidas de BPM
      //********************************************
      if (Promedio_Basal_BPM != 0 || BMP_Primitivo == true) {
        if (Capturar_BPM.size() < 11) {
          Capturar_BPM.push_back(BPM);
        } else {
          float Promedio_BPM_Alerta = 0;
          int Picos_BPM = 0;
          for (int i = 0; i < Capturar_BPM.size() - 1; i++) {
            if (fabs(Capturar_BPM[i] - Capturar_BPM[i + 1]) > 30) {
              Picos_BPM++;
            } else {
              Promedio_BPM_Alerta += Capturar_BPM[i];
            }
          }
          if ((Capturar_BPM.size() - Picos_BPM) > 0) {
            Promedio_BPM_Alerta /= (Capturar_BPM.size() - Picos_BPM);
            if (BMP_Primitivo == true) {
              Promedio_Basal_BPM = Promedio_BPM_Alerta;
              BMP_Primitivo = false;
            }
          }
          if (Dia_1 == true && Promedio_BPM_Alerta < Promedio_Basal_BPM && Hora_Actual < Hora_Dormir - 30) {
            Promedio_Basal_BPM = Promedio_BPM_Alerta;
          }
          Capturar_BPM.clear();
          if (fabs(Promedio_BPM_Alerta - Promedio_Basal_BPM) <= 8) {
            Serial.println("<---- Estado BMP Tranquilo");
            Reset_Contador_BMP_Alerta++;
            AlertaVar+="0";
          } else if (fabs(Promedio_BPM_Alerta - Promedio_Basal_BPM) <= 20) {
            Serial.println("<---- Estado BMP Intranquilo");
            AlertaVar+="1";
          } else if (fabs(Promedio_BPM_Alerta - Promedio_Basal_BPM) > 20) {
            Serial.println("<---- Estado BMP Alerta");
            Contador_BMP_Alerta++;
            AlertaVar+="2";
          }
          Serial.println(fabs(Promedio_BPM_Alerta - Promedio_Basal_BPM));
          AlertaVar+="0";
          infoPOST(Promedio_BPM_Alerta,Promedio_HVR_Estres_Global,Tocando_Piel,AlertaVar);
          AlertaVar="";
        }
      }
      // Capturar el movimiento del ususario y compararlo con el anterior para saber si esta en movimiento
      if (Capturar_Movimiento.size() < 11) {
        Capturar_Movimiento.push_back(Magnitud_Movimiento);
      } else {
        float Promedio_Movimiento = 0;
        for (int i = 0; i < Capturar_Movimiento.size(); i++) {
          Promedio_Movimiento += Capturar_Movimiento[i];
        }
        Promedio_Movimiento /= Capturar_Movimiento.size();
        if (fabs(Promedio_Anterior_Movimiento - Promedio_Movimiento) < 0.5) {
          Esta_Quieto++;
        } else {
          Esta_Quieto = 0;
        }
        Promedio_Anterior_Movimiento = Promedio_Movimiento;
        Serial.print("Movimiento: ");
        Serial.println(Promedio_Anterior_Movimiento);
        Capturar_Movimiento.clear();
      }
      //********************************************************
      // Máquina de estados periódica para el análisis del Sueño
      //********************************************************
      switch (Estado_Dato_Sueño) {
        case Esperando_15_Min:
          if (Tiempo_Actual_Sueño - Tiempo_Comparacion_Sueño >= Tiempo_Esperando_Sueño) {
            Recopilador_Cardiaco.clear();
            Estado_Dato_Sueño = Capturando_5_Min;
            Tiempo_Comparacion_Sueño = Tiempo_Actual;
          }
          break;
        case Capturando_5_Min:
          if (Tiempo_Actual_Sueño - Tiempo_Comparacion_Sueño >= Tiempo_Capturando_Sueño) {
            Estado_Dato_Sueño = Esperando_15_Min;
            Tiempo_Comparacion_Sueño = Tiempo_Actual;
            Desactivar_Promedio_Sueño = false;
          }
          if (BPM != 0) {
            Recopilador_Cardiaco.push_back(BPM);
          }
          break;
      }
    }
    //********************************
    // Reporte diario de promedios HVR
    //********************************
    if (Contador_HVR >= 180 && Datos_De_Hoy == false) {
      for (int i = 0; i < Contador_HVR - 1; i++) {
        Diferencia_HVR[i] = fabs(Recopilador_HVR[i] - Recopilador_HVR[i + 1]);
        Promedio_HVR += Diferencia_HVR[i];
      }
      Promedio_HVR /= Contador_HVR - 1;
      Contador_HVR = 0;
      if (Contador_Promedios < 7) {
        Guardar_Promedios_HVR[Contador_Promedios] = Promedio_HVR;
        preferences.putBytes("hvr", Guardar_Promedios_HVR, sizeof(Guardar_Promedios_HVR));
      }
    }
    //************************************************
    // Reporte diario de promedios BMP
    //************************************************
    if (Contador_BPM >= 180 && Datos_De_Hoy == false) {
      for (int i = 0; i < Contador_BPM; i++) {
        Promedio_BPM += Recopilador_BPM[i];
      }
      Promedio_BPM /= Contador_BPM;
      Contador_BPM = 0;
      if (Contador_Promedios < 7) {
        Guardar_Promedios_BPM[Contador_Promedios] = Promedio_BPM;
        preferences.putBytes("bpm", Guardar_Promedios_BPM, sizeof(Guardar_Promedios_BPM));
        Contador_Promedios++;
        preferences.putInt("contador", Contador_Promedios);
        Dia_1 = false;
        preferences.putBool("dia1", Dia_1);
        preferences.end();
      }
    }

    // Procesamiento de patrones del sueño y autocalibración basal
    if (Estado_Dato_Sueño == Esperando_15_Min && Desactivar_Promedio_Sueño == false) {
      Picos_De_Frecuencia = 0;
      Promedio_Cardiaco_Sueño = 0;

      for (int i = 1; i < Recopilador_Cardiaco.size(); i++) {
        float Ignorar_Picos = fabs(Recopilador_Cardiaco[i] - Recopilador_Cardiaco[i - 1]);
        if (Ignorar_Picos > 15) {
          Picos_De_Frecuencia++;
        } else {
          Promedio_Cardiaco_Sueño += Recopilador_Cardiaco[i];
        }
      }
      if ((Recopilador_Cardiaco.size() - Picos_De_Frecuencia) > 0) {
        Promedio_Cardiaco_Sueño /= (Recopilador_Cardiaco.size() - Picos_De_Frecuencia);
      }
    }
    if (Hora_Actual >= Hora_Dormir && Esta_Quieto > 15 && Promedio_Basal_BPM + 4 >= Promedio_Cardiaco_Sueño) {
      Esta_Dormido = true;
    } else {
      Esta_Dormido = false;
    }
    if (Reset_Contador_Estres > 12) {
      Contador_Estres = 0;
      Reset_Contador_Estres = 0;
    }
    if (Reset_Contador_BMP_Alerta > 12) {
      Contador_BMP_Alerta = 0;
      Reset_Contador_BMP_Alerta = 0;
    }

    if (Contador_Estres > 9) {
      Alerta_Estres = true;
    } else {
      Alerta_Estres = false;
    }
    if (Contador_BMP_Alerta > 9) {
      Alerta_Cardiaca = true;
    } else {
      Alerta_Cardiaca = false;
    }
  } else {
    digitalWrite(Led_Verde, LOW);
  }
}
