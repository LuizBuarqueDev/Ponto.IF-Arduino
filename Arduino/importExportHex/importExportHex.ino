// importExportHex.ino
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

#define SENSOR_RX_PIN 2
#define SENSOR_TX_PIN 3

SoftwareSerial mySerial(SENSOR_RX_PIN, SENSOR_TX_PIN);
Adafruit_Fingerprint finger(&mySerial);

// Config AS608
const int TEMPLATE_SIZE = 512;   // bytes
const int CHUNK_SIZE    = 128;   // pacote de envio em bytes (AS608 usa 128)

// Packet command codes (usados no protocolo)
const uint8_t PS_DOWNCHAR  = 0x09;
const uint8_t PS_UPCHAR    = 0x08;
const uint8_t PS_STORECHAR = 0x06;
const uint8_t PS_EMPTY     = 0x0D;

///// ----------------- utilitários serial / hex -----------------
String sanitizeHex(const String &input) {
  String clean = "";
  for (size_t i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    if ((c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'F') ||
        (c >= 'a' && c <= 'f')) {
      clean += c;
    }
  }
  return clean;
}

int hexCharToNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  return -1;
}

bool hexToBytes(const String &hex, uint8_t *out, int outLen) {
  if ((int)hex.length() != outLen * 2) return false;
  for (int i = 0; i < outLen; i++) {
    int hi = hexCharToNibble(hex.charAt(2*i));
    int lo = hexCharToNibble(hex.charAt(2*i+1));
    if (hi < 0 || lo < 0) return false;
    out[i] = (hi << 4) | lo;
  }
  return true;
}

String bytesToHexOneLine(const uint8_t *buf, int len) {
  String s;
  s.reserve(len * 2);
  char tmp[3];
  for (int i = 0; i < len; i++) {
    sprintf(tmp, "%02X", buf[i]);
    s += tmp;
  }
  return s;
}

///// ----------------- low-level protocol helpers -----------------
size_t rawRead(uint8_t *out, size_t maxLen, unsigned long timeout = 1000) {
  unsigned long start = millis();
  size_t idx = 0;
  while ((millis() - start < timeout) && (idx < maxLen)) {
    while (mySerial.available() && idx < maxLen) {
      out[idx++] = mySerial.read();
      start = millis();
    }
  }
  return idx;
}

bool writeCommandPacket(uint8_t *payload, uint16_t payloadLen) {
  // Full packet: header (EF 01  FF FF FF FF)  packet type(0x01)  lenH lenL  payload.. checksumH checksumL
  mySerial.write(0xEF); mySerial.write(0x01);
  mySerial.write(0xFF); mySerial.write(0xFF); mySerial.write(0xFF); mySerial.write(0xFF);
  mySerial.write(0x01); // comando
  uint16_t packetLen = payloadLen + 2;
  mySerial.write((packetLen >> 8) & 0xFF);
  mySerial.write(packetLen & 0xFF);
  uint16_t chksum = 0x01 + (packetLen >> 8) + (packetLen & 0xFF);
  for (uint16_t i = 0; i < payloadLen; i++) {
    mySerial.write(payload[i]);
    chksum += payload[i];
  }
  mySerial.write((chksum >> 8) & 0xFF);
  mySerial.write(chksum & 0xFF);
  mySerial.flush();
  return true;
}

bool writeDataPacket(const uint8_t *bytes, uint16_t chunkLen) {
  // Data packet type = 0x02
  mySerial.write(0xEF); mySerial.write(0x01);
  mySerial.write(0xFF); mySerial.write(0xFF); mySerial.write(0xFF); mySerial.write(0xFF);
  mySerial.write(0x02); // data
  uint16_t packetLen = chunkLen + 2;
  mySerial.write((packetLen >> 8) & 0xFF);
  mySerial.write(packetLen & 0xFF);
  uint16_t chksum = 0x02 + (packetLen >> 8) + (packetLen & 0xFF);
  for (uint16_t i = 0; i < chunkLen; i++) {
    mySerial.write(bytes[i]);
    chksum += bytes[i];
  }
  mySerial.write((chksum >> 8) & 0xFF);
  mySerial.write(chksum & 0xFF);
  mySerial.flush();
  return true;
}

