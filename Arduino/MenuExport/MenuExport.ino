#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

SoftwareSerial mySerial(2, 3);  // RX=2, TX=3
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

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
    int space1 = cmd.indexOf(' ');
    int space2 = cmd.indexOf(' ', space1 + 1);
    int id = cmd.substring(space1 + 1, space2).toInt();
    int length = cmd.substring(space2 + 1).toInt();
    doImport(id, length);
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
  } else if (cmd == "VERIFY") {
    doVerify();
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

// ===== EXPORT com debug extra =====
void doExport(int id) {
  Serial.print("EXPORT_ID=");
  Serial.println(id);

  uint8_t r;

  r = finger.loadModel(id);
  Serial.print("loadModel -> "); Serial.println(r);
  if (r != FINGERPRINT_OK) { Serial.println("EXPORT_FAIL_LOAD"); return; }

  r = finger.getModel();
  Serial.print("getModel -> "); Serial.println(r);
  if (r != FINGERPRINT_OK) { Serial.println("EXPORT_FAIL_UPLOAD"); return; }

  Serial.println(">>> TEMPLATE_RAW_START <<<");

  unsigned long start = millis();
  int count = 0;
  while (millis() - start < 2000) {
    if (mySerial.available()) {
      int b = mySerial.read();
      Serial.write(b);
      count++;
      start = millis();
    }
  }

  Serial.println(">>> TEMPLATE_RAW_END <<<");
  Serial.print("TEMPLATE_SIZE=");
  Serial.println(count);
}

// ===== IMPORT =====
void doImport(int id, int length) {
  Serial.print("IMPORT_ID=");
  Serial.println(id);
  Serial.print("Esperando bytes=");
  Serial.println(length);

  uint8_t buf[length];
  int received = 0;
  unsigned long start = millis();

  while (received < length && millis() - start < 10000) {
    if (Serial.available()) {
      buf[received++] = Serial.read();
      start = millis();
    }
  }

  Serial.print("Recebidos=");
  Serial.println(received);

  if (received != length) {
    Serial.println("IMPORT_FAIL_LEN");
    return;
  }

  if (!finger.write_template_to_sensor(length, buf)) {
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

// ===== VERIFY =====
void doVerify() {
  Serial.println("Coloque o dedo para verificar...");
  int p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) { delay(100); continue; }
    if (p == FINGERPRINT_PACKETRECIEVEERR) { Serial.println("VERIFY_FAIL_COMM"); return; }
    if (p == FINGERPRINT_IMAGEFAIL) { Serial.println("VERIFY_FAIL_IMAGE"); return; }
  }
  Serial.println("Imagem capturada!");

  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    Serial.println("VERIFY_FAIL_TZ");
    return;
  }

  if (finger.fingerFastSearch() == FINGERPRINT_OK) {
    Serial.print("MATCH:");
    Serial.println(finger.fingerID);
  } else {
    Serial.println("NO_MATCH");
  }
}