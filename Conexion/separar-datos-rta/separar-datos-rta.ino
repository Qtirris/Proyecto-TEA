void setup() {
  Serial.begin(115200);
  delay(500);
  String micadena = "0 2026-06-02 21:02:32";
  const char* micadena2 = micadena.c_str();
  int longitud = strlen(micadena2);
  int contador = 0;
  char value[1] = {};
  char dia[10] = {};
  char hora[8] = {};
  char* alert_info[3] = { value, dia, hora };
  for (int i = 0; i < longitud; i++) {
    if (micadena2[i] == ' ') {
      contador++;
    } else if (contador == 0) {
      value[0] = micadena2[i];
    } else if (contador == 1) {
      dia[i - 2] = micadena2[i];
    } else {
      hora[i - 13] = micadena2[i];
    }
  }

  Serial.println(value[0]);

  for (int j =0; j<10;j++){
    Serial.print(dia[j]);
  }

  Serial.println(" ");
  for (int j =0; j<8;j++){
    Serial.print(hora[j]);
  }


}

void loop() {
}
