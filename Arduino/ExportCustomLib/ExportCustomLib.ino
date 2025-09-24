#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

SoftwareSerial mySerial(2, 3);  // RX=2, TX=3
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

void setup() {
  Serial.begin(115200);
  while (!Serial);  // Aguarda serial
  Serial.println("Extrator de Templates de Digitais - Backup");

  finger.begin(57600);  // Baud rate do sensor

  if (finger.verifyPassword()) {
    Serial.println("Sensor encontrado!");
  } else {
    Serial.println("Sensor nao encontrado :(");
    while (1) { delay(1); }
  }

  // Extrai templates de ID 1 a 200
  for (uint16_t id = 1; id <= 200; id++) {
    downloadFingerprintTemplate(id);
  }
  Serial.println("Backup completo! Copie os dados do serial para um arquivo .bin");
}

void loop() {}  // Não precisa de loop

uint8_t downloadFingerprintTemplate(uint16_t id) {
  Serial.println("------------------------------------");
  Serial.print("Tentando carregar ID #"); Serial.println(id);

  uint8_t p = finger.loadModel(id);  // Carrega o modelo do ID
  if (p != FINGERPRINT_OK) {
    Serial.print("Falha ao carregar modelo, codigo: ");
    Serial.println(p);
    return p;
  }

  // Solicita upload do template
  p = finger.getModel();
  if (p != FINGERPRINT_OK) {
    Serial.print("Falha ao iniciar upload, codigo: ");
    Serial.println(p);
    return p;
  }

  // Buffer para os 512 bytes do template
  uint8_t templ[512];
  p = finger.get_template_buffer(512, templ);
  if (p != FINGERPRINT_OK) {
    Serial.print("Erro ao obter template, codigo: ");
    Serial.println(p);
    return p;
  }

  // Imprime os bytes em HEX
  Serial.print("Template #"); Serial.print(id); Serial.println(":");
  for (int i = 0; i < 512; i++) {
    if (i % 16 == 0) Serial.println();
    if (templ[i] < 0x10) Serial.print("0");
    Serial.print(templ[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  return FINGERPRINT_OK;
}
