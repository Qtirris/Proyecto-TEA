#include <Wire.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "MAX30105.h"           
#include "heartRate.h"          

MAX30105 particleSensor;

// Configuración de Pines
const int PIN_LED = 10; 

// Variables de pulso
unsigned long tiempoUltimoLatido = 0;
long ibi = 0; 
float bpm = 0;
unsigned long numeroLatido = 0;

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
          numeroLatido = 0;
          Serial.println("Contador de latidos reiniciado por Bluetooth.");
        }
      }
    }
};

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW); 

  while (!Serial) { ; }
  Serial.println("\n--- Iniciando Sistema Transmisor BLE Completo ---");

  // 1. Inicializar Bus I2C para el sensor de pulso (Estable a 100kHz para el C3)
  Wire.begin(2, 3); 
  Wire.setClock(100000); 

  if (!particleSensor.begin(Wire)) { 
    Serial.println("ERROR: No se encontró el MAX30102.");
    while (1) {
      digitalWrite(PIN_LED, HIGH); delay(100);
      digitalWrite(PIN_LED, LOW);  delay(100);
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
  pCharacteristicTX = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
  pCharacteristicTX->addDescriptor(new BLE2902());

  // Crear la característica RX (Recepción desde el celular)
  pCharacteristicRX = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_RX,
                        BLECharacteristic::PROPERTY_WRITE
                      );
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
    digitalWrite(PIN_LED, LOW);
    return; 
  }

  // Si detecta un latido
  if (checkForBeat(irValue) == true) {
    digitalWrite(PIN_LED, HIGH); // Feedback visual inmediato

    unsigned long tiempoActual = millis();
    ibi = tiempoActual - tiempoUltimoLatido;
    tiempoUltimoLatido = tiempoActual;

    // Filtro básico para descartar ruido (Frecuencias cardíacas entre 46 y 150 BPM)
    if (ibi > 400 && ibi < 1300) {
      bpm = 60000.0 / ibi; 
      numeroLatido++;

      // Mostrar en Monitor Serie de la PC
      Serial.print("❤️ Latido #"); Serial.print(numeroLatido);
      Serial.print(" | IBI: "); Serial.print(ibi); Serial.print(" ms");
      Serial.print(" | BPM: "); Serial.println(bpm, 1);

      // --- ENVIAR AL CELULAR POR BLE ---
      if (dispositivoConectado) {
        String datosEnviar = String(numeroLatido) + "," + String(ibi) + "," + String(bpm, 1) + "\n";
        
        pCharacteristicTX->setValue((uint8_t*)datosEnviar.c_str(), datosEnviar.length());
        pCharacteristicTX->notify(); 
      }
    }

    delay(50); 
    digitalWrite(PIN_LED, LOW);
  }
}