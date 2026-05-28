#include <Wire.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "MAX30105.h"           
#include "heartRate.h"          

MAX30105 Sensor_Cardiaco;
// Configuración de Pines
<<<<<<< HEAD
int Led_Verde = 5; 
int Led_Rojo = 4;
//Variables de promedio
int Recopilador_HVR[180];
=======
int Led_Verde = 6; 
int Led_Rojo = 8;
//Variables de promedio
int Recopilador_HVR[180];
int Contador_HVR = 0;
>>>>>>> test
// Variables de pulso
unsigned long Tempo_Ultimo_Latido = 0;
long HVR = 0; 
float BPM = 0;
unsigned long Numero_Latido = 0;
<<<<<<< HEAD
bool Esta_Dormido = false
=======
bool Esta_Dormido = false;
>>>>>>> test
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
        
        // Si se envia la letra 'r' desde la app del celular, reinicia el contador
        if(rxValue[0] == 'r') {
          Numero_Latido = 0;
          Serial.println("Contador de latidos reiniciado por Bluetooth.");
        }
      }
    }
};
//Inicializacion de variables
void setup() {
  Serial.begin(115200);
  //Modo de los Pines
  pinMode(Led_Verde, OUTPUT);
  pinMode(Led_Rojo, OUTPUT);
<<<<<<< HEAD
  Wire.begin(2, 3); 
=======
  Wire.begin(1, 0); //SDA, SLC
>>>>>>> test
  //Apagar todo
  digitalWrite(Led_Verde, LOW); 
  digitalWrite(Led_Rojo, LOW); 

  while (!Serial) { ; }
  Serial.println("\n--- Iniciando Sistema Transmisor BLE Completo ---");
  // 1. Inicializar Bus I2C para el sensor de pulso (Estable a 100kHz para el C3)
  // Si no se detecta, Parpadea el Led_Rojo
  Wire.setClock(100000); 

  if (!Sensor_Cardiaco.begin(Wire)) { 
    Serial.println("ERROR: No se encontró el MAX30102.");
    while (1) {
      digitalWrite(Led_Rojo, HIGH); delay(100);
      digitalWrite(Led_Rojo, LOW);  delay(100);
    }
  }
  Serial.println("-> Sensor MAX30102 detectado.");
  Sensor_Cardiaco.setup(); 
  Sensor_Cardiaco.setPulseAmplitudeRed(0x0A); 
  Sensor_Cardiaco.setPulseAmplitudeIR(0x1F);  
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
  long Valor_Presencia = Sensor_Cardiaco.getIR();
  // Si no hay dedo puesto en el sensor
  if (Valor_Presencia < 50000) {
    digitalWrite(Led_Rojo, HIGH);
    return; 
  }else {
    digitalWrite(Led_Rojo, LOW);
  }

  // Si detecta un latido
  if (checkForBeat(Valor_Presencia) == true) {
    digitalWrite(Led_Verde, HIGH); 
    digitalWrite(Led_Rojo, LOW);

    unsigned long Tiempo_Actual = millis();
    HVR = Tiempo_Actual - Tempo_Ultimo_Latido;
    Tempo_Ultimo_Latido = Tiempo_Actual;

    // Filtro básico para descartar ruido (Frecuencias cardíacas entre 46 y 150 BPM)
    if (HVR > 400 && HVR < 1300) {
      BPM = 60000.0 / HVR; 
      Numero_Latido++;

      // Mostrar en Monitor Serie de la PC
      Serial.print("<3 Latido #"); 
      Serial.print(Numero_Latido);
      Serial.print(" | HVR: "); 
      Serial.print(HVR); 
      Serial.print("ms");
      Serial.print(" | BPM: "); 
      Serial.println(BPM, 1);
<<<<<<< HEAD

=======
      // Agregar a las listas
      if (Contador_HVR < 180) {
        Recopilador_HVR[Numero_Latido-1] = HVR;
        Contador_HVR++;
      }

      if (Contador_HVR % 10 == 0) {
        for (int i = 0; i < Contador_HVR; i++) {
          Serial.print("|");
          Serial.print(Recopilador_HVR[i]);
          Serial.print("|");    
        }
        Serial.println("<----- Lista HVR");
      }

>>>>>>> test
      //Enviar al Celular
      if (dispositivoConectado) {
        String datosEnviar = String(Numero_Latido) + "||" + String(HVR) + "||" + String(BPM, 1) + "\n";
        
        pCharacteristicTX->setValue((uint8_t*)datosEnviar.c_str(), datosEnviar.length());
        pCharacteristicTX->notify(); 
      }
    }
<<<<<<< HEAD
    digitalWrite(Led_Verde, LOW);
    digitalWrite(Led_Rojo, HIGH);
  }
=======
  }
  else{
    digitalWrite(Led_Verde, LOW);
  }
>>>>>>> test
}
