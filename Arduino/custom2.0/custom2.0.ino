#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

SoftwareSerial mySerial(2, 3);  // RX=2, TX=3
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

const int TEMPLATE_SIZE = 498;  // tamanho real do template

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
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "CAPTURE") {
      captureAndSend();
    }
    else if (cmd.startsWith("PERSIST")) {
      int id = cmd.substring(7).toInt();
      persistTemplate(id);
    }
    else if (cmd == "VERIFY") {
      doVerify();
    }
  }
}

void captureAndSend() {
  Serial.println("Coloque o dedo no sensor...");

  int p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) { delay(100); continue; }
    if (p == FINGERPRINT_PACKETRECIEVEERR) { Serial.println("CAPTURE_FAIL_COMM"); return; }
    if (p == FINGERPRINT_IMAGEFAIL) { Serial.println("CAPTURE_FAIL_IMAGE"); return; }
  }
  Serial.println("Imagem capturada!");

  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    Serial.println("CAPTURE_FAIL_TZ");
    return;
  }

  if (finger.createModel() != FINGERPRINT_OK) {
    Serial.println("CAPTURE_FAIL_MODEL");
    return;
  }

  uint8_t buf[TEMPLATE_SIZE];
  if (finger.get_template_buffer(TEMPLATE_SIZE, buf) != FINGERPRINT_OK) {
    Serial.println("CAPTURE_FAIL_BUF");
    return;
  }

  // envia em binário cru
  Serial.println("TEMPLATE_BIN_START");
  Serial.write(buf, TEMPLATE_SIZE);
  Serial.println("TEMPLATE_BIN_END");
}

void persistTemplate(int id) {
  uint8_t buf[TEMPLATE_SIZE];
  int received = 0;
  unsigned long start = millis();

  while (received < TEMPLATE_SIZE && millis() - start < 10000) {
    if (Serial.available()) {
      buf[received++] = Serial.read();
      start = millis();
    }
  }

  Serial.print("Recebi bytes: ");
  Serial.println(received);

  if (received != TEMPLATE_SIZE) {
    Serial.println("PERSIST_FAIL_LEN");
    return;
  }

  if (!finger.write_template_to_sensor(TEMPLATE_SIZE, buf)) {
    Serial.println("PERSIST_FAIL_WRITE");
    return;
  }
  Serial.println("Buffer escrito no sensor!");

  uint8_t r = finger.storeModel(id);
  if (r != FINGERPRINT_OK) {
    Serial.print("PERSIST_FAIL_STORE code=");
    Serial.println(r, HEX);
    return;
  }
  Serial.println("PERSIST_OK");
}

void doVerify() {
  int p = -1;
  Serial.println("Coloque o dedo para verificar...");
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
