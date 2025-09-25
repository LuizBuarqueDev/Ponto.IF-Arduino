import serial, time, re

PORT = "COM3"     # ajuste conforme necessário
BAUD = 115200

def wait_for_marker(ser, markers, timeout=10):
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting)
            buf += chunk
            try:
                print(chunk.decode(errors="ignore"), end='')
            except:
                pass
            for m in markers:
                if m.encode() in buf:
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
    wait_for_marker(ser, ["READY"], 5)

    # === 1) CAPTURE ===
    print("\n=== CAPTURE ===")
    send_line(ser, "CAPTURE")
    buf = wait_for_marker(ser, ["TEMPLATE_BIN_START"], 10)

    # lê os 512 bytes binários
    data = ser.read(512)
    end = wait_for_marker(ser, ["TEMPLATE_BIN_END"], 5)

    if len(data) != 512:
        print(f"Falha ao capturar template, só recebi {len(data)} bytes")
        return

    with open("template.bin","wb") as f:
        f.write(data)
    print(f"\nTemplate capturado ({len(data)} bytes).")

    # === 2) PERSIST ===
    print("\n=== PERSIST ===")
    send_line(ser, "PERSIST 1")
    time.sleep(0.1)
    with open("template.bin","rb") as f:
        ser.write(f.read())
    ser.flush()

    buf2 = wait_for_marker(ser, ["PERSIST_OK","PERSIST_FAIL"], 20)
    print("\n[DEBUG Persistência]\n", buf2.decode(errors="ignore"))
    if b"PERSIST_OK" in buf2:
        print("Persistência concluída.")
    else:
        print("Falha ao persistir.")
        return

    # === 3) LOOP VERIFY ===
    print("\n=== LOOP VERIFY ===")
    while True:
        send_line(ser, "VERIFY")
        buf3 = wait_for_marker(ser, ["MATCH:", "NO_MATCH"], 10)
        text = buf3.decode(errors="ignore")
        if "MATCH:" in text:
            m2 = re.search(r"MATCH:(\d+)", text)
            if m2:
                print("\nRESULT: MATCH ID =", m2.group(1))
            else:
                print("\nRESULT: MATCH (sem ID extraído)")
        elif "NO_MATCH" in text:
            print("\nRESULT: NO MATCH")
        else:
            print("\nVERIFY falhou:", text)
        time.sleep(2)

if __name__ == "__main__":
    main()
