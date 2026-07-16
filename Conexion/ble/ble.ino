#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#define SERVICE_UUID "3373E991-457D-4656-9544-28DE1576896D"
#define WIFI_CHAR_UUID "DFDE7591-38A4-4019-A6A1-C09B4D0FCE70"
#define PASS_CHAR_UUID "FB4E9190-0D85-4810-A2A0-124BFD25A1AA"

bool client_connected = false;
BLEServer *pServer = nullptr;
BLEService *pService = nullptr;
BLECharacteristic *pWifiChar = nullptr;
BLECharacteristic *pPassChar = nullptr;

class Server_Callback : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    Serial.println("Cliente conectado.");
    client_connected = true;
  }
  void onDisconnect(BLEServer *pServer) {
    Serial.println("Cliente desconectado.");
    client_connected = false;
    pServer->startAdvertising();
  }
};

class Char_Callback : public BLECharacteristicCallbacks {
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

void wifi_credentials() {
  bool server_status = BLEDevice::init("ESP32 TEA");

  if (!server_status) {
    Serial.println("Error al establecer el servidor.");
    return;
  }
  Serial.println("BLE Correcto.");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new Server_Callback());

  pService = pServer->createService(SERVICE_UUID);

  pWifiChar = pService->createCharacteristic(WIFI_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pPassChar = pService->createCharacteristic(PASS_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);

  pWifiChar->setCallbacks(new Char_Callback());
  pPassChar->setCallbacks(new Char_Callback());

  pWifiChar->setValue("PRUEBA");
  pPassChar->setValue("");

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);

  BLEDevice::startAdvertising();
}

void setup() {
  Serial.begin(115200);
  wifi_credentials();
}

void loop() {
  // put your main code here, to run repeatedly:
}