///// ----------------- reading data packets (payload only) -----------------
int readDataPackets(uint8_t *outBuf, int maxLen, unsigned long timeout = 3000) {
  int got = 0;
  unsigned long start = millis();

  while (millis() - start < timeout && got < maxLen) {
    if (mySerial.available() >= 9) {
      int h1 = mySerial.read();
      if (h1 != 0xEF) continue;
      int h2 = mySerial.read();
      if (h2 != 0x01) continue;
      // skip 4-byte address
      for (int i = 0; i < 4; i++) {
        if (mySerial.available()) mySerial.read(); else return got;
      }
      if (!mySerial.available()) return got;
      uint8_t pid = mySerial.read(); // 0x02 data, 0x08 end packet, etc.
      if (!mySerial.available()) return got;
      uint16_t len = ((uint16_t)mySerial.read() << 8);
      if (!mySerial.available()) return got;
      len |= mySerial.read();
      if (len < 2) continue;
      uint16_t payloadLen = len - 2;
      for (uint16_t i = 0; i < payloadLen; i++) {
        unsigned long innerStart = millis();
        while (!mySerial.available()) {
          if (millis() - innerStart > timeout) return got;
        }
        int b = mySerial.read();
        if (b < 0) return got;
        if (got < maxLen) outBuf[got++] = (uint8_t)b;
      }
      // consume checksum
      unsigned long chkStart = millis();
      while (mySerial.available() < 2) {
        if (millis() - chkStart > timeout) break;
      }
      if (mySerial.available() >= 2) { mySerial.read(); mySerial.read(); }
      if (pid == 0x08) break;
    }
  }
  return got;
}

///// ----------------- sensor commands -----------------
bool sendEmptyDatabaseCommand() {
  uint8_t payload[1] = { PS_EMPTY };
  if (!writeCommandPacket(payload, 1)) return false;
  uint8_t rbuf[32];
  size_t rlen = rawRead(rbuf, sizeof(rbuf), 2000);
  if (rlen >= 10) {
    uint8_t confirmCode = rbuf[9];
    if (confirmCode == 0x00) return true;
    Serial.print("PS_EMPTY falhou, código=0x");
    Serial.println(confirmCode, HEX);
  }
  return false;
}

bool captureToBuffer1_wait() {
  Serial.println("Coloque o dedo...");
  unsigned long start = millis();
  while (millis() - start < 8000) {
    uint8_t p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      if (finger.image2Tz(1) == FINGERPRINT_OK) {
        Serial.println("Dedo capturado.");
        return true;
      } else {
        Serial.println("Falha image2Tz.");
        return false;
      }
    }
    delay(100);
  }
  Serial.println("Timeout.");
  return false;
}

// Puxa somente os 512 bytes de template (CharBuffer1) e devolve número de bytes
int pullTemplateFromSensor(uint8_t *outBuf, int maxLen) {
  uint8_t payloadCmd[2] = { PS_UPCHAR, 0x01 };
  if (!writeCommandPacket(payloadCmd, 2)) return -1;

  // lê ack inicial
  uint8_t ackBuf[32];
  rawRead(ackBuf, sizeof(ackBuf), 1000);

  int got = 0;
  unsigned long start = millis();

  while (millis() - start < 5000 && got < maxLen) {
    if (mySerial.available() >= 9) {
      // cabeçalho
      uint8_t hdr[9];
      mySerial.readBytes(hdr, 9);
      uint8_t pid = hdr[6]; // tipo de pacote
      uint16_t plen = ((uint16_t)hdr[7] << 8) | hdr[8];
      if (plen < 2) continue;

      // payload + checksum
      uint16_t dataLen = plen - 2;
      static uint8_t tmp[600];
      mySerial.readBytes(tmp, plen);

      if (pid == 0x02 || pid == 0x08) { // data ou last data
        if (got + dataLen > maxLen) dataLen = maxLen - got;
        memcpy(outBuf + got, tmp, dataLen);
        got += dataLen;
        if (pid == 0x08) break; // pacote final
      }
    }
  }

  return got;
}


// Faz upload do template (512 bytes) para o sensor e faz STORE ID=targetID
bool uploadTemplateToSensorAndStore(uint8_t *buf, int len, uint16_t targetID) {
  // 1) send DOWNCHAR command (tells sensor to expect data for char buffer 1)
  uint8_t payloadCmd[2] = { PS_DOWNCHAR, 0x01 };
  if (!writeCommandPacket(payloadCmd, 2)) return false;
  delay(50);

  // 2) send the data in chunks of CHUNK_SIZE (usually 128)
  int sent = 0;
  while (sent < len) {
    int chunk = min(CHUNK_SIZE, len - sent);
    if (!writeDataPacket(buf + sent, chunk)) return false;
    sent += chunk;
    delay(20);
  }

  // 3) send STORECHAR command to store buffer 1 into targetID
  uint8_t payloadStore[4] = { PS_STORECHAR, 0x01, (uint8_t)(targetID >> 8), (uint8_t)(targetID & 0xFF) };
  if (!writeCommandPacket(payloadStore, 4)) return false;

  // 4) read ack
  uint8_t ack[32];
  size_t ackLen = rawRead(ack, sizeof(ack), 1500);
  if (ackLen >= 10 && ack[9] == 0x00) return true;
  return false;
}

