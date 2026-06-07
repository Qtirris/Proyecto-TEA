#include <WiFi.h>
#include <HTTPClient.h>

void wifiScan() {
  const int min_rssi = -80;  //Limite de señal
  Serial.println("Buscando Redes...");
  byte redes = WiFi.scanNetworks();  //Cantidad de redes encontradas
  if (redes == 0) {
    Serial.println("No se encontraron redes.");
  } else {
    Serial.print(redes);
    Serial.println(" Redes encontradas");

    for (int i = 0; i < redes; i++) {  //Recorre las redes
      if (WiFi.RSSI(i) > min_rssi) {   //Imprime solo las redes con señales fuertes
        Serial.print(i + 1);
        Serial.print(" Red: ");
        Serial.print(WiFi.SSID(i));
        Serial.print(" Señal: ");
        Serial.println(WiFi.RSSI(i));
      }
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

void alertPOST(const char* IP, const bool valor) {  //Recibe la IP y el valor de la alerta
  HTTPClient http;                                  //Iniciamos el objeto

  Serial.println("Conectando al servidor...");
  http.begin(IP);  //Conectar al servidor

  Serial.println("Haciendo POST");

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  //El solo post envia el texto, content-type= plain text
  //Especificamos en el header que el content-type = form
  //Form lo lee clave:valor
  //Esto facilida su almacenamiento.

  String alerta = "alert=" + String(valor);
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

void alertGET(const char* IP) {  //Solo recibe la IP
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
  wifiScan();
  delay(500);

  wifiConnect("A56Prueba", "11223345");  //Esto es para porbar que se desconecte de la red inicial
  delay(500);

  wifiConnect("TATAN_ARDILA", "91011814");
  delay(500);

  alertPOST("http://192.168.20.143/estres/recibir.php", 1);
  alertGET("http://192.168.20.143/estres/recibir.php");

  delay(200);

  alertPOST("http://192.168.20.143/estres/recibir.php", 0);
  alertGET("http://192.168.20.143/estres/recibir.php");
}


void loop() {
}
