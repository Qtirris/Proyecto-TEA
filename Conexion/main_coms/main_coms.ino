//**************
//Librerias coms
//**************
#include <WiFi.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEServer.h>
//**********************
//Configuaricón BLE coms
//**********************
#define WIFI_CRED_SERV_UUID "3373E991-457D-4656-9544-28DE1576896D"   //UUID para el servicio
#define WIFI_START_SERV_UUID "897DC0D1-1C3A-4567-BF0E-1EDB5DD83855"  //UUID para el servicio
#define WIFI_CRED_CHAR_UUID "DFDE7591-38A4-4019-A6A1-C09B4D0FCE70"   //UUID para la caracteristica
#define PASS_CRED_CHAR_UUID "FB4E9190-0D85-4810-A2A0-124BFD25A1AA"   //UUID para la caracteristica
#define WIFI_START_CHAR_UUID "03FE09DA-15E3-43B4-9C1B-47A7DA1AC992"  //UUID para la caracteristica

String wifiScan();  //Forward Declaration
void wifiConnect(const char *ssid, const char *pass);  //Forward Declaration
String wifi_pass="";
String wifi_ssid="";
//***************
//Objetos del BLE
//***************
BLEServer *pServer = nullptr;
BLEService *pWifiCredService = nullptr;
BLEService *pWifiStartService = nullptr;
BLECharacteristic *pWifiCredChar = nullptr;
BLECharacteristic *pPassCredChar = nullptr;
BLECharacteristic *pWifiStartChar = nullptr;
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
    String uuid=pChar->getUUID().toString();
    uuid.toUpperCase();
    if (uuid==WIFI_CRED_CHAR_UUID){
      wifi_ssid=valor;
    } else if(uuid==PASS_CRED_CHAR_UUID){
      wifi_pass=valor;
      Serial.println(wifi_ssid);
      Serial.println(wifi_pass);
      wifiConnect(wifi_ssid.c_str(),wifi_pass.c_str());
    }
  }
  void onRead(BLECharacteristic *pChar) {
    String valor = pChar->getValue();
    Serial.print("Leyó: ");
    Serial.println(valor.c_str());
  }
};
//*****************
//Función principal
//*****************
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
  pWifiCredChar = pWifiCredService->createCharacteristic(WIFI_CRED_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pPassCredChar = pWifiCredService->createCharacteristic(PASS_CRED_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pWifiCredChar->setCallbacks(new WifiCredChar_Callback());
  pPassCredChar->setCallbacks(new WifiCredChar_Callback());
  pWifiCredChar->setValue("");
  pPassCredChar->setValue("");
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
void wifiConnect(const char *ssid, const char *pass) {
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
  } else {
    Serial.println("Error al conectar.");
    Serial.println("La conexión tardo demasiado, vefifique la contraseña e intentelo nuevmente");
    WiFi.disconnect();
  }
}
void statusPOST(const char *IP, const bool status) {
  HTTPClient http;

  Serial.println("Conectando al servidor...");
  http.begin(IP);  //Conectar al servidor
  Serial.println(http.begin(IP));

  Serial.println("Haciendo POST");

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String alerta = "status=" + String(status);//Solo recibe string

  int httpCode = http.POST(alerta);

  if (httpCode > 0) {  //Evita los errores de HTTPClient
    Serial.print("httpCode= ");
    Serial.println(httpCode);

    if (httpCode == HTTP_CODE_OK) {
      String respuesta = http.getString();
      Serial.println("Respuesta: ");
      Serial.println(respuesta);
    }
  } else {
    Serial.printf("HTTPClient Error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}
void promediosPOST(const char *IP, const float dato) {
  HTTPClient http;

  Serial.println("Conectando al servidor...");
  http.begin(IP);

  Serial.println("Haciendo POST");

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String promedio = "status=" + String(dato);

  int httpCode = http.POST(promedio);

  if (httpCode > 0) {  //Evita los errores de HTTPClient
    Serial.print("httpCode= ");
    Serial.println(httpCode);

    if (httpCode == HTTP_CODE_OK) {
      String respuesta = http.getString();
      //Guarda la respuesta
      Serial.println("Respuesta: ");
      Serial.println(respuesta);
    }
  } else {
    Serial.printf("HTTPClient Error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}
void statusGET(const char *IP) {  //Solo recibe la IP
  HTTPClient http;

  Serial.println("Conectando al servidor...");
  http.begin(IP);
  
  Serial.println("Haciendo GET");
  int httpCode = http.GET();

  if (httpCode > 0) {
    Serial.print("httpCode= ");
    Serial.println(httpCode);

    if (httpCode == HTTP_CODE_OK) {
      String respuesta = http.getString();
      Serial.println("Respuesta: ");
      Serial.println(respuesta);
    }
  } else {
    Serial.printf("HTTPClient Error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);       // Poner en modo station a la ESP
  wifi_start_credentials();  //Llama al BLE
  
}
void loop() {

}
