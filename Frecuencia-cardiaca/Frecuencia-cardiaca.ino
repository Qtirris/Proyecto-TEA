#include <Wire.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "MAX30105.h"           
#include "heartRate.h"          
#include <vector>

MAX30105 Sensor_Cardiaco;

// Configuración de Pines
int Led_Verde = 6; 
int Led_Rojo = 8;

// Variables de datos
bool Datos_De_Hoy = false;

// Variables de HVR
long HVR = 0; 
int Recopilador_HVR[180];
float Diferencia_HVR[179];
int Contador_HVR = 0;
float Promedio_HVR = 0;
float Promedio_Basal_HVR = 0;
std::vector<int> Capturar_HVR;
int Nivel_Superior_HVR = 20;

// Variables del estres
float Nivel_Estres = 0;
float Promedio_HVR_Estres = 0;
int Contador_Estres = 0;
int Reset_Contador_Estres = 0;
bool Alerta_Estres = false;

// Variables de pulso
int Recopilador_BPM[180];
unsigned long Tempo_Ultimo_Latido = 0;
float BPM = 0;
unsigned long Numero_Latido = 0;
float Promedio_BPM = 0;
float Promedio_Basal_BPM = 0;
float Promedio_BPM_Alerta = 0;
int Contador_BPM = 0;
std::vector<int> Capturar_BPM;
bool Alerta_Cardiaca = false;
int Contador_BMP_Alerta = 0;
int Reset_Contador_BMP_Alerta = 0;

// Variables de deteccion del sueño
int Picos_De_Frecuencia = 0;
std::vector<int> Recopilador_Cardiaco;
const unsigned long Tiempo_Esperando_Sueño = 900000;
const unsigned long Tiempo_Capturando_Sueño = 300000;
unsigned long Tiempo_Comparacion_Sueño = 0;
bool Esta_Dormido = false;
enum Estado_Datos_Dormido {
  Esperando_15_Min,
  Capturando_5_Min
};
Estado_Datos_Dormido Estado_Dato_Sueño = Capturando_5_Min;
bool Desactivar_Promedio_Sueño = true;
float Promedio_Cardiaco_Sueño = 0;

// Configuración de BLE
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristicTX = NULL; 
BLECharacteristic *pCharacteristicRX = NULL; 
bool Dispositivo_Conectado = false;

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" 

class MisCallbacksServidor: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      Dispositivo_Conectado = true;
    };
    void onDisconnect(BLEServer* pServer) {
      Dispositivo_Conectado = false;
      pServer->startAdvertising(); 
    }
};

class MisCallbacksRX: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue(); 
      if (rxValue.length() > 0) {
        Serial.print("Dato recibido desde el celular: ");
        Serial.println(rxValue); 
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
  pinMode(Led_Rojo, OUTPUT);
  
  Wire.begin(1, 0); // SDA, SCL
  digitalWrite(Led_Verde, LOW); 
  digitalWrite(Led_Rojo, LOW); 

  while (!Serial) { ; }
  Serial.println("\n--- Iniciando Sistema Transmisor BLE Completo ---");
  
  Wire.setClock(100000); 

  if (!Sensor_Cardiaco.begin(Wire)) { 
    Serial.println("ERROR: No se encontró el MAX30102.");
    while (1) {
      digitalWrite(Led_Rojo, HIGH); delay(100);
      digitalWrite(Led_Rojo, LOW);  delay(100);
    }
  }
  Serial.println("-> Sensor MAX30102 detectado.");

  //CONFIGURACIÓN OPTIMIZADA PARA MUÑECA (BPM + HRV)
  byte Brillo = 100; // Brillo elevado para penetrar tejido de la muñeca
  byte Lecturas = 1;   // Sin promediar lecturas para capturar picos reales de HRV
  byte Modo = 2;         // Modo 2: Rojo + IR
  int Velocidad_Muestreo = 400;     // Muestreo rápido a 400 Hz para precisión temporal
  int Ancho_Pulso = 411;     // Ancho de pulso máximo (18 bits de resolución del ADC)
  int Rango_ADC = 8192;      // Rango del ADC extendido para mitigar saturaciones

  Sensor_Cardiaco.setup(Brillo, Lecturas, Modo, Velocidad_Muestreo, Ancho_Pulso, Rango_ADC); 
  Serial.println("-> Registro de variables para muñeca configurado.");

  // Inicializar Bluetooth BLE
  BLEDevice::init("ESP32-C3_HRV"); 
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MisCallbacksServidor());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristicTX = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristicTX->addDescriptor(new BLE2902());
  pCharacteristicRX = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pCharacteristicRX->setCallbacks(new MisCallbacksRX()); 
  
  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("-> Bluetooth BLE (TX/RX) activo. Esperando conexión segura...");
}

