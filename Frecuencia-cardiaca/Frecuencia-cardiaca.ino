#include <Wire.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "MAX30105.h"           
#include "heartRate.h"          

MAX30105 particleSensor;

// Configuración de Pines
int Led_Verde = 5; 
int Led_Rojo = 4;
int Recopilador_HVR[180];
// Variables de pulso
unsigned long Tempo_Ultimo_Latido = 0;
long HVR = 0; 
float BPM = 0;
unsigned long Numero_Latido = 0;

// Configuración de BLE
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristicTX = NULL; 
BLECharacteristic *pCharacteristicRX = NULL; 
bool dispositivoConectado = false;

// UUIDs estándar del servicio UART de Nordic (Compatibles con la App)
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" 

// Callbacks para detectar conexión y desconexión del celular
class MisCallbacksServidor: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      dispositivoConectado = true;
    };
    void onDisconnect(BLEServer* pServer) {
      dispositivoConectado = false;
      pServer->startAdvertising(); // Reiniciar publicidad para permitir reconexión
    }
};

// Callbacks para recibir comandos desde el celular (Corregido para nuevas versiones)
class MisCallbacksRX: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue(); 

      if (rxValue.length() > 0) {
        Serial.print("Dato recibido desde el celular: ");
        Serial.println(rxValue); 
        
        // Si envías la letra 'r' desde la app del celular, reinicia el contador
        if(rxValue[0] == 'r') {
          Numero_Latido = 0;
          Serial.println("Contador de latidos reiniciado por Bluetooth.");
        }
      }
    }
};

void setup() {
  Serial.begin(115200);
  
  pinMode(Led_Verde, OUTPUT);
  digitalWrite(Led_Verde, LOW); 
  pinMode(Led_Rojo, OUTPUT);
  digitalWrite(Led_Rojo, LOW); 

  while (!Serial) { ; }
  Serial.println("\n--- Iniciando Sistema Transmisor BLE Completo ---");

  // 1. Inicializar Bus I2C para el sensor de pulso (Estable a 100kHz para el C3)
  // Si no se detecta, Parpadea el Led_Rojo
  Wire.begin(2, 3); 
  Wire.setClock(100000); 

  if (!particleSensor.begin(Wire)) { 
    Serial.println("ERROR: No se encontró el MAX30102.");
    while (1) {
      digitalWrite(Led_Rojo, HIGH); delay(100);
      digitalWrite(Led_Rojo, LOW);  delay(100);
    }
  }
  Serial.println("-> Sensor MAX30102 detectado.");
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x0A); 
  particleSensor.setPulseAmplitudeIR(0x1F);  

  // 2. Inicializar Bluetooth BLE
  BLEDevice::init("ESP32-C3_HRV"); 
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MisCallbacksServidor());

  // Crear el servicio UART
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Crear la característica TX (Transmisión hacia el celular)
  pCharacteristicTX = pService->createCharacteristic(CHARACTERISTIC_UUID_TX,BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristicTX->addDescriptor(new BLE2902());

  // Crear la característica RX (Recepción desde el celular)
  pCharacteristicRX = pService->createCharacteristic(CHARACTERISTIC_UUID_RX,BLECharacteristic::PROPERTY_WRITE);
  pCharacteristicRX->setCallbacks(new MisCallbacksRX()); 

  // Arrancar el servicio y empezar a transmitir señal de presencia
  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("-> Bluetooth BLE (TX/RX) activo. Esperando conexión segura...");
}

void loop() {
  long irValue = particleSensor.getIR();

  // Si no hay dedo puesto en el sensor
  if (irValue < 50000) {
    digitalWrite(Led_Verde, LOW);
    return; 
  }

  // Si detecta un latido
  if (checkForBeat(irValue) == true) {
    digitalWrite(Led_Verde, HIGH); // Feedback visual inmediato

    unsigned long Tiempo_Actual = millis();
    HVR = Tiempo_Actual - Tempo_Ultimo_Latido;
    Tempo_Ultimo_Latido = Tiempo_Actual;

    // Filtro básico para descartar ruido (Frecuencias cardíacas entre 46 y 150 BPM)
    if (HVR > 400 && HVR < 1300) {
      BPM = 60000.0 / HVR; 
      Numero_Latido++;

      // Mostrar en Monitor Serie de la PC
      Serial.print("<3 Latido #"); Serial.print(Numero_Latido);
      Serial.print(" | HVR: "); Serial.print(HVR); Serial.print(" ms");
      Serial.print(" | BPM: "); Serial.println(BPM, 1);

      // --- ENVIAR AL CELULAR POR BLE ---
      if (dispositivoConectado) {
        String datosEnviar = String(Numero_Latido) + "," + String(HVR) + "," + String(BPM, 1) + "\n";
        
        pCharacteristicTX->setValue((uint8_t*)datosEnviar.c_str(), datosEnviar.length());
        pCharacteristicTX->notify(); 
      }
    }
    digitalWrite(Led_Verde, LOW);
  }
}
