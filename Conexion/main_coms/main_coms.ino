#include <WiFi.h>
#include <HTTPClient.h>
#include <BLEDevice.h>

#include <BLEServer.h>

#define WIFI_CRED_SERV_UUID "3373E991-457D-4656-9544-28DE1576896D" //UUID para el BLE
#define WIFI_CRED_CHAR_UUID "DFDE7591-38A4-4019-A6A1-C09B4D0FCE70" //UUID para el BLE
#define PASS_CRED_CHAR_UUID "FB4E9190-0D85-4810-A2A0-124BFD25A1AA" //UUID para el BLE

#define WIFI_START_SERV_UUID "897DC0D1-1C3A-4567-BF0E-1EDB5DD83855"
#define WIFI_START_CHAR_UUID "03FE09DA-15E3-43B4-9C1B-47A7DA1AC992"

bool client_connected = false;
//Objetos del BLE
BLEServer *pServer = nullptr;
BLEService *pWifiCredService = nullptr;
BLEService *pWifiStartService = nullptr;
BLECharacteristic *pWifiCredChar = nullptr;
BLECharacteristic *pPassCredChar = nullptr;
BLECharacteristic *pWifiStartChar = nullptr;

void wifiScan();
//Callback del servidor (Conectar y desconectar)
class Server_Callback : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    Serial.println("Cliente conectado.");
    client_connected = true;
    pServer->stopAdvertising();
  }
  void onDisconnect(BLEServer *pServer) {
    Serial.println("Cliente desconectado.");
    client_connected = false;
    pServer->startAdvertising();
  }
};

//Callback Caracteristicas Wifi Credentials (Write y Read)
class WifiCredChar_Callback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) {
    String valor = pChar->getValue();
    Serial.print("Valor: ");
    Serial.println(valor.c_str());
  }
  void onRead(BLECharacteristic *pChar) {
    String valor = pChar->getValue();
    Serial.print("Leyó:");
    Serial.println(valor.c_str());
  }
};
class WifiStartChar_Callback : public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic *pChar){
    String valor = pChar->getValue();
    if (valor=="1"){
      wifiScan();
      pChar->setValue("0");
    }
    else{
      pChar->setValue("0")
    }
  }
  void onRead(BLECharacteristic *pChar){
    String valor = pChar->getValue();
    Serial.println(valor);
  }
};
//Función principal 
void wifi_credentials() {
  bool server_status = BLEDevice::init("ESP32 TEA");

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
  pWifiStartChar->setValue("0");
  pWifiStartService->start();
  
  pWifiCredService = pServer->createService(WIFI_CRED_SERV_UUID);

  pWifiCredChar = pWifiCredService->createCharacteristic(WIFI_CRED_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pPassCredChar = pWifiCredService->createCharacteristic(PASS_CRED_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);

  pWifiCredChar->setCallbacks(new WifiCredChar_Callback());
  pPassCredChar->setCallbacks(new WifiCredChar_Callback());

  pWifiCredChar->setValue("PRUEBA");
  pPassCredChar->setValue("");

  pWifiCredService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(WIFI_CRED_SERV_UUID);
  pAdvertising->addServiceUUID(WIFI_START_SERV_UUID);
  pAdvertising->setScanResponse(true);

  BLEDevice::startAdvertising();
}


void wifiScan() {
  //const int min_rssi = -80;  //Limite de señal
  Serial.println("Buscando Redes...");
  byte redes = WiFi.scanNetworks();  //Cantidad de redes encontradas
  if (redes == 0) {
    Serial.println("No se encontraron redes.");
  } else {
    Serial.print(redes);
    Serial.println(" Redes encontradas");
    String wifi_array[redes];

    for (int i = 0; i < redes; i++) {  //Recorre las redes
      //if (WiFi.RSSI(i) > min_rssi) {   //Imprime solo las redes con señales fuertes
        wifi_array[i]=WiFi.SSID(i)+";"+WiFi.RSSI(i);
      //}
    }
    for (int i;i<redes;i++){
      Serial.println(wifi_array[i]);
    }
  }
}

void wifiConnect(const char* ssid, const char* pass) {  //Toma como parametros el nombre y la contraseña de la red
  int intentos = 0;
  wifiDisconnect(); //Se desconecta si ya esta conectado

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
void wifiDisconnect(){  //Se va a usar para desconectarse por orden de la app.
  if (WiFi.status()==WL_CONNECTED){
    Serial.print("Desconectando de la red: ");
    Serial.println(WiFi.SSID());
    WiFi.disconnect();
    delay(200);
  }

}

void statusPOST(const char* IP, const bool valor) {  //Recibe la IP y el valor de la alerta
  HTTPClient http;                                  //Iniciamos el objeto

  Serial.println("Conectando al servidor...");
  http.begin(IP);  //Conectar al servidor

  Serial.println("Haciendo POST");

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  //El solo post envia el texto, content-type= plain text
  //Especificamos en el header que el content-type = form
  //Form lo lee clave:valor
  //Esto facilida su almacenamiento.

  String alerta = "status=" + String(valor);
  //http.POST() solo recibe string, asi que los concatenamos

  int httpCode = http.POST(alerta);
  //Hace la petición y almacena el codigio de respues http
  //alert es el nombre de la columna de la tabla de mySQL con tipo de variable booleano (1 o 0)

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

void statusGET(const char* IP) {  //Solo recibe la IP
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
  delay(500);

  WiFi.mode(WIFI_STA);
  //wifiScan();
  //delay(500);

  //wifiConnect("A56Prueba", "11223345");  //Esto es para porbar que se desconecte de la red inicial
  //delay(500);

  //wifiConnect("TATAN_ARDILA", "91011814");
  //delay(500);

  //statusPOST("https://teapp.lat/recibir/recibir.php", 1);
  //statusGET("https://teapp.lat/recibir/recibir.php");

  //delay(200);

  //statusPOST("https://teapp.lat/recibir/recibir.php", 0);
  //statusGET("https://teapp.lat/recibir/recibir.php");

  wifi_credentials();
}


void loop() {
}