int doFingerSearch() {
  if (finger.fingerSearch() == FINGERPRINT_OK) {
    return finger.fingerID;
  }
  return -1;
}

///// ----------------- Main: commands via Serial -----------------
void setup() {
  Serial.begin(115200);
  mySerial.begin(9600); // ajuste se seu módulo for 9600
  delay(100);
  finger.begin(9600);

  Serial.println("\nInicializando sensor...");
  if (!finger.verifyPassword()) {
    Serial.println("SENSOR NÃO ENCONTRADO!");
    while (1) delay(1);
  }
  Serial.println("Sensor OK.");

  // não limpamos automaticamente aqui (remova se quiser)
  Serial.println("\nComandos:");
  Serial.println(" - ENROLL   -> captura e retorna TEMPLATE (não grava no sensor)");
  Serial.println(" - INJECT   -> envie INJECT <ENTER>, depois cole o HEX (1024 chars) e ENTER");
  Serial.println(" - VERIFY   -> captura dedo e busca no sensor (procura ID salvo)");
  Serial.println(" - CLEAR    -> limpa banco (PS_EMPTY)");
  Serial.println();
  Serial.println("READY");
}

void processEnroll() {
  if (!captureToBuffer1_wait()) {
    Serial.println("ENROLL_FAIL");
    return;
  }
  uint8_t tpl[TEMPLATE_SIZE];
  memset(tpl, 0, sizeof(tpl));
  int got = pullTemplateFromSensor(tpl, TEMPLATE_SIZE);
  if (got != TEMPLATE_SIZE) {
    // still send what we have so host can inspect
    Serial.print("ENROLL_PARTIAL:");
    Serial.println(got);
  }
  // print template in one line (1024 chars) and also with markers
  String hex = bytesToHexOneLine(tpl, TEMPLATE_SIZE);
  Serial.println("TEMPLATE_START");
  Serial.println(hex);
  Serial.println("TEMPLATE_END");
}


void processInject(String &hexLine) {
  String clean = sanitizeHex(hexLine);
  if ((int)clean.length() != TEMPLATE_SIZE * 2) {
    Serial.print("INJECT_FAIL_LEN:");
    Serial.println(clean.length());
    return;
  }
  // convert to bytes
  uint8_t tpl[TEMPLATE_SIZE];
  if (!hexToBytes(clean, tpl, TEMPLATE_SIZE)) {
    Serial.println("INJECT_FAIL_PARSE");
    return;
  }
  Serial.println("INJECT_START");
  bool ok = uploadTemplateToSensorAndStore(tpl, TEMPLATE_SIZE, 1);
  if (ok) {
    Serial.println("INJECT_OK");
  } else {
    Serial.println("INJECT_FAIL");
  }
}

void processVerify() {
  if (!captureToBuffer1_wait()) {
    Serial.println("VERIFY_FAIL_CAPTURE");
    return;
  }
  int id = doFingerSearch();
  if (id >= 0) {
    Serial.print("MATCH:");
    Serial.println(id);
  } else {
    Serial.println("NO_MATCH");
  }
}

void loop() {
  // read a line command
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.equalsIgnoreCase("ENROLL")) {
    processEnroll();
    return;
  }
  else if (cmd.equalsIgnoreCase("INJECT")) {
    // Expect next line to be the HEX; block until it arrives (with timeout)
    unsigned long start = millis();
    while (Serial.available() == 0 && (millis() - start < 8000)) delay(10);
    if (Serial.available() == 0) {
      Serial.println("INJECT_FAIL_NOHEX");
      return;
    }
    String hexLine = Serial.readStringUntil('\n');
    hexLine.trim();
    processInject(hexLine);
    return;
  }
  else if (cmd.equalsIgnoreCase("VERIFY")) {
    processVerify();
    return;
  }
  else if (cmd.equalsIgnoreCase("CLEAR")) {
    if (sendEmptyDatabaseCommand()) Serial.println("CLEAR_OK"); else Serial.println("CLEAR_FAIL");
    return;
  }
  else {
    Serial.print("UNKNOWN_CMD:");
    Serial.println(cmd);
  }
}