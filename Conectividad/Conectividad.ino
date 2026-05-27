#include <WiFi.h>

void wifiScan(){
  const int min_rssi = -80; //Esto ignorenlo
  byte redes = WiFi.scanNetworks();//Cantidad de redes encontradas
  if (redes == 0){
    Serial.println("No se encontraron redes.");
  }
  else {
    Serial.print(redes);
    Serial.println(" Redes encontradas");

    for (int i =0; i<redes;i++){//Imprime las redes
      Serial.print(i+1);
      Serial.print(" Red: ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" Señal: ");
      Serial.println(WiFi.RSSI(i));
      
    }
  }
}


void wifiConnect(){
  char* ssid="TATAN_ARDILA";
  char* pass="91011814";
  int intentos = 0;
  if (WiFi.status()==WL_CONNECTED){
    Serial.println("Desconectando de la Red actual");
    WiFi.disconnect();
    delay(500);
  }

  Serial.println("Conectando a la red...");
  WiFi.begin(ssid,pass);//COnectarse

  while (WiFi.status()!=WL_CONNECTED && intentos<20){/
    Serial.println(".");
    delay(500);
    intentos++;
  }
  
  if (WiFi.status()==WL_CONNECTED){
    Serial.println("Conectado");
    Serial.println(WiFi.SSID());
    Serial.println(WiFi.localIP());
  }
  else{
    Serial.println("Error al conectar.")
  }
  
}



void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  wifiScan();
  delay(500);

  wifiConnect();

}

void loop() {
  

}
