import serial, time, re

PORT = "COM3"
BAUD = 115200
TOTAL_SIZE = 512

def wait_for_marker(ser, marker, timeout=10):
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if ser.in_waiting:
            buf += ser.read(ser.in_waiting)
            if marker.encode() in buf:
                return buf
        else:
            time.sleep(0.01)
    return buf

def send_line(ser, line):
    ser.write((line + "\n").encode('ascii'))
    ser.flush()

def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    time.sleep(1.5)
    ser.reset_input_buffer()
    print("Esperando READY...")
    wait_for_marker(ser, "READY", 5)

    # === CAPTURE ===
    print("\n=== CAPTURE ===")
    send_line(ser, "CAPTURE")

    wait_for_marker(ser, "TEMPLATE_BIN_START", 10)

    data = ser.read(TOTAL_SIZE)   # lê 512 bytes binários
    wait_for_marker(ser, "TEMPLATE_BIN_END", 5)

    if len(data) != TOTAL_SIZE:
        print(f"Falha: recebi {len(data)} bytes em vez de {TOTAL_SIZE}")
        return

    with open("template.bin", "wb") as f:
        f.write(data)
    print(f"Template salvo em template.bin ({len(data)} bytes)")

    # === PERSIST ===
    print("\n=== PERSIST ===")
    send_line(ser, "PERSIST 1")
    time.sleep(0.1)
    with open("template.bin", "rb") as f:
        ser.write(f.read())
    ser.flush()

    resp = wait_for_marker(ser, "PERSIST_OK", 10)
    if b"PERSIST_OK" in resp:
        print("Persistência concluída.")
    else:
        print("Falha ao persistir:", resp.decode(errors="ignore"))

    # === VERIFY ===
    print("\n=== VERIFY ===")
    while True:
        send_line(ser, "VERIFY")
        buf = wait_for_marker(ser, "NO_MATCH", 10)
        text = buf.decode(errors="ignore")
        print("Resposta:", text.strip())
        time.sleep(2)

if __name__ == "__main__":
    main()
