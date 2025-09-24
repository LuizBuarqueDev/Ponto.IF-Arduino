#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

#define SENSOR_RX_PIN 2
#define SENSOR_TX_PIN 3

SoftwareSerial mySerial(SENSOR_RX_PIN, SENSOR_TX_PIN);
Adafruit_Fingerprint finger(&mySerial);

void setup() {
  Serial.begin(115200);
  mySerial.begin(57600);
  finger.begin(57600);

  Serial.println("\n[TESTE LOOP IDENTIFICAR SENSOR]");
}

void loop() {
  Serial.println("Tentando identificar sensor...");

  if (finger.verifyPassword()) {
    Serial.println("✅ Sensor encontrado!");

    if (finger.getParameters() == FINGERPRINT_OK) {
      Serial.println("Parâmetros do sensor:");
      Serial.print(" - System ID: "); Serial.println(finger.system_id, HEX);
      Serial.print(" - Capacidade: "); Serial.println(finger.capacity);
      Serial.print(" - Security Level: "); Serial.println(finger.security_level);
      Serial.print(" - Packet Length: "); Serial.println(finger.packet_len);
      Serial.print(" - Baud Rate: "); Serial.println(finger.baud_rate);
    } else {
      Serial.println("⚠️  Não consegui ler os parâmetros do sensor.");
    }

    // uma vez identificado, trava aqui
    while (1) {
      delay(1000);
    }
  } else {
    Serial.println("❌ Sensor não encontrado. Verifique conexões e alimentação.");
  }

  delay(2000); // espera 2 segundos antes de tentar de novo
}
