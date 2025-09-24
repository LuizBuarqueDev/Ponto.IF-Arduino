#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

#define SENSOR_RX_PIN 2
#define SENSOR_TX_PIN 3

SoftwareSerial mySerial(SENSOR_RX_PIN, SENSOR_TX_PIN);
Adafruit_Fingerprint finger(&mySerial);

// Configuração para AS608
const int TEMPLATE_SIZE = 512;   // 512 bytes -> 1024 chars HEX
const int CHUNK_SIZE    = 128;   // pacotes de 128 bytes

const uint8_t PS_DOWNCHAR  = 0x09;
const uint8_t PS_UPCHAR    = 0x08;
const uint8_t PS_STORECHAR = 0x06;
const uint8_t PS_EMPTY     = 0x0D;

// ======================================================
// Utilitários
// ======================================================
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

// ======================================================
// Comunicação com o sensor
// ======================================================
size_t rawRead(uint8_t *out, size_t maxLen, unsigned long timeout = 1000) {
  unsigned long start = millis();
  size_t idx = 0;
  while ((millis() - start < timeout) && (idx < maxLen)) {
    while (mySerial.available() && idx < maxLen) {
      out[idx++] = mySerial.read();
      start = millis(); // renova timeout a cada byte recebido
    }
  }
  return idx;
}

bool writeCommandPacket(uint8_t *payload, uint16_t payloadLen) {
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

bool writeDataPacket(uint8_t *bytes, uint16_t chunkLen) {
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

// ======================================================
// Comandos do sensor
// ======================================================
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

bool captureToBuffer1() {
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
// === Pega o template LIMPO (só os 512 bytes do charBuffer1) ===
int pullTemplateFromSensor(uint8_t *outBuf, int maxLen) {
  // Comando UPCHAR para CharBuffer1
  uint8_t payloadCmd[2] = { PS_UPCHAR, 0x01 };
  if (!writeCommandPacket(payloadCmd, 2)) return -1;

  int written = 0;
  unsigned long start = millis();

  while (millis() - start < 3000 && written < maxLen) {
    // Espera header 0xEF01
    if (mySerial.available() >= 9) {
      // Lê cabeçalho
      uint8_t hdr[9];
      mySerial.readBytes(hdr, 9);

      // Tipo do pacote está no byte 6
      uint8_t pType = hdr[6];

      // Comprimento do payload + checksum
      uint16_t plen = ((uint16_t)hdr[7] << 8) | hdr[8];
      if (plen < 2) continue;

      // Lê payload + checksum
      static uint8_t tmp[600];
      mySerial.readBytes(tmp, plen);

      // Dados úteis = plen - 2 (tirando checksum)
      uint16_t dataLen = plen - 2;

      if (pType == 0x02 || pType == 0x08) { // DATA ou END
        if (written + dataLen > maxLen) dataLen = maxLen - written;
        memcpy(outBuf + written, tmp, dataLen);
        written += dataLen;

        if (pType == 0x08) break; // pacote final
      }
    }
  }
  return written;
}

// === Faz upload do template e grava no ID ===
bool uploadTemplateToSensorAndStore(uint8_t *buf, int len, uint16_t targetID) {
  uint8_t payloadCmd[2] = { PS_DOWNCHAR, 0x01 };
  if (!writeCommandPacket(payloadCmd, 2)) return false;
  delay(50);

  int sent = 0;
  while (sent < len) {
    int chunk = min(CHUNK_SIZE, len - sent);
    if (!writeDataPacket(buf + sent, chunk)) return false;
    sent += chunk;
    delay(20);
  }

  uint8_t payloadStore[4] = { PS_STORECHAR, 0x01,
                              (uint8_t)(targetID >> 8),
                              (uint8_t)(targetID & 0xFF) };
  if (!writeCommandPacket(payloadStore, 4)) return false;

  uint8_t ack[32];
  size_t ackLen = rawRead(ack, sizeof(ack), 1000);
  if (ackLen >= 10 && ack[9] == 0x00) return true;
  return false;
}

// === Busca digital atual no banco ===
int doFingerSearch() {
  if (finger.fingerSearch() == FINGERPRINT_OK) {
    return finger.fingerID;
  }
  return -1;
}



// ======================================================
// Setup & Loop
// ======================================================
void setup() {
  Serial.begin(115200);
  mySerial.begin(57600); // ou 9600 dependendo do AS608
  delay(100);
  finger.begin(57600);

  Serial.println("\nInicializando sensor...");
  if (!finger.verifyPassword()) {
    Serial.println("SENSOR NÃO ENCONTRADO!");
    while (1) delay(1);
  }
  Serial.println("Sensor OK.");

  Serial.println("Limpando todas as digitais...");
  if (sendEmptyDatabaseCommand()) {
    Serial.println("Database limpa.");
  } else {
    Serial.println("Falha ao limpar.");
  }

  Serial.println("\nComandos:");
  Serial.println(" - Digite 'C' e ENTER para capturar e imprimir HEX.");
  Serial.print(" - Ou cole o HEX (");
  Serial.print(TEMPLATE_SIZE * 2);
  Serial.println(" chars) para injetar no ID=1.");
}
void loop() {
  if (Serial.available()) {
    char c = Serial.read();  // lê 1 caractere
    if (c == 'C' || c == 'c') {
      if (!captureToBuffer1()) return;
      uint8_t outBuf[TEMPLATE_SIZE];
      memset(outBuf, 0, sizeof(outBuf));
      int got = pullTemplateFromSensor(outBuf, TEMPLATE_SIZE);
      Serial.print("Bytes recebidos: ");
      Serial.println(got);

      Serial.println("TEMPLATE_HEX_START");
      for (int i = 0; i < got; i++) {
        if (outBuf[i] < 16) Serial.print('0');
        Serial.print(outBuf[i], HEX);
        if ((i+1) % 64 == 0) Serial.println();
      }
      Serial.println();
      Serial.println("TEMPLATE_HEX_END");

      Serial.println("\nPronto. Digite C de novo ou cole HEX para injetar.");
    }
  }
}
