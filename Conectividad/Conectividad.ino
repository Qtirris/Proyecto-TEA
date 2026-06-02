#include <WiFi.h>
#include <HTTPClient.h>

void wifiScan(){
  const int min_rssi = -80; //Limite de señal
  Serial.println("Buscando Redes...");
  byte redes = WiFi.scanNetworks();//Cantidad de redes encontradas
  if (redes == 0){
    Serial.println("No se encontraron redes.");
  }
  else {
    Serial.print(redes);
    Serial.println(" Redes encontradas");

    for (int i =0; i<redes;i++){//Recorre las redes
      if (WiFi.RSSI(i)>min_rssi){//Imprime solo las redes con señales fuertes
        Serial.print(i+1);
        Serial.print(" Red: ");
        Serial.print(WiFi.SSID(i));
        Serial.print(" Señal: ");
        Serial.println(WiFi.RSSI(i));
      }
    }
  }
}

void wifiConnect(const char* ssid, const char* pass){ //Toma como parametros el nombre y la contraseña de la red
  int intentos = 0;
  if (WiFi.status()==WL_CONNECTED){//Se desconecta en caso de estar conectada
    Serial.println("Desconectando de la Red actual");
    WiFi.disconnect();
    delay(500);
  }

  Serial.print("Conectando a la red: ");
  Serial.println(ssid);

  WiFi.begin(ssid,pass);//Conectarse

  while (WiFi.status()!=WL_CONNECTED && intentos<20){
    Serial.println(".");
    delay(500);
    intentos++;
  }
  
  if (WiFi.status()==WL_CONNECTED){
    Serial.println("Conectado");
    Serial.println(WiFi.SSID());//Imprime la red
    Serial.println(WiFi.localIP());//Imprime la IP local de la ESP
  }
  else{
    Serial.println("Error al conectar.");
    Serial.println("La conexión tardo demasiado, vefifique la contraseña e intentelo nuevmente");
    WiFi.disconnect();
  }
  
}
  
void HTTPinit(){
  HTTPClient http;//Iniciamos el objeto

  Serial.println("Conectando al servidor...");
  http.begin("http://192.168.20.143/estres/recibir.php");//Conectar al servidor

  Serial.println("Haciendo POST");

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  //El solo post envia el texto, content-type= plain text
  //Especificamos en el header que el content-type = form
  //Form lo lee clave:valor
  //Esto facilida su almacenamiento.

  int httpCode = http.POST("alert=1");
  //Hace la petición y almacena el codigio de respues http

  if (httpCode > 0) {//Evita los errores de HTTPClient
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
void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  wifiScan();
  delay(500);

  wifiConnect("A56Prueba","11223345");//Esto es para porbar que se desconecte de la red inicial
  delay(500);

  wifiConnect("TATAN_ARDILA","91011814");
  delay(500);

  HTTPinit();
}

void loop() {
  

}
