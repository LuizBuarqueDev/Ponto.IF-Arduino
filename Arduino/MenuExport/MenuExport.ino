#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

SoftwareSerial mySerial(2, 3);  // RX=2, TX=3
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

const int TEMPLATE_SIZE = 498;

void setup() {
  Serial.begin(115200);
  finger.begin(57600);
  if (!finger.verifyPassword()) {
    Serial.println("SENSOR_FAIL");
    while (1);
  }
  Serial.println("READY");
}

void loop() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd.startsWith("ENROLL")) {
    int id = cmd.substring(7).toInt();
    doEnroll(id);
  } else if (cmd.startsWith("EXPORT")) {
    int id = cmd.substring(7).toInt();
    doExport(id);
  } else if (cmd.startsWith("IMPORT")) {
    int id = cmd.substring(7).toInt();
    doImport(id);
  } else if (cmd == "CLEAR") {
    uint8_t r = finger.emptyDatabase();
    if (r == FINGERPRINT_OK) Serial.println("CLEARED");
    else { Serial.print("CLEAR_FAIL code="); Serial.println(r, HEX); }
  } else if (cmd == "COUNT") {
    uint8_t r = finger.getTemplateCount();
    if (r == FINGERPRINT_OK) {
      Serial.print("COUNT=");
      Serial.println(finger.templateCount);
    } else {
      Serial.print("COUNT_FAIL code=");
      Serial.println(r, HEX);
    }
  }
}

// ===== ENROLL =====
void doEnroll(int id) {
  Serial.print("ENROLL_ID=");
  Serial.println(id);

  Serial.println("Coloque o dedo...");
  while (finger.getImage() != FINGERPRINT_OK) delay(100);
  if (finger.image2Tz(1) != FINGERPRINT_OK) { Serial.println("FAIL_TZ1"); return; }

  Serial.println("Remova o dedo...");
  while (finger.getImage() != FINGERPRINT_NOFINGER) delay(100);

  Serial.println("Coloque o mesmo dedo de novo...");
  while (finger.getImage() != FINGERPRINT_OK) delay(100);
  if (finger.image2Tz(2) != FINGERPRINT_OK) { Serial.println("FAIL_TZ2"); return; }

  if (finger.createModel() != FINGERPRINT_OK) { Serial.println("FAIL_MODEL"); return; }

  uint8_t r = finger.storeModel(id);
  if (r == FINGERPRINT_OK) {
    Serial.println("ENROLL_OK");
  } else {
    Serial.print("FAIL_STORE code=0x");
    Serial.println(r, HEX);
  }
}

// ===== EXPORT =====
void doExport(int id) {
  if (finger.loadModel(id) != FINGERPRINT_OK) { Serial.println("EXPORT_FAIL_LOAD"); return; }
  if (finger.getModel() != FINGERPRINT_OK) { Serial.println("EXPORT_FAIL_UPLOAD"); return; }

  uint8_t buf[TEMPLATE_SIZE];
  if (finger.get_template_buffer(TEMPLATE_SIZE, buf) != FINGERPRINT_OK) {
    Serial.println("EXPORT_FAIL_BUF");
    return;
  }
  Serial.println("TEMPLATE_BIN_START");
  for (int i = 0; i < TEMPLATE_SIZE; i++) {
    Serial.write(buf[i]);
    delayMicroseconds(200); // dá tempo do host ler
  }
  Serial.println("TEMPLATE_BIN_END");
}

// ===== IMPORT =====
void doImport(int id) {
  Serial.print("IMPORT_ID=");
  Serial.println(id);

  uint8_t buf[TEMPLATE_SIZE];
  int received = 0;
  unsigned long start = millis();

  while (received < TEMPLATE_SIZE && millis() - start < 10000) {
    if (Serial.available()) {
      buf[received++] = Serial.read();
      start = millis();
    }
  }

  Serial.print("Recebidos=");
  Serial.println(received);

  if (received != TEMPLATE_SIZE) { 
    Serial.println("IMPORT_FAIL_LEN"); 
    return; 
  }

  if (!finger.write_template_to_sensor(TEMPLATE_SIZE, buf)) {
    Serial.println("IMPORT_FAIL_WRITE"); 
    return;
  }

  uint8_t r = finger.storeModel(id);
  if (r == FINGERPRINT_OK) {
    Serial.println("IMPORT_OK");
  } else {
    Serial.print("IMPORT_FAIL_STORE code=0x");
    Serial.println(r, HEX);
  }
}