void loop() {
  // Comprobar constantemente si existen nuevas muestras acumuladas en el buffer de la librería
  Sensor_Cardiaco.check(); 
  
  long Valor_Presencia = Sensor_Cardiaco.getIR();

  // Si no hay muñeca colocada en el sensor (Lectura baja)
  if (Valor_Presencia < 50000) {
    digitalWrite(Led_Rojo, HIGH);
    return; 
  } else {
    digitalWrite(Led_Rojo, LOW);
  }

  // Verificar si la lectura corresponde al pico de un latido cardíaco
  if (checkForBeat(Valor_Presencia) == true) {
    digitalWrite(Led_Verde, HIGH); 
    digitalWrite(Led_Rojo, LOW);

    unsigned long Tiempo_Actual = millis();
    HVR = Tiempo_Actual - Tempo_Ultimo_Latido;
    Tempo_Ultimo_Latido = Tiempo_Actual;
    unsigned long Tiempo_Actual_Sueño = millis();

    // Filtro básico para descartar ruido de lectura (Pulsos entre 46 y 150 BPM)
    if (HVR > 400 && HVR < 1300) {
      BPM = 60000.0 / HVR; 
      Numero_Latido++;

      // Mostrar lecturas en Monitor Serie
      Serial.print("<3 Latido #"); 
      Serial.print(Numero_Latido);
      Serial.print(" | HVR: "); 
      Serial.print(HVR); 
      Serial.print("ms");
      Serial.print(" | BPM: "); 
      Serial.println(BPM, 1);

      // Agregar a las listas de almacenamiento diario si está durmiendo
      if (Contador_HVR < 180 && Esta_Dormido == true) {
        Recopilador_HVR[Contador_HVR] = HVR;
        Contador_HVR++;
      }
      if (Contador_BPM < 180 && Esta_Dormido == true) {
        Recopilador_BPM[Contador_BPM] = BPM;
        Contador_BPM++;
      }

      // Procesamiento de HRV y algoritmos de estrés
      if (Promedio_Basal_HVR != 0) {
        if (Capturar_HVR.size() < 11) { 
          Capturar_HVR.push_back(HVR);
        } else {
          int Picos_HVR = 0;
          for (int i = 0; i < Capturar_HVR.size() - 1; i++) {
            Capturar_HVR[i] = Capturar_HVR[i] - Capturar_HVR[i+1];
            Capturar_HVR[i] = Capturar_HVR[i] * Capturar_HVR[i];
            if (!(sqrt(Capturar_HVR[i] > Promedio_Basal_HVR + Nivel_Superior_HVR))) {
              Capturar_HVR[i] = sqrt(Capturar_HVR[i]);
              Promedio_HVR_Estres += Capturar_HVR[i];
            } else {
              Picos_HVR++;
            }
          }
          Reset_Contador_Estres++;
          if((Capturar_HVR.size() - Picos_HVR) > 0) {
            Promedio_HVR_Estres /= (Capturar_HVR.size() - Picos_HVR);
          }
          Capturar_HVR.clear();
        }

        int Promediar_Estres = Promedio_HVR_Estres - Promedio_Basal_HVR;
        Promediar_Estres *= Promediar_Estres;
        Promediar_Estres = sqrt(Promediar_Estres);
        if((Promediar_Estres/Promedio_Basal_HVR)*100 <= 15) {
          Serial.println("<---- Estado HVR Tranquilo");
        }
        else if ((Promediar_Estres/Promedio_Basal_HVR)*100 <= 30) {
          Serial.println("<---- Estado HVR Intranquilo");
        }
        else if ((Promediar_Estres/Promedio_Basal_HVR)*100 > 30) {
          Serial.println("<---- Estado HVR Alerta");
          String Envira_Datos = "<---- Estado HVR Alerta\n";
          pCharacteristicTX->setValue((uint8_t*)Envira_Datos.c_str(), Envira_Datos.length());
          pCharacteristicTX->notify(); 
          Contador_Estres++;
        }
      }

      // Procesamiento de variaciones rápidas de BPM
      if (Promedio_Basal_BPM != 0){
        if (Capturar_BPM.size() < 11) { 
          Capturar_BPM.push_back(BPM);
        } else {
          int Picos_BPM = 0;
          for (int i = 1; i < Capturar_BPM.size(); i++) {
            if (sqrt((Capturar_BPM[i] - Capturar_BPM[i-1])*(Capturar_BPM[i] - Capturar_BPM[i-1])) > 40) {
              Picos_BPM++;
            } else {
              Promedio_BPM_Alerta += Capturar_BPM[i];
            } 
          }
          Reset_Contador_BMP_Alerta++;
          if((Capturar_BPM.size() - Picos_BPM) > 0) {
            Promedio_BPM_Alerta /= (Capturar_BPM.size() - Picos_BPM);
          }
        }

        if (Promedio_BPM_Alerta - Promedio_Basal_BPM <= 8) {
          Serial.println("<---- Estado BMP Tranquilo");
        }
        else if (Promedio_BPM_Alerta - Promedio_Basal_BPM <= 20) {
          Serial.println("<---- Estado BMP Intranquilo");
        }
        else if (Promedio_BPM_Alerta - Promedio_Basal_BPM > 20) {
          Serial.println("<---- Estado BMP Alerta");
          String Envira_Datos = "<---- Estado BMP Alerta\n";
          pCharacteristicTX->setValue((uint8_t*)Envira_Datos.c_str(), Envira_Datos.length());
          pCharacteristicTX->notify(); 
          Contador_BMP_Alerta++;
        }
      }

      // Máquina de estados periódica para el análisis del Sueño
      switch (Estado_Dato_Sueño) {
        case Esperando_15_Min:
          if(Tiempo_Actual_Sueño - Tiempo_Comparacion_Sueño >= Tiempo_Esperando_Sueño){
            Recopilador_Cardiaco.clear();
            Estado_Dato_Sueño = Capturando_5_Min;
            Tiempo_Comparacion_Sueño = Tiempo_Actual;
          }
        break;
        case Capturando_5_Min:
          if(Tiempo_Actual_Sueño - Tiempo_Esperando_Sueño >= Tiempo_Capturando_Sueño) {
            Estado_Dato_Sueño = Esperando_15_Min;
            Tiempo_Comparacion_Sueño = Tiempo_Actual;
            Desactivar_Promedio_Sueño = false;
          }
          Recopilador_Cardiaco.push_back(BPM);
        break;
      }

      // Transmisión inmediata vía BLE en tiempo real
      if (Dispositivo_Conectado) {
        String Envira_Datos = String(Numero_Latido) + "||" + String(HVR) + "||" + String(BPM, 1) + "\n";
        pCharacteristicTX->setValue((uint8_t*)Envira_Datos.c_str(), Envira_Datos.length());
        pCharacteristicTX->notify(); 
      }
    }

    // Reporte diario de promedios HVR
    if (Contador_HVR >= 180 && Datos_De_Hoy == false){
        for (int i = 0; i < Contador_HVR-1; i++) {
          Diferencia_HVR[i] = Recopilador_HVR[i] - Recopilador_HVR[i+1];
          Diferencia_HVR[i] = Diferencia_HVR[i] * Diferencia_HVR[i];
          Diferencia_HVR[i] = sqrt(Diferencia_HVR[i]);
          Promedio_HVR += Diferencia_HVR[i];
        }
        Promedio_HVR /= Contador_HVR-1;
        Contador_HVR = 0;
        String Envira_Datos = "<---- Promedio_HVR: " + String(Promedio_HVR)+"\n";
        pCharacteristicTX->setValue((uint8_t*)Envira_Datos.c_str(), Envira_Datos.length());
        pCharacteristicTX->notify(); 
    }

    // Reporte diario de promedios cardíacos generales
    if (Contador_BPM >= 180 && Datos_De_Hoy == false){
        for (int i = 0; i < Contador_BPM; i++) { 
          Promedio_BPM += Recopilador_BPM[i];
        }
        Promedio_BPM /= Contador_BPM;
        Contador_BPM = 0;
        String Envira_Datos = "<---- Promedio_BPM: " + String(Promedio_BPM)+"\n";
        pCharacteristicTX->setValue((uint8_t*)Envira_Datos.c_str(), Envira_Datos.length());
        pCharacteristicTX->notify(); 
    }

    // Procesamiento de patrones del sueño descartando variaciones agudas
    if (Estado_Dato_Sueño == Esperando_15_Min && Desactivar_Promedio_Sueño == false) {
      for (size_t i = 1; i < Recopilador_Cardiaco.size(); i++) {
        float Ignorar_Picos = Recopilador_Cardiaco[i] - Recopilador_Cardiaco[i-1];
        Ignorar_Picos = Ignorar_Picos * Ignorar_Picos;
        Ignorar_Picos = sqrt(Ignorar_Picos);
        if(Ignorar_Picos > 15){
          Picos_De_Frecuencia++;
        } else {
          Promedio_Cardiaco_Sueño += Recopilador_Cardiaco[i];
        }
      }
      if((Recopilador_Cardiaco.size() - Picos_De_Frecuencia) > 0) {
        Promedio_Cardiaco_Sueño /= (Recopilador_Cardiaco.size() - Picos_De_Frecuencia);
      }
      Serial.print(Promedio_Cardiaco_Sueño);
      Serial.println("<------- Promedio Cardiaco Sueño");
      Desactivar_Promedio_Sueño = true;
    }

    // Resetear contadores cíclicos de eventos de estrés y alertas
    if (Reset_Contador_Estres > 18){
      Contador_Estres = 0;
      Reset_Contador_Estres = 0;
    }
    if (Reset_Contador_BMP_Alerta > 18){
      Contador_BMP_Alerta = 0;
      Reset_Contador_BMP_Alerta = 0;
    }
    
    // Conmutación lógica de banderas globales de alertas
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
